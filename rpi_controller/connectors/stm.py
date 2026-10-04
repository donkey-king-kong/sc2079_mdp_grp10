"""STM serial connection and command/response protocol helpers."""

from dataclasses import dataclass
import re

import serial


MOVEMENT_COMMAND = re.compile(r"^(?:SF|SB|LF|LB|RF|RB)[+-]?\d+$")


@dataclass(frozen=True)
class STMResponse:
    raw: str
    kind: str
    body: str = ""


def validate_command(command: str) -> str:
    command = command.strip().upper()
    if command == "!" or command == "ZH" or command == "D":
        return command
    if MOVEMENT_COMMAND.fullmatch(command):
        return command
    raise ValueError("Unsupported Task 1 STM command: %s" % command)


def parse_response(raw: str) -> STMResponse:
    line = raw.strip()
    if not line.startswith("A"):
        return STMResponse(line, "unknown", line)
    body = line[1:].strip()
    if body == "ZH":
        return STMResponse(line, "heading", body)
    if body.startswith("d=") or body.startswith("a="):
        return STMResponse(line, "movement", body)
    if body.startswith("ERR "):
        return STMResponse(line, "error", body[4:])
    return STMResponse(line, "ack", body)


class STMConnector:
    """Creates the one shared serial connection used by the two STM workers."""

    def __init__(self, port="/dev/ttyUSB0", baudrate=115200, timeout=0.2):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.connection = None

    def connect(self):
        self.connection = serial.Serial(self.port, self.baudrate, timeout=self.timeout)
        print("[STM] Connected to %s at %s baud" % (self.port, self.baudrate))
        return self.connection

    def disconnect(self):
        if self.connection and self.connection.is_open:
            self.connection.close()
            print("[STM] Disconnected")

    def send_command(self, command: str):
        if not self.connection or not self.connection.is_open:
            raise RuntimeError("STM is not connected")
        command = validate_command(command)
        data = b"!" if command == "!" else (command + "\n").encode("ascii")
        self.connection.write(data)
        self.connection.flush()

    def read_ack(self):
        if not self.connection or not self.connection.is_open:
            raise RuntimeError("STM is not connected")
        raw = self.connection.readline()
        return raw.decode("utf-8", errors="replace").strip() if raw else None
