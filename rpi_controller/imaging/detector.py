"""Local Ultralytics YOLO inference with a small public result type."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np


BBox = tuple[int, int, int, int]

TEMPORARY_KEAN_SYMBOLS = {
    "10": "Bullseye", "11": "1", "12": "2", "13": "3", "14": "4",
    "15": "5", "16": "6", "17": "7", "18": "8", "19": "9",
    "20": "a", "21": "b", "22": "c", "23": "d", "24": "e", "25": "f",
    "26": "g", "27": "h", "28": "s", "29": "t", "30": "u", "31": "v",
    "32": "w", "33": "x", "34": "y", "35": "z", "36": "Up Arrow",
    "37": "Down Arrow", "38": "Right Arrow", "39": "Left Arrow", "40": "Target",
}


def symbol_for_target(target_id: str | None) -> str | None:
    return TEMPORARY_KEAN_SYMBOLS.get(target_id) if target_id is not None else None


@dataclass(frozen=True)
class DetectionResult:
    found: bool
    target_id: str | None = None
    confidence: float | None = None
    bbox: BBox | None = None

    @classmethod
    def not_found(cls) -> "DetectionResult":
        return cls(found=False)


class DetectorSetupError(RuntimeError):
    """Raised when local YOLO inference cannot be configured."""


class LocalYoloDetector:
    """Detect the single highest-confidence target in an image."""

    def __init__(self, model_path: str | Path, confidence: float = 0.30, image_size: int = 640):
        self.model_path = Path(model_path)
        self.confidence = confidence
        self.image_size = image_size
        self._model = None

    def _load_model(self) -> None:
        if not self.model_path.is_file():
            raise DetectorSetupError(f"YOLO weights were not found at {self.model_path}.")
        try:
            from ultralytics import YOLO
        except ImportError as error:
            raise DetectorSetupError("Ultralytics is not installed.") from error
        self._model = YOLO(str(self.model_path))

    def detect(self, image: np.ndarray) -> DetectionResult:
        """Return target ID, confidence and bounding box."""
        if self._model is None:
            self._load_model()

        results = self._model.predict(
            source=image,
            conf=self.confidence,
            imgsz=self.image_size,
            device="cpu",
            verbose=False,
        )
        if not results or results[0].boxes is None or len(results[0].boxes) == 0:
            return DetectionResult.not_found()

        result = results[0]
        best_box = max(result.boxes, key=lambda box: float(box.conf[0]))
        class_index = int(best_box.cls[0])
        target_id = str(result.names[class_index])
        confidence = float(best_box.conf[0])
        x1, y1, x2, y2 = (round(value) for value in best_box.xyxy[0].tolist())
        return DetectionResult(True, target_id, confidence, (x1, y1, x2, y2))
