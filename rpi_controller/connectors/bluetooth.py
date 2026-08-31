import time

from protocol import AndroidStreamParser


class BluetoothConnector:
    def __init__(self, device="/dev/rfcomm0", retry_delay=1):
        self.device = device
        self.retry_delay = retry_delay
        self.connection = None
        self.parser = AndroidStreamParser()

    def connect(self):
        while True:
            try:
                self.connection = open(
                    self.device,
                    "r+b",
                    buffering=0,
                )

                print(f"[BT] Connected to {self.device}")
                return True

            except FileNotFoundError:
                print(f"[BT] Waiting for {self.device}...")
                time.sleep(self.retry_delay)

            except OSError as e:
                print(f"[BT] Connection error: {e}")
                time.sleep(self.retry_delay)

    def disconnect(self):
        if self.connection:
            self.connection.close()
            self.connection = None
            print("[BT] Disconnected")

    def send(self, message: str):
        if not self.connection:
            raise RuntimeError("Bluetooth is not connected")

        self.connection.write(
            message.encode("utf-8")
        )

    def read_messages(self, size=1024):
        if not self.connection:
            raise RuntimeError("Bluetooth is not connected")

        data = self.connection.read(size)

        if not data:
            return []

        chunk = data.decode(
            "utf-8",
            errors="ignore",
        )

        return self.parser.feed(chunk)