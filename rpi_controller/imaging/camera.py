"""Picamera2 capture logic for the RPi imaging subsystem."""

from __future__ import annotations

import time

import numpy as np
from picamera2 import Picamera2


class CameraCapture:
    """A started Picamera2 capture session, independent of detection."""

    def __init__(self, width: int = 1640, height: int = 1232, warmup_seconds: float = 1.0):
        self.camera = Picamera2()
        config = self.camera.create_still_configuration(
            main={"size": (width, height), "format": "RGB888"}
        )
        self.camera.configure(config)
        self.camera.start()
        time.sleep(warmup_seconds)

    def capture(self) -> np.ndarray:
        """Return one RGB frame from the active camera session."""
        return self.camera.capture_array("main")

    def close(self) -> None:
        self.camera.stop()
        self.camera.close()

    def __enter__(self) -> "CameraCapture":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()


def capture_one(width: int = 1640, height: int = 1232, warmup_seconds: float = 1.0) -> np.ndarray:
    """Capture one frame using a short-lived camera session."""
    with CameraCapture(width, height, warmup_seconds) as camera:
        return camera.capture()
