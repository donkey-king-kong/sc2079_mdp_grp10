"""Serve the camera feed with YOLO bounding boxes for VLC."""

from __future__ import annotations

import argparse
from http import server as http_server
import io
from pathlib import Path
import sys
import time

from PIL import Image, ImageDraw, ImageFont

sys.path.append("..")

from imaging.camera import CameraCapture  # noqa: E402
from imaging.detector import DetectionResult, DetectorSetupError, LocalYoloDetector, symbol_for_target  # noqa: E402


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MODEL = PROJECT_ROOT / "imaging" / "models" / "best.pt"


def jpeg_with_detection(image, result: DetectionResult) -> bytes:
    output = Image.fromarray(image)
    draw = ImageDraw.Draw(output)
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 36)
    except OSError:
        font = ImageFont.load_default()

    if result.found and result.bbox is not None:
        draw.rectangle(result.bbox, outline="lime", width=6)
        symbol = symbol_for_target(result.target_id)
        label = f"ID {result.target_id}"
        if symbol:
            label += f" ({symbol})"
        if result.confidence is not None:
            label += f"  {result.confidence:.2f}"
        draw.rectangle((10, 10, 10 + len(label) * 22, 60), fill="black")
        draw.text((15, 15), label, font=font, fill="lime")
    else:
        draw.text((10, 10), "No target detected", font=font, fill="red")

    buffer = io.BytesIO()
    output.save(buffer, format="JPEG", quality=80)
    return buffer.getvalue()


def main() -> int:
    parser = argparse.ArgumentParser(description="Serve an annotated camera stream for VLC.")
    parser.add_argument("--model", type=Path, default=DEFAULT_MODEL)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8888)
    parser.add_argument("--interval", type=float, default=0.2)
    parser.add_argument("--confidence", type=float, default=0.30)
    args = parser.parse_args()

    detector = LocalYoloDetector(args.model, args.confidence)

    class StreamHandler(http_server.BaseHTTPRequestHandler):
        def do_GET(self) -> None:  # noqa: N802
            if self.path != "/":
                self.send_error(404)
                return

            self.send_response(200)
            self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()

            try:
                while True:
                    image = live_camera.capture()
                    result = detector.detect(image)
                    jpeg = jpeg_with_detection(image, result)
                    self.wfile.write(b"--frame\r\nContent-Type: image/jpeg\r\n")
                    self.wfile.write(f"Content-Length: {len(jpeg)}\r\n\r\n".encode())
                    self.wfile.write(jpeg + b"\r\n")
                    self.wfile.flush()
                    if args.interval > 0:
                        time.sleep(args.interval)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def log_message(self, format: str, *args: object) -> None:
            return

    try:
        with CameraCapture() as live_camera:
            with http_server.ThreadingHTTPServer((args.host, args.port), StreamHandler) as httpd:
                print(f"Open http://<rpi-ip>:{args.port}/ in VLC")
                httpd.serve_forever()
    except DetectorSetupError as error:
        print(f"Detector setup error: {error}")
        return 2
    except KeyboardInterrupt:
        print("Stream stopped.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
