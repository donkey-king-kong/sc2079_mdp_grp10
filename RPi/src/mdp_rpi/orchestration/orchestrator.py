"""Queue-driven mission coordinator with no direct Android I/O."""

from __future__ import annotations

from queue import Empty
from threading import Event as StopEvent
from typing import Callable

from mdp_rpi.domain.events import AndroidEvent, AndroidEventType, Event
from mdp_rpi.orchestration.queues import AndroidTXQueue, EventQueue
from mdp_rpi.protocols.android import AndroidMessage


OrchestratorEvent = Event | AndroidEvent
EventHandler = Callable[[OrchestratorEvent], None]


class AndroidOrchestrator:
    """Own mission decisions; Android I/O is performed by worker threads."""

    def __init__(
        self,
        event_queue: EventQueue,
        android_tx_queue: AndroidTXQueue,
        *,
        event_handler: EventHandler | None = None,
    ) -> None:
        """Create an orchestrator connected to inbound and outbound queues."""
        self.event_queue = event_queue
        self.android_tx_queue = android_tx_queue
        self.event_handler = event_handler
        self.stopped = False
        self.stop_event: StopEvent | None = None

    def run_once(self, *, timeout: float | None = None) -> bool:
        """Handle one queued event; return false when no event is available."""
        try:
            event = self.event_queue.get(timeout=timeout)
        except Empty:
            return False
        try:
            self.handle_event(event)
        finally:
            self.event_queue.task_done()
        return True

    def run_forever(self, stop_event: StopEvent) -> None:
        """Consume events until shutdown is requested or Android sends STOP."""
        self.stop_event = stop_event
        while not stop_event.is_set() and not self.stopped:
            self.run_once(timeout=0.1)

    def handle_event(self, event: OrchestratorEvent) -> None:
        """Apply one event and notify the optional mission decision handler."""
        if isinstance(event, AndroidEvent) and event.event_type is AndroidEventType.STOP:
            self.stopped = True
            if self.stop_event is not None:
                self.stop_event.set()
        if self.event_handler is not None:
            self.event_handler(event)

    def send_to_android(self, message: AndroidMessage) -> None:
        """Queue one outbound Android message for the TX worker."""
        self.android_tx_queue.put(message)


# Keep the shorter name available for the central coordinator used by main.py.
Orchestrator = AndroidOrchestrator
