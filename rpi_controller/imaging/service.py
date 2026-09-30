"""Capture and inference orchestration for the controller."""

from __future__ import annotations

from datetime import datetime
from pathlib import Path
import re

from PIL import Image, ImageDraw

from .camera import CameraCapture
from .detector import DetectionResult, DetectorSetupError, LocalYoloDetector, symbol_for_target


DEFAULT_MODEL_PATH = Path(__file__).with_name("models") / "best.pt"
DATA_DIR = Path(__file__).parents[1] / "imaging" / "data"


def _image_path(symbol: str | None, data_dir: Path) -> Path:
    """Create a collision-safe, second-resolution image filename."""
    data_dir.mkdir(parents=True, exist_ok=True)
    safe_symbol = re.sub(r"[^A-Za-z0-9_-]+", "_", str(symbol or "no_symbol")).strip("_") or "no_symbol"
    stem = f"{datetime.now():%Y%m%d_%H%M%S}_{safe_symbol}"
    path = data_dir / f"{stem}.jpg"
    suffix = 2
    while path.exists():
        path = data_dir / f"{stem}_{suffix}.jpg"
        suffix += 1
    return path


def save_detection(frame, result: DetectionResult, obstacle_id: object, data_dir: Path) -> Path:
    """Save one detected target with its bounding box, ID and symbol."""
    image = Image.fromarray(frame)
    draw = ImageDraw.Draw(image)
    draw.rectangle(result.bbox, outline="lime", width=6)

    symbol = symbol_for_target(result.target_id)
    label = f"ID {result.target_id}" + (f" ({symbol})" if symbol else "")
    x1, y1, _, _ = result.bbox
    label_y = max(0, y1 - 20)
    draw.rectangle((x1, label_y, x1 + len(label) * 7, label_y + 20), fill="black")
    draw.text((x1 + 2, label_y + 2), label, fill="lime")

    detected_symbol = symbol_for_target(result.target_id) or result.target_id
    path = _image_path(detected_symbol, data_dir)
    image.save(path, format="JPEG")
    print(f"[IMAGING] Saved {path}")
    return path


def save_capture(frame, data_dir: Path) -> Path:
    """Persist a SNAP image even when no target symbol was detected."""
    path = _image_path("no_symbol", data_dir)
    Image.fromarray(frame).save(path, format="JPEG")
    print(f"[IMAGING] Saved {path}")
    return path


class ImagingService:
    """Run one camera capture and, when available, one local inference."""

    def __init__(
        self,
        model_path: str | Path = DEFAULT_MODEL_PATH,
        confidence: float = 0.30,
        width: int = 1640,
        height: int = 1232,
        warmup_seconds: float = 1.0,
        data_dir: str | Path = DATA_DIR,
    ):
        self.model_path = Path(model_path)
        self.width = width
        self.height = height
        self.warmup_seconds = warmup_seconds
        self.data_dir = Path(data_dir)
        self.detector = LocalYoloDetector(self.model_path, confidence) if self.model_path.is_file() else None
        self._camera: CameraCapture | None = None

    def start(self) -> None:
        """Open the camera and load the model once for the worker lifetime."""
        if self._camera is None:
            self._camera = CameraCapture(self.width, self.height, self.warmup_seconds)
            print("[IMAGING] Camera ready")
        # Load weights before the first SNAP, rather than adding that delay to
        # the first requested capture. Inference itself remains per SNAP.
        if self.detector is not None:
            self.detector.load()
            print("[IMAGING] Model ready")

    def close(self) -> None:
        """Release the persistent camera session during worker shutdown."""
        if self._camera is not None:
            self._camera.close()
            self._camera = None

    def capture_and_predict(
        self,
        obstacle_id: object,
        *,
        save_on_no_detection: bool = True,
        log_no_detection: bool = True,
    ) -> dict[str, object | None]:
        """Capture one frame and return the existing controller result shape.

        SNAP requests save a frame even without a target. Continuous polling
        skips blank frames to avoid filling the RPi disk.
        """
        # Task1's CameraCVWorker calls start() once. Retain this short-lived
        # fallback so existing one-shot callers still work unchanged.
        if self._camera is not None:
            frame = self._camera.capture()
            result = self.detector.detect(frame) if self.detector is not None else DetectionResult.not_found()
        else:
            with CameraCapture(self.width, self.height, self.warmup_seconds) as camera:
                frame = camera.capture()
                result = self.detector.detect(frame) if self.detector is not None else DetectionResult.not_found()

        image_path = None
        if self.detector is None and log_no_detection:
            print(f"[IMAGING] Model weights not found at {self.model_path}; capture only")
        elif not result.found and log_no_detection:
            print("[IMAGING] No target detected")
        elif result.bbox is not None:
            image_path = save_detection(frame, result, obstacle_id, self.data_dir)

        if image_path is None and save_on_no_detection:
            image_path = save_capture(frame, self.data_dir)

        return {
            "obstacle_id": str(obstacle_id),
            "image_id": result.target_id,
            "confidence": result.confidence,
            "bbox": result.bbox,
            "image_path": str(image_path) if image_path else None,
        }
