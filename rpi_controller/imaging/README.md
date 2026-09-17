# Imaging subsystem

This package handles one `SNAP` request from the RPi controller:

1. `camera.py` starts Picamera2 and captures an RGB frame.
2. `detector.py` lazily loads a local Ultralytics YOLO model and returns the highest-confidence detection.
3. `service.py` combines capture and inference, returning the controller result shape: obstacle ID, image ID, and confidence.
4. `connectors/imaging.py` exposes that service to `RPiManager` when it handles a `SNAPx` command.

## Model on the RPi

The deployed inference weights are stored on the RPi at `/model/best.pt`. They are deliberately not committed to GitHub. Pass that path when constructing `ImagingService` on the RPi (or provide an equivalent local model path); the code falls back to capture-only mode if its configured weights file is absent.

## Manual checks

- `dummy_test/test_imaging_service.py` captures one image and prints its inference result.
- `dummy_test/test_camera_http.py` serves an annotated MJPEG stream for viewing in VLC.

Both checks require RPi camera access, Picamera2, and locally installed model weights.
