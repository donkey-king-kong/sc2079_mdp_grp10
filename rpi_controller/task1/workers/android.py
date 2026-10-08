"""Android Bluetooth workers for Task 1."""

from __future__ import annotations

import json
import queue
import threading
import time
from pathlib import Path

from connectors.android import AndroidImageSender
from connectors.bluetooth import BluetoothDisconnectedError
from imaging.stitcher import stitch_images
from task1.events import AndroidImageRequest, AndroidMessage, Event, EventType


DIRECTION_MAP = {
    0: "N",
    1: "E",
    2: "S",
    3: "W",
}


class AndroidRXWorker(threading.Thread):
    """Continuously receive Android messages over Bluetooth."""

    def __init__(
        self,
        bluetooth,
        android,
        events,
        shutdown,
        on_disconnect=None,
        reconnect_delay_seconds=1,
    ):
        super().__init__(name="ANDROID-RX")

        self.bluetooth = bluetooth
        self.android = android
        self.events = events
        self.shutdown = shutdown
        self.on_disconnect = on_disconnect
        self.reconnect_delay_seconds = reconnect_delay_seconds

    def run(self):
        while not self.shutdown.is_set():
            connected = False
            try:
                self.bluetooth.connect()
                connected = True
                print("[ANDROID RX] Bluetooth connected")

                while not self.shutdown.is_set():
                    for message in self.bluetooth.read_messages():
                        self._handle_message(message)

            except BluetoothDisconnectedError as error:
                if not self.shutdown.is_set():
                    print(f"[ANDROID RX] Android disconnected: {error}")
            except Exception as error:
                if self.shutdown.is_set():
                    break
                print(f"[ANDROID RX] Bluetooth error: {error}")
                self.events.put(
                    Event(
                        EventType.ANDROID_ERROR,
                        {"error": str(error)},
                    )
                )
            finally:
                if connected:
                    self.bluetooth.disconnect()

            if self.shutdown.is_set():
                break

            self._reset_bluetooth_service()
            if not self.shutdown.is_set():
                time.sleep(self.reconnect_delay_seconds)

    def _reset_bluetooth_service(self):
        if self.on_disconnect is None:
            print("[ANDROID RX] Waiting for Android to reconnect")
            return

        print("[ANDROID RX] Resetting Bluetooth service after disconnect")
        try:
            self.on_disconnect()
        except Exception as error:
            print(f"[ANDROID RX] Bluetooth service reset failed: {error}")
            self.events.put(
                Event(
                    EventType.ANDROID_ERROR,
                    {"error": f"Bluetooth service reset failed: {error}"},
                )
            )

    def _handle_message(self, message):
        if not isinstance(message, dict):
            return

        category = message.get("cat")
        value = message.get("value")

        if category == "sendArena":
            self._print_arena(message)
        else:
            print(f"[ANDROID RX] {message}")

        if category == "sendArena":
            if not isinstance(value, dict):
                print("[ANDROID RX] sendArena has no arena data")
                return

            robot = {
                "x": value.get("robot_x"),
                "y": value.get("robot_y"),
                "dir": DIRECTION_MAP.get(
                    value.get("robot_direction")
                ),
            }

            obstacles = []

            for obstacle in value.get("obstacles", []):
                obstacles.append({
                    "id": obstacle.get("id"),
                    "x": obstacle.get("x"),
                    "y": obstacle.get("y"),
                    "dir": DIRECTION_MAP.get(
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
            if value in {"beginExplore", "beginFastest"}:
                self.events.put(
                    Event(
                        EventType.ANDROID_START_REQUEST,
                        {"mode": value},
                    )
                )
                return

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

        elif category in {"beginExplore", "beginFastest"}:
            self.events.put(
                Event(
                    EventType.ANDROID_START_REQUEST,
                    {"mode": category},
                )
            )

    @staticmethod
    def _print_arena(message):
        """Print Android's arena JSON with human-readable direction codes."""
        formatted = json.loads(json.dumps(message))
        value = formatted.get("value")

        if isinstance(value, dict):
            value["robot_direction"] = AndroidRXWorker._format_direction(
                value.get("robot_direction")
            )
            obstacles = value.get("obstacles")
            if isinstance(obstacles, list):
                for obstacle in obstacles:
                    if isinstance(obstacle, dict):
                        obstacle["d"] = AndroidRXWorker._format_direction(
                            obstacle.get("d")
                        )

        print("[ANDROID RX] Arena received:")
        print(json.dumps(formatted, indent=2, sort_keys=True))

    @staticmethod
    def _format_direction(direction):
        label = DIRECTION_MAP.get(direction)
        if label is None:
            return f"{direction} (invalid direction)"
        return f"{direction} ({label})"


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
        self.events.put(
            Event(
                EventType.ANDROID_IMAGE_TRANSFER_COMPLETE,
                {"image_path": str(stitched_path)},
            )
        )
