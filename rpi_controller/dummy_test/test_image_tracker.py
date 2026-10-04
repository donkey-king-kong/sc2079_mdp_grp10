import tempfile
import time
from pathlib import Path

from imaging.tracker import ImageTracker


def main():
    with tempfile.TemporaryDirectory() as temp_dir:
        data_dir = Path(temp_dir)
        tracker = ImageTracker(data_dir)

        # Pretend this image existed before SNAP.
        old_image = data_dir / "20260928_120000_000000_10.jpg"
        old_image.touch()

        before = tracker.snapshot()

        # Pretend imaging created this image during SNAP2.
        time.sleep(0.01)
        new_image = data_dir / "20260928_120001_000000_38.jpg"
        new_image.touch()

        selected = tracker.record_new_image("2", before)

        print("Selected:", selected.name if selected else None)
        print("Tracked:", tracker.selected_images)

        assert selected == new_image
        assert tracker.selected_images["2"] == new_image

        print("PASS")


if __name__ == "__main__":
    main()
