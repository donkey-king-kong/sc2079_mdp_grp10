import tempfile
from pathlib import Path

from PIL import Image

from imaging.stitcher import stitch_images


def main():
    with tempfile.TemporaryDirectory() as temp_dir:
        temp_dir = Path(temp_dir)

        image_paths = []

        sizes = [
            (300, 200),
            (400, 300),
            (250, 350),
        ]

        for index, size in enumerate(sizes, start=1):
            path = temp_dir / f"detection_{index}.jpg"

            image = Image.new(
                "RGB",
                size,
                "white",
            )

            image.save(path)
            image_paths.append(path)

        output_path = temp_dir / "stitched.jpg"

        result = stitch_images(
            image_paths,
            output_path,
        )

        print("Result:", result)
        print("Exists:", output_path.exists())

        assert result == output_path
        assert output_path.exists()

        with Image.open(output_path) as stitched:
            print("Final size:", stitched.size)

        print("PASS")


if __name__ == "__main__":
    main()
