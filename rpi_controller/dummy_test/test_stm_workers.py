import queue
import sys
import threading
import types
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
if "serial" not in sys.modules:
    serial_stub = types.ModuleType("serial")
    serial_stub.SerialException = OSError
    sys.modules["serial"] = serial_stub

from connectors.stm import STMRXWorker, STMTXWorker, parse_response, validate_command
from task1_events import EventType, STMCommand


class FakeSerial:
    def __init__(self):
        self.is_open, self.sent, self.lines = True, [], queue.Queue()

    def write(self, data):
        self.sent.append(data)

    def flush(self):
        pass

    def readline(self):
        try:
            return self.lines.get(timeout=0.02)
        except queue.Empty:
            return b""


class STMWorkerTests(unittest.TestCase):
    def test_tx_only_writes_and_does_not_wait_for_reply(self):
        serial, commands, events = FakeSerial(), queue.Queue(), queue.Queue()
        commands.put(STMCommand("SF010"))
        commands.put(None)
        worker = STMTXWorker(serial, commands, events, threading.Event())
        worker.start(); worker.join(1)
        self.assertEqual(serial.sent, [b"SF010\n"])
        self.assertTrue(events.empty())

    def test_rx_turns_completion_and_malformed_lines_into_events(self):
        serial, events, shutdown = FakeSerial(), queue.Queue(), threading.Event()
        serial.lines.put(b"A d=10.0 e=+0.1\n")
        serial.lines.put(b"garbage\n")
        worker = STMRXWorker(serial, events, shutdown)
        worker.start()
        first, second = events.get(timeout=1), events.get(timeout=1)
        shutdown.set(); worker.join(1)
        self.assertEqual(first.type, EventType.STM_MOVEMENT_COMPLETE)
        self.assertEqual(second.type, EventType.STM_UNKNOWN_RESPONSE)

    def test_abort_and_timeout_are_distinct(self):
        self.assertEqual(parse_response("A d=10 e=0 ABORT").kind, "movement")
        self.assertEqual(validate_command("RF090"), "RF090")
        with self.assertRaises(ValueError):
            validate_command("F90")


if __name__ == "__main__":
    unittest.main()
