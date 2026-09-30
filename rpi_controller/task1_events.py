"""Queue messages shared by the Task 1 kernel and I/O workers."""

from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Mapping


class EventType(str, Enum):
    ALGO_ROUTE_RECEIVED = "ALGO_ROUTE_RECEIVED"
    ALGO_ERROR = "ALGO_ERROR"
    STM_ZH_COMPLETE = "STM_ZH_COMPLETE"
    STM_MOVEMENT_COMPLETE = "STM_MOVEMENT_COMPLETE"
    STM_ABORT = "STM_ABORT"
    STM_TIMEOUT = "STM_TIMEOUT"
    STM_ERROR = "STM_ERROR"
    STM_UNKNOWN_RESPONSE = "STM_UNKNOWN_RESPONSE"
    CV_RESULT_ACCEPTED = "CV_RESULT_ACCEPTED"
    CV_RETRY_REQUIRED = "CV_RETRY_REQUIRED"
    CV_CONTINUOUS_RESULT = "CV_CONTINUOUS_RESULT"
    CV_REPOSITION_REQUIRED = "CV_REPOSITION_REQUIRED"
    CV_ERROR = "CV_ERROR"
    IMAGE_TRANSFER_ERROR = "IMAGE_TRANSFER_ERROR"
    IMAGE_TRANSFER_QUEUE_FULL = "IMAGE_TRANSFER_QUEUE_FULL"
    SHUTDOWN = "SHUTDOWN"


@dataclass(frozen=True)
class Event:
    type: EventType
    payload: Mapping[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class AlgoRequest:
    robot: Mapping[str, Any]
    obstacles: list[Mapping[str, Any]]
    strategy: str = "exhaustive"
    metric: str = "time"


@dataclass(frozen=True)
class STMCommand:
    command: str


@dataclass(frozen=True)
class CameraRequest:
    obstacle_id: str


@dataclass(frozen=True)
class ImageTransferRequest:
    image_path: str
    metadata_path: str
