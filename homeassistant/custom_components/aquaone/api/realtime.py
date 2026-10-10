"""Immutable models and strict validation for the aquaOne Realtime wire."""

from dataclasses import dataclass
from enum import Enum
import json
import re
from typing import Any

from .errors import AquaOneInvalidResponseError

UINT64_MAX = (1 << 64) - 1
_RUNTIME_ID = re.compile(r"[0-9A-F]{16}\Z")
_SEQUENCE = re.compile(r"[1-9][0-9]{0,19}\Z")


def _object(value: Any, field: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise AquaOneInvalidResponseError(f"Invalid {field}")
    return value


def _required_string(data: dict[str, Any], field: str) -> str:
    value = data.get(field)
    if not isinstance(value, str):
        raise AquaOneInvalidResponseError(f"Invalid {field}")
    return value


def _parse_sequence(value: Any) -> int:
    if not isinstance(value, str) or not _SEQUENCE.fullmatch(value):
        raise AquaOneInvalidResponseError("Invalid sequence")
    sequence = int(value)
    if sequence > UINT64_MAX:
        raise AquaOneInvalidResponseError("Invalid sequence")
    return sequence


@dataclass(frozen=True, slots=True)
class RuntimeIdentity:
    value: str

    def __post_init__(self) -> None:
        if (not isinstance(self.value, str) or
                not _RUNTIME_ID.fullmatch(self.value) or
                self.value == "0000000000000000"):
            raise AquaOneInvalidResponseError("Invalid runtime_id")

    @classmethod
    def from_json(cls, value: Any) -> "RuntimeIdentity":
        if not isinstance(value, str):
            raise AquaOneInvalidResponseError("Invalid runtime_id")
        return cls(value)


@dataclass(frozen=True, slots=True)
class RealtimeStreamPosition:
    kind: str
    sequence: int | None = None

    def __post_init__(self) -> None:
        if self.kind == "before_first":
            if self.sequence is not None:
                raise AquaOneInvalidResponseError("Invalid position")
            return
        if self.kind != "at" or type(self.sequence) is not int or not (
                1 <= self.sequence <= UINT64_MAX):
            raise AquaOneInvalidResponseError("Invalid position")

    @classmethod
    def before_first(cls) -> "RealtimeStreamPosition":
        return cls("before_first")

    @classmethod
    def at(cls, sequence: int) -> "RealtimeStreamPosition":
        return cls("at", sequence)

    @classmethod
    def from_json(cls, value: Any) -> "RealtimeStreamPosition":
        data = _object(value, "position")
        kind = _required_string(data, "kind")
        if kind == "before_first":
            if "sequence" in data:
                raise AquaOneInvalidResponseError("Invalid position")
            return cls.before_first()
        if kind == "at":
            if "sequence" not in data:
                raise AquaOneInvalidResponseError("Invalid position")
            return cls.at(_parse_sequence(data["sequence"]))
        raise AquaOneInvalidResponseError("Invalid position")


@dataclass(frozen=True, slots=True)
class RealtimeStreamStart:
    runtime_id: RuntimeIdentity
    position: RealtimeStreamPosition

    def __post_init__(self) -> None:
        if (not isinstance(self.runtime_id, RuntimeIdentity) or
                not isinstance(self.position, RealtimeStreamPosition)):
            raise AquaOneInvalidResponseError("Invalid stream_start")

    @classmethod
    def from_json(cls, value: Any) -> "RealtimeStreamStart":
        data = _object(value, "stream_start")
        if _required_string(data, "type") != "stream_start":
            raise AquaOneInvalidResponseError("Invalid message type")
        return cls(
            RuntimeIdentity.from_json(data.get("runtime_id")),
            RealtimeStreamPosition.from_json(data.get("position")),
        )


@dataclass(frozen=True, slots=True)
class RealtimeNotification:
    runtime_id: RuntimeIdentity
    sequence: int
    kind: str

    def __post_init__(self) -> None:
        if not isinstance(self.runtime_id, RuntimeIdentity):
            raise AquaOneInvalidResponseError("Invalid runtime_id")
        if type(self.sequence) is not int or not 1 <= self.sequence <= UINT64_MAX:
            raise AquaOneInvalidResponseError("Invalid sequence")
        if self.kind != "luma_status_changed":
            raise AquaOneInvalidResponseError("Invalid notification kind")

    @classmethod
    def from_json(cls, value: Any) -> "RealtimeNotification":
        data = _object(value, "notification")
        if _required_string(data, "type") != "notification":
            raise AquaOneInvalidResponseError("Invalid message type")
        return cls(
            RuntimeIdentity.from_json(data.get("runtime_id")),
            _parse_sequence(data.get("sequence")),
            _required_string(data, "kind"),
        )


@dataclass(frozen=True, slots=True)
class RealtimeStatusMetadata:
    runtime_id: RuntimeIdentity
    position: RealtimeStreamPosition

    def __post_init__(self) -> None:
        if (not isinstance(self.runtime_id, RuntimeIdentity) or
                not isinstance(self.position, RealtimeStreamPosition)):
            raise AquaOneInvalidResponseError("Invalid realtime")

    @classmethod
    def from_json(cls, value: Any) -> "RealtimeStatusMetadata":
        data = _object(value, "realtime")
        return cls(
            RuntimeIdentity.from_json(data.get("runtime_id")),
            RealtimeStreamPosition.from_json(data.get("position")),
        )


RealtimeMessage = RealtimeStreamStart | RealtimeNotification


def _reject_nonfinite(_: str) -> None:
    raise ValueError("Non-finite JSON number")


def parse_realtime_message(message: str) -> RealtimeMessage:
    """Parse one complete WebSocket TEXT message."""
    if not isinstance(message, str):
        raise AquaOneInvalidResponseError("Realtime message must be TEXT")
    try:
        value = json.loads(message, parse_constant=_reject_nonfinite)
    except (TypeError, ValueError) as exc:
        raise AquaOneInvalidResponseError("Malformed Realtime JSON") from exc
    data = _object(value, "Realtime message")
    message_type = _required_string(data, "type")
    if message_type == "stream_start":
        return RealtimeStreamStart.from_json(data)
    if message_type == "notification":
        return RealtimeNotification.from_json(data)
    raise AquaOneInvalidResponseError("Invalid message type")


class PositionComparison(Enum):
    DIFFERENT_RUNTIME = "different_runtime"
    OLDER = "older"
    EQUAL = "equal"
    NEWER = "newer"


class NotificationSequenceState(Enum):
    DIFFERENT_RUNTIME = "different_runtime"
    DUPLICATE_OR_OLD = "duplicate_or_old"
    CONTIGUOUS = "contiguous"
    FORWARD_GAP = "forward_gap"


def compare_realtime_positions(
    left: RealtimeStatusMetadata,
    right: RealtimeStatusMetadata,
) -> PositionComparison:
    """Compare left to right without ordering unrelated runtimes."""
    if left.runtime_id != right.runtime_id:
        return PositionComparison.DIFFERENT_RUNTIME
    left_value = left.position.sequence or 0
    right_value = right.position.sequence or 0
    if left_value < right_value:
        return PositionComparison.OLDER
    if left_value > right_value:
        return PositionComparison.NEWER
    return PositionComparison.EQUAL


def classify_notification(
    current: RealtimeStatusMetadata,
    notification: RealtimeNotification,
) -> NotificationSequenceState:
    """Classify a notification relative to one installed runtime position."""
    if current.runtime_id != notification.runtime_id:
        return NotificationSequenceState.DIFFERENT_RUNTIME
    current_sequence = current.position.sequence or 0
    if notification.sequence <= current_sequence:
        return NotificationSequenceState.DUPLICATE_OR_OLD
    if current_sequence < UINT64_MAX and notification.sequence == current_sequence + 1:
        return NotificationSequenceState.CONTIGUOUS
    return NotificationSequenceState.FORWARD_GAP
