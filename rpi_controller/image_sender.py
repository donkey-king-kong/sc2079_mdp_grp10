import base64
import json
import time
from pathlib import Path


class AndroidImageSender:
    def __init__(
        self,
        bluetooth,
        chunk_size=700,
        delay_seconds=0.05,
    ):
        self.bluetooth = bluetooth
        self.chunk_size = chunk_size
        self.delay_seconds = delay_seconds

    def _send_value(self, value):
        message = json.dumps({
            "cat": "stitch-image",
            "value": value,
        })

        self.bluetooth.send(message)
        time.sleep(self.delay_seconds)

    def send_image(self, image_path):
        image_path = Path(image_path)

        if not image_path.is_file():
            print(
                f"[IMAGE SENDER] Image not found: {image_path}"
            )
            return False

        # Convert JPEG bytes into Base64 text.
        image_bytes = image_path.read_bytes()

        encoded = base64.b64encode(
            image_bytes
        ).decode("ascii")

        print(
            f"[IMAGE SENDER] Sending {image_path.name}"
        )
        print(
            f"[IMAGE SENDER] Base64 size: {len(encoded)} chars"
        )

        # Tell Android to clear its existing Base64 buffer.
        self._send_value("starting stitch")

        chunk_count = 0

        for start in range(
            0,
            len(encoded),
            self.chunk_size,
        ):
            chunk = encoded[
                start:start + self.chunk_size
            ]

            self._send_value(chunk)
            chunk_count += 1

        # Tell Android that all chunks have arrived.
        self._send_value("ending stitch")

        print(
            f"[IMAGE SENDER] Sent {chunk_count} chunks"
        )

        return True
