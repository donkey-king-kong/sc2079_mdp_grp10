"""Capture and inference orchestration for the controller."""

from __future__ import annotations

from datetime import datetime
from pathlib import Path
import re

from PIL import Image, ImageDraw

from .camera import CameraCapture
from .detector import DetectionResult, DetectorSetupError, LocalYoloDetector, symbol_for_target


DEFAULT_MODEL_PATH = Path(__file__).with_name("models") / "best.pt"
DATA_DIR = Path(__file__).parents[1] / "imaging" / "data"


def save_detection(frame, result: DetectionResult, obstacle_id: object) -> Path:
    """Save one detected target with its bounding box, ID and symbol."""
    image = Image.fromarray(frame)
    draw = ImageDraw.Draw(image)
    draw.rectangle(result.bbox, outline="lime", width=6)

    symbol = symbol_for_target(result.target_id)
    label = f"ID {result.target_id}" + (f" ({symbol})" if symbol else "")
    x1, y1, _, _ = result.bbox
    label_y = max(0, y1 - 20)
    draw.rectangle((x1, label_y, x1 + len(label) * 7, label_y + 20), fill="black")
    draw.text((x1 + 2, label_y + 2), label, fill="lime")

    DATA_DIR.mkdir(parents=True, exist_ok=True)
    detected_symbol = symbol_for_target(result.target_id) or result.target_id or "unknown"
    safe_symbol = re.sub(r"[^A-Za-z0-9_-]+", "_", str(detected_symbol)).strip("_") or "unknown"
    stem = f"{datetime.now():%Y%m%d_%H%M%S}_{safe_symbol}"
    path = DATA_DIR / f"{stem}.jpg"
    suffix = 2
    while path.exists():
        path = DATA_DIR / f"{stem}_{suffix}.jpg"
        suffix += 1
    image.save(path, format="JPEG")
    print(f"[IMAGING] Saved {path}")
    return path


class ImagingService:
    """Run one camera capture and, when available, one local inference."""

    def __init__(
        self,
        model_path: str | Path = DEFAULT_MODEL_PATH,
        confidence: float = 0.30,
        width: int = 1640,
        height: int = 1232,
        warmup_seconds: float = 1.0,
    ):
        self.model_path = Path(model_path)
        self.width = width
        self.height = height
        self.warmup_seconds = warmup_seconds
        self.detector = LocalYoloDetector(self.model_path, confidence) if self.model_path.is_file() else None

    def capture_and_predict(self, obstacle_id: object) -> dict[str, object | None]:
        """Capture one frame and return the existing controller result shape."""
        with CameraCapture(self.width, self.height, self.warmup_seconds) as camera:
            frame = camera.capture()
            result = self.detector.detect(frame) if self.detector is not None else DetectionResult.not_found()

        image_path = None
        if self.detector is None:
            print(f"[IMAGING] Model weights not found at {self.model_path}; capture only")
        elif not result.found:
            print("[IMAGING] No target detected")
        elif result.bbox is not None:
            image_path = save_detection(frame, result, obstacle_id)

        return {
            "obstacle_id": str(obstacle_id),
            "image_id": result.target_id,
            "confidence": result.confidence,
            "bbox": result.bbox,
            "image_path": str(image_path) if image_path else None,
        }
