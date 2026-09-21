from manager import RPiManager


class FakeSTMConnection:
    def __init__(self):
        self.is_open = True
        self.sent = []

    def write(self, data):
        self.sent.append(data)

    def flush(self):
        pass

    def readline(self):
        return b"A\n"


class FakeImagingConnector:
    def capture_and_predict(self, obstacle_id):
        return {
            "obstacle_id": str(obstacle_id),
            "image_id": "38",
            "confidence": 0.94,
        }


def main():
    manager = RPiManager()

    manager.stm.connection = FakeSTMConnection()
    manager.imaging = FakeImagingConnector()

    android_arena_message = {
        "cat": "sendArena",
        "value": {
            "obstacles": [
                {
                    "x": 8,
                    "y": 5,
                    "d": 2,
                    "id": 1,
                },
                {
                    "x": 14,
                    "y": 6,
                    "d": 3,
                    "id": 2,
                },
            ],
            "robot_x": 1,
            "robot_y": 1,
            "robot_direction": 0,
        },
    }

    manager.handle_android_message(
        android_arena_message
    )


if __name__ == "__main__":
    main()