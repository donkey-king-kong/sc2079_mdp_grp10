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


def main():
    manager = RPiManager()
    manager.stm.connection = FakeSTMConnection()

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

    manager.handle_android_message(android_arena_message)


if __name__ == "__main__":
    main()