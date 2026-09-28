"""Run on the PC to copy detected images from the RPi."""

from pathlib import Path
import subprocess

from migrate import RPI_HOST, RPI_SSH_PORT, RPI_USER, TARGET_DIR


LOCAL_DATA_DIR = Path(__file__).resolve().parent / "imaging" / "data"
REMOTE_DATA_DIR = f"{TARGET_DIR}/imaging/data/"


def main() -> None:
    LOCAL_DATA_DIR.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            "rsync", "-av", "--progress",
            "-e", f"ssh -p {RPI_SSH_PORT}",
            f"{RPI_USER}@{RPI_HOST}:{REMOTE_DATA_DIR}",
            f"{LOCAL_DATA_DIR}/",
        ],
        check=True,
    )


if __name__ == "__main__":
    main()
