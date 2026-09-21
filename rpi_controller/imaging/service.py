"""Capture and inference orchestration for the controller."""

from __future__ import annotations

from pathlib import Path

from .camera import CameraCapture
from .detector import DetectionResult, DetectorSetupError, LocalYoloDetector


DEFAULT_MODEL_PATH = Path(__file__).with_name("models") / "best.pt"


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

        if self.detector is None:
            print(f"[IMAGING] Model weights not found at {self.model_path}; capture only")
        elif not result.found:
            print("[IMAGING] No target detected")

        return {
            "obstacle_id": str(obstacle_id),
            "image_id": result.target_id,
            "confidence": result.confidence,
        }
