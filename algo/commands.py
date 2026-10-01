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
    RadiusSpec,
    Segment,
    Trajectory,
    merge_segments,
    normalise_angle,
    turning_radii,
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


class HeadingTracker:
    """Rounds turns to whole degrees without letting the heading drift.

    The STM only takes whole degrees, and it keeps an ABSOLUTE heading: every
    turn moves its target heading by exactly the number it is sent. Rounding
    each turn on its own lets the up-to-half-degree errors add up over a run of
    thirty commands. Rounding the running total instead -- send whatever whole
    number brings the target closest to the planned heading so far -- keeps the
    target within half a degree of the plan after every single command. A turn
    too small to send is not lost either: it carries into the next one.
    """

    def __init__(self) -> None:
        self.planned = 0.0      # degrees of heading change the plan has asked for
        self.sent = 0           # degrees actually sent, as signed whole numbers

    def take(self, planned: float) -> int:
        """Signed whole degrees to send for a turn of `planned` degrees (0: skip it)."""
        self.planned += planned
        if abs(planned) < math.degrees(cfg.MIN_COMMAND_ANGLE):
            return 0
        owed = self.planned - self.sent
        whole = int(math.floor(abs(owed) + 0.5)) * (1 if owed >= 0 else -1)
        # A turn can only be sent in its own direction; if the carried error
        # outweighs it, send nothing now and let the next turn settle up.
        if whole == 0 or (whole > 0) != (planned > 0):
            return 0
        self.sent += whole
        return whole


def segment_to_commands(segment: Segment,
                        heading: Optional[HeadingTracker] = None) -> List[str]:
    """One segment -> the commands that drive it.

    Usually one command. It becomes several when the sweep is longer than the
    STM firmware will accept in a single instruction -- two arcs around the same
    circle merge into one segment, and that can legitimately be a 300-degree
    turn.

    Pass the route's `HeadingTracker` to round turns against the running
    heading rather than on their own; without one, each turn is rounded alone.
    """
    if segment.steering == STRAIGHT:
        if segment.length < cfg.MIN_COMMAND_DISTANCE:
            return []
        prefix = _STRAIGHT_PREFIXES[segment.gear]
        return [prefix + _field(part)
                for part in _split(segment.length, cfg.MAX_STRAIGHT_COMMAND_CM)]

    prefix = _TURN_PREFIXES[(segment.gear, segment.steering)]
    if heading is not None and not cfg.SNAP_TO_90_TURNS:
        whole = heading.take(math.degrees(segment.turn_angle))
        if whole == 0:
            return []
        return [prefix + _field(part) for part in _split(abs(whole), cfg.MAX_TURN_COMMAND_DEG)]

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
    return [prefix + _field(part) for part in _split(degrees, cfg.MAX_TURN_COMMAND_DEG)]


def segment_to_command(segment: Segment) -> Optional[str]:
    """Convenience wrapper for the common single-command case."""
    produced = segment_to_commands(segment)
    return produced[0] if len(produced) == 1 else None


def trajectory_to_commands(trajectory: Trajectory,
                           heading: Optional[HeadingTracker] = None) -> List[str]:
    """Every command needed to drive one leg.

    Segments are merged first: Hybrid A* emits one segment per 5cm primitive,
    and sending forty `SF005`s instead of one `SF200` means forty accelerate-
    and-stop cycles, which is both far slower and far less accurate on the real
    chassis than a single continuous run.

    Turns are rounded against `heading`, a fresh tracker if none is given.
    """
    heading = heading if heading is not None else HeadingTracker()
    commands: List[str] = []
    for segment in merge_segments(trajectory.segments):
        commands.extend(segment_to_commands(segment, heading))
    return commands


def leg_commands(trajectory: Trajectory, heading: HeadingTracker,
                 origin_theta: float) -> List[str]:
    """The next leg's commands, given everything already sent on `heading`.

    `origin_theta` is the heading the run started at, so the STM's target
    heading right now is that plus what has been sent. The planner starts each
    leg from where the commands really leave the robot (see
    `planner.anchor_legs`), which can differ from where the plan had it by a
    fraction of a degree. The tracker is lined up with the leg's own starting
    heading first, so the leg's turns are rounded from where the robot really
    points. When legs join up exactly this changes nothing.
    """
    target = origin_theta + math.radians(heading.sent)
    heading.planned = heading.sent + math.degrees(
        normalise_angle(trajectory.start_pose().theta - target))
    return trajectory_to_commands(trajectory, heading)


def route_leg_commands(route) -> List[List[str]]:
    """The commands for each leg of a route, in order.

    One `HeadingTracker` runs across the whole route, because the STM's heading
    is absolute for the whole run, not per leg. So a leg's commands depend on
    the legs before it, and this is the only correct way to get them.
    """
    if not route.legs:
        return []
    heading = HeadingTracker()
    origin = route.legs[0].trajectory.start_pose().theta
    return [leg_commands(leg.trajectory, heading, origin) for leg in route.legs]


def route_to_commands(route, snap: bool = True, finish: bool = True) -> List[str]:
    """The full command list for a planned route.

    A `SNAP<id>` goes in after each leg so the RPi knows exactly when the robot
    is parked and pointing at obstacle <id>, and `FIN` marks the end of the run.
    """
    commands: List[str] = []
    for leg, leg_commands in zip(route.legs, route_leg_commands(route)):
        commands.extend(leg_commands)
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
                           radius: RadiusSpec = None) -> Trajectory:
    """Replay a command list into the trajectory it describes.

    The inverse of `trajectory_to_commands`, so a test can drive a planned path
    out to strings and back and check the robot ends up in the same place. That
    round trip is what catches a bad field width or a swapped L/R before it
    becomes a crash on the arena.

    A turn command carries only an angle; the STM drives it at whatever radius
    that steering side physically has. So `LF090` is replayed round the left
    circle and `RF090` round the (wider) right one -- the robot's configured
    radii unless `radius` says otherwise.
    """
    radii = turning_radii(radius)
    segments: List[Segment] = []
    pose = start
    for command in commands:
        kind, value = parse(command)
        if kind in (cfg.CMD_FINISH, cfg.CMD_SNAP) or value is None:
            continue

        if kind in _STRAIGHT_PREFIXES.values():
            gear = FORWARD if kind == cfg.CMD_STRAIGHT_FORWARD else BACKWARD
            segment = Segment(gear, STRAIGHT, value, 0.0, pose)
        else:
            gear, steering = next(key for key, prefix in _TURN_PREFIXES.items()
                                  if prefix == kind)
            # Commands carry the swept angle; the segment wants the arc length.
            side = radii.of(steering)
            segment = Segment(gear, steering, math.radians(value) * side, side, pose)

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
