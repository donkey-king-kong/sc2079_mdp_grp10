"""Run on the host PC before Task 1 to pull and display its completed images."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import time

try:
    import paramiko
except ImportError:  # Report the missing host-only dependency clearly in main().
    paramiko = None

from migrate import RPI_HOST, RPI_SSH_PORT, RPI_USER, TARGET_DIR


# Set this once on the HOST PC. This file is excluded from migrate.py.
RPI_PASSWORD = "CHANGE_ME"

LOCAL_DATA_DIR = Path(__file__).resolve().parent.parent / "imaging" / "data"
REMOTE_DATA_DIR = f"{TARGET_DIR}/imaging/data"
REMOTE_MARKER = f"{REMOTE_DATA_DIR}/task1_complete.txt"


def connect():
    """Connect from macOS or Windows without an interactive password prompt."""
    if RPI_PASSWORD == "CHANGE_ME" or not RPI_PASSWORD:
        raise RuntimeError("Set RPI_PASSWORD at the top of scripts/giveIMG.py")
    if paramiko is None:
        raise RuntimeError("Install host dependency first: python -m pip install -r scripts/requirements-host.txt")

    client = paramiko.SSHClient()
    client.load_system_host_keys()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(
        RPI_HOST,
        port=RPI_SSH_PORT,
        username=RPI_USER,
        password=RPI_PASSWORD,
        timeout=5,
        banner_timeout=5,
        auth_timeout=5,
        look_for_keys=False,
        allow_agent=False,
    )
    return client


def read_marker(client) -> str | None:
    _, stdout, _ = client.exec_command(f"cat '{REMOTE_MARKER}'", timeout=5)
    marker = stdout.read().decode("utf-8").strip()
    return marker or None


def pull_run(client, run_name: str) -> Path:
    if Path(run_name).name != run_name:
        raise RuntimeError("Invalid Task 1 completion marker received from RPi")
    destination = LOCAL_DATA_DIR / run_name
    destination.mkdir(parents=True, exist_ok=True)
    remote_run_dir = f"{REMOTE_DATA_DIR}/{run_name}"
    sftp = client.open_sftp()
    try:
        for item in sftp.listdir_attr(remote_run_dir):
            if item.filename in (".", ".."):
                continue
            remote_file = f"{remote_run_dir}/{item.filename}"
            local_file = destination / item.filename
            # A completed run is immutable. Existing matching files are skipped,
            # making a manual retry safe after a partial connection failure.
            if local_file.exists() and local_file.stat().st_size == item.st_size:
                continue
            print("[GIVEIMG] Downloading %s" % item.filename)
            partial_file = local_file.with_suffix(local_file.suffix + ".part")
            sftp.get(remote_file, str(partial_file))
            partial_file.replace(local_file)
    finally:
        sftp.close()
    return destination


def show_gallery(run_dir: Path) -> None:
    """Display all JPEGs from the completed run, including stitched.jpg."""
    try:
        import tkinter as tk
        from PIL import Image, ImageTk
    except ImportError:
        if sys.platform == "win32":
            os.startfile(run_dir)  # type: ignore[attr-defined]
        else:
            subprocess.Popen(["open", str(run_dir)])
        return

    root = tk.Tk()
    root.title("MDP Task 1 — %s" % run_dir.name)
    images = sorted(run_dir.glob("*.jpg"))
    tk.Label(root, text="Task 1 images: %d" % len(images), padx=10, pady=8).grid(row=0, column=0, columnspan=3, sticky="w")
    for index, path in enumerate(images):
        with Image.open(path) as source:
            image = source.convert("RGB")
        image.thumbnail((300, 220))
        photo = ImageTk.PhotoImage(image)
        frame = tk.Frame(root, padx=6, pady=6)
        row, column = divmod(index, 3)
        frame.grid(row=row + 1, column=column, sticky="n")
        label = tk.Label(frame, image=photo)
        label.image = photo
        label.pack()
        tk.Label(frame, text=path.name).pack()
    root.mainloop()


def main() -> int:
    client = None
    try:
        client = connect()
        previous_run = read_marker(client)
        print("[GIVEIMG] Waiting for Task 1 to finish...")
        while True:
            run_name = read_marker(client)
            if run_name and run_name != previous_run:
                print("[GIVEIMG] Pulling %s" % run_name)
                run_dir = pull_run(client, run_name)
                print("[GIVEIMG] Download complete: %s" % run_dir)
                show_gallery(run_dir)
                return 0
            time.sleep(2)
    except (OSError, RuntimeError, EOFError, Exception) as error:
        print("[GIVEIMG] %s" % error, file=sys.stderr)
        return 1
    finally:
        if client is not None:
            client.close()


if __name__ == "__main__":
    raise SystemExit(main())
