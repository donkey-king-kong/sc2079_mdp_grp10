"""One-command Android + RPi Task 1 entrypoint.

Edit the configuration block below for a test run, then launch on the RPi:

    python3 task1.py
"""

from datetime import datetime
from pathlib import Path
import re
import subprocess

from connectors.algo import AlgoConnector
from connectors.android import AndroidConnector
from connectors.bluetooth import BluetoothConnector
from connectors.imaging import ImagingConnector
from connectors.stm import STMConnector
from imaging.service import ImagingService
from task1.runtime import MissionState, Task1Runtime

# ---- Test-run configuration -------------------------------------------------
STM_PORT = "/dev/ttyACM0"
ALGO_HOST = "10.42.0.208"  # Laptop IP on the RPi network.
ALGO_TIMEOUT_SECONDS = 15

# Select a file in imaging/model/.  The model weights are intentionally ignored
# by Git and must be deployed to the RPi separately.
CV_MODEL_FILENAME = "best.pt"  # Alternative: "best_JH.pt"
CV_SAMPLE_COUNT = 1
CV_MIN_FRAMES_SEEN = 1
CV_ROTATION_ANGLES = (0,)
CV_USE_CONTRAST = False
# False: capture and infer before the car continues. True: capture at SNAP,
# then run inference while the car continues its route.
PARALLEL_INFERENCE = True

BLUETOOTH_DEVICE = "/dev/rfcomm0"
RESET_BLUETOOTH_ON_START = True
BLUETOOTH_SERVICE = "mdp-bluetooth.service"

# Set to True only when separately polling for symbols while the route runs.
CONTINUOUS_CAMERA_SCAN = False
CONTINUOUS_SCAN_INTERVAL_SECONDS = 1.0


def reset_bluetooth_service() -> None:
    if not RESET_BLUETOOTH_ON_START:
        return
    print(f"[BT] Restarting {BLUETOOTH_SERVICE}...")
    subprocess.run(
        ["sudo", "systemctl", "restart", BLUETOOTH_SERVICE],
        check=True,
    )
    print("[BT] Service restarted; waiting for Android to connect...")


def create_run_directory() -> Path:
    """Create a separate image directory for this Task 1 run."""
    base_dir = Path(__file__).resolve().parent / "imaging" / "data"
    highest_run = 0
    for path in base_dir.glob("run*_*") if base_dir.exists() else ():
        match = re.match(r"run(\d+)_", path.name)
        if match:
            highest_run = max(highest_run, int(match.group(1)))
    run_dir = base_dir / f"run{highest_run + 1:03d}_{datetime.now():%Y%m%d_%H%M%S}"
    run_dir.mkdir(parents=True, exist_ok=False)
    return run_dir


def main() -> int:
    reset_bluetooth_service()
    stm = STMConnector(port=STM_PORT)
    bluetooth = BluetoothConnector(device=BLUETOOTH_DEVICE)
    android = AndroidConnector()
    runtime = None

    try:
        connection = stm.connect()
        run_dir = create_run_directory()
        model_path = Path(__file__).resolve().parent / "imaging" / "model" / CV_MODEL_FILENAME
        print(f"[KERNEL] image run directory: {run_dir}")
        print(f"[IMAGING] configured model: {model_path}")

        runtime = Task1Runtime(
            AlgoConnector(host=ALGO_HOST, timeout=ALGO_TIMEOUT_SECONDS),
            connection,
            imaging=ImagingConnector(
                ImagingService(
                    model_path=model_path,
                    data_dir=run_dir,
                    sample_count=CV_SAMPLE_COUNT,
                    min_frames_seen=CV_MIN_FRAMES_SEEN,
                    rotation_angles=CV_ROTATION_ANGLES,
                    use_contrast_variants=CV_USE_CONTRAST,
                )
            ),
            detections_dir=run_dir,
            continuous_camera_scan=CONTINUOUS_CAMERA_SCAN,
            continuous_scan_interval_seconds=CONTINUOUS_SCAN_INTERVAL_SECONDS,
            parallel_inference=PARALLEL_INFERENCE,
            bluetooth=bluetooth,
            android=android,
        )
        runtime.start()
        print("[KERNEL] Waiting for Android arena...")
        state = runtime.kernel.run()
        print(f"[KERNEL] mission ended: {state.value}")
        return 0 if state is MissionState.COMPLETE else 1
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
