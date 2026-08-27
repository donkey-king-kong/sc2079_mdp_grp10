"""Shared event types used by RPi workers and the central orchestrator."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import Any


@dataclass(frozen=True)
class Event:
    """Generic event carrying a kind identifier and optional payload."""

    kind: str
    data: Any = None


class AndroidEventType(str, Enum):
    """Application-level actions and updates originating from Android."""

    BEGIN_EXPLORE = "BEGIN_EXPLORE"
    BEGIN_FASTEST = "BEGIN_FASTEST"
    SEND_ARENA = "SEND_ARENA"
    STITCH_IMAGES = "STITCH_IMAGES"
    STOP = "STOP"
    STM_COMMAND = "STM_COMMAND"
    STATUS = "STATUS"
    LOCATION = "LOCATION"
    HEALTH = "HEALTH"
    IMAGE_RECOGNITION = "IMAGE_RECOGNITION"


@dataclass(frozen=True)
class AndroidEvent:
    """A decoded Android message represented as an application event."""

    event_type: AndroidEventType
    value: Any = None


ANDROID_CONNECTED = "android.connected"
ANDROID_DISCONNECTED = "android.disconnected"
ANDROID_MESSAGE = "android.message"
ANDROID_PROTOCOL_ERROR = "android.protocol_error"
ANDROID_TRANSPORT_ERROR = "android.transport_error"
STM_ACK = "stm.ack"
STM_ERROR = "stm.error"
PLANNER_READY = "planner.ready"
PLANNER_ERROR = "planner.error"
VISION_READY = "vision.ready"
VISION_ERROR = "vision.error"
