"""Compose captured detection images into a single Android-ready JPEG."""

from pathlib import Path

from PIL import Image, ImageOps


def stitch_images(
    image_paths,
    output_path,
    image_width=400,
    padding=10,
):
    """
    Combine selected detection images horizontally into one JPEG.

    Each image is resized to the same width while preserving
    its aspect ratio.
    """
    paths = [Path(path) for path in image_paths]

    if not paths:
        print("[STITCHER] No images to stitch")
        return None

    images = []

    for path in paths:
        if not path.is_file():
            print(f"[STITCHER] Missing image: {path}")
            continue

        with Image.open(path) as image:
            image = image.convert("RGB")

            ratio = image_width / image.width
            new_height = int(image.height * ratio)

            resized = image.resize(
                (image_width, new_height)
            )

            images.append(resized.copy())

    if not images:
        print("[STITCHER] No valid images found")
        return None

    max_height = max(image.height for image in images)

    # Give every image the same height using white padding.
    normalized = []

    for image in images:
        bottom_padding = max_height - image.height

        normalized.append(
            ImageOps.expand(
                image,
                border=(0, 0, 0, bottom_padding),
                fill="white",
            )
        )

    total_width = (
        sum(image.width for image in normalized)
        + padding * (len(normalized) - 1)
    )

    stitched = Image.new(
        "RGB",
        (total_width, max_height),
        "white",
    )

    x = 0

    for image in normalized:
        stitched.paste(image, (x, 0))
        x += image.width + padding

    output_path = Path(output_path)
    output_path.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    stitched.save(
        output_path,
        format="JPEG",
        quality=85,
    )

    print(
        f"[STITCHER] Created {output_path} "
        f"from {len(normalized)} images"
    )

    return output_path
