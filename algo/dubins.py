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

def _get_radius(steer: int, r_left: float, r_right: float) -> float:
    if steer == LEFT:
        return r_left
    elif steer == RIGHT:
        return r_right
    return r_left


def _rotate(vx: float, vy: float, angle: float) -> Tuple[float, float]:
    """Rotate a vector counter-clockwise by `angle` (briefing slide 25)."""
    c, s = math.cos(angle), math.sin(angle)
    return vx * c - vy * s, vx * s + vy * c


def _tangent_points_csc(
    p1: Tuple[float, float],
    p2: Tuple[float, float],
    r1: float,
    r2: float,
    first: int,
    last: int,
) -> Optional[Tuple[Tuple[float, float], Tuple[float, float]]]:
    v1x, v1y = p2[0] - p1[0], p2[1] - p1[1]
    d = math.hypot(v1x, v1y)
    if d < 1e-9:
        return None

    if first == last:
        # Outer tangent (LSL / RSR)
        if d < abs(r1 - r2):
            return None
        cos_phi = max(-1.0, min(1.0, (r1 - r2) / d))
        phi = math.acos(cos_phi)
        angle = phi if first == RIGHT else -phi
        ux, uy = _rotate(v1x / d, v1y / d, angle)
        pt1 = (p1[0] + r1 * ux, p1[1] + r1 * uy)
        pt2 = (p2[0] + r2 * ux, p2[1] + r2 * uy)
        return pt1, pt2

    # Inner tangent (RSL / LSR)
    if d < (r1 + r2):
        return None
    gamma = math.acos(max(-1.0, min(1.0, (r1 + r2) / d)))
    angle = gamma if first == RIGHT else -gamma
    ux, uy = _rotate(v1x / d, v1y / d, angle)
    pt1 = (p1[0] + r1 * ux, p1[1] + r1 * uy)
    pt2 = (p2[0] - r2 * ux, p2[1] - r2 * uy)
    return pt1, pt2


def _third_circle_ccc(
    p1: Tuple[float, float],
    p2: Tuple[float, float],
    r1: float,
    r2: float,
    r3: float,
    clockwise: bool,
) -> Optional[Tuple[float, float]]:
    d1 = r1 + r2
    d2 = r3 + r2
    v1x, v1y = p2[0] - p1[0], p2[1] - p1[1]
    d = math.hypot(v1x, v1y)

    if d < 1e-9 or d >= (d1 + d2) or d <= abs(d1 - d2):
        return None

    a = (d1 * d1 - d2 * d2 + d * d) / (2.0 * d)
    h = math.sqrt(max(0.0, d1 * d1 - a * a))

    px = p1[0] + (a / d) * v1x
    py = p1[1] + (a / d) * v1y

    if clockwise:
        v2x, v2y = v1y, -v1x
    else:
        v2x, v2y = -v1y, v1x

    scale = h / d
    return (px + scale * v2x, py + scale * v2y)


def _build(
    word: str,
    start: Pose,
    goal: Pose,
    r_left: float,
    r_right: float,
    third_clockwise: bool = True,
) -> Optional[Trajectory]:
    first, middle_steer, last = _STEER[word[0]], _STEER[word[1]], _STEER[word[2]]
    r1 = _get_radius(first, r_left, r_right)
    r3 = _get_radius(last, r_left, r_right)

    c1 = turn_centre(start, r1, first)
    c2 = turn_centre(goal, r3, last)

    if middle_steer == STRAIGHT:
        tangents = _tangent_points_csc(c1, c2, r1, r3, first, last)
        if tangents is None:
            return None
        pt1, pt2 = tangents
        c3 = None
        r2 = 0.0
    else:
        r2 = _get_radius(middle_steer, r_left, r_right)
        c3 = _third_circle_ccc(c1, c2, r1, r2, r3, third_clockwise)
        if c3 is None:
            return None
        pt1 = (
            c1[0] + (r1 / (r1 + r2)) * (c3[0] - c1[0]),
            c1[1] + (r1 / (r1 + r2)) * (c3[1] - c1[1]),
        )
        pt2 = (
            c2[0] + (r3 / (r3 + r2)) * (c3[0] - c2[0]),
            c2[1] + (r3 / (r3 + r2)) * (c3[1] - c2[1]),
        )

    # --- first arc ---
    sweep1 = arc_sweep(c1, start, pt1[0], pt1[1], first)
    seg1 = Segment(FORWARD, first, abs(sweep1) * r1, r1, start)
    mid = seg1.end

    # --- middle segment ---
    if middle_steer == STRAIGHT:
        straight_len = math.hypot(pt2[0] - pt1[0], pt2[1] - pt1[1])
        seg2 = Segment(FORWARD, STRAIGHT, straight_len, r1, mid)
    else:
        sweep2 = arc_sweep(c3, mid, pt2[0], pt2[1], middle_steer)
        seg2 = Segment(FORWARD, middle_steer, abs(sweep2) * r2, r2, mid)
    mid2 = seg2.end

    if math.hypot(mid2.x - pt2[0], mid2.y - pt2[1]) > 1e-6:
        return None

    # --- last arc ---
    sweep3 = arc_sweep(c2, mid2, goal.x, goal.y, last)
    seg3 = Segment(FORWARD, last, abs(sweep3) * r3, r3, mid2)

    end = seg3.end
    if math.hypot(end.x - goal.x, end.y - goal.y) > _POS_TOL:
        return None
    if abs(normalise_angle(end.theta - goal.theta)) > _ANGLE_TOL:
        return None

    moving = [s for s in (seg1, seg2, seg3) if s.length > 1e-9]
    if not moving:
        moving = [Segment(FORWARD, STRAIGHT, 0.0, r1, start)]
    return Trajectory(moving)


def plan_all(
    start: Pose,
    goal: Pose,
    r_left: float = cfg.TURNING_RADIUS_LEFT,
    r_right: float = cfg.TURNING_RADIUS_RIGHT,
) -> List[Tuple[str, Trajectory]]:
    candidates: List[Tuple[str, Trajectory]] = []
    for word in WORDS:
        if word in ("RLR", "LRL"):
            for third_cw in (True, False):
                traj = _build(word, start, goal, r_left, r_right, third_clockwise=third_cw)
                if traj is not None:
                    candidates.append((word, traj))
        else:
            traj = _build(word, start, goal, r_left, r_right)
            if traj is not None:
                candidates.append((word, traj))
    return candidates

def shortest_length(
    start: Pose,
    goal: Pose,
    r_left: float = cfg.TURNING_RADIUS_LEFT,
    r_right: float = cfg.TURNING_RADIUS_RIGHT,
) -> float:
    candidates = plan_all(start, goal, r_left, r_right)
    return min((traj.length for _, traj in candidates), default=float("inf"))

def plan(
    start: Pose,
    goal: Pose,
    r_left: float = cfg.TURNING_RADIUS_LEFT,
    r_right: float = cfg.TURNING_RADIUS_RIGHT,
    is_pose_free: Optional[Callable[[Pose], bool]] = None,
) -> Optional[Tuple[str, Trajectory]]:
    candidates = plan_all(start, goal, r_left, r_right)
    candidates.sort(key=lambda item: item[1].length)
    
    for word, traj in candidates:
        if is_pose_free is None:
            return (word, traj)
        
        collision = False
        for segment in traj.segments:
            if not all(is_pose_free(p) for p in segment.iter_sample(cfg.COLLISION_SAMPLE_STEP)):
                collision = True
                break
        if not collision:
            return (word, traj)
            
    return None
