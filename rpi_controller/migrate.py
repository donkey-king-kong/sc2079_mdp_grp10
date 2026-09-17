"""Copy the local RPi controller source to the Raspberry Pi.

Run this script on the PC, not on the RPi. Set `COPIED_DIR` and `TARGET_DIR`
below, then run from this directory:

    python migrate.py

The script copies everything in `COPIED_DIR` (including `.venv` and `best.pt`)
into `TARGET_DIR`, then updates that copy with the PC source. It never changes
`COPIED_DIR` or deletes remote files.
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
COPIED_DIR = "/home/rpi_controller"
TARGET_DIR = "/home/rpi_controller_updated"

# Local source and rsync behaviour.
LOCAL_RPI_CONTROLLER_DIR = Path(__file__).resolve().parent
EXCLUDED_PATHS = (".venv", "__pycache__", "*.pyc", ".DS_Store")


def main() -> int:
    if RPI_HOST == "CHANGE_ME":
        print("Set RPI_HOST at the top of migrate.py before running this script.")
        return 2

    if shutil.which("rsync") is None:
        print("rsync is required but was not found on this computer.")
        return 2

    if COPIED_DIR == TARGET_DIR:
        print("COPIED_DIR and TARGET_DIR must differ.")
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

    remote_copy_command = (
        f"test -d {shlex.quote(COPIED_DIR)} && "
        f"mkdir -p {shlex.quote(TARGET_DIR)} && "
        f"cp -a {shlex.quote(COPIED_DIR)}/. {shlex.quote(TARGET_DIR)}/"
    )
    print(
        "[MIGRATE] Copying RPi controller (including .venv): "
        f"{COPIED_DIR} -> {TARGET_DIR}"
    )
    subprocess.run(
        ["ssh", "-p", str(RPI_SSH_PORT), ssh_destination, remote_copy_command],
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

    print(f"[MIGRATE] Updating copied controller from {LOCAL_RPI_CONTROLLER_DIR} -> {destination}")
    subprocess.run(command, check=True)
    print("[MIGRATE] Done. The target controller keeps the copied RPi .venv.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
