"""Local Ultralytics YOLO inference with a small public result type."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import numpy as np


BBox = tuple[int, int, int, int]

TARGET_SYMBOLS = {
    "10": "Bullseye", "11": "1", "12": "2", "13": "3", "14": "4",
    "15": "5", "16": "6", "17": "7", "18": "8", "19": "9",
    "20": "a", "21": "b", "22": "c", "23": "d", "24": "e", "25": "f",
    "26": "g", "27": "h", "28": "s", "29": "t", "30": "u", "31": "v",
    "32": "w", "33": "x", "34": "y", "35": "z", "36": "Up Arrow",
    "37": "Down Arrow", "38": "Right Arrow", "39": "Left Arrow", "40": "Target",
}
BULLSEYE_SYMBOL_NAMES = {"bullseye", "end"}


def symbol_for_target(target_id: str | None) -> str | None:
    return TARGET_SYMBOLS.get(target_id) if target_id is not None else None


def is_bullseye_target(target_id: str) -> bool:
    """Whether a model target label maps to the non-returnable bullseye marker."""
    # ``end`` is the Bullseye semantic name in a legacy ``best.pt``;
    # deployed numeric-label models resolve via TARGET_SYMBOLS instead.
    return (symbol_for_target(target_id) or target_id).casefold() in BULLSEYE_SYMBOL_NAMES


@dataclass(frozen=True)
class DetectionResult:
    found: bool
    target_id: str | None = None
    confidence: float | None = None
    bbox: BBox | None = None

    @classmethod
    def not_found(cls) -> "DetectionResult":
        return cls(found=False)


def valid_detections(
    detections: tuple[DetectionResult, ...], image_shape: tuple[int, ...],
) -> tuple[DetectionResult, ...]:
    """Keep usable, non-bullseye detections for target selection.

    The model is deliberately still allowed to emit bullseye.  It is excluded
    here, before any confidence comparison performed by callers.
    """
    height, width = image_shape[:2]
    return tuple(
        detection
        for detection in detections
        if detection.target_id is not None
        and not is_bullseye_target(detection.target_id)
        and detection.bbox is not None
        and detection.bbox[0] < detection.bbox[2]
        and detection.bbox[1] < detection.bbox[3]
        and 0 <= detection.bbox[0] < width
        and 0 <= detection.bbox[1] < height
        and 0 < detection.bbox[2] <= width
        and 0 < detection.bbox[3] <= height
    )


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

    def load(self) -> None:
        """Load weights ahead of time without performing an inference."""
        if self._model is None:
            self._load_model()

    def detect_all(self, image: np.ndarray) -> tuple[DetectionResult, ...]:
        """Return every YOLO detection, including bullseye, for diagnostics."""
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
            return ()

        result = results[0]
        return tuple(
            DetectionResult(
                found=True,
                target_id=str(result.names[int(box.cls[0])]),
                confidence=float(box.conf[0]),
                bbox=tuple(round(value) for value in box.xyxy[0].tolist()),
            )
            for box in result.boxes
        )

    def detect(self, image: np.ndarray) -> DetectionResult:
        """Return the strongest valid target; bullseye is never returned."""
        candidates = valid_detections(self.detect_all(image), image.shape)
        if not candidates:
            return DetectionResult.not_found()
        return max(candidates, key=lambda detection: detection.confidence or 0.0)
