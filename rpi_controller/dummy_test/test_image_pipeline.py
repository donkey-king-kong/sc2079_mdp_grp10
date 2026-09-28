import json
import tempfile
from pathlib import Path

from PIL import Image

from image_tracker import ImageTracker
from image_stitcher import stitch_images
from image_sender import AndroidImageSender


class FakeBluetooth:
    def __init__(self):
        self.messages = []

    def send(self, message):
        self.messages.append(message)


def create_detection(data_dir, obstacle_id, image_id):
    before = set(data_dir.glob("*.jpg"))

    image_path = (
        data_dir
        / f"20260928_12000{obstacle_id}_000000_{image_id}.jpg"
    )

    Image.new(
        "RGB",
        (300, 200),
        "white",
    ).save(image_path)

    return before, image_path


def main():
    with tempfile.TemporaryDirectory() as temp_dir:
        data_dir = Path(temp_dir)

        tracker = ImageTracker(data_dir)
        bluetooth = FakeBluetooth()

        sender = AndroidImageSender(
            bluetooth,
            chunk_size=700,
            delay_seconds=0,
        )

        # ---- SNAP1 ----

        before, _ = create_detection(
            data_dir,
            obstacle_id="1",
            image_id="38",
        )

        tracker.record_new_image(
            "1",
            before,
        )

        bluetooth.send("TARGET,1,38\n")

        # ---- SNAP2 ----

        before, _ = create_detection(
            data_dir,
            obstacle_id="2",
            image_id="20",
        )

        tracker.record_new_image(
            "2",
            before,
        )

        bluetooth.send("TARGET,2,20\n")

        # ---- FIN ----

        selected_images = tracker.get_selected_images()

        assert len(selected_images) == 2

        stitched_path = stitch_images(
            selected_images,
            data_dir / "stitched.jpg",
        )

        assert stitched_path is not None
        assert stitched_path.exists()

        sender.send_image(stitched_path)

        # ---- Verify Bluetooth output ----

        assert bluetooth.messages[0] == "TARGET,1,38\n"
        assert bluetooth.messages[1] == "TARGET,2,20\n"

        image_messages = [
            json.loads(message)
            for message in bluetooth.messages[2:]
        ]

        assert image_messages[0] == {
            "cat": "stitch-image",
            "value": "starting stitch",
        }

        assert image_messages[-1] == {
            "cat": "stitch-image",
            "value": "ending stitch",
        }

        chunks = image_messages[1:-1]

        assert len(chunks) > 0

        print("Tracked images:", len(selected_images))
        print("Stitched image:", stitched_path.name)
        print("TARGET messages: 2")
        print("Image chunks:", len(chunks))
        print("PASS")


if __name__ == "__main__":
    main()
