"""Task 1 preprocessing variants adapted from the benchmark experiments."""

from __future__ import annotations

import cv2
import numpy as np


def relative_contrast_variants(image: np.ndarray) -> dict[str, np.ndarray]:
    """Maximise local foreground/background separation in both polarities.

    A heavily blurred luminance image estimates the nearby board/background.
    The signed difference isolates pixels that are respectively darker or
    lighter than that background.  Each response is stretched to black/white:
    a dark symbol on a light board becomes black on white, and a light symbol
    on a dark board becomes white on black.
    """
    if image.ndim != 3 or image.shape[2] != 3:
        raise ValueError("expected an HxWx3 RGB image")

    gray = cv2.cvtColor(image, cv2.COLOR_RGB2GRAY)
    background = cv2.GaussianBlur(gray, (0, 0), sigmaX=31, sigmaY=31)

    def stretch(response: np.ndarray) -> np.ndarray:
        low, high = np.percentile(response, (2, 99))
        if high <= low:
            return np.zeros_like(response)
        return np.clip((response.astype(np.float32) - low) * 255 / (high - low), 0, 255).astype(np.uint8)

    darker_than_background = stretch(cv2.subtract(background, gray))
    lighter_than_background = stretch(cv2.subtract(gray, background))
    return {
        "relative_dark_symbol_light_background": cv2.cvtColor(
            cv2.bitwise_not(darker_than_background), cv2.COLOR_GRAY2RGB,
        ),
        "relative_light_symbol_dark_background": cv2.cvtColor(
            lighter_than_background, cv2.COLOR_GRAY2RGB,
        ),
    }


def task1_preprocessing_variants(image: np.ndarray) -> dict[str, np.ndarray]:
    """Return RGB preprocessing candidates, retaining source-sized output."""
    if image.ndim != 3 or image.shape[2] != 3:
        raise ValueError("expected an HxWx3 RGB image")

    gray = cv2.cvtColor(image, cv2.COLOR_RGB2GRAY)
    clahe = cv2.createCLAHE(clipLimit=4.0, tileGridSize=(8, 8))
    gray_clahe = clahe.apply(gray)

    lab = cv2.cvtColor(image, cv2.COLOR_RGB2LAB)
    lab[:, :, 0] = clahe.apply(lab[:, :, 0])
    lab_clahe = cv2.cvtColor(lab, cv2.COLOR_LAB2RGB)

    blurred = cv2.GaussianBlur(gray_clahe, (5, 5), 0)
    _, otsu = cv2.threshold(blurred, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    adaptive = cv2.adaptiveThreshold(
        gray_clahe,
        255,
        cv2.ADAPTIVE_THRESH_GAUSSIAN_C,
        cv2.THRESH_BINARY,
        51,
        4,
    )
    adaptive_inverse = cv2.bitwise_not(adaptive)
    unsharp_lab = cv2.addWeighted(
        lab_clahe,
        1.6,
        cv2.GaussianBlur(lab_clahe, (0, 0), 3),
        -0.6,
        0,
    )
    height, width = image.shape[:2]
    crop_5 = image[int(height * 0.05):int(height * 0.95), int(width * 0.05):int(width * 0.95)]
    crop_10 = image[int(height * 0.10):int(height * 0.90), int(width * 0.10):int(width * 0.90)]

    variants = {
        "original": image,
        "grayscale_clahe": cv2.cvtColor(gray_clahe, cv2.COLOR_GRAY2RGB),
        "lab_clahe": lab_clahe,
        "lab_clahe_unsharp": unsharp_lab,
        # The paired local-threshold outputs deliberately discard colour.  They
        # maximise symbol/background separation for both possible polarities:
        # a light symbol on a dark board and a dark symbol on a light board.
        "bright_symbol_dark_background": cv2.cvtColor(adaptive, cv2.COLOR_GRAY2RGB),
        "dark_symbol_light_background": cv2.cvtColor(adaptive_inverse, cv2.COLOR_GRAY2RGB),
        "crop_5_percent": cv2.resize(crop_5, (width, height), interpolation=cv2.INTER_CUBIC),
        "crop_10_percent": cv2.resize(crop_10, (width, height), interpolation=cv2.INTER_CUBIC),
        "otsu_dark_on_light": cv2.cvtColor(otsu, cv2.COLOR_GRAY2RGB),
        "otsu_bright_to_black": cv2.cvtColor(cv2.bitwise_not(otsu), cv2.COLOR_GRAY2RGB),
    }
    variants.update(relative_contrast_variants(image))
    return variants


def map_preprocessed_bbox_to_source(
    variant_name: str, bbox: tuple[int, int, int, int], source_shape: tuple[int, ...],
) -> tuple[int, int, int, int]:
    """Map a source-sized preprocessing detection back to the original frame.

    Only the crop-and-resize variants change image geometry.  All other
    transformations retain the same pixel coordinates.
    """
    crop_fraction = {"crop_5_percent": 0.05, "crop_10_percent": 0.10}.get(variant_name)
    if crop_fraction is None:
        return bbox

    height, width = source_shape[:2]
    x_offset, y_offset = int(width * crop_fraction), int(height * crop_fraction)
    crop_width, crop_height = width - 2 * x_offset, height - 2 * y_offset
    x1, y1, x2, y2 = bbox
    return (
        max(0, min(width, round(x_offset + x1 * crop_width / width))),
        max(0, min(height, round(y_offset + y1 * crop_height / height))),
        max(0, min(width, round(x_offset + x2 * crop_width / width))),
        max(0, min(height, round(y_offset + y2 * crop_height / height))),
    )
