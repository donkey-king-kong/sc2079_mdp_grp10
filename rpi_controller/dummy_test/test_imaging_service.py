import sys

sys.path.append("..")

from connectors.imaging import ImagingConnector
from imaging.detector import symbol_for_target


obstacle_id = input("Obstacle ID [1]: ").strip() or "1"

imaging = ImagingConnector()
result = imaging.capture_and_predict(obstacle_id)
symbol = symbol_for_target(result["image_id"])

print(f"Result: {result}")
print(f"Symbol: {symbol}")
