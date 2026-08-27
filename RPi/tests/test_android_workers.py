from __future__ import annotations

from threading import Event as StopEvent

from mdp_rpi.domain.events import (
    ANDROID_DISCONNECTED,
    ANDROID_PROTOCOL_ERROR,
    ANDROID_TRANSPORT_ERROR,
    AndroidEvent,
    AndroidEventType,
    Event,
)
from mdp_rpi.links.android import AndroidLink
from mdp_rpi.orchestration.orchestrator import AndroidOrchestrator
from mdp_rpi.orchestration.queues import AndroidTXQueue, EventQueue
from mdp_rpi.protocols.android import AndroidMessage
from mdp_rpi.workers.android import AndroidRXWorker, AndroidTXWorker


class FakeConnection:
    def __init__(self, reads: list[bytes]) -> None:
        self.reads = iter(reads)
        self.sent: list[bytes] = []
        self.closed = False

    def recv(self, size: int) -> bytes:
        return next(self.reads)

    def sendall(self, data: bytes) -> None:
        self.sent.append(data)

    def close(self) -> None:
        self.closed = True


def test_link_frames_android_tx_and_rx() -> None:
    first = AndroidMessage("status", "running")
    second = AndroidMessage("status", "finished")
    connection = FakeConnection(
        [first.to_json_line()[:5], first.to_json_line()[5:] + second.to_json_line()]
    )
    link = AndroidLink(connection)

    assert link.receive_once() == []
    assert link.receive_once() == [first, second]
    link.send(first)
    assert connection.sent == [first.to_json_line()]


def test_rx_worker_maps_messages_and_separates_protocol_errors() -> None:
    connection = FakeConnection(
        [
            AndroidMessage("control", "stop").to_json_line()
            + b'{"cat":"unknown","value":1}\n',
            b"",
        ]
    )
    queue = EventQueue()
    stop_event = StopEvent()
    worker = AndroidRXWorker(AndroidLink(connection), queue, stop_event)

    worker.run()

    assert queue.get_nowait() == AndroidEvent(AndroidEventType.STOP, "stop")
    protocol_error = queue.get_nowait()
    assert isinstance(protocol_error, Event)
    assert protocol_error.kind == ANDROID_PROTOCOL_ERROR
    assert queue.get_nowait().kind == ANDROID_DISCONNECTED


def test_tx_worker_sends_and_reports_transport_failure() -> None:
    class FailingConnection(FakeConnection):
        def sendall(self, data: bytes) -> None:
            raise OSError("send failed")

    events = EventQueue()
    messages = AndroidTXQueue()
    stop_event = StopEvent()
    messages.put(AndroidMessage("status", "running"))
    worker = AndroidTXWorker(
        AndroidLink(FailingConnection([])), messages, events, stop_event
    )

    worker.run()

    assert stop_event.is_set()
    assert events.get_nowait().kind == ANDROID_TRANSPORT_ERROR
    assert messages.unfinished_tasks == 0


def test_orchestrator_consumes_events_and_publishes_tx_messages() -> None:
    events = EventQueue()
    messages = AndroidTXQueue()
    seen: list[object] = []
    orchestrator = AndroidOrchestrator(events, messages, event_handler=seen.append)
    event = AndroidEvent(AndroidEventType.BEGIN_EXPLORE, "beginExplore")
    events.put(event)

    assert orchestrator.run_once(timeout=0) is True
    orchestrator.send_to_android(AndroidMessage("status", "planning"))

    assert seen == [event]
    assert messages.get_nowait() == AndroidMessage("status", "planning")
