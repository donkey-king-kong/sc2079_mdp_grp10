import serial


class STMConnector:
    def __init__(self, port="/dev/ttyUSB0", baudrate=115200, timeout=2):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.connection = None

    def connect(self):
        self.connection = serial.Serial(
            port=self.port,
            baudrate=self.baudrate,
            timeout=self.timeout,
        )

        print(
            f"[STM] Connected to {self.port} "
            f"at {self.baudrate} baud"
        )

    def disconnect(self):
        if self.connection and self.connection.is_open:
            self.connection.close()
            print("[STM] Disconnected")

    def send_command(self, command: str):
        if not self.connection or not self.connection.is_open:
            raise RuntimeError("STM is not connected")

        message = command + "\n"

        self.connection.write(
            message.encode("utf-8")
        )
        self.connection.flush()

    def read_ack(self):
        if not self.connection or not self.connection.is_open:
            raise RuntimeError("STM is not connected")

        response = self.connection.readline()

        if not response:
            return None

        ack = response.decode(
            "utf-8",
            errors="ignore",
        ).strip()

        if ack == "A":
            return "A"

        return ack