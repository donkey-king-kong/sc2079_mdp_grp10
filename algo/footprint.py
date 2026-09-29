"""The robot's real outline, and the exact test of it against the arena.

Every planner pose is the centre of the rear axle (see
`config.TURNING_CENTRE_OFFSET`). The body is the rectangle from `ROBOT_REAR`
behind that point to `ROBOT_FRONT` ahead of it, `ROBOT_WIDTH` across, and it
turns with the robot -- so on a turn the front outer corner sweeps a much wider
circle than the rear axle does, and that corner is what hits things.

Briefing slide 36's trick (inflate every obstacle by half a robot and treat the
robot as a dot) is only exact for a disc or for a box that never rotates. For a
25 x 21cm rectangle it either leaks at the corners (inflate by the half-width)
or blocks gaps the robot fits through (inflate by the half-diagonal), so the
collision test here uses the rotated rectangle itself: the true distance between
it and each obstacle and wall, compared against one clearance.

A trajectory is checked at sampled poses. `config.SWEEP_PAD` is added to the
clearance so that the margin holds along the whole continuous sweep, not just
at the samples -- see its comment in config.py.
"""

import math
from typing import Callable, List, Optional, Sequence, Tuple

import config as cfg
from motion import Pose

Box = Tuple[float, float, float, float]      # x0, y0, x1, y1


def outline(pose: Pose) -> List[Tuple[float, float]]:
    """The four corners of the body, anticlockwise from front-right."""
    c, s = math.cos(pose.theta), math.sin(pose.theta)
    half = cfg.ROBOT_WIDTH / 2.0
    local = ((cfg.ROBOT_FRONT, -half), (cfg.ROBOT_FRONT, half),
             (-cfg.ROBOT_REAR, half), (-cfg.ROBOT_REAR, -half))
    return [(pose.x + a * c - b * s, pose.y + a * s + b * c) for a, b in local]


def _point_to_box(px: float, py: float, box: Box) -> float:
    x0, y0, x1, y1 = box
    dx = max(x0 - px, 0.0, px - x1)
    dy = max(y0 - py, 0.0, py - y1)
    return math.hypot(dx, dy)


def _point_to_body(px: float, py: float, pose: Pose) -> float:
    c, s = math.cos(pose.theta), math.sin(pose.theta)
    a = (px - pose.x) * c + (py - pose.y) * s          # along the heading
    b = -(px - pose.x) * s + (py - pose.y) * c         # to the left
    half = cfg.ROBOT_WIDTH / 2.0
    da = max(-cfg.ROBOT_REAR - a, 0.0, a - cfg.ROBOT_FRONT)
    db = max(-half - b, 0.0, b - half)
    return math.hypot(da, db)


def box_clearance(pose: Pose, box: Box) -> float:
    """Signed distance from the body to an axis-aligned box, in cm.

    Positive is the gap; negative means they overlap, by the smallest distance
    that would separate them (separating-axis theorem).
    """
    corners = outline(pose)
    x0, y0, x1, y1 = box
    box_corners = ((x0, y0), (x1, y0), (x1, y1), (x0, y1))
    c, s = math.cos(pose.theta), math.sin(pose.theta)
    overlap = math.inf
    for ax, ay in ((1.0, 0.0), (0.0, 1.0), (c, s), (-s, c)):
        p = [cx * ax + cy * ay for cx, cy in corners]
        q = [cx * ax + cy * ay for cx, cy in box_corners]
        depth = min(max(p), max(q)) - max(min(p), min(q))
        if depth <= 0.0:
            break
        overlap = min(overlap, depth)
    else:
        return -overlap
    # Two disjoint convex polygons are closest at a vertex of one of them.
    return min(min(_point_to_box(cx, cy, box) for cx, cy in corners),
               min(_point_to_body(cx, cy, pose) for cx, cy in box_corners))


def wall_clearances(pose: Pose) -> Tuple[float, float, float, float]:
    """Distance from the body to the left, right, bottom and top walls."""
    corners = outline(pose)
    xs = [x for x, _ in corners]
    ys = [y for _, y in corners]
    size = cfg.ARENA_SIZE
    return (min(xs), size - max(xs), min(ys), size - max(ys))


def wall_clearance(pose: Pose) -> float:
    """Distance from the body to the nearest arena wall (negative = outside)."""
    return min(wall_clearances(pose))


def make_checker(boxes: Sequence[Box], clearance: float,
                 start_zone: Optional[Tuple[float, Tuple[float, float, float, float]]] = None
                 ) -> Callable[[Pose], bool]:
    """A test for "is the body at least `clearance` from every box and wall?".

    This is the planner's hot loop -- a single plan asks it over a million
    times -- so the geometry is fixed once here and most poses are settled from
    the body's middle without the exact rectangle test: an obstacle farther than
    the half-diagonal (plus clearance) cannot be reached by any corner, and one
    nearer than the half-width (plus clearance) is too close whichever way the
    robot faces. Only the band in between needs `box_clearance`.

    `start_zone` is `(size, (left, right, bottom, top))`: while the rear axle is
    within `size` of the bottom-left corner, each wall only needs the clearance
    given for it instead of `clearance`. That is how the corner start pose,
    which touches two walls, is allowed (see config.START_WALL_TOLERANCE).
    """
    offset = cfg.TURNING_CENTRE_OFFSET
    half_length = (cfg.ROBOT_FRONT + cfg.ROBOT_REAR) / 2.0
    half_width = cfg.ROBOT_WIDTH / 2.0
    outer = math.hypot(half_length, half_width) + clearance
    inner = min(half_length, half_width) + clearance
    low, high = outer, cfg.ARENA_SIZE - outer
    # Each box grown by `outer`: a middle outside it is out of reach, decided
    # with four comparisons.
    near = [(box, box[0] - outer, box[1] - outer, box[2] + outer, box[3] + outer)
            for box in boxes]
    cos, sin = math.cos, math.sin

    def is_clear(pose: Pose) -> bool:
        mx = pose.x + offset * cos(pose.theta)
        my = pose.y + offset * sin(pose.theta)
        if not (low <= mx <= high and low <= my <= high):
            if start_zone is not None and pose.x < start_zone[0] and pose.y < start_zone[0]:
                if any(have < need for have, need
                       in zip(wall_clearances(pose), start_zone[1])):
                    return False
            elif wall_clearance(pose) < clearance:
                return False
        for box, x0, y0, x1, y1 in near:
            if x0 < mx < x1 and y0 < my < y1:
                gap = _point_to_box(mx, my, box)
                if gap >= outer:
                    continue
                if gap < inner or box_clearance(pose, box) < clearance:
                    return False
        return True

    return is_clear
