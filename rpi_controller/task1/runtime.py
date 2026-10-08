"""Task 1 mission state machine and worker lifecycle."""

from __future__ import annotations

from enum import Enum
from pathlib import Path
import queue
import threading
from typing import Any, Mapping, Optional

from connectors.stm import MOVEMENT_COMMAND
from task1.events import (
    AlgoRequest,
    AndroidImageRequest,
    AndroidMessage,
    CameraRequest,
    Event,
    EventType,
    STMCommand,
)
from task1.workers.algo import AlgoWorker
from task1.workers.android import AndroidRXWorker, AndroidTXWorker
from task1.workers.camera import CameraCVWorker, InferenceWorker
from task1.workers.stm import STMRXWorker, STMTXWorker
from task1.workers.transfer import ImageTransferWorker, TransferConfig


class MissionState(str, Enum):
    IDLE = "IDLE"
    WAITING_FOR_ALGO = "WAITING_FOR_ALGO"
    WAITING_FOR_ZH = "WAITING_FOR_ZH"
    WAITING_FOR_STM = "WAITING_FOR_STM"
    WAITING_FOR_CV = "WAITING_FOR_CV"
    WAITING_FOR_CAPTURE = "WAITING_FOR_CAPTURE"
    WAITING_FOR_INFERENCE = "WAITING_FOR_INFERENCE"
    WAITING_FOR_ANDROID_IMAGE_TRANSFER = "WAITING_FOR_ANDROID_IMAGE_TRANSFER"
    COMPLETE = "COMPLETE"
    FAILED = "FAILED"
    STOPPED = "STOPPED"


class Task1Kernel:
    """Single writer for Task 1 route and execution state."""

    def __init__(
        self,
        algo_requests,
        stm_commands,
        camera_requests,
        events,
        shutdown,
        android_requests=None,
        parallel_inference: bool = True,
        wait_for_start: bool = False,
    ):
        self.algo_requests, self.stm_commands = algo_requests, stm_commands
        self.camera_requests, self.events, self.shutdown = camera_requests, events, shutdown
        self.android_requests = android_requests
        self.parallel_inference = parallel_inference
        self.wait_for_start = wait_for_start
        self.detected_images = []
        self.state = MissionState.IDLE
        self.route: list[str] = []
        self.route_index = 0
        self.active_stm_command: Optional[str] = None
        self.active_scan_obstacle: Optional[str] = None
        self.pending_inference_jobs = 0
        self.pending_arena: Optional[tuple[Mapping[str, Any], list[Mapping[str, Any]]]] = None

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
        elif event.type is EventType.ANDROID_ARENA_RECEIVED:
            if self.state is not MissionState.IDLE:
                print("[KERNEL] Ignoring arena: mission already active")
                return

            robot = event.payload.get("robot")
            obstacles = event.payload.get("obstacles")

            if not isinstance(robot, dict) or not isinstance(obstacles, list):
                self._fail("invalid Android arena data")
                return

            self.pending_arena = (dict(robot), [dict(item) for item in obstacles])
            if self.wait_for_start:
                print(
                    "[KERNEL] Android arena saved: %d obstacles; "
                    "waiting for Android start command" % len(obstacles)
                )
                return

            print(
                "[KERNEL] Android arena received: %d obstacles; "
                "starting Algo request" % len(obstacles)
            )
            self.start(robot, obstacles)
        elif event.type is EventType.ANDROID_START_REQUEST:
            if self.state is not MissionState.IDLE:
                print("[KERNEL] Ignoring Android start: mission already active")
                return
            if self.pending_arena is None:
                print("[KERNEL] Ignoring Android start: no arena has been received")
                return

            robot, obstacles = self.pending_arena
            print(
                "[KERNEL] Android start received (%s): %d obstacles"
                % (event.payload.get("mode"), len(obstacles))
            )
            self.start(robot, obstacles)
        elif event.type is EventType.ANDROID_STM_COMMAND:
            command = event.payload.get("command")

            if not isinstance(command, str):
                print("[KERNEL] Invalid Android STM command")
                return

            if self.state is not MissionState.IDLE:
                print(
                    "[KERNEL] Ignoring manual STM command "
                    "while mission is active"
                )
                return

            print(
                f"[KERNEL] Android manual STM command: {command}"
            )

            self.stm_commands.put(
                STMCommand(command)
            )

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
        elif (
            event.type is EventType.CV_CAPTURED
            and self.state is MissionState.WAITING_FOR_CAPTURE
        ):
            print(
                "[KERNEL] image captured for obstacle %s; inference continuing in background"
                % event.payload.get("obstacle_id")
            )
            self.route_index += 1
            self.active_scan_obstacle = None
            self._dispatch_route_item()
        elif event.type is EventType.CV_RESULT_ACCEPTED:
            print(
                "[KERNEL] image accepted for obstacle %s"
                % event.payload.get("obstacle_id")
            )
            result = event.payload.get("result", {})
            obstacle_id = event.payload.get("obstacle_id")
            image_id = result.get("image_id")
            image_path = result.get("image_path")

            if image_path:
                self.detected_images.append(str(image_path))

            if (
                self.android_requests is not None
                and obstacle_id is not None
                and image_id is not None
            ):
                self.android_requests.put(
                    AndroidMessage(
                        f"TARGET,{obstacle_id},{image_id}\n"
                    )
                )
            if self.parallel_inference:
                self._inference_finished()
            else:
                self._sequential_scan_finished()
        elif event.type is EventType.CV_RETRY_REQUIRED:
            print(
                "[KERNEL] no symbol for obstacle %s; capture saved, continuing"
                % event.payload.get("obstacle_id")
            )
            if self.parallel_inference:
                self._inference_finished()
            else:
                self._sequential_scan_finished()
        elif event.type is EventType.CV_CONTINUOUS_RESULT:
            # Informational only: continuous detection must never advance or
            # satisfy an Algo SNAP command.
            print(
                "[KERNEL] continuous image saved: %s"
                % event.payload.get("result", {}).get("image_path")
            )
        elif event.type is EventType.CV_ERROR:
            if event.payload.get("stage") == "inference":
                print("[KERNEL] background inference failed: %s" % event.payload.get("error"))
                self._inference_finished()
            elif self.state in (
                MissionState.WAITING_FOR_CAPTURE,
                MissionState.WAITING_FOR_CV,
            ):
                self._fail("camera capture error: %s" % event.payload.get("error"))
        elif (
            event.type is EventType.ANDROID_IMAGE_TRANSFER_COMPLETE
            and self.state is MissionState.WAITING_FOR_ANDROID_IMAGE_TRANSFER
        ):
            print("[KERNEL] final Android image transfer complete")
            self.state = MissionState.COMPLETE
        elif (
            event.type is EventType.ANDROID_ERROR
            and self.state is MissionState.WAITING_FOR_ANDROID_IMAGE_TRANSFER
        ):
            self._fail("Android image transfer failed: %s" % event.payload.get("error"))
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
                self.state = (
                    MissionState.WAITING_FOR_CAPTURE
                    if self.parallel_inference
                    else MissionState.WAITING_FOR_CV
                )
                self.active_scan_obstacle = obstacle_id
                if self.parallel_inference:
                    self.pending_inference_jobs += 1
                self.camera_requests.put(CameraRequest(obstacle_id))
                return
            if command == "FIN":
                print("[KERNEL] FIN received")

                if self.pending_inference_jobs:
                    print(
                        "[KERNEL] waiting for %d background image job(s)"
                        % self.pending_inference_jobs
                    )
                    self.state = MissionState.WAITING_FOR_INFERENCE
                    return
                self._complete_after_inference()
                return
            self._fail("unknown Algo route command: %s" % command)
            return
        self._fail("route ended without FIN")

    def _inference_finished(self):
        self.pending_inference_jobs = max(0, self.pending_inference_jobs - 1)
        if self.state is MissionState.WAITING_FOR_INFERENCE and not self.pending_inference_jobs:
            self._complete_after_inference()

    def _sequential_scan_finished(self):
        self.route_index += 1
        self.active_scan_obstacle = None
        self._dispatch_route_item()

    def _complete_after_inference(self):
        if (
            self.android_requests is not None
            and self.detected_images
        ):
            self.android_requests.put(AndroidImageRequest(list(self.detected_images)))
            self.state = MissionState.WAITING_FOR_ANDROID_IMAGE_TRANSFER
            return
        self.state = MissionState.COMPLETE

    def _fail(self, message: str):
        print("[KERNEL] FAILED: %s" % message)
        self.state = MissionState.FAILED


class Task1Runtime:
    """Lifecycle wrapper; workers own resources, kernel owns all mission state."""

    def __init__(
        self, algo_connector, stm_connection, imaging=None, transfer_config=None,
        detections_dir="imaging/data", continuous_camera_scan: bool = False,
        continuous_scan_interval_seconds: float = 1.0,
        bluetooth=None, android=None,
        on_android_disconnect=None,
        wait_for_start: bool = False,
        parallel_inference: bool = True,
    ):
        self.shutdown = threading.Event()
        self.events = queue.Queue()
        self.algo_requests = queue.Queue()
        self.stm_commands = queue.Queue()
        self.camera_requests = queue.Queue()
        self.inference_requests = queue.Queue()
        self.android_requests = queue.Queue()
        self.transfer_requests = queue.Queue(maxsize=32)
        self.kernel = Task1Kernel(
            self.algo_requests,
            self.stm_commands,
            self.camera_requests,
            self.events,
            self.shutdown,
            self.android_requests,
            parallel_inference,
            wait_for_start,
        )
        self.workers = [
            AlgoWorker(algo_connector, self.algo_requests, self.events, self.shutdown),
            STMTXWorker(stm_connection, self.stm_commands, self.events, self.shutdown),
            STMRXWorker(stm_connection, self.events, self.shutdown),
            ImageTransferWorker(self.transfer_requests, self.events, self.shutdown, transfer_config or TransferConfig.from_env()),
        ]
        if imaging is not None:
            self.workers.append(
                CameraCVWorker(
                    imaging,
                    self.camera_requests,
                    self.transfer_requests,
                    self.events,
                    self.shutdown,
                    Path(detections_dir),
                    continuous_camera_scan,
                    continuous_scan_interval_seconds,
                    inference_requests=self.inference_requests,
                    parallel_inference=parallel_inference,
                )
            )
            if parallel_inference:
                self.workers.append(
                    InferenceWorker(
                        imaging,
                        self.inference_requests,
                        self.transfer_requests,
                        self.events,
                        self.shutdown,
                        Path(detections_dir),
                    )
                )
        if bluetooth is not None and android is not None:
            self.workers.extend([
                AndroidRXWorker(
                    bluetooth,
                    android,
                    self.events,
                    self.shutdown,
                    on_disconnect=on_android_disconnect,
                ),
                AndroidTXWorker(
                    bluetooth,
                    self.android_requests,
                    self.events,
                    self.shutdown,
                    Path(detections_dir),
                ),
            ])

    def start(self):
        for worker in self.workers:
            worker.start()

    def stop(self):
        self.shutdown.set()
        for work_queue in (
            self.algo_requests,
            self.stm_commands,
            self.camera_requests,
            self.inference_requests,
            self.transfer_requests,
            self.android_requests,
        ):
            try:
                work_queue.put_nowait(None)
            except queue.Full:
                pass
        self.events.put(Event(EventType.SHUTDOWN))
        for worker in self.workers:
            worker.join(timeout=2)
