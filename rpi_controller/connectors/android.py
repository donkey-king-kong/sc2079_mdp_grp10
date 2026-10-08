"""Android message parsing and stitched-image transport helpers."""

import base64
import json
from pathlib import Path
import time


class AndroidConnector:
    def parse_message(self, raw_message: str):
        raw_message = raw_message.strip()

        if not raw_message:
            return None

        # Plain-text command used by the Android app
        if raw_message == "sendArena":
            return {
                "cat": "sendArena",
                "value": None,
            }

        # JSON messages
        try:
            return json.loads(raw_message)

        except json.JSONDecodeError:
            return {
                "cat": "unknown",
                "value": raw_message,
            }

    def to_stm_command(self, android_command: str):
        if not android_command.startswith("<") or not android_command.endswith(">"):
            return None

        command = android_command[1:-1]

        if len(command) < 3:
            return None

        prefix = command[:2]
        value = command[2:]

        prefix_map = {
            "FW": "SF",
            "BW": "SB",
            "FL": "LF",
            "FR": "RF",
            "BL": "LB",
            "BR": "RB",
        }

        stm_prefix = prefix_map.get(prefix)

        if stm_prefix is None:
            return None

        return stm_prefix + value


class AndroidStreamParser:
    """Incrementally parse concatenated Android Bluetooth messages."""

    def __init__(self):
        self.buffer = ""
        self.decoder = json.JSONDecoder()
        self.plain_commands = {
            "sendArena",
            "beginExplore",
            "beginFastest",
            "tr",
            "tl",
        }

    def feed(self, chunk: str):
        self.buffer += chunk
        messages = []

        while self.buffer:
            self.buffer = self.buffer.lstrip()
            if not self.buffer:
                break

            if self.buffer.startswith("{"):
                try:
                    message, index = self.decoder.raw_decode(self.buffer)
                    messages.append(message)
                    self.buffer = self.buffer[index:]
                    continue
                except json.JSONDecodeError:
                    break

            matched_command = next(
                (
                    command
                    for command in self.plain_commands
                    if self.buffer.startswith(command)
                ),
                None,
            )
            if matched_command is not None:
                messages.append({"cat": matched_command, "value": None})
                self.buffer = self.buffer[len(matched_command):]
                continue

            if any(command.startswith(self.buffer) for command in self.plain_commands):
                break
            self.buffer = self.buffer[1:]

        return messages


class AndroidImageSender:
    """Serialize and send a stitched image over the Android Bluetooth protocol."""

    def __init__(self, bluetooth, chunk_size=700, delay_seconds=0.05):
        self.bluetooth = bluetooth
        self.chunk_size = chunk_size
        self.delay_seconds = delay_seconds

    def _send_value(self, value):
        self.bluetooth.send(
            json.dumps({"cat": "stitch-image", "value": value}) + "\n"
        )
        time.sleep(self.delay_seconds)

    def send_image(self, image_path):
        image_path = Path(image_path)
        if not image_path.is_file():
            print(f"[IMAGE SENDER] Image not found: {image_path}")
            return False

        encoded = base64.b64encode(image_path.read_bytes()).decode("ascii")
        print(f"[IMAGE SENDER] Sending {image_path.name}")
        print(f"[IMAGE SENDER] Base64 size: {len(encoded)} chars")
        self._send_value("starting stitch")

        chunk_count = 0
        for start in range(0, len(encoded), self.chunk_size):
            self._send_value(encoded[start:start + self.chunk_size])
            chunk_count += 1

        self._send_value("ending stitch")
        print(f"[IMAGE SENDER] Sent {chunk_count} chunks")
        return True
