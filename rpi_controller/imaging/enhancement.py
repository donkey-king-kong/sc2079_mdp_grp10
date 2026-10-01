"""Lightweight, dependency-free frame enhancements for difficult captures.

These are deliberately applied only as inference fallbacks.  The normal camera
frame remains the first input to the model, so good lighting has no added
latency or colour transformation.
"""

from __future__ import annotations

import math
import numpy as np
from PIL import Image


def _percentile_stretch(frame: np.ndarray, low: float, high: float) -> np.ndarray:
    """Expand useful RGB values while discarding a small outlier tail."""
    pixels = frame.astype(np.float32)
    black, white = np.percentile(pixels, (low, high), axis=(0, 1), keepdims=True)
    # A nearly flat channel cannot be meaningfully stretched.  Keeping it
    # unchanged avoids amplifying sensor noise in a uniformly coloured scene.
    scale = np.where(white - black > 1.0, 255.0 / (white - black), 1.0)
    return np.clip((pixels - black) * scale, 0, 255).astype(np.uint8)


def _brighten_shadows(frame: np.ndarray, gamma: float = 0.72) -> np.ndarray:
    """Lift shadow detail without clipping bright regions as a gain would."""
    normalized = frame.astype(np.float32) / 255.0
    return np.clip(255.0 * np.power(normalized, gamma), 0, 255).astype(np.uint8)


def contrast_variants(frame: np.ndarray) -> tuple[np.ndarray, ...]:
    """Return two complementary alternatives for a dark or low-contrast frame.

    The source must be an HxWx3 uint8 RGB array, as returned by ``Picamera2``.
    The returned frames preserve exactly the same geometry, so detector bounding
    boxes can be drawn and saved without coordinate conversion.
    """
    if frame.ndim != 3 or frame.shape[2] != 3:
        raise ValueError("expected an HxWx3 RGB camera frame")
    if frame.dtype != np.uint8:
        frame = np.clip(frame, 0, 255).astype(np.uint8)

    # One version recovers details hidden in shadows; the other expands subtle
    # colour/luminance differences between a symbol and a similar background.
    return (
        _brighten_shadows(frame),
        _percentile_stretch(frame, low=2.0, high=98.0),
    )


def rotate_frame(frame: np.ndarray, angle: int) -> np.ndarray:
    """Rotate an RGB inference pass while preserving the original untouched frame."""
    if angle == 0:
        return frame
    return np.asarray(Image.fromarray(frame).rotate(angle, expand=True, fillcolor=(0, 0, 0)))


def map_rotated_bbox_to_source(bbox, source_shape: tuple[int, ...], rotated_shape: tuple[int, ...], angle: int):
    """Map an expanded PIL-rotation box to an upright source-image box."""
    source_height, source_width = source_shape[:2]
    rotated_height, rotated_width = rotated_shape[:2]
    cosine, sine = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    x1, y1, x2, y2 = bbox
    upright_corners = []
    for x, y in ((x1, y1), (x2, y1), (x2, y2), (x1, y2)):
        rotated_x, rotated_y = x - rotated_width / 2, y - rotated_height / 2
        upright_corners.append((
            cosine * rotated_x - sine * rotated_y + source_width / 2,
            sine * rotated_x + cosine * rotated_y + source_height / 2,
        ))
    xs, ys = zip(*upright_corners)
    return (
        max(0, round(min(xs))), max(0, round(min(ys))),
        min(source_width, round(max(xs))), min(source_height, round(max(ys))),
    )
