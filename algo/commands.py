"""Geometry to STM command strings, and back again.

The planner produces arcs and straight lines; the STM board wants short ASCII
instructions. This module is the translation, in both directions -- `parse()`
exists so tests can drive a trajectory through `to_commands()` and back and
confirm nothing was lost, which is the only way to catch an off-by-one in the
field widths before the robot drives into a wall.

The grammar itself is NOT specified anywhere upstream. Field widths, whether
turns are expressed as angles or arc lengths, whether "L" means the steering
goes left or the nose swings left -- all of that is a per-team convention to
agree with whoever owns the STM board. It lives in `config.py` so that agreeing
on something different is a one-line change here, not a rewrite.

Default grammar (matching the reference repo):

    SF100   drive straight forward 100cm
    SB050   drive straight backward 50cm
    LF090   forward, steering left, through 90 degrees
    RF090   forward, steering right, through 90 degrees
    LB090   reverse, steering left, through 90 degrees
    RB090   reverse, steering right, through 90 degrees
    SNAP3   photograph obstacle 3
    FIN     path complete
"""

import math
import re
from typing import Iterable, List, Optional, Sequence, Tuple

import config as cfg
from motion import (
    BACKWARD,
    FORWARD,
    LEFT,
    RIGHT,
    STRAIGHT,
    Pose,
    Segment,
    Trajectory,
    merge_segments,
)

_TURN_PREFIXES = {
    (FORWARD, LEFT): cfg.CMD_LEFT_FORWARD,
    (FORWARD, RIGHT): cfg.CMD_RIGHT_FORWARD,
    (BACKWARD, LEFT): cfg.CMD_LEFT_BACKWARD,
    (BACKWARD, RIGHT): cfg.CMD_RIGHT_BACKWARD,
}
_STRAIGHT_PREFIXES = {
    FORWARD: cfg.CMD_STRAIGHT_FORWARD,
    BACKWARD: cfg.CMD_STRAIGHT_BACKWARD,
}

# Longest prefix first, so "SNAP" is not mistaken for a straight-line command.
_PREFIXES = sorted(
    set(_TURN_PREFIXES.values()) | set(_STRAIGHT_PREFIXES.values()) | {cfg.CMD_SNAP},
    key=len, reverse=True,
)
_PATTERN = re.compile(r"^(%s)(\d+)$" % "|".join(re.escape(p) for p in _PREFIXES))


def _field(value: float) -> str:
    """Format a magnitude into the agreed zero-padded field.

    Clamped rather than truncated: a value too wide for the field would
    otherwise silently wrap into a much smaller number, and the robot would
    drive 5cm where 105cm was intended.
    """
    rounded = int(round(value))
    ceiling = 10 ** cfg.COMMAND_NUM_WIDTH - 1
    return str(min(max(rounded, 0), ceiling)).zfill(cfg.COMMAND_NUM_WIDTH)


def _split(total: float, limit: float) -> List[float]:
    """Break a magnitude into chunks no larger than `limit` (0 disables splitting)."""
    if limit <= 0 or total <= limit:
        return [total]
    chunks = []
    remaining = total
    while remaining > limit:
        chunks.append(limit)
        remaining -= limit
    if remaining > 0:
        chunks.append(remaining)
    return chunks


def segment_to_commands(segment: Segment) -> List[str]:
    """One segment -> the commands that drive it.

    Usually one command. It becomes several when the sweep is longer than the
    STM firmware will accept in a single instruction -- two arcs around the same
    circle merge into one segment, and that can legitimately be a 300-degree
    turn.
    """
    if segment.steering == STRAIGHT:
        if segment.length < cfg.MIN_COMMAND_DISTANCE:
            return []
        prefix = _STRAIGHT_PREFIXES[segment.gear]
        return [prefix + _field(part)
                for part in _split(segment.length, cfg.MAX_STRAIGHT_COMMAND_CM)]

    angle = abs(segment.turn_angle)
    if angle < cfg.MIN_COMMAND_ANGLE:
        return []
    degrees = math.degrees(angle)
    if cfg.SNAP_TO_90_TURNS:
        # Some STM firmwares only implement quarter turns. This throws away real
        # path accuracy, so leave SNAP_TO_90_TURNS off unless the firmware
        # genuinely requires it.
        degrees = round(degrees / 90.0) * 90.0
        if degrees < 1.0:
            return []
    prefix = _TURN_PREFIXES[(segment.gear, segment.steering)]
    return [prefix + _field(part) for part in _split(degrees, cfg.MAX_TURN_COMMAND_DEG)]


def segment_to_command(segment: Segment) -> Optional[str]:
    """Convenience wrapper for the common single-command case."""
    produced = segment_to_commands(segment)
    return produced[0] if len(produced) == 1 else None


def trajectory_to_commands(trajectory: Trajectory) -> List[str]:
    """Every command needed to drive one leg.

    Segments are merged first: Hybrid A* emits one segment per 5cm primitive,
    and sending forty `SF005`s instead of one `SF200` means forty accelerate-
    and-stop cycles, which is both far slower and far less accurate on the real
    chassis than a single continuous run.
    """
    commands: List[str] = []
    for segment in merge_segments(trajectory.segments):
        commands.extend(segment_to_commands(segment))
    return commands


def route_to_commands(route, snap: bool = True, finish: bool = True) -> List[str]:
    """The full command list for a planned route.

    A `SNAP<id>` goes in after each leg so the RPi knows exactly when the robot
    is parked and pointing at obstacle <id>, and `FIN` marks the end of the run.
    """
    commands: List[str] = []
    for leg in route.legs:
        commands.extend(trajectory_to_commands(leg.trajectory))
        if snap:
            commands.append("%s%d" % (cfg.CMD_SNAP, leg.obstacle_id))
    if finish:
        commands.append(cfg.CMD_FINISH)
    return commands


# --------------------------------------------------------------------------
# The other direction -- used by the round-trip tests
# --------------------------------------------------------------------------


def parse(command: str) -> Tuple[str, Optional[float]]:
    """Split a command into (kind, magnitude).

    `kind` is one of the configured prefixes or "FIN"; magnitude is cm for a
    straight, degrees for a turn, the obstacle id for a SNAP, and None for FIN.
    """
    command = command.strip().upper()
    if command == cfg.CMD_FINISH:
        return (cfg.CMD_FINISH, None)
    match = _PATTERN.match(command)
    if not match:
        raise ValueError("unrecognised command %r" % (command,))
    return (match.group(1), float(match.group(2)))


def commands_to_trajectory(commands: Iterable[str], start: Pose,
                           radius: float = cfg.TURNING_RADIUS) -> Trajectory:
    """Replay a command list into the trajectory it describes.

    The inverse of `trajectory_to_commands`, so a test can drive a planned path
    out to strings and back and check the robot ends up in the same place. That
    round trip is what catches a bad field width or a swapped L/R before it
    becomes a crash on the arena.
    """
    segments: List[Segment] = []
    pose = start
    for command in commands:
        kind, value = parse(command)
        if kind in (cfg.CMD_FINISH, cfg.CMD_SNAP) or value is None:
            continue

        if kind in _STRAIGHT_PREFIXES.values():
            gear = FORWARD if kind == cfg.CMD_STRAIGHT_FORWARD else BACKWARD
            segment = Segment(gear, STRAIGHT, value, radius, pose)
        else:
            gear, steering = next(key for key, prefix in _TURN_PREFIXES.items()
                                  if prefix == kind)
            # Commands carry the swept angle; the segment wants the arc length.
            segment = Segment(gear, steering, math.radians(value) * radius, radius, pose)

        segments.append(segment)
        pose = segment.end
    return Trajectory(segments)


def describe(commands: Sequence[str]) -> str:
    """Human-readable rendering, for logs and the simulator's command panel."""
    parts = []
    for command in commands:
        kind, value = parse(command)
        if kind == cfg.CMD_FINISH:
            parts.append("finish")
        elif kind == cfg.CMD_SNAP:
            parts.append("photograph obstacle %d" % int(value))
        elif kind in _STRAIGHT_PREFIXES.values():
            direction = "forward" if kind == cfg.CMD_STRAIGHT_FORWARD else "backward"
            parts.append("%s %.0fcm" % (direction, value))
        else:
            gear, steering = next(key for key, prefix in _TURN_PREFIXES.items()
                                  if prefix == kind)
            parts.append("%s %s %.0f deg" % (
                "forward" if gear == FORWARD else "reverse",
                "left" if steering == LEFT else "right", value))
    return ", ".join(parts)
