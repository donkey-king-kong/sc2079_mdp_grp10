"""Thread-safe queue boundaries between RPi workers and orchestration."""

from __future__ import annotations

from queue import Queue

from mdp_rpi.domain.events import AndroidEvent, Event
from mdp_rpi.protocols.android import AndroidMessage


class EventQueue(Queue[Event | AndroidEvent]):
    """Events published by workers and consumed by the orchestrator."""


class AndroidTXQueue(Queue[AndroidMessage]):
    """Outbound Android messages produced by the orchestrator."""
