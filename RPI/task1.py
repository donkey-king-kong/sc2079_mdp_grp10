import io
import json
import threading
import time

import requests
import serial
from picamera import PiCamera

# Configuration
SERIAL_PORT_STM32 =  "/dev/ttyACM0"
SERIAL_PORT_ANDROID = "/dev/rfcomm0"
BAUD_RATE = 115200
SERVER_URL_IMAGE = "http://192.168.22.21:5000/detect"
SERVER_URL_COORDINATES = "http://192.168.7.230:4000/path"
TIMEOUT = 60  # HTTP request timeout in seconds
MAX_RETRIES=3
RETRY_DELAY=0.3

DIR_MAP = {
  "N": 0,
  "E": 2,
  "S": 4,
  "W": 6
}

DIR_MAP_ANDROID = {
  0: "N",
  2: "E",
  4: "S",
  6: "W"
}

DIRECTION = {
  "FW": "forward",
  "BW": "reverse",
}

IMAGE_MAPPING = {
  "Number 1": 11,
  "Number 2": 12,
  "Number 3": 13,
  "Number 4": 14,
  "Number 5": 15,
  "Number 6": 16,
  "Number 7": 17,
  "Number 8": 18,
  "Number 9": 19,
  "Alphabet A": 20,
  "Alphabet B": 21,
  "Alphabet C": 22,
  "Alphabet D": 23,
  "Alphabet E": 24,
  "Alphabet F": 25,
  "Alphabet G": 26,
  "Alphabet H": 27,
  "Alphabet S": 28,
  "Alphabet T": 29,
  "Alphabet U": 30,
  "Alphabet V": 31,
  "Alphabet W": 32,
  "Alphabet X": 33,
  "Alphabet Y": 34,
  "Alphabet Z": 35,
  "Up Arrow": 36,
  "Down Arrow": 37,
  "Right Arrow": 38,
  "Left Arrow": 39,
  "Stop sign": 40,
}

snap_positions=[]
    
def initialize_camera():
    """Initialize the Pi Camera"""
    print("Initializing camera...")
    picam2 = PiCamera()
    picam2.resolution = (640, 480)
    time.sleep(2)  # Let camera warm up
    print("Camera ready!")
    return picam2


def capture_image(picam2):
    """Capture image from Pi Camera"""
    print("Capturing image...")
    # Capture to memory
    stream = io.BytesIO()
    picam2.capture(stream, format="jpeg")
    stream.seek(0)
    print("Image captured!")
    return stream


def send_image_to_server(image_stream, object_id, ser_android):
    """Send image to server via HTTP POST"""

    def send_image():
        print(f"Sending image to server: {SERVER_URL_IMAGE}")

        files = {"image": ("photo.jpg", image_stream, "image/jpeg")}
        data = {"object_id": object_id}

        try:
            response = requests.post(
                SERVER_URL_IMAGE, files=files, data=data, timeout=TIMEOUT
            )

            print(f"Server response status: {response.status_code}")

            # Check if response is successful
            if response.status_code == 200:
                result = response.json()

                # Check if object was detected
                if result.get("success") == True and result.get("detected") == True:
                    objects = result.get("objects", [])

                    if objects:
                        # Get the first (and only) detected object
                        sorted_obj = sorted(objects, key=lambda x: x.get("confidence",0), reverse=True)
                        print(sorted_obj)

                        selected_obj = None
                        for obj in sorted_obj:
                          class_label = obj.get("class_label") or obj.get("class", "Unkown")
                          if isinstance(class_label, str) and " - " in class_label:
                            class_label = class_label.split(" - ", 1)[0].strip()
                          if class_label != "Bullseye":
                            selected_obj = obj
                            break

                        if not selected_obj:
                          print("\n✗ Only Bullseye detected")
                          return

                        class_name = selected_obj.get("class", "Unkown")
                        confidence = selected_obj.get("confidence", 0) * 100

                        print(
                            f"\n✓ SUCCESS! Detected: {class_name} (confidence: {confidence:.1f}%)"
                        )

                        if ser_android and ser_android.is_open:
                            class_key = class_name
                            if isinstance(class_name, str) and " - " in class_name:
                              class_key = class_name.split(" - ", 1)[0].strip()
                            class_name = IMAGE_MAPPING[class_key]
                            resp = "TARGET," + str(object_id) + "," + str(class_name)
                            print(resp)
                            resp = json.dumps(resp) + "\n"
                            if not send_with_ack(ser_android, resp):
                              print("Warning could net send to android")
                            #ser_android.write(resp.encode("utf-8"))

                else:
                  print("\n✗ No object detected")
            else:
                print(f"✗ Server error: {response.status_code}")

        except requests.exceptions.RequestException as e:
            print(f"✗ HTTP request failed: {e}")
        except Exception as e:
            print(f"✗ Error processing response: {e}")

    thread = threading.Thread(target=send_image, daemon=True)
    thread.start()


def send_with_ack(ser_android, message, timeout=2):
  for attempt in range(MAX_RETRIES):
    try:
      print(f"{attempt} {message}")
      ser_android.write(message.encode("utf-8"))
      ser_android.flush()

      if attempt < MAX_RETRIES - 1:
        time.sleep(RETRY_DELAY)

    except Exception as e:
      print(f"Error: {e}")

  return True

def parseJson_Android(data: str) -> dict:
    # Parse the input JSON string
    parsed = json.loads(data)

    obstacles = []
    for obs in parsed.get("value", {}).get("obstacles", []):
        obs_id = int(obs.get("id", -1))
        if obs_id <= 0:
            # Ignore deleted/placeholder obstacles
            continue

        raw_dir = obs.get("dir", obs.get("d"))
        if isinstance(raw_dir, int):
            # Accept 1=N,2=E,3=S,4=W from Android
            dir_val = {1: 0, 2: 2, 3: 4, 4: 6}.get(raw_dir, -1)
        else:
            dir_val = DIR_MAP.get(raw_dir, -1)  # N/E/S/W strings
        if dir_val == -1:
            # Skip invalid directions
            continue

        new_obs = {
            "id": obs_id,
            "x": obs["x"] - 1,   # shift by -1
            "y": obs["y"] - 1,   # shift by -1
            "d": dir_val  # default -1 if unknown
        }
        obstacles.append(new_obs)

    # Final output with hardcoded fields
    result = {
        "obstacles": obstacles,
        "robot_x": 1,
        "robot_y": 1,
        "robot_dir": 0,
        "retrying": False
    }

    return result

def send_coordinates_to_server(json_data):
    try:
        response = requests.post(
            SERVER_URL_COORDINATES,
            json=json_data,
            timeout=TIMEOUT,
            headers={"Content-Type": "application/json"},
        )

        if response.status_code == 200:
            result = response.json()
            return result
        else:
            print(f"✗ Server error: {response.status_code}")
            return None
    except requests.exceptions.RequestException as e:
        print(f"✗ HTTP request failed: {e}")
        return None
    except Exception as e:
        print(f"✗ Error processing response: {e}")
        return None


def parse_route_response(route_response):
    """Parse the route server response and extract path and commands"""
    try:
        data = route_response.get("data")
        if not isinstance(data, dict):
            # Accept top-level response format as well
            data = route_response

        path = data.get("path", [])
        commands = data.get("commands", [])
        distance = data.get("distance", 0)
        global snap_positions
        snap_positions = data.get("snap_positions", [])
        if not snap_positions and path:
            # Derive snap positions from path when not provided
            derived = []
            for point in path:
                screenshot_id = point.get("s", point.get("screenshot_id", -1))
                if screenshot_id != -1:
                    derived.append(
                        {
                            "x": point.get("x"),
                            "y": point.get("y"),
                            "d": point.get("d"),
                        }
                    )
            snap_positions = derived

        print(f"\n✓ Route parsed successfully:")
        print(f"  Distance: {distance}")
        print(f"  Path points: {len(path)}")
        print(f"  Commands: {len(commands)}")

        print(commands)

        return path, commands
    except Exception as e:
        print(f"✗ Error parsing route response: {e}")
        return [], []

# ---------------- STM32 ----------------
def parse_and_send_command(command, ser_stm32, picam2, ser_android):
    """Translate a single command and send to STM32"""
    if command == "FIN":
        print("🚀 Command execution finished")
        return True

    if command.startswith("SNAP"):
        command = "SP" + command[4:]

    cmd_type = command[:2]
    cmd_val = command[2:]

    if cmd_type == "SP":  # SNAPSHOT command
        obj_id = int(cmd_val)
        print(f"📷 SNAP command (object_id={obj_id})")
        if picam2:
          img = capture_image(picam2)
          global snap_positions
          if snap_positions:
              snap_position_coord = snap_positions.pop(0)
              resp = "ROBOT," + str(int(snap_position_coord.get("x"))) + "," + str(int(snap_position_coord.get("y"))) + "," + str(DIR_MAP_ANDROID.get(snap_position_coord.get("d")))
              print(resp)
              resp = json.dumps(resp) + "\n"
              ser_android.write(resp.encode("utf-8"))
        send_image_to_server(img, obj_id, ser_android)
        return False

    if cmd_type in ("FW", "BW"):
        direction = DIRECTION[cmd_type]
        val = int(cmd_val)
        stm32_command = f"center,0,{direction},{val}\n".encode("utf-8")
        ser_stm32.write(stm32_command)
        print("➡️", stm32_command.decode().strip())
        return False

    if cmd_type in ("FR", "FL", "BR", "BL"):
        turn = "right" if cmd_type[1] == "R" else "left"
        move_dir = "forward" if cmd_type[0] == "F" else "reverse"
        stm32_command = f"{turn},90,{move_dir},0\n".encode("utf-8")
        ser_stm32.write(stm32_command)
        print("↩️", stm32_command.decode().strip())
        return False

    return False

def execute_commands(ser_stm32, commands_list, picam2, ser_android):
    print(f"🚀 Starting command execution ({len(commands_list)} commands)")
    for command in commands_list:
        finish = parse_and_send_command(command, ser_stm32, picam2, ser_android)
        if finish:
            break
        time.sleep(2)  # small delay between commands

def listen_for_coordinates(ser_android, ser_stm32, picam2):
    while True:
        try:
            if ser_android.in_waiting > 0:
                time.sleep(0.1)

                data = ser_android.read(ser_android.in_waiting).decode("utf-8").strip()

                if data:
                    json_data = parseJson_Android(data)

                    if json_data:
                        print(json_data)

                        route_response = send_coordinates_to_server(json_data)

                        if route_response:
                            _, commands_list = parse_route_response(
                                route_response
                            )

                            if commands_list:
                                execute_commands(
                                    ser_stm32, commands_list, picam2, ser_android
                                )
                            else:
                                print("✗ No commands found in response")
                        else:
                            print("✗ No route response found")
                    else:
                        print("✗ No JSON data found in response")

        except UnicodeDecodeError as e:
            print(f"✗ UnicodeDecodeError: {e}")
            print("Retrying...")
            time.sleep(1)
            continue
        except Exception as e:
            print(f"✗ Error: {e}")
            print("Retrying...")
            time.sleep(1)
            continue


def main():
    """Main execution loop"""
    # Initialize serial connection
    print(f"Opening serial port: {SERIAL_PORT_STM32}")
    ser_stm32 = serial.Serial(SERIAL_PORT_STM32, BAUD_RATE, timeout=1)

    print(f"Opening Android Serial Port: {SERIAL_PORT_ANDROID}")
    ser_android = serial.Serial(SERIAL_PORT_ANDROID, BAUD_RATE, timeout=1)

    # Initialize camera (but don't take photo yet)
    picam2 = initialize_camera()

    try:
        listen_for_coordinates(ser_android, ser_stm32, picam2)
    except KeyboardInterrupt:
        print("\n\nInterrupted by user")

    finally:
        # Cleanup
        print("Cleaning up...")
        picam2.close()
        ser_stm32.close()
        print("Done!")


if __name__ == "__main__":
    main()
