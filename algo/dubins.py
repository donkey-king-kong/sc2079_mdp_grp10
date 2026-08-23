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
    Segment,
    Trajectory,
    arc_sweep,
    normalise_angle,
    turn_centre,
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
                        radius: float, first: int, last: int):
    """Tangent points of the straight segment of a CSC path.

    `p1`/`p2` are the centres of the start and goal turning circles, `first`
    and `last` the steering directions of the two arcs.
    """
    v1x, v1y = p2[0] - p1[0], p2[1] - p1[1]
    d = math.hypot(v1x, v1y)
    if d < 1e-9:
        return None

    if first == last:
        # Outer tangent (LSL / RSR), briefing slide 28. The tangent line is
        # parallel to the centre-to-centre vector, offset by one radius on the
        # side the robot rides. RSR rides the left side of that vector, LSL the
        # right side.
        if first == RIGHT:
            v2x, v2y = -v1y, v1x        # rotate V1 counter-clockwise by pi/2
        else:
            v2x, v2y = v1y, -v1x        # rotate V1 clockwise by pi/2
        scale = radius / d
        pt1 = (p1[0] + scale * v2x, p1[1] + scale * v2y)
        pt2 = (pt1[0] + v1x, pt1[1] + v1y)
        return pt1, pt2

    # Inner tangent (RSL / LSR), briefing slide 29. The straight segment now
    # crosses between the circles, which is only possible if they are at least
    # two diameters apart.
    if d < 2.0 * radius:
        return None
    gamma = math.acos(min(1.0, 2.0 * radius / d))
    # RSL turns the offset counter-clockwise off the centre line, LSR clockwise.
    v2x, v2y = _rotate(v1x, v1y, gamma if first == RIGHT else -gamma)
    scale = radius / d
    pt1 = (p1[0] + scale * v2x, p1[1] + scale * v2y)
    pt2 = (p2[0] - scale * v2x, p2[1] - scale * v2y)
    return pt1, pt2


def _third_circle_ccc(p1: Tuple[float, float], p2: Tuple[float, float],
                      radius: float, clockwise: bool):
    """Centre of the middle circle of a CCC path (briefing slides 30-31).

    The middle circle touches both outer circles, so its centre is 2r from each
    -- one of the two intersection points of two circles of radius 2r. Passing
    `clockwise` picks which one; the caller tries both and keeps the shorter.
    """
    v1x, v1y = p2[0] - p1[0], p2[1] - p1[1]
    d = math.hypot(v1x, v1y)
    # "The CCC path is only useful when C1 and C2 are very close, i.e. the
    # distance between them is less than 4r" (slide 31).
    if d < 1e-9 or d >= 4.0 * radius:
        return None

    qx, qy = (p1[0] + p2[0]) / 2.0, (p1[1] + p2[1]) / 2.0
    if clockwise:
        v2x, v2y = v1y, -v1x
    else:
        v2x, v2y = -v1y, v1x
    h = math.sqrt(max(0.0, 4.0 * radius * radius - d * d / 4.0))
    scale = h / d
    return (qx + scale * v2x, qy + scale * v2y)


def _build(word: str, start: Pose, goal: Pose, radius: float,
           third_clockwise: bool = True) -> Optional[Trajectory]:
    """Construct one Dubins word, or None if that shape cannot connect the poses.

    Every candidate is verified against the goal pose before being returned, so
    a shape that is geometrically impossible for this pair simply drops out.
    """
    first, last = _STEER[word[0]], _STEER[word[2]]
    c1 = turn_centre(start, radius, first)
    c2 = turn_centre(goal, radius, last)

    if word[1] == "S":
        tangents = _tangent_points_csc(c1, c2, radius, first, last)
        if tangents is None:
            return None
        pt1, pt2 = tangents
        middle_steering = STRAIGHT
        c3 = None
    else:
        c3 = _third_circle_ccc(c1, c2, radius, third_clockwise)
        if c3 is None:
            return None
        # The outer circles touch the middle one at the midpoints of the
        # centre-to-centre lines, because all three radii are equal (slide 30).
        pt1 = ((c1[0] + c3[0]) / 2.0, (c1[1] + c3[1]) / 2.0)
        pt2 = ((c2[0] + c3[0]) / 2.0, (c2[1] + c3[1]) / 2.0)
        middle_steering = _STEER[word[1]]

    # --- first arc: start -> pt1 around c1 -------------------------------
    sweep1 = arc_sweep(c1, start, pt1[0], pt1[1], first)
    seg1 = Segment(FORWARD, first, abs(sweep1) * radius, radius, start)
    mid = seg1.end

    # --- middle segment: pt1 -> pt2 --------------------------------------
    if middle_steering == STRAIGHT:
        straight_len = math.hypot(pt2[0] - pt1[0], pt2[1] - pt1[1])
        seg2 = Segment(FORWARD, STRAIGHT, straight_len, radius, mid)
    else:
        sweep2 = arc_sweep(c3, mid, pt2[0], pt2[1], middle_steering)
        seg2 = Segment(FORWARD, middle_steering, abs(sweep2) * radius, radius, mid)
    mid2 = seg2.end

    # A wrong tangent choice shows up here: the robot arrives at pt1 pointing
    # away from pt2 and the straight run lands somewhere else entirely.
    if math.hypot(mid2.x - pt2[0], mid2.y - pt2[1]) > 1e-6:
        return None

    # --- last arc: pt2 -> goal around c2 ---------------------------------
    sweep3 = arc_sweep(c2, mid2, goal.x, goal.y, last)
    seg3 = Segment(FORWARD, last, abs(sweep3) * radius, radius, mid2)

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
        moving = [Segment(FORWARD, STRAIGHT, 0.0, radius, start)]
    return Trajectory(moving)


def plan_all(start: Pose, goal: Pose,
             radius: float = cfg.TURNING_RADIUS) -> List[Tuple[str, Trajectory]]:
    """Every geometrically valid Dubins word for this pose pair, shortest first.

    Collisions are not considered here -- that is `plan()`'s job.
    """
    candidates: List[Tuple[str, Trajectory]] = []
    for word in WORDS:
        if word[1] == "S":
            traj = _build(word, start, goal, radius)
            if traj is not None:
                candidates.append((word, traj))
        else:
            # Two placements exist for the middle circle; slide 31 notes one is
            # always longer, but it is cheap to build both and sort.
            best: Optional[Trajectory] = None
            for clockwise in (True, False):
                traj = _build(word, start, goal, radius, third_clockwise=clockwise)
                if traj is not None and (best is None or traj.length < best.length):
                    best = traj
            if best is not None:
                candidates.append((word, best))

    candidates.sort(key=lambda item: item[1].length)
    return candidates


def shortest_length(start: Pose, goal: Pose,
                    radius: float = cfg.TURNING_RADIUS) -> float:
    """Length of the shortest obstacle-free Dubins path, or inf if none exists.

    Used as the Hybrid A* heuristic: it respects the turning radius, so it is a
    much tighter lower bound than Euclidean distance, and it never overestimates
    because obstacles can only make the true path longer.
    """
    candidates = plan_all(start, goal, radius)
    return candidates[0][1].length if candidates else float("inf")


def plan(start: Pose, goal: Pose, radius: float = cfg.TURNING_RADIUS,
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
