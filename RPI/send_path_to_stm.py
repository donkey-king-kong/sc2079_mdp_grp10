import serial
import json
import argparse
import time
import sys
import os
import stat

def send_path_to_stm(device_path, json_path_string):
    """
    Sends a sequence of commands (a "path") to the STM32 device.

    Args:
        device_path (str): The path to the serial device (e.g., '/dev/ttyACM0')
                           or a named pipe (e.g., 'rpi_to_stm').
        json_path_string (str): A JSON string representing the path as a list of
                                command dictionaries, e.g.,
                                '[{"type":"FW", "value":50}, {"type":"TR", "value":90}]'.
    """
    try:
        commands = json.loads(json_path_string)
    except json.JSONDecodeError:
        print(f"Error: Invalid JSON path string provided: {json_path_string}", file=sys.stderr)
        return

    # Determine if it's a named pipe or a serial device
    is_named_pipe = False
    ser = None
    try:
        if os.path.exists(device_path) and stat.S_ISFIFO(os.stat(device_path).st_mode):
            is_named_pipe = True
            print(f"Opening named pipe: {device_path} for writing...")
            # For named pipes, open in text mode for simpler string writing,
            # with line buffering but without adding extra newlines.
            ser = open(device_path, 'w', buffering=1)
        else:
            print(f"Opening serial port: {device_path} at 115200 baud...")
            ser = serial.Serial(device_path, 115200, timeout=1) # 115200 baud rate from C code, 1s timeout
    except serial.SerialException as e:
        print(f"Error opening serial port {device_path}: {e}", file=sys.stderr)
        print("Please ensure 'pyserial' is installed ('pip install pyserial') if using a physical serial port.", file=sys.stderr)
        return
    except OSError as e:
        print(f"Error opening device {device_path}: {e}", file=sys.stderr)
        return
    except Exception as e:
        print(f"An unexpected error occurred during device opening: {e}", file=sys.stderr)
        return

    if ser is None:
        print("Failed to initialize device communication.", file=sys.stderr)
        return

    cmd_id_counter = 0
    DEFAULT_MOVE_SPEED_PERCENTAGE = 70
    DEFAULT_TURN_SPEED_PERCENTAGE = 60

    print(f"Sending path to STM32 via {device_path}...")

    try:
        for command in commands:
            cmd_id_counter += 1
            cmd_type = command.get("type")
            cmd_value = command.get("value")

            stm_command_str = ""
            if cmd_type == "FW":
                stm_command_str = f":{cmd_id_counter}/MOTOR/FWD/{DEFAULT_MOVE_SPEED_PERCENTAGE}/{cmd_value};"
            elif cmd_type == "TL":
                stm_command_str = f":{cmd_id_counter}/MOTOR/TURNL/{DEFAULT_TURN_SPEED_PERCENTAGE}/{cmd_value};"
            elif cmd_type == "TR":
                stm_command_str = f":{cmd_id_counter}/MOTOR/TURNR/{DEFAULT_TURN_SPEED_PERCENTAGE}/{cmd_value};"
            elif cmd_type == "SS":
                print(f"[Python Script] Skipping snapshot command (handled by RPi), ID: {cmd_id_counter}.")
                continue # Do not send snapshot commands to STM32
            else:
                print(f"Warning: Unknown command type '{cmd_type}'. Skipping.", file=sys.stderr)
                continue

            print(f"[To STM32] (ID:{cmd_id_counter}): {stm_command_str}")
            
            if is_named_pipe:
                ser.write(stm_command_str) # Write the raw string to the named pipe
                ser.flush() # Ensure it's written immediately
            else:
                ser.write(stm_command_str.encode('utf-8')) # Serial expects bytes
            
            time.sleep(0.1) # Small delay between commands for better serial communication

    except (serial.SerialException, OSError) as e:
        print(f"Error during communication with {device_path}: {e}", file=sys.stderr)
    except Exception as e:
        print(f"An unexpected error occurred during path sending: {e}", file=sys.stderr)
    finally:
        if ser:
            ser.close()
            print("Device communication closed.")

    print("Path sending complete.")

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Send a path of commands to an STM32 device.")
    parser.add_argument("device_path", help="Path to the serial device (e.g., /dev/ttyACM0) or named pipe (e.g., rpi_to_stm).")
    parser.add_argument("json_path_string", help="JSON string representing the path, e.g., '[{\"type\":\"FW\", \"value\":50}, {\"type\":\"TR\", \"value\":90}]'")

    args = parser.parse_args()

    send_path_to_stm(args.device_path, args.json_path_string)

# --- How to Run This Script ---
#
# This script sends a sequence of navigation commands to an STM32 device,
# either through a serial port or a named pipe.
#
# Arguments:
#   1. device_path: The path to the communication device.
#                   - For a physical serial port: e.g., '/dev/ttyACM0' (Linux) or 'COM1' (Windows)
#                   - For a named pipe (FIFO): e.g., 'rpi_to_stm' (used for testing/simulation)
#   2. json_path_string: A JSON string representing the path. It should be a list of
#                        dictionaries, each with a "type" (FW, TL, TR, SS) and a "value".
#                        Note: "SS" (SNAPSHOT) commands are skipped as they are handled by the RPi, not STM32.
#
# Examples:
#
# 1. Sending to a named pipe (e.g., 'rpi_to_stm' for RPi simulation/testing):
#    First, ensure the named pipe exists and is being read by another process (e.g., `cat < RPI/rpi_to_stm`).
#    (Refer to `multithread_communication.c` for setup instructions for RPi_TESTING mode).
#    
#    # Example JSON string:
#    # '[{"type":"FW", "value":50}, {"type":"TR", "value":90}, {"type":"SS", "value":1}, {"type":"FW", "value":40}]'
#
#    python3 send_path_to_stm.py rpi_to_stm '[{"type":"FW", "value":50}, {"type":"TR", "value":90}, {"type":"SS", "value":1}, {"type":"FW", "value":40}]'
#
# 2. Sending to a physical serial port (e.g., /dev/ttyACM0):
#    Ensure 'pyserial' is installed: `pip install pyserial`
#    
#    # Example JSON string:
#    # '[{"type":"FW", "value":50}, {"type":"TR", "value":90}, {"type":"FW", "value":40}]'
#
#    python3 send_path_to_stm.py /dev/ttyACM0 '[{"type":"FW", "value":50}, {"type":"TR", "value":90}, {"type":"FW", "value":40}]'