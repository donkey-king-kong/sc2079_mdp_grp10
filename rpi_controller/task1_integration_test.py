"""Tonight's Android-free Task 1 integration entrypoint."""

from connectors.algo import AlgoConnector
from connectors.imaging import ImagingConnector
from connectors.stm import STMConnector
from imaging.service import ImagingService
from task1_runtime import MissionState, Task1Runtime
from datetime import datetime
from pathlib import Path
import re

# ensure the stm port file is listed as below
# verify using "ls /dev/ttyACM0"
STM_PORT="/dev/ttyACM0"

# Start the Algo server on its host before this script:
#   cd algo && source .venv/bin/activate && python3 server.py --host 0.0.0.0
# Check that host's address with `ip addr` (or `ifconfig` on macOS), put it
# below, then verify: curl http://<ip-address>:5001/health
ALGO_IP_ADDR="10.42.0.208"

# robot initial position, can change
ROBOT = {"x": 0, "y": 0, "dir": "N"}

# arena obstacles input, can change
OBSTACLES = [
        {"id": 1, "x": 7, "y": 5, "dir": "S"},
        {"id": 2, "x": 12, "y": 9, "dir": "E"},
        {"id": 3, "x": 5, "y": 13, "dir": "W"},
        {"id": 4, "x": 15, "y": 15, "dir": "S"},
        {"id": 5, "x": 15, "y": 4, "dir": "N"},
        {"id": 6, "x": 9, "y": 19, "dir": "S"},
        {"id": 7, "x": 19, "y": 9, "dir": "W"},
]

# Set True to detect symbols continuously while the route executes. Only
# detections are saved in this mode; blank polling frames are discarded.
CONTINUOUS_CAMERA_SCAN = False
CONTINUOUS_SCAN_INTERVAL_SECONDS = 1.0


def create_run_directory() -> Path:
    """Allocate a new numbered image folder for this integration invocation."""
    base_dir = Path(__file__).resolve().parent / "imaging" / "data"
    highest_run = 0
    for path in base_dir.glob("run*_*") if base_dir.exists() else ():
        match = re.match(r"run(\d+)_", path.name)
        if match:
            highest_run = max(highest_run, int(match.group(1)))
    run_dir = base_dir / f"run{highest_run + 1:03d}_{datetime.now():%Y%m%d_%H%M%S}"
    run_dir.mkdir(parents=True, exist_ok=False)
    return run_dir


def main():
    stm = STMConnector(port=STM_PORT)
    runtime = None
    try:
        connection = stm.connect()
        run_dir = create_run_directory()
        print("[KERNEL] image run directory: %s" % run_dir)
        runtime = Task1Runtime(
            AlgoConnector(host=ALGO_IP_ADDR), connection,
            imaging=ImagingConnector(ImagingService(data_dir=run_dir)),
            detections_dir=run_dir,
            continuous_camera_scan=CONTINUOUS_CAMERA_SCAN,
            continuous_scan_interval_seconds=CONTINUOUS_SCAN_INTERVAL_SECONDS,
        )
        runtime.start()
        runtime.kernel.start(ROBOT, OBSTACLES)
        state = runtime.kernel.run()
        print("[KERNEL] mission ended: %s" % state.value)
        return 0 if state is MissionState.COMPLETE else 1
    except KeyboardInterrupt:
        print("[SHUTDOWN] Ctrl+C received")
        return 130
    finally:
        if runtime:
            runtime.stop()
        stm.disconnect()
        print("[SHUTDOWN] clean")


if __name__ == "__main__":
    raise SystemExit(main())
