"""Raw Android transport adapter with one-message-per-send framing."""

from __future__ import annotations
from typing import Protocol
from mdp_rpi.protocols.android import (
    AndroidMessage,
    AndroidProtocolError,
    AndroidStreamParser,
)


class AndroidConnectionClosed(ConnectionError):
    """Raised when the peer closes the Android transport."""

class ByteStream(Protocol):
    """Minimal socket-like interface required by :class:`AndroidLink`."""

    def recv(self, size: int) -> bytes:
        ...

    def sendall(self, data: bytes) -> None:
        ...

    def close(self) -> None:
        ...

class AndroidLink:
    """Own a byte stream and delegate Android framing to the protocol layer."""

    def __init__(
        self,
        connection: ByteStream,
        *,
        receive_size: int = 4096,
    ) -> None:
        """Create a link around a socket-like connection."""
        self._connection = connection
        self._receive_size = receive_size
        self._parser = AndroidStreamParser()
        self._closed = False

    def send(self, message: AndroidMessage) -> None:
        """Serialize one message and send exactly one newline-terminated frame."""
        if self._closed:
            raise RuntimeError("Android link is closed")
        self._connection.sendall(message.to_json_line())

    def receive_once(self) -> list[AndroidMessage]:
        """Read once and return every complete message found in the bytes."""
        if self._closed:
            raise RuntimeError("Android link is closed")
        data = self._connection.recv(self._receive_size)
        if data == b"":
            if self.pending_bytes:
                raise AndroidProtocolError(
                    "Android connection closed with an incomplete message"
                )
            raise AndroidConnectionClosed("Android connection closed by peer")
        return self._parser.feed(data)

    def close(self) -> None:
        """Close the underlying connection once."""
        if not self._closed:
            self._closed = True
            self._connection.close()

    @property
    def pending_bytes(self) -> bytes:
        """Return protocol bytes buffered from an incomplete frame."""
        return self._parser.pending_bytes()
