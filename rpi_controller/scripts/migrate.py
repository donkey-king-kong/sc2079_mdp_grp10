"""Copy the local RPi controller source to the Raspberry Pi.

Run this script on the PC, not on the RPi. It creates or updates a controller
checkout using local source files only; it never transfers virtual environments,
model directories, or captured images.

    python scripts/migrate.py

It never changes source directories or deletes remote files. Existing files in
`TARGET_DIR` with matching local source paths are overwritten by rsync.
"""

from __future__ import annotations

from pathlib import Path
import shlex
import shutil
import subprocess


# Migration configuration -- set these to the SSH details of your RPi.
RPI_HOST = "10.42.0.1"
RPI_USER = "mdp"
RPI_SSH_PORT = 22
TARGET_DIR = "/home/mdp/rpi_refactor"

# Local source and rsync behaviour.
LOCAL_RPI_CONTROLLER_DIR = Path(__file__).resolve().parent.parent
EXCLUDED_PATHS = (
    ".venv",
    "imaging/data",
    "imaging/model*",
    "__pycache__",
    "*.pyc",
    ".DS_Store",
)


def main() -> int:
    if RPI_HOST == "CHANGE_ME":
        print("Set RPI_HOST at the top of scripts/migrate.py before running this script.")
        return 2

    if shutil.which("rsync") is None:
        print("rsync is required but was not found on this computer.")
        return 2

    ssh_destination = f"{RPI_USER}@{RPI_HOST}"
    target_exists = subprocess.run(
        ["ssh", "-p", str(RPI_SSH_PORT), ssh_destination, f"test -e {shlex.quote(TARGET_DIR)}"],
        check=False,
    )
    if target_exists.returncode == 0:
        answer = input(
            f"WARNING: {TARGET_DIR} already exists and its matching files will be overwritten. Continue? [y/N] "
        ).strip().lower()
        if answer != "y":
            print("[MIGRATE] Cancelled; no remote files were changed.")
            return 0
    elif target_exists.returncode != 1:
        raise subprocess.CalledProcessError(target_exists.returncode, target_exists.args)

    print(f"[MIGRATE] Creating target directory: {TARGET_DIR}")
    subprocess.run(
        ["ssh", "-p", str(RPI_SSH_PORT), ssh_destination, f"mkdir -p {shlex.quote(TARGET_DIR)}"],
        check=True,
    )

    destination = f"{ssh_destination}:{TARGET_DIR}/"
    command = [
        "rsync",
        "--archive",
        "--verbose",
        "--human-readable",
        "--progress",
        "--rsh",
        f"ssh -p {RPI_SSH_PORT}",
    ]
    for excluded_path in EXCLUDED_PATHS:
        command.extend(("--exclude", excluded_path))
    command.extend((f"{LOCAL_RPI_CONTROLLER_DIR}/", destination))

    print(f"[MIGRATE] Copying source from {LOCAL_RPI_CONTROLLER_DIR} -> {destination}")
    subprocess.run(command, check=True)
    print("[MIGRATE] Done. Source files copied; virtual environments, models, and imaging data were skipped.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
