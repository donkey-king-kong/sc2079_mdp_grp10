"""Run on the host PC before Task 1 to pull and display its completed images."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from migrate import RPI_HOST, RPI_SSH_PORT, RPI_USER, TARGET_DIR


# Set this once on the HOST PC. This file is excluded from migrate.py.
RPI_PASSWORD = "CHANGE_ME"

LOCAL_DATA_DIR = Path(__file__).resolve().parent.parent / "imaging" / "data"
REMOTE_DATA_DIR = f"{TARGET_DIR}/imaging/data"
REMOTE_MARKER = f"{REMOTE_DATA_DIR}/task1_complete.txt"


def ssh_environment():
    """Create a short-lived SSH askpass helper so no terminal prompt appears."""
    if RPI_PASSWORD == "CHANGE_ME" or not RPI_PASSWORD:
        raise RuntimeError("Set RPI_PASSWORD at the top of scripts/giveIMG.py")
    descriptor, helper_name = tempfile.mkstemp(prefix="mdp-askpass-")
    os.close(descriptor)
    helper = Path(helper_name)
    helper.write_text(
        "#!%s\nimport os\nprint(os.environ['MDP_RPI_PASSWORD'])\n" % sys.executable,
        encoding="utf-8",
    )
    helper.chmod(0o700)
    environment = os.environ.copy()
    environment.update({
        "MDP_RPI_PASSWORD": RPI_PASSWORD,
        "SSH_ASKPASS": str(helper),
        "SSH_ASKPASS_REQUIRE": "force",
        "DISPLAY": environment.get("DISPLAY", ":0"),
    })
    return helper, environment


def ssh_options():
    return [
        "-p", str(RPI_SSH_PORT),
        "-o", "ConnectTimeout=5",
        "-o", "StrictHostKeyChecking=accept-new",
    ]


def read_marker(environment):
    command = ["ssh", *ssh_options(), f"{RPI_USER}@{RPI_HOST}", f"cat '{REMOTE_MARKER}'"]
    result = subprocess.run(command, check=False, capture_output=True, text=True, env=environment)
    return result.stdout.strip() if result.returncode == 0 else None


def pull_run(run_name: str, environment) -> Path:
    destination = LOCAL_DATA_DIR / run_name
    destination.mkdir(parents=True, exist_ok=True)
    transport = "ssh " + " ".join(ssh_options())
    command = [
        "rsync", "-av", "--partial", "--timeout=20",
        "-e", transport,
        f"{RPI_USER}@{RPI_HOST}:{REMOTE_DATA_DIR}/{run_name}/",
        f"{destination}/",
    ]
    subprocess.run(command, check=True, env=environment)
    return destination


def show_gallery(run_dir: Path) -> None:
    """Display all JPEGs from the completed run, including stitched.jpg."""
    try:
        import tkinter as tk
        from PIL import Image, ImageTk
    except ImportError:
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
    helper, environment = ssh_environment()
    try:
        previous_run = read_marker(environment)
        print("[GIVEIMG] Waiting for Task 1 to finish...")
        while True:
            run_name = read_marker(environment)
            if run_name and run_name != previous_run:
                print("[GIVEIMG] Pulling %s" % run_name)
                run_dir = pull_run(run_name, environment)
                print("[GIVEIMG] Download complete: %s" % run_dir)
                show_gallery(run_dir)
                return 0
            time.sleep(2)
    except (OSError, subprocess.SubprocessError, RuntimeError) as error:
        print("[GIVEIMG] %s" % error, file=sys.stderr)
        return 1
    finally:
        helper.unlink(missing_ok=True)


if __name__ == "__main__":
    raise SystemExit(main())
