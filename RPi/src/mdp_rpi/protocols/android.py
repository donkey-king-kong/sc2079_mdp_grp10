"""Android JSON envelope and incremental newline/JSON stream parser."""

from __future__ import annotations
from dataclasses import dataclass
import json
from typing import Any

class AndroidProtocolError(ValueError):
    """Raised when Android Message is invalid."""



@dataclass(frozen=True)
class AndroidMessage:
    """Validated Android message containing a category and arbitrary JSON value."""
    cat: str
    value: Any

    def to_json(self) -> str:
        """Return the compact JSON envelope without its line terminator."""
        try:
            return json.dumps(
                {"cat": self.cat, "value": self.value},
                ensure_ascii=False,
                allow_nan=False,
                separators=(",", ":"),
            )
        except (TypeError, ValueError) as exc:
            raise AndroidProtocolError("Android message is not JSON-serializable") from exc

    def to_json_line(self) -> bytes:
        """Return the UTF-8 JSON envelope terminated by exactly one newline."""
        return (self.to_json() + "\n").encode("utf-8")

    @classmethod
    def from_json(cls, raw: str) -> "AndroidMessage":
        """Decode and validate one JSON envelope."""
        try:
            payload = json.loads(raw)
        except (TypeError, json.JSONDecodeError) as exc:
            raise AndroidProtocolError("Invalid Android JSON message") from exc

        if not isinstance(payload, dict) or set(payload) != {"cat", "value"}:
            raise AndroidProtocolError("Android message must contain exactly 'cat' and 'value'")
        if not isinstance(payload["cat"], str):
            raise AndroidProtocolError("Android message category must be a string")
        return cls(cat=payload["cat"], value=payload["value"])



class AndroidStreamParser:
    """
    Parses a byte stream

    handles:
    - one JSON message split across multiple reads
    - multiple JSON messages in one read
    - newline delimited JSON
    - concatenated JSON objects without newlines.
    """
    def __init__(self) -> None:
        """Create an empty parser retaining bytes between socket reads."""
        self._buffer = bytearray()
        self._decoder = json.JSONDecoder()

    def feed(self, data: bytes) -> list[AndroidMessage]:
        """Parse complete messages from a byte chunk and retain incomplete data."""
        if not isinstance(data, bytes):
            raise TypeError("Android stream data must be bytes")
        self._buffer.extend(data)

        messages: list[AndroidMessage] = []
        while self._buffer:
            # UTF-8 decoding is deliberately attempted on the complete buffer:
            # a split multi-byte character should remain buffered until the next
            # read completes it.
            try:
                text = self._buffer.decode("utf-8")
            except UnicodeDecodeError as exc:
                if exc.reason == "unexpected end of data":
                    break
                raise AndroidProtocolError("Android stream is not valid UTF-8") from exc

            text = text.lstrip()
            if not text:
                self._buffer.clear()
                break

            try:
                payload, end = self._decoder.raw_decode(text)
            except json.JSONDecodeError as exc:
                # A complete newline-delimited record is malformed, so surface
                # the error. Otherwise an error at the end can simply mean that
                # the JSON object was split across reads.
                incomplete = (
                    exc.pos >= len(text.rstrip())
                    or exc.msg.startswith("Unterminated")
                )
                if "\n" in text or not incomplete:
                    leading = len(self._buffer) - len(self._buffer.lstrip())
                    if "\n" in text:
                        line_end = text.index("\n") + 1
                        del self._buffer[
                            : leading + len(text[:line_end].encode("utf-8"))
                        ]
                    else:
                        self._buffer.clear()
                    raise AndroidProtocolError("Invalid Android JSON message") from exc
                break

            raw_message = text[:end]
            consumed = len(text[:end].encode("utf-8"))
            leading = len(self._buffer) - len(self._buffer.lstrip())
            del self._buffer[: leading + consumed]
            messages.append(AndroidMessage.from_json(raw_message))

        return messages

    def pending_bytes(self) -> bytes:
        """Return the currently buffered, incomplete input bytes."""
        return bytes(self._buffer)
