"""STM UART serialization and parsing workers for the Task 1 kernel."""

from dataclasses import dataclass
import queue
import re
import threading

import serial

from task1_events import Event, EventType, STMCommand


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


def response_event(response: STMResponse) -> Event:
    payload = {"raw": response.raw, "body": response.body}
    if response.kind == "heading":
        return Event(EventType.STM_ZH_COMPLETE, payload)
    if response.kind == "movement":
        if " ABORT" in response.body:
            return Event(EventType.STM_ABORT, payload)
        if response.body.endswith(" TO"):
            return Event(EventType.STM_TIMEOUT, payload)
        return Event(EventType.STM_MOVEMENT_COMPLETE, payload)
    if response.kind == "error":
        return Event(EventType.STM_ERROR, payload)
    return Event(EventType.STM_UNKNOWN_RESPONSE, payload)


class STMTXWorker(threading.Thread):
    """Owns UART writes. It never waits for nor interprets STM replies."""

    def __init__(self, connection, command_queue, event_queue, shutdown_event):
        super().__init__(name="STM-TX")
        self.connection = connection
        self.command_queue = command_queue
        self.event_queue = event_queue
        self.shutdown_event = shutdown_event

    def run(self):
        while True:
            item = self.command_queue.get()
            if item is None:
                return
            try:
                command = validate_command(item.command)
                data = b"!" if command == "!" else (command + "\n").encode("ascii")
                self.connection.write(data)
                self.connection.flush()
                print("[STM TX] %s" % command)
            except (ValueError, serial.SerialException, OSError) as error:
                self.event_queue.put(Event(EventType.STM_ERROR, {"error": str(error)}))


class STMRXWorker(threading.Thread):
    """Owns UART reads and publishes parsed events; it has no route knowledge."""

    def __init__(self, connection, event_queue, shutdown_event):
        super().__init__(name="STM-RX")
        self.connection = connection
        self.event_queue = event_queue
        self.shutdown_event = shutdown_event

    def run(self):
        while not self.shutdown_event.is_set():
            try:
                raw = self.connection.readline()
            except (serial.SerialException, OSError) as error:
                if not self.shutdown_event.is_set():
                    self.event_queue.put(Event(EventType.STM_ERROR, {"error": str(error)}))
                return
            if not raw:
                continue
            response = parse_response(raw.decode("utf-8", errors="replace"))
            event = response_event(response)
            print("[STM RX] %s" % response.raw)
            self.event_queue.put(event)


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

    # Compatibility for the legacy Android manager. Task1Runtime uses the
    # dedicated STMTXWorker/STMRXWorker instead, never these blocking methods.
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
