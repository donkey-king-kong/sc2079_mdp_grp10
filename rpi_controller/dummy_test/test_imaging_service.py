import sys
import time

sys.path.append("..")

from connectors.imaging import ImagingConnector
from imaging.detector import symbol_for_target


OBSTACLE_ID = "1"
SCAN_INTERVAL_SECONDS = 0.2

imaging = ImagingConnector()

print("Scanning continuously. Press Ctrl-C to stop.")
try:
    while True:
        result = imaging.capture_and_predict(OBSTACLE_ID)
        symbol = symbol_for_target(result["image_id"])
        print(f"Result: {result}; Symbol: {symbol}")
        time.sleep(SCAN_INTERVAL_SECONDS)
except KeyboardInterrupt:
    print("\nScanning stopped.")
