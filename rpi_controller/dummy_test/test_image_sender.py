import json
import tempfile
from pathlib import Path

from PIL import Image

from connectors.android import AndroidImageSender


class FakeBluetooth:
    def __init__(self):
        self.messages = []

    def send(self, message):
        self.messages.append(message)


def main():
    with tempfile.TemporaryDirectory() as temp_dir:
        image_path = Path(temp_dir) / "stitched.jpg"

        # Create a small fake stitched image.
        Image.new(
            "RGB",
            (800, 300),
            "white",
        ).save(image_path, format="JPEG")

        bluetooth = FakeBluetooth()

        sender = AndroidImageSender(
            bluetooth,
            chunk_size=700,
            delay_seconds=0,
        )

        success = sender.send_image(image_path)

        assert success
        assert len(bluetooth.messages) >= 3

        decoded = [
            json.loads(message)
            for message in bluetooth.messages
        ]

        # First message must start transmission.
        assert decoded[0] == {
            "cat": "stitch-image",
            "value": "starting stitch",
        }

        # Last message must end transmission.
        assert decoded[-1] == {
            "cat": "stitch-image",
            "value": "ending stitch",
        }

        # Everything between them should be Base64 chunks.
        chunks = decoded[1:-1]

        assert all(
            message["cat"] == "stitch-image"
            for message in chunks
        )

        assert all(
            len(message["value"]) <= 700
            for message in chunks
        )

        print("Messages:", len(decoded))
        print("Data chunks:", len(chunks))
        print("Largest chunk:", max(
            len(message["value"])
            for message in chunks
        ))

        print("PASS")


if __name__ == "__main__":
    main()
