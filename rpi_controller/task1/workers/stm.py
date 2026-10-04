"""STM UART workers for Task 1."""

import threading

import serial

from connectors.stm import parse_response, validate_command
from task1.events import Event, EventType


def response_event(response):
    """Translate an STM protocol response into a Task 1 event."""
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
            self.event_queue.put(response_event(response))
            print("[STM RX] %s" % response.raw)
