from pathlib import Path


class ImageTracker:
    def __init__(self, data_dir: Path):
        self.data_dir = Path(data_dir)

        # obstacle_id -> selected image path
        self.selected_images = {}

    def snapshot(self):
        """Return all JPEG files currently in the imaging data directory."""
        if not self.data_dir.exists():
            return set()

        return set(self.data_dir.glob("*.jpg"))

    def record_new_image(self, obstacle_id, before):
        """
        Find the image created since the previous snapshot and associate
        it with this obstacle.
        """
        after = self.snapshot()
        new_files = after - before

        if not new_files:
            print(
                f"[IMAGE TRACKER] No new image for obstacle {obstacle_id}"
            )
            return None

        # Normally one SNAP creates one file.
        # If more than one appears, use the newest.
        selected = max(
            new_files,
            key=lambda path: path.stat().st_mtime_ns,
        )

        self.selected_images[str(obstacle_id)] = selected

        print(
            f"[IMAGE TRACKER] Obstacle {obstacle_id} -> {selected.name}"
        )

        return selected

    def get_selected_images(self):
        """Return selected images in obstacle insertion order."""
        return list(self.selected_images.values())

    def clear(self):
        """Clear images tracked for the current Task 1 run."""
        self.selected_images.clear()
