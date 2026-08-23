"""Hybrid A* -- the fallback search for legs no Dubins path can serve.

Dubins handles almost every leg, but it is forward-only. When an obstacle sits
directly behind the capture pose, or the pose is tucked against a wall inside
the turning circle, no forward-only path exists and all six words come back
blocked. This search covers those cases because its motion primitives include
reverse, so it can three-point-turn its way in -- which is also exactly what
briefing slide 33 describes the robot needing after it finishes a photo.

"Hybrid" A* searches *continuous* poses but de-duplicates them on a coarse
(x, y, theta) lattice, so unlike grid A* the path it produces is one the robot
can actually drive.
"""

import heapq
import math
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

import config as cfg
import dubins
from arena import Arena
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
    normalise_angle,
)

# The six ways the robot can move for one primitive step.
PRIMITIVES: Tuple[Tuple[int, int], ...] = (
    (FORWARD, STRAIGHT), (FORWARD, LEFT), (FORWARD, RIGHT),
    (BACKWARD, STRAIGHT), (BACKWARD, LEFT), (BACKWARD, RIGHT),
)


@dataclass(order=True)
class _Node:
    priority: float
    counter: int
    pose: Pose = None          # type: ignore[assignment]
    g: float = 0.0
    gear: int = 0
    steering: int = 0
    parent: Optional["_Node"] = None
    segment: Optional[Segment] = None


class _DistanceField:
    """Obstacle-aware lower bound on the distance from any cell to the goal.

    A plain Euclidean heuristic sends the search straight into whatever wall or
    obstacle sits between the robot and the goal, and it has to expand its way
    back out again. Instead we run one Dijkstra sweep outward from the goal
    over the free cells and cache the result: the heuristic then already knows
    it has to go *around* things.

    This ignores the turning radius, so it under-estimates on that axis. The
    8-connected grid metric can overshoot true Euclidean distance by a few
    percent, so the search is very slightly inadmissible -- an accepted trade in
    hybrid A*, and it only ever runs on legs Dubins could not solve at all.
    """

    def __init__(self, arena: Arena, goal: Pose, resolution: float = cfg.HA_XY_RESOLUTION):
        self.resolution = resolution
        self.n = int(math.ceil(cfg.ARENA_SIZE / resolution))
        self.cost: List[float] = [float("inf")] * (self.n * self.n)

        gx, gy = self._index(goal.x), self._index(goal.y)
        if not (0 <= gx < self.n and 0 <= gy < self.n):
            return

        diag = math.sqrt(2.0) * resolution
        start_index = gy * self.n + gx
        self.cost[start_index] = 0.0
        queue: List[Tuple[float, int]] = [(0.0, start_index)]

        while queue:
            dist, index = heapq.heappop(queue)
            if dist > self.cost[index]:
                continue
            cy, cx = divmod(index, self.n)
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    if dx == 0 and dy == 0:
                        continue
                    nx, ny = cx + dx, cy + dy
                    if not (0 <= nx < self.n and 0 <= ny < self.n):
                        continue
                    neighbour = ny * self.n + nx
                    if not arena.is_point_free((nx + 0.5) * resolution,
                                               (ny + 0.5) * resolution):
                        continue
                    step = diag if dx and dy else resolution
                    if dist + step < self.cost[neighbour]:
                        self.cost[neighbour] = dist + step
                        heapq.heappush(queue, (dist + step, neighbour))

    def _index(self, value: float) -> int:
        return int(value // self.resolution)

    def __call__(self, pose: Pose) -> float:
        cx, cy = self._index(pose.x), self._index(pose.y)
        if not (0 <= cx < self.n and 0 <= cy < self.n):
            return float("inf")
        return self.cost[cy * self.n + cx]


def _distance_field(arena: Arena, goal: Pose) -> "_DistanceField":
    """Memoised distance field, keyed by goal cell.

    Filling a hole in the cost matrix runs the search from many sources into the
    same one or two goal poses, and the field depends only on the goal -- so
    without this we would redo the same Dijkstra sweep dozens of times.
    """
    cache = getattr(arena, "_distance_fields", None)
    if cache is None:
        cache = {}
        setattr(arena, "_distance_fields", cache)
    key = (int(goal.x // cfg.HA_XY_RESOLUTION), int(goal.y // cfg.HA_XY_RESOLUTION))
    if key not in cache:
        cache[key] = _DistanceField(arena, goal)
    return cache[key]


def _lattice_key(pose: Pose) -> Tuple[int, int, int]:
    """Bucket a continuous pose so revisits of "the same" state collapse."""
    bin_size = 2.0 * math.pi / cfg.HA_THETA_BINS
    theta = normalise_angle(pose.theta) % (2.0 * math.pi)
    return (
        int(pose.x // cfg.HA_XY_RESOLUTION),
        int(pose.y // cfg.HA_XY_RESOLUTION),
        int(theta // bin_size) % cfg.HA_THETA_BINS,
    )


def _at_goal(pose: Pose, goal: Pose) -> bool:
    return (math.hypot(pose.x - goal.x, pose.y - goal.y) <= cfg.HA_GOAL_XY_TOLERANCE
            and abs(normalise_angle(pose.theta - goal.theta)) <= cfg.HA_GOAL_THETA_TOLERANCE)


def _reconstruct(node: _Node, tail: Optional[Trajectory] = None) -> Trajectory:
    segments: List[Segment] = []
    cursor: Optional[_Node] = node
    while cursor is not None and cursor.segment is not None:
        segments.append(cursor.segment)
        cursor = cursor.parent
    segments.reverse()
    if tail is not None:
        segments.extend(tail.segments)
    return Trajectory(merge_segments(segments))


def plan(arena: Arena, start: Pose, goal: Pose,
         radius: float = cfg.TURNING_RADIUS,
         max_expansions: int = cfg.HA_MAX_EXPANSIONS) -> Optional[Trajectory]:
    """Shortest drivable path from `start` to `goal` avoiding obstacles.

    Returns None if no path is found within `max_expansions` -- a bound that
    exists so an unreachable capture pose costs a fraction of a second instead
    of hanging the demo. The planner just moves on to the next pose in the menu.
    """
    if not arena.is_pose_free(start) or not arena.is_pose_free(goal):
        return None

    heuristic = _distance_field(arena, goal)
    # The Dijkstra sweep only reaches cells connected to the goal. If the start
    # is not one of them the goal is walled off and no amount of searching will
    # help -- bail now rather than burning the whole expansion budget proving it.
    if math.isinf(heuristic(start)):
        return None
    counter = 0
    root = _Node(priority=heuristic(start), counter=counter, pose=start, g=0.0,
                 gear=FORWARD, steering=STRAIGHT)
    open_set: List[_Node] = [root]
    best_g: Dict[Tuple[int, int, int], float] = {_lattice_key(start): 0.0}
    expansions = 0

    while open_set and expansions < max_expansions:
        node = heapq.heappop(open_set)
        key = _lattice_key(node.pose)
        if node.g > best_g.get(key, float("inf")) + 1e-9:
            continue
        expansions += 1

        # Analytic expansion. Every so often -- and always once we are close --
        # try to close the remaining gap with a single exact Dubins path. When
        # it works the robot lands on the goal pose *exactly* rather than
        # within the lattice tolerance, which matters because the next leg
        # starts from wherever this one ended.
        if expansions % 8 == 0 or heuristic(node.pose) < 3.0 * radius:
            shot = dubins.plan(node.pose, goal, radius, arena.is_pose_free)
            if shot is not None:
                return _reconstruct(node, shot[1])

        if _at_goal(node.pose, goal):
            return _reconstruct(node)

        for gear, steering in PRIMITIVES:
            segment = Segment(gear, steering, cfg.HA_STEP, radius, node.pose)
            # Check the whole swept step, not just where it lands, or the robot
            # will happily clip a corner mid-primitive.
            if not all(arena.is_pose_free(p) for p in segment.iter_sample(cfg.COLLISION_SAMPLE_STEP)):
                continue

            child_pose = segment.end
            step_cost = cfg.HA_STEP * (cfg.HA_REVERSE_COST if gear == BACKWARD else 1.0)
            if node.segment is not None:
                if gear != node.gear:
                    step_cost += cfg.HA_GEAR_CHANGE_COST
                if steering != node.steering:
                    step_cost += cfg.HA_STEER_CHANGE_COST

            g = node.g + step_cost
            child_key = _lattice_key(child_pose)
            if g >= best_g.get(child_key, float("inf")) - 1e-9:
                continue
            estimate = heuristic(child_pose)
            if math.isinf(estimate):
                continue

            best_g[child_key] = g
            counter += 1
            heapq.heappush(open_set, _Node(
                priority=g + estimate, counter=counter, pose=child_pose, g=g,
                gear=gear, steering=steering, parent=node, segment=segment,
            ))

    return None
