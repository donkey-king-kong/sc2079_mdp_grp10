"""Bluetooth workers for Android communication during Task 1."""

from __future__ import annotations

import queue
import threading
from pathlib import Path

from image_sender import AndroidImageSender
from image_stitcher import stitch_images
from task1_events import AndroidImageRequest, AndroidMessage, Event, EventType


class AndroidRXWorker(threading.Thread):
    """Continuously receive Android messages over Bluetooth."""

    def __init__(
        self,
        bluetooth,
        android,
        events,
        shutdown,
    ):
        super().__init__(name="ANDROID-RX")

        self.bluetooth = bluetooth
        self.android = android
        self.events = events
        self.shutdown = shutdown

    def run(self):
        try:
            self.bluetooth.connect()
            print("[ANDROID RX] Bluetooth connected")

            while not self.shutdown.is_set():
                messages = self.bluetooth.read_messages()

                for message in messages:
                    self._handle_message(message)

        except Exception as error:
            if not self.shutdown.is_set():
                self.events.put(
                    Event(
                        EventType.ANDROID_ERROR,
                        {"error": str(error)},
                    )
                )

    def _handle_message(self, message):
        if not isinstance(message, dict):
            return

        category = message.get("cat")
        value = message.get("value")

        print(f"[ANDROID RX] {message}")

        if category == "sendArena":
            if not isinstance(value, dict):
                print("[ANDROID RX] sendArena has no arena data")
                return

            direction_map = {
                1: "N",
                2: "E",
                3: "S",
                4: "W",
            }
            robot = {
                "x": value.get("robot_x"),
                "y": value.get("robot_y"),
                "dir": direction_map.get(
                    value.get("robot_direction")
                ),
            }

            obstacles = []

            for obstacle in value.get("obstacles", []):
                obstacles.append({
                    "id": obstacle.get("id"),
                    "x": obstacle.get("x"),
                    "y": obstacle.get("y"),
                    "dir": direction_map.get(
                        obstacle.get("d")
                    ),
                })

            self.events.put(
                Event(
                    EventType.ANDROID_ARENA_RECEIVED,
                    {
                        "robot": robot,
                        "obstacles": obstacles,
                    },
                )
            )

        elif category == "stm":
            command = self.android.to_stm_command(value)

            if command is None:
                print(
                    f"[ANDROID RX] Invalid STM command: {value}"
                )
                return

            self.events.put(
                Event(
                    EventType.ANDROID_STM_COMMAND,
                    {"command": command},
                )
            )


class AndroidTXWorker(threading.Thread):
    """Serialize all outbound Android Bluetooth traffic."""

    def __init__(
        self,
        bluetooth,
        requests,
        events,
        shutdown,
        output_dir,
    ):
        super().__init__(name="ANDROID-TX")

        self.bluetooth = bluetooth
        self.requests = requests
        self.events = events
        self.shutdown = shutdown
        self.output_dir = Path(output_dir)

        self.image_sender = AndroidImageSender(
            bluetooth
        )

    def run(self):
        while True:
            request = self.requests.get()

            if request is None:
                return

            try:
                if isinstance(request, AndroidMessage):
                    self.bluetooth.send(request.message)

                    print(
                        f"[ANDROID TX] {request.message.strip()}"
                    )

                elif isinstance(request, AndroidImageRequest):
                    self._send_images(request.image_paths)

            except Exception as error:
                if not self.shutdown.is_set():
                    self.events.put(
                        Event(
                            EventType.ANDROID_ERROR,
                            {"error": str(error)},
                        )
                    )

    def _send_images(self, image_paths):
        if not image_paths:
            print("[ANDROID TX] No images to send")
            return

        output_path = (
            self.output_dir / "stitched.jpg"
        )

        stitched_path = stitch_images(
            image_paths,
            output_path,
        )

        if stitched_path is None:
            raise RuntimeError(
                "failed to create stitched image"
            )

        if not self.image_sender.send_image(
            stitched_path
        ):
            raise RuntimeError(
                "failed to send stitched image"
            )

        print(
            f"[ANDROID TX] Final image sent "
            f"from {len(image_paths)} detections"
        )
