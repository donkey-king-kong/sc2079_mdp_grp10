"""Small, event-driven Task 1 runtime. The kernel is the only state writer."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
import json
import os
from pathlib import Path
import queue
import subprocess
import threading
from datetime import datetime, timezone
from typing import Any, Mapping, Optional

from connectors.stm import MOVEMENT_COMMAND, STMRXWorker, STMTXWorker
from task1_events import (
    AlgoRequest, CameraRequest, Event, EventType, ImageTransferRequest, STMCommand,
)


class MissionState(str, Enum):
    IDLE = "IDLE"
    WAITING_FOR_ALGO = "WAITING_FOR_ALGO"
    WAITING_FOR_ZH = "WAITING_FOR_ZH"
    WAITING_FOR_STM = "WAITING_FOR_STM"
    WAITING_FOR_CV = "WAITING_FOR_CV"
    COMPLETE = "COMPLETE"
    FAILED = "FAILED"
    STOPPED = "STOPPED"


class AlgoWorker(threading.Thread):
    """One HTTP request/response worker; it never owns a route."""

    def __init__(self, connector, requests, events, shutdown):
        super().__init__(name="ALGO")
        self.connector, self.requests, self.events, self.shutdown = connector, requests, events, shutdown

    def run(self):
        while True:
            request = self.requests.get()
            if request is None:
                return
            try:
                print("[ALGO TX] START_TASK obstacles=%d" % len(request.obstacles))
                result = self.connector.navigate(
                    robot=dict(request.robot), obstacles=[dict(item) for item in request.obstacles],
                    strategy=request.strategy, metric=request.metric,
                )
                commands = result.get("data", {}).get("commands") if isinstance(result, dict) else None
                if not isinstance(commands, list) or not all(isinstance(item, str) for item in commands):
                    raise ValueError("Algo NAVIGATION response has no string commands list")
                print("[ALGO RX] route received: %d commands" % len(commands))
                self.events.put(Event(EventType.ALGO_ROUTE_RECEIVED, {"result": result, "commands": commands}))
            except Exception as error:  # requests errors and malformed payloads become kernel events
                self.events.put(Event(EventType.ALGO_ERROR, {"error": str(error)}))


class CameraCVWorker(threading.Thread):
    """Owns capture/inference and local persistence; no mission-state mutation."""

    def __init__(self, imaging, requests, transfers, events, shutdown, data_dir: Path):
        super().__init__(name="CAMERA-CV")
        self.imaging, self.requests, self.transfers = imaging, requests, transfers
        self.events, self.shutdown, self.data_dir = events, shutdown, data_dir

    def run(self):
        while True:
            request = self.requests.get()
            if request is None:
                return
            try:
                result = self.imaging.capture_and_predict(request.obstacle_id)
                if not isinstance(result, dict):
                    raise ValueError("imaging result must be a dictionary")
                if not result.get("image_id") or not result.get("image_path"):
                    self.events.put(Event(EventType.CV_RETRY_REQUIRED, {"obstacle_id": request.obstacle_id, "result": result}))
                    continue
                metadata_path = self._save_metadata(request.obstacle_id, result)
                transfer = ImageTransferRequest(str(result["image_path"]), str(metadata_path))
                try:
                    self.transfers.put_nowait(transfer)
                except queue.Full:
                    self.events.put(Event(EventType.IMAGE_TRANSFER_QUEUE_FULL, {"image_path": transfer.image_path}))
                self.events.put(Event(EventType.CV_RESULT_ACCEPTED, {"obstacle_id": request.obstacle_id, "result": result, "metadata_path": str(metadata_path)}))
            except Exception as error:
                self.events.put(Event(EventType.CV_ERROR, {"obstacle_id": request.obstacle_id, "error": str(error)}))

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


@dataclass(frozen=True)
class TransferConfig:
    host: str
    username: str
    destination: str
    identity_file: Optional[str] = None

    @classmethod
    def from_env(cls) -> Optional["TransferConfig"]:
        host, username, destination = os.getenv("MDP_PC_HOST"), os.getenv("MDP_PC_USER"), os.getenv("MDP_PC_DEST")
        if not all((host, username, destination)):
            return None
        return cls(host, username, destination, os.getenv("MDP_PC_SSH_KEY"))


class ImageTransferWorker(threading.Thread):
    """Owns PC transfer. Failures retain the RPi files and only emit events."""

    def __init__(self, requests, events, shutdown, config: Optional[TransferConfig]):
        super().__init__(name="IMAGE-TRANSFER")
        self.requests, self.events, self.shutdown, self.config = requests, events, shutdown, config

    def run(self):
        while True:
            request = self.requests.get()
            if request is None:
                return
            if self.config is None:
                self.events.put(Event(EventType.IMAGE_TRANSFER_ERROR, {"image_path": request.image_path, "error": "PC transfer is not configured"}))
                continue
            cmd = ["scp"]
            if self.config.identity_file:
                cmd.extend(["-i", self.config.identity_file])
            cmd.extend([request.image_path, request.metadata_path, "%s@%s:%s" % (self.config.username, self.config.host, self.config.destination)])
            try:
                subprocess.run(cmd, check=True, timeout=30, capture_output=True, text=True)
            except (OSError, subprocess.SubprocessError) as error:
                self.events.put(Event(EventType.IMAGE_TRANSFER_ERROR, {"image_path": request.image_path, "error": str(error)}))


class Task1Kernel:
    """Single writer for Task 1 route and execution state."""

    def __init__(self, algo_requests, stm_commands, camera_requests, events, shutdown):
        self.algo_requests, self.stm_commands = algo_requests, stm_commands
        self.camera_requests, self.events, self.shutdown = camera_requests, events, shutdown
        self.state = MissionState.IDLE
        self.route: list[str] = []
        self.route_index = 0
        self.active_stm_command: Optional[str] = None

    def start(self, robot: Mapping[str, Any], obstacles: list[Mapping[str, Any]]):
        if self.state is not MissionState.IDLE:
            raise RuntimeError("Task 1 mission has already started")
        self.state = MissionState.WAITING_FOR_ALGO
        self.algo_requests.put(AlgoRequest(robot=dict(robot), obstacles=[dict(item) for item in obstacles]))

    def run(self):
        while self.state not in (MissionState.COMPLETE, MissionState.FAILED, MissionState.STOPPED):
            self.handle_event(self.events.get())
        return self.state

    def handle_event(self, event: Event):
        if event.type is EventType.SHUTDOWN:
            self.state = MissionState.STOPPED
        elif event.type is EventType.ALGO_ROUTE_RECEIVED:
            self._load_route(event.payload.get("commands"))
        elif event.type is EventType.ALGO_ERROR:
            self._fail("Algo error: %s" % event.payload.get("error"))
        elif event.type is EventType.STM_ZH_COMPLETE and self.state is MissionState.WAITING_FOR_ZH:
            print("[KERNEL] STM initialized")
            self.active_stm_command = None
            self._dispatch_route_item()
        elif event.type is EventType.STM_MOVEMENT_COMPLETE and self.state is MissionState.WAITING_FOR_STM:
            print("[KERNEL] movement %d complete" % (self.route_index + 1))
            self.route_index += 1
            self.active_stm_command = None
            self._dispatch_route_item()
        elif event.type is EventType.CV_RESULT_ACCEPTED and self.state is MissionState.WAITING_FOR_CV:
            print("[KERNEL] image accepted for obstacle %s" % event.payload.get("obstacle_id"))
            self.route_index += 1
            self._dispatch_route_item()
        elif event.type is EventType.CV_RETRY_REQUIRED and self.state is MissionState.WAITING_FOR_CV:
            self._fail("no symbol detected for obstacle %s" % event.payload.get("obstacle_id"))
        elif event.type is EventType.CV_ERROR and self.state is MissionState.WAITING_FOR_CV:
            self._fail("camera error: %s" % event.payload.get("error"))
        elif event.type in (EventType.STM_ABORT, EventType.STM_TIMEOUT, EventType.STM_ERROR, EventType.STM_UNKNOWN_RESPONSE):
            self._fail("STM %s: %s" % (event.type.value, event.payload.get("raw", event.payload.get("error", ""))))

    def _load_route(self, commands: Any):
        if self.state is not MissionState.WAITING_FOR_ALGO or not isinstance(commands, list):
            self._fail("unexpected or malformed Algo route")
            return
        self.route = commands
        self.route_index = 0
        self.state = MissionState.WAITING_FOR_ZH
        print("[KERNEL] route loaded: %d commands" % len(self.route))
        self.stm_commands.put(STMCommand("ZH"))

    def _dispatch_route_item(self):
        while self.route_index < len(self.route):
            command = self.route[self.route_index]
            if MOVEMENT_COMMAND.fullmatch(command):
                self.active_stm_command = command
                self.state = MissionState.WAITING_FOR_STM
                self.stm_commands.put(STMCommand(command))
                return
            if command.startswith("SNAP"):
                obstacle_id = command[4:]
                if not obstacle_id:
                    self._fail("invalid SNAP command: %s" % command)
                    return
                print("[KERNEL] requesting image scan for obstacle %s" % obstacle_id)
                self.state = MissionState.WAITING_FOR_CV
                self.camera_requests.put(CameraRequest(obstacle_id))
                return
            if command == "FIN":
                print("[KERNEL] FIN received")
                self.state = MissionState.COMPLETE
                return
            self._fail("unknown Algo route command: %s" % command)
            return
        self._fail("route ended without FIN")

    def _fail(self, message: str):
        print("[KERNEL] FAILED: %s" % message)
        self.state = MissionState.FAILED


class Task1Runtime:
    """Lifecycle wrapper; workers own resources, kernel owns all mission state."""

    def __init__(self, algo_connector, stm_connection, imaging=None, transfer_config=None, detections_dir="imaging/data"):
        self.shutdown = threading.Event()
        self.events = queue.Queue()
        self.algo_requests = queue.Queue()
        self.stm_commands = queue.Queue()
        self.camera_requests = queue.Queue()
        self.transfer_requests = queue.Queue(maxsize=32)
        self.kernel = Task1Kernel(
            self.algo_requests, self.stm_commands, self.camera_requests,
            self.events, self.shutdown,
        )
        self.workers = [
            AlgoWorker(algo_connector, self.algo_requests, self.events, self.shutdown),
            STMTXWorker(stm_connection, self.stm_commands, self.events, self.shutdown),
            STMRXWorker(stm_connection, self.events, self.shutdown),
            ImageTransferWorker(self.transfer_requests, self.events, self.shutdown, transfer_config or TransferConfig.from_env()),
        ]
        if imaging is not None:
            self.workers.append(CameraCVWorker(imaging, self.camera_requests, self.transfer_requests, self.events, self.shutdown, Path(detections_dir)))

    def start(self):
        for worker in self.workers:
            worker.start()

    def stop(self):
        self.shutdown.set()
        for work_queue in (self.algo_requests, self.stm_commands, self.camera_requests, self.transfer_requests):
            try:
                work_queue.put_nowait(None)
            except queue.Full:
                pass
        self.events.put(Event(EventType.SHUTDOWN))
        for worker in self.workers:
            worker.join(timeout=2)
