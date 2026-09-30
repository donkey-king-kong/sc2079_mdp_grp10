"""Tonight's Android-free Task 1 integration entrypoint."""

from connectors.algo import AlgoConnector
from connectors.imaging import ImagingConnector
from connectors.stm import STMConnector
from imaging.service import ImagingService
from task1_runtime import MissionState, Task1Runtime
from datetime import datetime
from pathlib import Path
import re


ROBOT = {"x": 1, "y": 1, "dir": "N"}
OBSTACLES = [
    {"id": 1, "x": 5, "y": 13, "dir": "W"},
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
    stm = STMConnector()
    runtime = None
    try:
        connection = stm.connect()
        run_dir = create_run_directory()
        print("[KERNEL] image run directory: %s" % run_dir)
        runtime = Task1Runtime(
            AlgoConnector(), connection,
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
