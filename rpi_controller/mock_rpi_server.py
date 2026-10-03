"""
Mock RPi server — runs on a Windows laptop connected to the Android phone via Bluetooth.

Setup:
  1. Pair the Android phone to the Windows laptop via Bluetooth settings.
  2. In Windows: Settings -> Bluetooth -> More Bluetooth options -> COM Ports tab.
     Note the incoming COM port assigned (e.g. COM3).
  3. Run:  python mock_rpi_server.py COM3
  4. On the Android app, connect to the laptop via Bluetooth and send arena data.

The server prints every received arena message with obstacles and robot position.
No algo, STM, or imaging involved — pure receive-and-display.
"""

import sys
import serial
from protocol import AndroidStreamParser

BAUD = 115200


DIRECTION_LABELS = {0: "N", 1: "E", 2: "S", 3: "W"}


def print_arena(value: dict):
    robot_x = value.get("robot_x", "?")
    robot_y = value.get("robot_y", "?")
    robot_dir = DIRECTION_LABELS.get(value.get("robot_direction"), "?")

    print()
    print("=== ARENA RECEIVED ===")
    print(f"  Robot   x={robot_x}  y={robot_y}  dir={robot_dir}")

    obstacles = value.get("obstacles", [])
    if obstacles:
        print(f"  Obstacles ({len(obstacles)}):")
        for obs in obstacles:
            ox = obs.get("x", "?")
            oy = obs.get("y", "?")
            od = DIRECTION_LABELS.get(obs.get("d"), "?")
            oid = obs.get("id", "?")
            print(f"    id={oid}  x={ox}  y={oy}  facing={od}")
    else:
        print("  Obstacles: none")

    print("======================")
    print()


def handle_message(message: dict):
    cat = message.get("cat")
    value = message.get("value")

    if cat == "sendArena":
        if isinstance(value, dict):
            print_arena(value)
        else:
            print(f"[MOCK RPi] sendArena received but value is not a dict: {value}")

    else:
        print(f"[MOCK RPi] Received: {message}")


def run(port: str):
    print(f"[MOCK RPi] Opening {port} at {BAUD} baud...")

    try:
        conn = serial.Serial(port, baudrate=BAUD, timeout=1)
    except serial.SerialException as e:
        print(f"[MOCK RPi] Failed to open {port}: {e}")
        print("  Make sure the COM port is correct and the phone is paired + connected.")
        sys.exit(1)

    print(f"[MOCK RPi] Listening on {port}. Connect the Android app now.")
    print("  Press Ctrl+C to stop.\n")

    parser = AndroidStreamParser()

    try:
        while True:
            data = conn.read(1024)
            if not data:
                continue

            chunk = data.decode("utf-8", errors="ignore")
            messages = parser.feed(chunk)

            for message in messages:
                handle_message(message)

    except KeyboardInterrupt:
        print("\n[MOCK RPi] Stopped.")

    finally:
        conn.close()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python mock_rpi_server.py <COM_PORT>")
        print("Example: python mock_rpi_server.py COM3")
        sys.exit(1)

    run(sys.argv[1])
