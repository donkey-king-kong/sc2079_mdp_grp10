"""Dubins paths -- the analytic, forward-only shortest path between two poses.

The robot cannot spin on the spot, so a straight line between two poses is not
a path it can follow. Dubins (1957) proved that for a car that only drives
forward at a fixed minimum turning radius, the shortest path between any two
poses is one of six shapes (briefing slide 18):

    CSC:  LSL  RSR  LSR  RSL          CCC:  RLR  LRL

`plan_all()` builds all six candidates, `plan()` returns the shortest one that
survives a collision check. This is tried first for every leg because it is
analytic -- microseconds, no search -- and it is provably optimal when nothing
is in the way. `hybrid_astar.py` only runs when all six are blocked.

The construction follows the briefing directly: slides 27-29 for the CSC
tangent points, slides 30-31 for the CCC third circle, slide 33 for arc length.
Slide 43's worked rsr example is reproduced in tests/test_dubins.py.

**Asymmetric steering.** The briefing assumes one radius for both sides. Ours
turns 20cm left and 36cm right (`config.TURNING_RADIUS_LEFT`), so each circle
here is drawn at the radius of its own steering side. We keep the same six
words. Dubins' optimality proof assumes equal radii, so on this robot "shortest
of the six" is a very good path rather than a proven optimum. That is fine: each
word is still exact, drivable geometry, and anything it cannot reach falls
through to transit poses and Hybrid A* as before. The constructions generalise:

* LSL / RSR: both circles are on the same side, so they share a radius and
  slide 28 applies unchanged.
* LSR / RSL: the inner tangent of two circles of radii r1 and r2 crosses the
  centre line where the circles are r1 + r2 apart, not 2r (slide 29).
* LRL / RLR: the middle circle touches each outer one, so its centre is
  r_outer + r_middle from both, not 2r, and the touching points divide the
  centre lines in the ratio of the radii rather than at the midpoint (slide 30).

With equal radii every one of these reduces exactly to the slide's formula.
"""

import math
from typing import Callable, List, Optional, Tuple

import config as cfg
from motion import (
    FORWARD,
    LEFT,
    RIGHT,
    STRAIGHT,
    Pose,
    RadiusSpec,
    Segment,
    Trajectory,
    TurningRadii,
    arc_sweep,
    normalise_angle,
    turn_centre,
    turning_radii,
)

# Order matters only for tie-breaking; every word is always tried.
WORDS = ("LSL", "RSR", "LSR", "RSL", "RLR", "LRL")

_STEER = {"L": LEFT, "R": RIGHT, "S": STRAIGHT}

# A candidate is accepted only if it actually lands on the goal. These are the
# tolerances for that self-check; they catch tangent-selection sign errors
# rather than absorbing them.
_POS_TOL = 1e-6
_ANGLE_TOL = 1e-6


def _rotate(vx: float, vy: float, angle: float) -> Tuple[float, float]:
    """Rotate a vector counter-clockwise by `angle` (briefing slide 25)."""
    c, s = math.cos(angle), math.sin(angle)
    return vx * c - vy * s, vx * s + vy * c


def _tangent_points_csc(p1: Tuple[float, float], p2: Tuple[float, float],
                        r1: float, r2: float, first: int, last: int):
    """Tangent points of the straight segment of a CSC path.

    `p1`/`p2` are the centres of the start and goal turning circles, `r1`/`r2`
    their radii, `first` and `last` the steering directions of the two arcs.
    """
    v1x, v1y = p2[0] - p1[0], p2[1] - p1[1]
    d = math.hypot(v1x, v1y)
    if d < 1e-9:
        return None

    if first == last:
        # Outer tangent (LSL / RSR), briefing slide 28. Both arcs steer the
        # same way, so r1 == r2 and the tangent line is parallel to the
        # centre-to-centre vector, offset by one radius on the side the robot
        # rides. RSR rides the left side of that vector, LSL the right side.
        if first == RIGHT:
            v2x, v2y = -v1y, v1x        # rotate V1 counter-clockwise by pi/2
        else:
            v2x, v2y = v1y, -v1x        # rotate V1 clockwise by pi/2
        scale = r1 / d
        pt1 = (p1[0] + scale * v2x, p1[1] + scale * v2y)
        pt2 = (pt1[0] + v1x, pt1[1] + v1y)
        return pt1, pt2

    # Inner tangent (RSL / LSR), briefing slide 29. The straight segment now
    # crosses between the circles, which is only possible if they do not
    # overlap: slide 29's "two diameters apart" is r1 + r2 once the radii differ.
    if d < r1 + r2:
        return None
    gamma = math.acos(min(1.0, (r1 + r2) / d))
    # RSL turns the offset counter-clockwise off the centre line, LSR clockwise.
    # The same direction serves both circles: each tangent point is its own
    # radius along it, outward from p1 and back from p2.
    v2x, v2y = _rotate(v1x, v1y, gamma if first == RIGHT else -gamma)
    pt1 = (p1[0] + r1 / d * v2x, p1[1] + r1 / d * v2y)
    pt2 = (p2[0] - r2 / d * v2x, p2[1] - r2 / d * v2y)
    return pt1, pt2


def _third_circle_ccc(p1: Tuple[float, float], p2: Tuple[float, float],
                      r_outer: float, r_middle: float, clockwise: bool):
    """Centre of the middle circle of a CCC path (briefing slides 30-31).

    The middle circle touches both outer circles, so its centre is
    r_outer + r_middle from each (2r on the slide, where the radii are equal)
    -- one of the two intersection points of two circles of that radius. The
    two outer circles steer the same way, so they share `r_outer`. Passing
    `clockwise` picks which intersection; the caller tries both and keeps the
    shorter.
    """
    v1x, v1y = p2[0] - p1[0], p2[1] - p1[1]
    d = math.hypot(v1x, v1y)
    reach = r_outer + r_middle
    # "The CCC path is only useful when C1 and C2 are very close, i.e. the
    # distance between them is less than 4r" (slide 31) -- i.e. 2 * reach.
    if d < 1e-9 or d >= 2.0 * reach:
        return None

    qx, qy = (p1[0] + p2[0]) / 2.0, (p1[1] + p2[1]) / 2.0
    if clockwise:
        v2x, v2y = v1y, -v1x
    else:
        v2x, v2y = -v1y, v1x
    h = math.sqrt(max(0.0, reach * reach - d * d / 4.0))
    scale = h / d
    return (qx + scale * v2x, qy + scale * v2y)


def _build(word: str, start: Pose, goal: Pose, radii: TurningRadii,
           third_clockwise: bool = True) -> Optional[Trajectory]:
    """Construct one Dubins word, or None if that shape cannot connect the poses.

    Every candidate is verified against the goal pose before being returned, so
    a shape that is geometrically impossible for this pair simply drops out.
    """
    first, last = _STEER[word[0]], _STEER[word[2]]
    middle_steering = _STEER[word[1]]
    r1, r2, r3 = radii.of(first), radii.of(middle_steering), radii.of(last)
    c1 = turn_centre(start, r1, first)
    c2 = turn_centre(goal, r3, last)

    if middle_steering == STRAIGHT:
        tangents = _tangent_points_csc(c1, c2, r1, r3, first, last)
        if tangents is None:
            return None
        pt1, pt2 = tangents
        c3 = None
    else:
        c3 = _third_circle_ccc(c1, c2, r1, r2, third_clockwise)
        if c3 is None:
            return None
        # The outer circles touch the middle one on the centre-to-centre lines,
        # one outer radius along from the outer centre. With all three radii
        # equal that is the midpoint (slide 30).
        f1, f2 = r1 / (r1 + r2), r3 / (r3 + r2)
        pt1 = (c1[0] + f1 * (c3[0] - c1[0]), c1[1] + f1 * (c3[1] - c1[1]))
        pt2 = (c2[0] + f2 * (c3[0] - c2[0]), c2[1] + f2 * (c3[1] - c2[1]))

    # --- first arc: start -> pt1 around c1 -------------------------------
    sweep1 = arc_sweep(c1, start, pt1[0], pt1[1], first)
    seg1 = Segment(FORWARD, first, abs(sweep1) * r1, r1, start)
    mid = seg1.end

    # --- middle segment: pt1 -> pt2 --------------------------------------
    if middle_steering == STRAIGHT:
        straight_len = math.hypot(pt2[0] - pt1[0], pt2[1] - pt1[1])
        seg2 = Segment(FORWARD, STRAIGHT, straight_len, 0.0, mid)
    else:
        sweep2 = arc_sweep(c3, mid, pt2[0], pt2[1], middle_steering)
        seg2 = Segment(FORWARD, middle_steering, abs(sweep2) * r2, r2, mid)
    mid2 = seg2.end

    # A wrong tangent choice shows up here: the robot arrives at pt1 pointing
    # away from pt2 and the straight run lands somewhere else entirely.
    if math.hypot(mid2.x - pt2[0], mid2.y - pt2[1]) > 1e-6:
        return None

    # --- last arc: pt2 -> goal around c2 ---------------------------------
    sweep3 = arc_sweep(c2, mid2, goal.x, goal.y, last)
    seg3 = Segment(FORWARD, last, abs(sweep3) * r3, r3, mid2)

    end = seg3.end
    if math.hypot(end.x - goal.x, end.y - goal.y) > _POS_TOL:
        return None
    if abs(normalise_angle(end.theta - goal.theta)) > _ANGLE_TOL:
        return None

    moving = [s for s in (seg1, seg2, seg3) if s.length > 1e-9]
    if not moving:
        # Start and goal are the same pose. Keep one zero-length segment rather
        # than returning an empty trajectory, which would have no pose to report
        # as its start or end.
        moving = [Segment(FORWARD, STRAIGHT, 0.0, 0.0, start)]
    return Trajectory(moving)


def plan_all(start: Pose, goal: Pose,
             radius: RadiusSpec = None) -> List[Tuple[str, Trajectory]]:
    """Every geometrically valid Dubins word for this pose pair, shortest first.

    `radius` is the robot's configured left/right radii when omitted, a single
    number for a symmetric robot, or a `(left, right)` pair -- see
    `motion.turning_radii`. Collisions are not considered here -- that is
    `plan()`'s job.
    """
    radii = turning_radii(radius)
    candidates: List[Tuple[str, Trajectory]] = []
    for word in WORDS:
        if word[1] == "S":
            traj = _build(word, start, goal, radii)
            if traj is not None:
                candidates.append((word, traj))
        else:
            # Two placements exist for the middle circle; slide 31 notes one is
            # always longer, but it is cheap to build both and sort.
            best: Optional[Trajectory] = None
            for clockwise in (True, False):
                traj = _build(word, start, goal, radii, third_clockwise=clockwise)
                if traj is not None and (best is None or traj.length < best.length):
                    best = traj
            if best is not None:
                candidates.append((word, best))

    candidates.sort(key=lambda item: item[1].length)
    return candidates


def shortest_length(start: Pose, goal: Pose, radius: RadiusSpec = None) -> float:
    """Length of the shortest obstacle-free Dubins path, or inf if none exists.

    Used as the Hybrid A* heuristic: it respects the turning radius, so it is a
    much tighter lower bound than Euclidean distance, and it never overestimates
    because obstacles can only make the true path longer.
    """
    candidates = plan_all(start, goal, radius)
    return candidates[0][1].length if candidates else float("inf")


def plan(start: Pose, goal: Pose, radius: RadiusSpec = None,
         is_free: Optional[Callable[[Pose], bool]] = None
         ) -> Optional[Tuple[str, Trajectory]]:
    """Shortest Dubins path that stays clear of obstacles, or None.

    `is_free` is called on poses sampled every COLLISION_SAMPLE_STEP cm; pass
    `Arena.is_pose_free`. With no `is_free` this is just the shortest word.
    """
    for word, traj in plan_all(start, goal, radius):
        if is_free is None or all(is_free(p) for p in traj.iter_sample(cfg.COLLISION_SAMPLE_STEP)):
            return word, traj
    return None
