from imaging.service import ImagingService


class ImagingConnector:
    """
    Interface between the RPi controller and the imaging subsystem.

    The actual camera capture + image recognition implementation can be
    added by the imaging teammate later without changing manager.py.
    """

    def __init__(self, service: ImagingService | None = None):
        self.service = service or ImagingService()

    def capture_and_predict(self, obstacle_id, **options):
        """
        Capture an image for the given obstacle and run recognition.

        Expected return format:

        {
            "obstacle_id": "2",
            "image_id": "38",
            "confidence": 0.94
        }

        For now this method is a placeholder until the imaging system
        is integrated.
        """

        print(
            f"[IMAGING] Capture requested for obstacle {obstacle_id}"
        )

        # Placeholder result for integration testing
        # return {
        #     "obstacle_id": str(obstacle_id),
        #     "image_id": None,
        #     "confidence": None,
        # }
        return self.service.capture_and_predict(obstacle_id, **options)

    def capture_samples(self):
        """Capture the frames for a SNAP without blocking on YOLO inference."""
        return self.service.capture_samples()

    def predict_captured(self, obstacle_id, frames, **options):
        """Run YOLO and persistence for frames captured by the camera worker."""
        return self.service.predict_captured(obstacle_id, frames, **options)

    def start(self):
        """Prepare the camera/model once when the CameraCV worker starts."""
        return self.service.start()

    def close(self):
        """Release camera resources when the CameraCV worker stops."""
        return self.service.close()
