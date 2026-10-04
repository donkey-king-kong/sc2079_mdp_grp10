"""Camera capture and inference workers for Task 1."""

from __future__ import annotations

from datetime import datetime, timezone
import json
from pathlib import Path
import queue
import threading
from typing import Any, Mapping

from task1.events import Event, EventType, ImageTransferRequest, InferenceRequest


class CameraCVWorker(threading.Thread):
    """Owns capture/inference and local persistence; no mission-state mutation."""

    def __init__(
        self, imaging, requests, transfers, events, shutdown, data_dir: Path,
        continuous_scan: bool = False, continuous_scan_interval_seconds: float = 1.0,
        inference_requests=None,
        parallel_inference: bool = True,
    ):
        super().__init__(name="CAMERA-CV")
        self.imaging, self.requests = imaging, requests
        self.inference_requests = inference_requests or queue.Queue()
        self.transfers = transfers
        self.parallel_inference = parallel_inference
        self.events, self.shutdown, self.data_dir = events, shutdown, data_dir
        self.continuous_scan = continuous_scan
        self.continuous_scan_interval_seconds = max(0.1, continuous_scan_interval_seconds)

    def run(self):
        startup_error = None
        try:
            start = getattr(self.imaging, "start", None)
            if callable(start):
                start()
        except Exception as error:
            # Surface startup failure when a SNAP actually needs the camera;
            # this keeps route handling in the kernel rather than the worker.
            startup_error = error
            print("[IMAGING] Camera startup failed: %s" % error)

        try:
            while True:
                try:
                    request = self.requests.get(
                        timeout=self.continuous_scan_interval_seconds if self.continuous_scan else None,
                    )
                except queue.Empty:
                    self._scan_continuously(startup_error)
                    continue
                if request is None:
                    return
                try:
                    if startup_error is not None:
                        raise RuntimeError("camera startup failed: %s" % startup_error)
                    if self.parallel_inference:
                        frames = self.imaging.capture_samples()
                        self.inference_requests.put(
                            InferenceRequest(request.obstacle_id, frames)
                        )
                        self.events.put(
                            Event(EventType.CV_CAPTURED, {"obstacle_id": request.obstacle_id})
                        )
                    else:
                        result = self.imaging.capture_and_predict(request.obstacle_id)
                        metadata_path = self._persist_and_queue(request.obstacle_id, result)
                        event_type = (
                            EventType.CV_RESULT_ACCEPTED
                            if result.get("image_id")
                            else EventType.CV_RETRY_REQUIRED
                        )
                        self.events.put(
                            Event(
                                event_type,
                                {
                                    "obstacle_id": request.obstacle_id,
                                    "result": result,
                                    "metadata_path": str(metadata_path),
                                },
                            )
                        )
                except Exception as error:
                    self.events.put(
                        Event(
                            EventType.CV_ERROR,
                            {
                                "obstacle_id": request.obstacle_id,
                                "error": str(error),
                                "stage": "capture",
                            },
                        )
                    )
        finally:
            close = getattr(self.imaging, "close", None)
            if callable(close):
                try:
                    close()
                except Exception as error:
                    print("[IMAGING] Camera shutdown failed: %s" % error)

    def _scan_continuously(self, startup_error: Exception | None) -> None:
        """Poll independently of SNAP without altering the kernel route state."""
        try:
            if startup_error is not None:
                return
            result = self.imaging.capture_and_predict(
                "continuous", save_on_no_detection=False, log_no_detection=False,
            )
            # Blank polling frames are intentionally discarded. A valid target
            # is persisted and transferred, but has its own event so it cannot
            # satisfy a pending SNAP in the kernel.
            if not result.get("image_id"):
                return
            metadata_path = self._persist_and_queue("continuous", result)
            print("[IMAGING] Continuous scan detected %s" % result.get("image_id"))
            self.events.put(Event(EventType.CV_CONTINUOUS_RESULT, {
                "result": result, "metadata_path": str(metadata_path),
            }))
        except Exception as error:
            print("[IMAGING] Continuous scan error: %s" % error)

    def _persist_and_queue(self, obstacle_id: str, result: Mapping[str, Any]) -> Path:
        if not isinstance(result, dict):
            raise ValueError("imaging result must be a dictionary")
        if not result.get("image_path"):
            raise ValueError("imaging result has no saved image path")
        metadata_path = self._save_metadata(obstacle_id, result)
        transfer = ImageTransferRequest(str(result["image_path"]), str(metadata_path))
        try:
            self.transfers.put_nowait(transfer)
        except queue.Full:
            self.events.put(Event(EventType.IMAGE_TRANSFER_QUEUE_FULL, {"image_path": transfer.image_path}))
        return metadata_path

    def _save_metadata(self, obstacle_id: str, result: Mapping[str, Any]) -> Path:
        image_path = Path(str(result["image_path"]))
        # The annotated image has already been persisted by ImagingService in
        # imaging/data. Keep its metadata beside it so ImageTracker sees the
        # JPEG directly in that directory.
        image_path.parent.mkdir(parents=True, exist_ok=True)
        metadata_path = image_path.with_suffix(".json")
        metadata = {
            "obstacle_id": str(obstacle_id), "class_id": result.get("image_id"),
            "confidence": result.get("confidence"), "bbox": result.get("bbox"),
            "timestamp": datetime.now(timezone.utc).isoformat(), "local_image_path": str(image_path),
        }
        metadata_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
        return metadata_path


class InferenceWorker(threading.Thread):
    """Runs YOLO and image persistence independently of STM route execution."""

    def __init__(self, imaging, requests, transfers, events, shutdown, data_dir: Path):
        super().__init__(name="CV-INFERENCE")
        self.imaging, self.requests, self.transfers = imaging, requests, transfers
        self.events, self.shutdown, self.data_dir = events, shutdown, data_dir

    def run(self):
        while True:
            request = self.requests.get()
            if request is None:
                return
            try:
                result = self.imaging.predict_captured(request.obstacle_id, request.frames)
                metadata_path = self._persist_and_queue(request.obstacle_id, result)
                event_type = (
                    EventType.CV_RESULT_ACCEPTED
                    if result.get("image_id")
                    else EventType.CV_RETRY_REQUIRED
                )
                self.events.put(
                    Event(
                        event_type,
                        {
                            "obstacle_id": request.obstacle_id,
                            "result": result,
                            "metadata_path": str(metadata_path),
                        },
                    )
                )
            except Exception as error:
                self.events.put(
                    Event(
                        EventType.CV_ERROR,
                        {
                            "obstacle_id": request.obstacle_id,
                            "error": str(error),
                            "stage": "inference",
                        },
                    )
                )

    def _persist_and_queue(self, obstacle_id: str, result: Mapping[str, Any]) -> Path:
        image_path = Path(str(result["image_path"]))
        metadata_path = image_path.with_suffix(".json")
        metadata = {
            "obstacle_id": str(obstacle_id),
            "class_id": result.get("image_id"),
            "confidence": result.get("confidence"),
            "bbox": result.get("bbox"),
        }
        metadata_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
        self.transfers.put_nowait(ImageTransferRequest(str(image_path), str(metadata_path)))
        return metadata_path


