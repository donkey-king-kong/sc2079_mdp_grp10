"""Geometry primitives shared by the Dubins planner and Hybrid A*.

A *trajectory* in this package is a list of `Segment`s. A segment is either a
straight run or a constant-radius arc, always driven at the robot's minimum
turning radius for that side -- which on our robot is not the same both ways
(see `TurningRadii`). Keeping trajectories as segments (rather than as a soup of
sampled points) is what lets `commands.py` emit a handful of STM instructions
instead of hundreds of tiny ones.
"""

import math
from dataclasses import dataclass, field
from typing import Callable, Iterator, List, Optional, Tuple, Union

import config as cfg

TWO_PI = 2.0 * math.pi

# Below this many radians an arc sweep is treated as no rotation at all. It has
# to be this loose rather than machine epsilon because the CSC construction
# feeds acos() a value right at 1, where acos has infinite slope: a rounding
# error of 1e-16 in the input comes out as 1e-8 in the angle. At a 36cm radius
# 1e-6 rad is 36 microns of arc, so this can never hide a turn that matters.
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
    """Robot turning-centre position plus heading.

    The turning centre is the point the robot rotates about, not the middle of
    its footprint -- see `footprint_centre()` for that.
    """

    x: float
    y: float
    theta: float

    def as_tuple(self) -> Tuple[float, float, float]:
        return (self.x, self.y, self.theta)

    def normalised(self) -> "Pose":
        return Pose(self.x, self.y, normalise_angle(self.theta))


def footprint_centre(pose: Pose) -> Tuple[float, float]:
    """Middle of the robot's body for a pose, which is about its turning centre.

    The body sits `config.TURNING_CENTRE_OFFSET` ahead of the turning centre
    along the heading. Read at call time so a calibration script can change it.
    """
    offset = cfg.TURNING_CENTRE_OFFSET
    return (pose.x + offset * math.cos(pose.theta),
            pose.y + offset * math.sin(pose.theta))


def pose_from_footprint_centre(x: float, y: float, theta: float) -> Pose:
    """The planner pose that puts the middle of the robot's body at (x, y)."""
    offset = cfg.TURNING_CENTRE_OFFSET
    return Pose(x - offset * math.cos(theta), y - offset * math.sin(theta),
                normalise_angle(theta))


@dataclass(frozen=True)
class TurningRadii:
    """The robot's minimum turning radius on each steering side, in cm.

    Our chassis turns tighter to the left (20.2cm) than to the right (36.2cm) --
    see `config.TURNING_RADIUS_LEFT`. Every arc is driven at the radius of its
    *steering* side, so a reverse-left arc is on the left circle too.
    """

    left: float
    right: float

    def of(self, steering: int) -> float:
        """Radius for LEFT or RIGHT steering; 0.0 for STRAIGHT, which has none."""
        if steering == LEFT:
            return self.left
        if steering == RIGHT:
            return self.right
        return 0.0

    @property
    def widest(self) -> float:
        return max(self.left, self.right)


RadiusSpec = Union[None, float, Tuple[float, float], TurningRadii]


def turning_radii(radius: RadiusSpec = None) -> TurningRadii:
    """Normalise whatever the caller passed as a radius into a `TurningRadii`.

    `None` means the robot as calibrated in config.py, read at call time so a
    test or a calibration script can change it. A single number is a symmetric
    robot, which is how the briefing's own worked examples (slide 43) are posed;
    a `(left, right)` pair is an asymmetric one.
    """
    if radius is None:
        return TurningRadii(cfg.TURNING_RADIUS_LEFT, cfg.TURNING_RADIUS_RIGHT)
    if isinstance(radius, TurningRadii):
        return radius
    if isinstance(radius, (int, float)):
        return TurningRadii(float(radius), float(radius))
    left, right = radius
    return TurningRadii(float(left), float(right))


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

        On an arc the step is also capped so the heading turns at most
        `config.COLLISION_SAMPLE_ANGLE` between samples: the body's far corner
        moves much further than the rear axle does, and `config.SWEEP_PAD` is
        computed on the assumption that both limits hold.

        A generator rather than a list because collision checking is the hot
        loop of the whole planner and most blocked paths collide early -- the
        caller's `all()` short-circuits instead of sampling the full arc.
        """
        n, pose_of = self.sampler(step)
        for i in range(1, n + 1):
            yield pose_of(i)

    def sampler(self, step: float) -> Tuple[int, Callable[[int], Pose]]:
        """`(n, pose_of)`: the segment is sampled at `pose_of(1)` .. `pose_of(n)`.

        The sample points `iter_sample` walks through, but addressable in any
        order. The same arithmetic as `pose_at`, with the circle worked out once
        per segment rather than once per sample -- this is the planner's hot loop.
        """
        if self.length <= 1e-9:
            return 0, lambda i: self.start
        step = max(step, 1e-6)
        start = self.start
        if self.steering == STRAIGHT or self.radius <= 0.0:
            n = max(1, int(math.ceil(self.length / step)))
            dx = self.gear * math.cos(start.theta) * self.length / n
            dy = self.gear * math.sin(start.theta) * self.length / n
            return n, lambda i: Pose(start.x + dx * i, start.y + dy * i, start.theta)

        step = min(step, cfg.COLLISION_SAMPLE_ANGLE * self.radius)
        n = max(1, int(math.ceil(self.length / step)))
        cx, cy = turn_centre(start, self.radius, self.steering)
        phi0 = math.atan2(start.y - cy, start.x - cx)
        sweep = self.steering * self.gear * (self.length / self.radius) / n
        radius, cos, sin = self.radius, math.cos, math.sin

        def pose_of(i: int) -> Pose:
            swept = sweep * i
            return Pose(cx + radius * cos(phi0 + swept), cy + radius * sin(phi0 + swept),
                        normalise_angle(start.theta + swept))

        return n, pose_of

    def sample(self, step: float) -> List[Pose]:
        return list(self.iter_sample(step))

    def duration(self) -> float:
        """Seconds this segment takes on the robot, per the time model in config.py.

        A segment goes to the STM as one command -- several if it is longer
        than the firmware's per-command limit, exactly as commands.py splits it
        -- and every command starts and ends at rest: accelerate, cruise,
        decelerate, plus the fixed COMMAND_OVERHEAD. A segment too small to
        become a command at all costs nothing.
        """
        if self.steering == STRAIGHT:
            if self.length < cfg.MIN_COMMAND_DISTANCE:
                return 0.0
            limit = cfg.MAX_STRAIGHT_COMMAND_CM
            profile = (cfg.SPEED_STRAIGHT, cfg.ACCEL_STRAIGHT, cfg.DECEL_STRAIGHT)
        else:
            if self.radius <= 0.0 or self.length / self.radius < cfg.MIN_COMMAND_ANGLE:
                return 0.0
            limit = math.radians(cfg.MAX_TURN_COMMAND_DEG) * self.radius
            profile = (cfg.SPEED_TURN, cfg.ACCEL_TURN, cfg.DECEL_TURN)
        total, remaining = 0.0, self.length
        while limit > 0 and remaining > limit:
            total += move_time(limit, *profile) + cfg.COMMAND_OVERHEAD
            remaining -= limit
        return total + move_time(remaining, *profile) + cfg.COMMAND_OVERHEAD


def move_time(distance: float, cruise: float, accel: float, decel: float) -> float:
    """Seconds for one stop-to-stop move of `distance` cm.

    Trapezoidal speed profile: accelerate to `cruise`, hold it, decelerate. A
    move too short to reach cruise speed is triangular instead.
    """
    if distance <= 0.0:
        return 0.0
    ramps = cruise * cruise / (2.0 * accel) + cruise * cruise / (2.0 * decel)
    if distance >= ramps:
        return cruise / accel + cruise / decel + (distance - ramps) / cruise
    peak = math.sqrt(2.0 * distance * accel * decel / (accel + decel))
    return peak / accel + peak / decel


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
        """Seconds to drive the whole trajectory, one stop-to-stop command at a time.

        This is the cost B.3 minimises. Distance alone would happily choose a
        path made of six alternating micro-turns over a slightly longer path
        made of one straight, which on real hardware is much slower: every
        command pays its own acceleration, deceleration and fixed overhead.
        Segments are merged first, exactly as commands.py does before emitting
        them, so a Hybrid A* run of 5cm steps is costed as the one command the
        robot is actually sent.
        """
        return sum(seg.duration() for seg in merge_segments(self.segments))

    def iter_sample(self, step: float = cfg.COLLISION_SAMPLE_STEP) -> Iterator[Pose]:
        """Every pose along the trajectory, starting with the start pose."""
        if not self.segments:
            return
        yield self.segments[0].start
        for seg in self.segments:
            for pose in seg.iter_sample(step):
                yield pose

    def iter_sample_coarse_first(self, step: float = cfg.COLLISION_SAMPLE_STEP,
                                 stride: int = 4) -> Iterator[Pose]:
        """The same poses as `iter_sample`, every `stride`-th one first.

        For collision checking, where only "is any of them blocked?" matters and
        most candidate paths are blocked somewhere in the middle: the coarse pass
        finds that about `stride` times sooner. A clear path still has every one
        of its poses checked. Poses are computed only as they are reached.
        """
        if not self.segments:
            return
        yield self.segments[0].start
        samplers = [seg.sampler(step) for seg in self.segments]
        for n, pose_of in samplers:
            for i in range(stride, n + 1, stride):
                yield pose_of(i)
        for n, pose_of in samplers:
            for i in range(1, n + 1):
                if i % stride:
                    yield pose_of(i)

    def sample(self, step: float = cfg.COLLISION_SAMPLE_STEP) -> List[Pose]:
        return list(self.iter_sample(step))

    def sample_with_time(self, step: float = cfg.COLLISION_SAMPLE_STEP,
                         start_time: float = 0.0) -> List[Tuple[Pose, float]]:
        """Poses along the trajectory, each with the clock reading when it happens.

        Same model as `duration()`, so the simulator's clock and the planner's
        reported total are the same number by construction. Deriving the time
        from sampled positions instead looks equivalent and is not: it silently
        drops the acceleration and per-command overhead, and the animation then
        finishes well before the figure the plan is judged on. Within one
        command the clock is spread evenly over the distance -- close enough for
        the animation, and exact at every command boundary.
        """
        if not self.segments:
            return []
        clock = start_time
        result: List[Tuple[Pose, float]] = [(self.segments[0].start, clock)]

        for seg in merge_segments(self.segments):
            if seg.length <= 1e-9:
                continue
            seconds = seg.duration()
            n = max(1, int(math.ceil(seg.length / max(step, 1e-6))))
            for i in range(1, n + 1):
                travelled = seg.length * i / n
                result.append((seg.pose_at(travelled), clock + seconds * i / n))
            clock += seconds
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
            # A straight has no radius, so whatever its `radius` field holds must
            # not stop two straights fusing.
            same_shape = (prev.steering == seg.steering
                          and (seg.steering == STRAIGHT
                               or abs(prev.radius - seg.radius) < 1e-9))
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
