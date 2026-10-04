"""
Mock RPi server — runs on a Windows laptop connected to the Android phone via Bluetooth.

Setup:
  1. Pair the Android phone to the Windows laptop via Bluetooth settings.
  2. In Windows: Settings -> Bluetooth -> More Bluetooth options -> COM Ports tab.
     Note the incoming COM port assigned (e.g. COM3).
  3. Run:  python mock_rpi_server.py COM3
     Or:   python mock_rpi_server.py COM3 --script
  4. On the Android app, connect to the laptop via Bluetooth and send arena data.

The server prints every received arena message with obstacles and robot position.
It can also send TARGET, ROBOT, custom, and stitched-image mock responses.
"""

from __future__ import annotations

import argparse
import base64
import json
import queue
import sys
import threading
import time
from pathlib import Path
from typing import Any, Callable, Optional, cast

import serial

from image_stitcher import stitch_images  # type: ignore[reportUnknownVariableType]
from protocol import AndroidStreamParser

BAUD = 115200
CHUNK_SIZE = 700

DIRECTION_LABELS: dict[int, str] = {0: "N", 1: "E", 2: "S", 3: "W"}
VALID_DIRECTIONS: set[str] = {"N", "E", "S", "W"}
StitchFunc = Callable[[list[Path], Path], Optional[Path]]
stitch_images_typed = cast(StitchFunc, stitch_images)


def print_arena(value: dict[str, Any]) -> None:
    robot_x = value.get("robot_x", "?")
    robot_y = value.get("robot_y", "?")
    robot_direction = value.get("robot_direction")
    robot_dir = (
        DIRECTION_LABELS.get(robot_direction, "?")
        if isinstance(robot_direction, int)
        else "?"
    )

    print()
    print("=== ARENA RECEIVED ===")
    print(f"  Robot   x={robot_x}  y={robot_y}  dir={robot_dir}")

    obstacles = value.get("obstacles", [])
    if obstacles:
        print(f"  Obstacles ({len(obstacles)}):")
        for obs in obstacles:
            if not isinstance(obs, dict):
                print(f"    {obs}")
                continue

            obs_dict = cast(dict[str, Any], obs)
            ox = obs_dict.get("x", "?")
            oy = obs_dict.get("y", "?")
            obstacle_direction = obs_dict.get("d")
            od = (
                DIRECTION_LABELS.get(obstacle_direction, "?")
                if isinstance(obstacle_direction, int)
                else "?"
            )
            oid = obs_dict.get("id", "?")
            print(f"    id={oid}  x={ox}  y={oy}  facing={od}")
    else:
        print("  Obstacles: none")

    print("======================")
    print()


def send(conn: Any, lock: threading.Lock, message: str) -> None:
    with lock:
        conn.write(message.encode("utf-8"))
    print(f"[MOCK RPi] SENT: {repr(message)}")


def stitch_and_encode(image_dir: Path) -> Optional[str]:
    image_paths = sorted(
        path
        for path in image_dir.iterdir()
        if path.is_file() and path.suffix.lower() in {".jpg", ".jpeg"}
    )

    if not image_paths:
        print(f"[MOCK RPi] No images found in {image_dir}")
        return None

    output_path = image_dir / "stitched_output.jpg"
    stitched_path = stitch_images_typed(image_paths, output_path)

    if stitched_path is None:
        return None

    image_bytes = Path(stitched_path).read_bytes()
    return base64.b64encode(image_bytes).decode("ascii")


def make_stitch_message(value: str) -> str:
    return json.dumps(
        {
            "cat": "stitch-image",
            "value": value,
        },
        separators=(",", ":"),
    ) + "\n"


def send_stitched_image(conn: Any, lock: threading.Lock, image_dir: Path) -> None:
    encoded = stitch_and_encode(image_dir)

    if encoded is None:
        print("[MOCK RPi] Cannot send stitched image: no images found")
        return

    send(conn, lock, make_stitch_message("starting stitch"))

    for start in range(0, len(encoded), CHUNK_SIZE):
        chunk = encoded[start:start + CHUNK_SIZE]
        send(conn, lock, make_stitch_message(chunk))

    send(conn, lock, make_stitch_message("ending stitch"))


def reader_loop(
    conn: Any,
    messages: queue.Queue[dict[str, Any]],
    shutdown_event: threading.Event,
) -> None:
    parser = AndroidStreamParser()

    while not shutdown_event.is_set():
        try:
            data = conn.read(1024)
        except serial.SerialException as e:
            print(f"[MOCK RPi] Serial read failed: {e}")
            shutdown_event.set()
            break

        if not data:
            continue

        chunk = data.decode("utf-8", errors="ignore")
        print(f"[MOCK RPi] RAW RECV: {repr(chunk)}")

        parsed_messages = cast(list[dict[str, Any]], parser.feed(chunk))
        for message in parsed_messages:
            messages.put(message)


def prompt_non_empty(label: str) -> str:
    while True:
        value = input(label).strip()
        if value:
            return value
        print("  Value cannot be empty.")


def prompt_direction() -> str:
    while True:
        direction = input("Direction (N/E/S/W): ").strip().upper()
        if direction in VALID_DIRECTIONS:
            return direction
        print('  Direction must be one of "N", "E", "S", or "W".')


def show_menu_and_handle_choice(
    conn: Any,
    lock: threading.Lock,
    image_dir: Path,
) -> None:
    print("[1] Send TARGET response")
    print("[2] Send ROBOT position update")
    print("[3] Send stitched image (from mock_test_images/)")
    print("[4] Send custom message")
    print("[5] Wait for next message")

    choice = input("Select option: ").strip()

    if choice == "1":
        obstacle_id = prompt_non_empty("obstacle_id: ")
        image_id = prompt_non_empty("image_id: ")
        send(conn, lock, f"TARGET,{obstacle_id},{image_id}\n")
    elif choice == "2":
        x = prompt_non_empty("x: ")
        y = prompt_non_empty("y: ")
        direction = prompt_direction()
        send(conn, lock, f"ROBOT,{x},{y},{direction}\n")
    elif choice == "3":
        send_stitched_image(conn, lock, image_dir)
    elif choice == "4":
        message = input("Raw string to send: ")
        send(conn, lock, message)
    elif choice == "5":
        return
    else:
        print("  Unknown option. Waiting for next message.")


def handle_message(
    message: dict[str, Any],
    conn: Any,
    lock: threading.Lock,
    image_dir: Path,
    script: bool,
    script_state: dict[str, bool],
) -> None:
    cat = message.get("cat")
    value = message.get("value")

    if cat == "sendArena":
        if isinstance(value, dict):
            print_arena(cast(dict[str, Any], value))
        else:
            print(f"[MOCK RPi] sendArena received but value is not a dict: {value}")

        if script and not script_state["sent"]:
            script_state["sent"] = True
            time.sleep(1)
            send(conn, lock, "TARGET,1,38\n")
            time.sleep(1)
            send(conn, lock, "ROBOT,5,3,N\n")
            time.sleep(1)
            send_stitched_image(conn, lock, image_dir)
        elif not script:
            show_menu_and_handle_choice(conn, lock, image_dir)
    else:
        print(f"[MOCK RPi] Received: {message}")


def run(port: str, script: bool = False) -> None:
    print(f"[MOCK RPi] Opening {port} at {BAUD} baud...")

    try:
        conn = serial.Serial(port, baudrate=BAUD, timeout=1)
    except serial.SerialException as e:
        print(f"[MOCK RPi] Failed to open {port}: {e}")
        print("  Make sure the COM port is correct and the phone is paired + connected.")
        sys.exit(1)

    image_dir = Path(__file__).resolve().parent / "mock_test_images"
    image_dir.mkdir(exist_ok=True)
    messages: queue.Queue[dict[str, Any]] = queue.Queue()
    write_lock = threading.Lock()
    shutdown_event = threading.Event()
    reader = threading.Thread(
        target=reader_loop,
        args=(conn, messages, shutdown_event),
        daemon=True,
    )
    script_state = {"sent": False}

    print(f"[MOCK RPi] Listening on {port}. Connect the Android app now.")
    print(f"[MOCK RPi] Image directory: {image_dir}")
    print("  Press Ctrl+C to stop.\n")

    reader.start()

    try:
        while not shutdown_event.is_set():
            try:
                message = messages.get(timeout=0.2)
            except queue.Empty:
                continue

            handle_message(
                message,
                conn,
                write_lock,
                image_dir,
                script,
                script_state,
            )
    except KeyboardInterrupt:
        print("\n[MOCK RPi] Stopped.")
    finally:
        shutdown_event.set()
        reader.join(timeout=2)
        conn.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Mock RPi Bluetooth server for Android testing."
    )
    parser.add_argument("port", help="Bluetooth COM port, e.g. COM3")
    parser.add_argument(
        "--script",
        action="store_true",
        help="Auto-send one scripted response sequence after the first sendArena.",
    )
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    run(args.port, script=args.script)
