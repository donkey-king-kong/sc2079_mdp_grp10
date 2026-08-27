"""Android RX/TX workers and their explicit shared lifecycle manager."""

from __future__ import annotations

from queue import Empty
from threading import Event as StopEvent, Thread

from mdp_rpi.domain.events import (
    ANDROID_DISCONNECTED,
    ANDROID_PROTOCOL_ERROR,
    ANDROID_TRANSPORT_ERROR,
    AndroidEvent,
    AndroidEventType,
    Event,
)
from mdp_rpi.links.android import AndroidConnectionClosed, AndroidLink
from mdp_rpi.orchestration.queues import AndroidTXQueue, EventQueue
from mdp_rpi.protocols.android import AndroidMessage, AndroidProtocolError


def android_message_to_event(message: AndroidMessage) -> AndroidEvent:
    """Translate the Android wire vocabulary into application events."""
    if message.cat == "control":
        controls = {
            "beginExplore": AndroidEventType.BEGIN_EXPLORE,
            "beginFastest": AndroidEventType.BEGIN_FASTEST,
            "stop": AndroidEventType.STOP,
        }
        try:
            event_type = controls[message.value]
        except (KeyError, TypeError) as exc:
            raise AndroidProtocolError(
                f"Unknown control value: {message.value!r}"
            ) from exc
        return AndroidEvent(event_type, message.value)

    if message.cat == "stm":
        if (
            not isinstance(message.value, str)
            or not message.value.startswith("<")
            or not message.value.endswith(">")
        ):
            raise AndroidProtocolError("STM command must be a '<...>' string")
        return AndroidEvent(AndroidEventType.STM_COMMAND, message.value)

    categories = {
        "sendArena": AndroidEventType.SEND_ARENA,
        "stitch-image": AndroidEventType.STITCH_IMAGES,
        "status": AndroidEventType.STATUS,
        "location": AndroidEventType.LOCATION,
        "health": AndroidEventType.HEALTH,
        "image-rec": AndroidEventType.IMAGE_RECOGNITION,
    }
    try:
        event_type = categories[message.cat]
    except KeyError as exc:
        raise AndroidProtocolError(
            f"Unknown Android category: {message.cat!r}"
        ) from exc
    return AndroidEvent(event_type, message.value)


class AndroidRXWorker:
    """Receive bytes, parse Android messages, and publish events."""

    def __init__(
        self,
        android_link: AndroidLink,
        event_queue: EventQueue,
        stop_event: StopEvent,
    ) -> None:
        """Configure the receive worker with transport, event queue, and stop signal."""
        self.android_link = android_link
        self.event_queue = event_queue
        self.stop_event = stop_event

    def run(self) -> None:
        """Receive, parse, map, and publish Android input until shutdown."""
        while not self.stop_event.is_set():
            try:
                messages = self.android_link.receive_once()
            except AndroidConnectionClosed as exc:
                self.event_queue.put(Event(ANDROID_DISCONNECTED, exc))
                self.stop_event.set()
                return
            except AndroidProtocolError as exc:
                self.event_queue.put(Event(ANDROID_PROTOCOL_ERROR, exc))
                continue
            except OSError as exc:
                self.event_queue.put(Event(ANDROID_TRANSPORT_ERROR, exc))
                self.stop_event.set()
                return
            except RuntimeError as exc:
                self.event_queue.put(Event(ANDROID_TRANSPORT_ERROR, exc))
                self.stop_event.set()
                return

            for message in messages:
                try:
                    self.event_queue.put(android_message_to_event(message))
                except AndroidProtocolError as exc:
                    self.event_queue.put(Event(ANDROID_PROTOCOL_ERROR, exc))


class AndroidTXWorker:
    """Consume outbound Android messages and send them over the link."""

    def __init__(
        self,
        android_link: AndroidLink,
        tx_queue: AndroidTXQueue,
        event_queue: EventQueue,
        stop_event: StopEvent,
        *,
        poll_timeout: float = 0.1,
    ) -> None:
        """Configure the transmit worker with its queues and stop signal."""
        self.android_link = android_link
        self.tx_queue = tx_queue
        self.event_queue = event_queue
        self.stop_event = stop_event
        self.poll_timeout = poll_timeout

    def run(self) -> None:
        """Send queued messages until shutdown or a transport failure."""
        while not self.stop_event.is_set():
            try:
                message = self.tx_queue.get(timeout=self.poll_timeout)
            except Empty:
                continue

            try:
                self.android_link.send(message)
            except OSError as exc:
                self.event_queue.put(Event(ANDROID_TRANSPORT_ERROR, exc))
                self.stop_event.set()
                return
            except RuntimeError as exc:
                self.event_queue.put(Event(ANDROID_TRANSPORT_ERROR, exc))
                self.stop_event.set()
                return
            finally:
                self.tx_queue.task_done()


class AndroidRuntime:
    """Own the Android link lifecycle and join both worker threads."""

    def __init__(self, android_link: AndroidLink) -> None:
        """Create the queues, workers, stop signal, and lifecycle owner."""
        self.android_link = android_link
        self.event_queue = EventQueue()
        self.android_tx_queue = AndroidTXQueue()
        self.stop_event = StopEvent()
        self.rx_worker = AndroidRXWorker(
            android_link, self.event_queue, self.stop_event
        )
        self.tx_worker = AndroidTXWorker(
            android_link,
            self.android_tx_queue,
            self.event_queue,
            self.stop_event,
        )
        self._threads: list[Thread] = []
        self._closed = False

    def start(self) -> None:
        """Start the RX and TX threads."""
        if self._threads:
            raise RuntimeError("Android runtime has already started")
        self._threads = [
            Thread(target=self.rx_worker.run, name="android-rx"),
            Thread(target=self.tx_worker.run, name="android-tx"),
        ]
        for thread in self._threads:
            thread.start()

    def shutdown(self, timeout: float | None = None) -> None:
        """Signal workers, close the link once, and join every started thread."""
        if self._closed:
            return
        self._closed = True
        self.stop_event.set()
        # Closing the transport unblocks a blocking recv; workers themselves do
        # not own or close the link.
        self.android_link.close()
        for thread in self._threads:
            thread.join(timeout=timeout)
