"""Geometry primitives shared by the Dubins planner and Hybrid A*.

A *trajectory* in this package is a list of `Segment`s. A segment is either a
straight run or a constant-radius arc, always driven at the robot's minimum
turning radius. Keeping trajectories as segments (rather than as a soup of
sampled points) is what lets `commands.py` emit a handful of STM instructions
instead of hundreds of tiny ones.
"""

import math
from dataclasses import dataclass, field
from typing import Iterator, List, Optional, Tuple

import config as cfg

TWO_PI = 2.0 * math.pi

# Below this many radians an arc sweep is treated as no rotation at all. It has
# to be this loose rather than machine epsilon because the CSC construction
# feeds acos() a value right at 1, where acos has infinite slope: a rounding
# error of 1e-16 in the input comes out as 1e-8 in the angle. At a 25cm radius
# 1e-6 rad is 25 microns of arc, so this can never hide a turn that matters.
_SWEEP_EPSILON = 1e-6

# Steering / gear encoding. These integers are used as multipliers in the
# kinematics, so the values matter: LEFT = +1 means "theta increases".
LEFT, STRAIGHT, RIGHT = 1, 0, -1
FORWARD, BACKWARD = 1, -1


def normalise_angle(theta: float) -> float:
    """Wrap an angle into (-pi, pi]."""
    theta = math.fmod(theta, TWO_PI)
    if theta <= -math.pi:
        theta += TWO_PI
    elif theta > math.pi:
        theta -= TWO_PI
    return theta


def heading_to_face(theta: float) -> str:
    """Nearest compass letter for a heading, for the Android/RPi protocol."""
    theta = normalise_angle(theta)
    if -math.pi / 4 < theta <= math.pi / 4:
        return "E"
    if math.pi / 4 < theta <= 3 * math.pi / 4:
        return "N"
    if -3 * math.pi / 4 < theta <= -math.pi / 4:
        return "S"
    return "W"


def face_to_heading(face: str) -> float:
    """Compass letter -> radians, East = 0 (briefing slide 7)."""
    try:
        return {"E": 0.0, "N": math.pi / 2, "W": math.pi, "S": -math.pi / 2}[face.upper()[0]]
    except (KeyError, IndexError):
        raise ValueError("face must be one of N/S/E/W, got %r" % (face,))


@dataclass(frozen=True)
class Pose:
    """Robot centre position plus heading."""

    x: float
    y: float
    theta: float

    def as_tuple(self) -> Tuple[float, float, float]:
        return (self.x, self.y, self.theta)

    def normalised(self) -> "Pose":
        return Pose(self.x, self.y, normalise_angle(self.theta))


def turn_centre(pose: Pose, radius: float, steering: int) -> Tuple[float, float]:
    """Centre of the circle the robot rolls around for a left/right turn.

    The centre is one radius to the robot's left (steering = LEFT) or right
    (steering = RIGHT), i.e. 90 degrees off the heading.
    """
    return (
        pose.x - steering * radius * math.sin(pose.theta),
        pose.y + steering * radius * math.cos(pose.theta),
    )


@dataclass
class Segment:
    """One straight run or one constant-radius arc.

    Attributes:
        gear:     FORWARD or BACKWARD.
        steering: LEFT, STRAIGHT or RIGHT.
        length:   arc length travelled, in cm, always >= 0.
        radius:   turning radius; ignored when steering is STRAIGHT.
        start:    pose at the beginning of the segment.
    """

    gear: int
    steering: int
    length: float
    radius: float
    start: Pose

    @property
    def turn_angle(self) -> float:
        """Signed heading change over the segment, radians (+ve = leftwards)."""
        if self.steering == STRAIGHT or self.radius <= 0.0:
            return 0.0
        # Reversing around a left-hand circle swings the nose to the right.
        return self.steering * self.gear * (self.length / self.radius)

    @property
    def end(self) -> Pose:
        return self.pose_at(self.length)

    def pose_at(self, distance: float) -> Pose:
        """Pose after driving `distance` cm along this segment."""
        distance = max(0.0, min(distance, self.length))
        if self.steering == STRAIGHT or self.radius <= 0.0:
            travel = self.gear * distance
            return Pose(
                self.start.x + travel * math.cos(self.start.theta),
                self.start.y + travel * math.sin(self.start.theta),
                self.start.theta,
            )

        cx, cy = turn_centre(self.start, self.radius, self.steering)
        # Angle of the robot as seen from the circle centre, and how far around
        # that circle it has swept.
        phi0 = math.atan2(self.start.y - cy, self.start.x - cx)
        swept = self.steering * self.gear * (distance / self.radius)
        phi = phi0 + swept
        return Pose(
            cx + self.radius * math.cos(phi),
            cy + self.radius * math.sin(phi),
            normalise_angle(self.start.theta + swept),
        )

    def iter_sample(self, step: float) -> Iterator[Pose]:
        """Poses along the segment every `step` cm, excluding the start pose.

        A generator rather than a list because collision checking is the hot
        loop of the whole planner and most blocked paths collide early -- the
        caller's `all()` short-circuits instead of sampling the full arc.
        """
        if self.length <= 1e-9:
            return
        n = max(1, int(math.ceil(self.length / max(step, 1e-6))))
        for i in range(1, n + 1):
            yield self.pose_at(self.length * i / n)

    def sample(self, step: float) -> List[Pose]:
        return list(self.iter_sample(step))

    def duration(self) -> float:
        """Seconds this segment takes, per the time model in config.py."""
        speed = cfg.SPEED_STRAIGHT if self.steering == STRAIGHT else cfg.SPEED_TURN
        return self.length / speed


@dataclass
class Trajectory:
    """An ordered run of segments from one pose to another."""

    segments: List[Segment] = field(default_factory=list)

    @property
    def length(self) -> float:
        return sum(s.length for s in self.segments)

    def start_pose(self) -> Pose:
        return self.segments[0].start if self.segments else Pose(0.0, 0.0, 0.0)

    def end_pose(self) -> Pose:
        return self.segments[-1].end if self.segments else self.start_pose()

    def duration(self) -> float:
        """Seconds to drive the whole trajectory, including switching costs.

        This is the cost B.3 minimises. Distance alone would happily choose a
        path made of six alternating micro-turns over a slightly longer path
        made of one straight, which on real hardware is much slower.
        """
        total = 0.0
        prev_gear = None
        prev_steering = None
        for seg in self.segments:
            if seg.length <= 1e-9:
                continue
            total += seg.duration()
            if prev_gear is not None and seg.gear != prev_gear:
                total += cfg.DIRECTION_CHANGE_TIME
            if prev_steering is not None and seg.steering != prev_steering:
                total += cfg.STEERING_CHANGE_TIME
            prev_gear, prev_steering = seg.gear, seg.steering
        return total

    def iter_sample(self, step: float = cfg.COLLISION_SAMPLE_STEP) -> Iterator[Pose]:
        """Every pose along the trajectory, starting with the start pose."""
        if not self.segments:
            return
        yield self.segments[0].start
        for seg in self.segments:
            for pose in seg.iter_sample(step):
                yield pose

    def sample(self, step: float = cfg.COLLISION_SAMPLE_STEP) -> List[Pose]:
        return list(self.iter_sample(step))

    def sample_with_time(self, step: float = cfg.COLLISION_SAMPLE_STEP,
                         start_time: float = 0.0) -> List[Tuple[Pose, float]]:
        """Poses along the trajectory, each with the clock reading when it happens.

        Same model as `duration()`, so the simulator's clock and the planner's
        reported total are the same number by construction. Deriving the time
        from sampled positions instead looks equivalent and is not: it silently
        drops the gear- and steering-change penalties, and the animation then
        finishes several seconds before the figure the plan is judged on.
        """
        if not self.segments:
            return []
        clock = start_time
        result: List[Tuple[Pose, float]] = [(self.segments[0].start, clock)]
        prev_gear: Optional[int] = None
        prev_steering: Optional[int] = None

        for seg in self.segments:
            if seg.length <= 1e-9:
                continue
            if prev_gear is not None and seg.gear != prev_gear:
                clock += cfg.DIRECTION_CHANGE_TIME
            if prev_steering is not None and seg.steering != prev_steering:
                clock += cfg.STEERING_CHANGE_TIME
            prev_gear, prev_steering = seg.gear, seg.steering

            speed = cfg.SPEED_STRAIGHT if seg.steering == STRAIGHT else cfg.SPEED_TURN
            n = max(1, int(math.ceil(seg.length / max(step, 1e-6))))
            for i in range(1, n + 1):
                travelled = seg.length * i / n
                result.append((seg.pose_at(travelled), clock + travelled / speed))
            clock += seg.length / speed
        return result

    def extend(self, other: "Trajectory") -> "Trajectory":
        return Trajectory(list(self.segments) + list(other.segments))


def arc_sweep(centre: Tuple[float, float], start: Pose, target_x: float,
              target_y: float, steering: int) -> float:
    """Signed angle swept going from `start` round `centre` to the target point.

    Briefing slide 33: take the angle between the two radius vectors with
    atan2, then push it into the correct half-turn for the direction of travel
    (a left turn always sweeps positive, a right turn always sweeps negative).
    """
    cx, cy = centre
    v1x, v1y = start.x - cx, start.y - cy
    v2x, v2y = target_x - cx, target_y - cy
    delta = math.atan2(v2y, v2x) - math.atan2(v1y, v1x)
    # A sweep that is zero to within floating-point noise must stay zero, and
    # must not come out as a full turn either. Without these guards a delta of
    # +1e-8 on a right turn is "positive", gets a full turn subtracted, and a
    # segment that should not move at all becomes a 157cm loop around the
    # circle -- which then poisons every cost that depends on it.
    if abs(delta) < _SWEEP_EPSILON:
        return 0.0
    if delta < 0 and steering == LEFT:
        delta += TWO_PI
    elif delta > 0 and steering == RIGHT:
        delta -= TWO_PI
    if abs(abs(delta) - TWO_PI) < _SWEEP_EPSILON:
        return 0.0
    return delta


def merge_segments(segments: List[Segment]) -> List[Segment]:
    """Tidy a trajectory into the fewest segments that drive the same path.

    Two things happen here.

    **Fusing.** Hybrid A* emits one segment per 5cm motion primitive, so a
    single straight run arrives as a dozen fragments. Left alone that becomes a
    dozen STM commands, each with its own acceleration and stop -- slow on the
    real robot and wildly inaccurate. Merging first means one `SF060` instead.

    **Cancelling.** Where two legs are stitched together, one can end with a
    short forward run into a pose and the next begin by reversing out of it,
    giving `SF005 SB030`: the robot creeps forward 5cm purely to give itself
    something to back out of. Since both runs are along the same heading, the
    pair is exactly one `SB025`, and every intermediate position is one the
    original pair already visited -- so if they were collision-free, so is the
    result.
    """
    merged: List[Segment] = []
    for seg in segments:
        if seg.length <= 1e-9:
            continue

        # Fold this segment into the tail of the list for as long as it will go:
        # a cancellation can expose a fuse behind it, and vice versa.
        while merged:
            prev = merged[-1]
            same_shape = (prev.steering == seg.steering
                          and abs(prev.radius - seg.radius) < 1e-9)
            if same_shape and prev.gear == seg.gear:
                seg = Segment(prev.gear, prev.steering, prev.length + seg.length,
                              prev.radius, prev.start)
                merged.pop()
                continue
            if prev.steering == STRAIGHT and seg.steering == STRAIGHT:
                # Opposing runs along one heading: keep the net motion.
                net = prev.length - seg.length
                merged.pop()
                if abs(net) <= 1e-9:
                    seg = None
                    break
                gear = prev.gear if net > 0 else seg.gear
                seg = Segment(gear, STRAIGHT, abs(net), prev.radius, prev.start)
                continue
            break

        if seg is not None and seg.length > 1e-9:
            merged.append(seg)

    if segments and not merged:
        # Everything cancelled out: the robot ends where it began. Keep one
        # zero-length segment so the trajectory still knows its own pose rather
        # than becoming an empty list that reports (0, 0, 0).
        first = segments[0]
        merged.append(Segment(FORWARD, STRAIGHT, 0.0, first.radius, first.start))
    return merged
