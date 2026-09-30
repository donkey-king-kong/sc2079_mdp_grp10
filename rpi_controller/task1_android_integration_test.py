"""Full Task 1 integration entrypoint with Android Bluetooth."""

from datetime import datetime
from pathlib import Path
import re

from connectors.algo import AlgoConnector
from connectors.android import AndroidConnector
from connectors.bluetooth import BluetoothConnector
from connectors.imaging import ImagingConnector
from connectors.stm import STMConnector
from imaging.service import ImagingService
from task1_runtime import MissionState, Task1Runtime


CONTINUOUS_CAMERA_SCAN = False
CONTINUOUS_SCAN_INTERVAL_SECONDS = 1.0


def create_run_directory() -> Path:
    """Create a separate image directory for this Task 1 run."""
    base_dir = (
        Path(__file__).resolve().parent
        / "imaging"
        / "data"
    )

    highest_run = 0

    for path in (
        base_dir.glob("run*_*")
        if base_dir.exists()
        else ()
    ):
        match = re.match(r"run(\d+)_", path.name)

        if match:
            highest_run = max(
                highest_run,
                int(match.group(1)),
            )

    run_dir = (
        base_dir
        / (
            f"run{highest_run + 1:03d}_"
            f"{datetime.now():%Y%m%d_%H%M%S}"
        )
    )

    run_dir.mkdir(
        parents=True,
        exist_ok=False,
    )

    return run_dir


def main():
    stm = STMConnector()
    bluetooth = BluetoothConnector()
    android = AndroidConnector()

    runtime = None

    try:
        connection = stm.connect()

        run_dir = create_run_directory()

        print(
            f"[KERNEL] image run directory: {run_dir}"
        )

        runtime = Task1Runtime(
            AlgoConnector(),
            connection,
            imaging=ImagingConnector(
                ImagingService(data_dir=run_dir)
            ),
            detections_dir=run_dir,
            continuous_camera_scan=CONTINUOUS_CAMERA_SCAN,
            continuous_scan_interval_seconds=(
                CONTINUOUS_SCAN_INTERVAL_SECONDS
            ),
            bluetooth=bluetooth,
            android=android,
        )

        runtime.start()

        print(
            "[KERNEL] Waiting for Android arena..."
        )

        # AndroidRXWorker will generate ANDROID_ARENA_RECEIVED.
        # The kernel then starts Algo automatically.
        state = runtime.kernel.run()

        print(
            f"[KERNEL] mission ended: {state.value}"
        )

        return (
            0
            if state is MissionState.COMPLETE
            else 1
        )

    except KeyboardInterrupt:
        print("[SHUTDOWN] Ctrl+C received")
        return 130

    finally:
        if runtime:
            runtime.stop()

        bluetooth.disconnect()
        stm.disconnect()

        print("[SHUTDOWN] clean")


if __name__ == "__main__":
    raise SystemExit(main())
