from connectors.android import AndroidConnector
from connectors.algo import AlgoConnector
from connectors.bluetooth import BluetoothConnector
from connectors.stm import STMConnector
from connectors.imaging import ImagingConnector
from router import CommandRouter
from pathlib import Path
from image_tracker import ImageTracker
from image_stitcher import stitch_images
from image_sender import AndroidImageSender
class RPiManager:
    def __init__(self):
        self.android = AndroidConnector()
        self.bluetooth = BluetoothConnector()
        self.algo = AlgoConnector()
        self.stm = STMConnector()
        self.imaging = ImagingConnector()
        self.router = CommandRouter()
        self.image_tracker = ImageTracker(
           Path(__file__).resolve().parent / "imaging" / "data"
        )
        self.image_sender = AndroidImageSender(
        self.bluetooth
        )

    def send_to_stm(self, command):
        print(f"[MANAGER] STM <- {command}")

        self.stm.send_command(command)

        ack = self.stm.read_ack()
        print(f"[MANAGER] STM ACK -> {ack}")

        return ack

    def handle_android_message(self, message):
        if not message:
            return

        category = message.get("cat")
        value = message.get("value")

        if category == "stm":
            stm_command = self.android.to_stm_command(value)

            if stm_command is None:
                print(f"[MANAGER] Invalid Android STM command: {value}")
                return

            self.send_to_stm(stm_command)

        elif category == "sendArena":
            self.handle_arena_data(value)

        else:
            print(f"[MANAGER] UNKNOWN ANDROID MESSAGE: {message}")

    def handle_arena_data(self, arena_data):
        print(f"[MANAGER] ARENA DATA -> {arena_data}")

        if not isinstance(arena_data, dict):
            print("[MANAGER] Invalid arena data")
            return

        direction_map = {
            0: "N",
            1: "E",
            2: "S",
            3: "W",
        }

        robot = {
            "x": arena_data.get("robot_x"),
            "y": arena_data.get("robot_y"),
            "dir": direction_map.get(
                arena_data.get("robot_direction")
            ),
        }

        obstacles = []

        for obstacle in arena_data.get("obstacles", []):
            obstacles.append({
                "id": obstacle.get("id"),
                "x": obstacle.get("x"),
                "y": obstacle.get("y"),
                "dir": direction_map.get(
                    obstacle.get("d")
                ),
            })

        if (
            robot["x"] is None
            or robot["y"] is None
            or robot["dir"] is None
        ):
            print("[MANAGER] Invalid robot data")
            return

        self.request_algo_navigation(
            robot=robot,
            obstacles=obstacles,
        )

    def request_algo_navigation(self, robot, obstacles):
        print("[MANAGER] Requesting navigation plan from Algo...")

        result = self.algo.navigate(
            robot=robot,
            obstacles=obstacles,
        )

        commands = result["data"]["commands"]

        print(
            f"[MANAGER] Received {len(commands)} commands from Algo"
        )

        self.handle_algo_commands(commands)

    def handle_algo_commands(self, commands):
        for command in commands:
            routed = self.router.classify_algo_command(command)

            target = routed["target"]
            value = routed["value"]

            if target == "stm":
                self.send_to_stm(value)

            elif target == "camera":
                self.handle_camera_snap(value)

            elif target == "finish":
                self.handle_finish()

            else:
                print(
                    f"[MANAGER] UNKNOWN ALGO COMMAND: {value}"
                )

    def handle_camera_snap(self, obstacle_id):
        print(
            f"[MANAGER] CAMERA SNAP obstacle {obstacle_id}"
        )

        # Remember which images existed before this SNAP.
        before = self.image_tracker.snapshot()

        # Existing imaging code remains unchanged.
        result = self.imaging.capture_and_predict(
            obstacle_id
        )

        # Associate the newly saved image with this obstacle.
        selected_image = self.image_tracker.record_new_image(
            obstacle_id,
            before,
        )

        print(
            f"[MANAGER] IMAGING RESULT -> {result}"
        )

        if selected_image is not None:
            print(
                f"[MANAGER] SELECTED IMAGE -> "
                f"{selected_image.name}"
            )

        self.handle_imaging_result(result)
       

    def handle_imaging_result(self, result):
        if not isinstance(result, dict):
            print("[MANAGER] Invalid imaging result")
            return

        obstacle_id = result.get("obstacle_id")
        image_id = result.get("image_id")
        confidence = result.get("confidence")

        print(
            "[MANAGER] IMAGE RECOGNITION "
            f"obstacle={obstacle_id}, "
            f"image_id={image_id}, "
            f"confidence={confidence}"
        )

        # No valid target was detected.
        if obstacle_id is None or image_id is None:
            print(
                "[MANAGER] No valid target to send to Android"
            )
            return

        # Android expects:
        # TARGET,<obstacle_id>,<image_id>
        message = f"TARGET,{obstacle_id},{image_id}\n"

        self.bluetooth.send(message)

        print(
            f"[MANAGER] ANDROID <- "
            f"TARGET,{obstacle_id},{image_id}"
        )
    def handle_finish(self):
        print("[MANAGER] FINISHED")

        selected_images = self.image_tracker.get_selected_images()

        if not selected_images:
            print("[MANAGER] No images available to send to Android")
            return

        print(
            f"[MANAGER] Stitching {len(selected_images)} selected images"
        )

        output_path = (
            Path(__file__).resolve().parent
            / "imaging"
            / "data"
            / "stitched.jpg"
        )

        stitched_path = stitch_images(
            selected_images,
            output_path,
        )

        if stitched_path is None:
            print("[MANAGER] Failed to create stitched image")
            return

        sent = self.image_sender.send_image(
            stitched_path
        )

        if sent:
            print("[MANAGER] Final image sent to Android")
        else:
            print("[MANAGER] Failed to send final image to Android")

        # Prepare tracker for the next Task 1 run.
        self.image_tracker.clear()    

    def run_bluetooth_loop(self):
        print("[MANAGER] Starting Bluetooth connection...")

        self.bluetooth.connect()

        print("[MANAGER] Waiting for Android messages...")

        try:
            while True:
                messages = self.bluetooth.read_messages()

                for message in messages:
                    print(
                        f"[MANAGER] ANDROID -> {message}"
                    )
                    self.handle_android_message(message)

        except KeyboardInterrupt:
            print("\n[MANAGER] Stopping...")

        finally:
            self.bluetooth.disconnect()

    def start(self):
        print("[MANAGER] Starting RPi Manager")

        self.stm.connect()
        self.run_bluetooth_loop()


if __name__ == "__main__":
    manager = RPiManager()
    manager.start()
