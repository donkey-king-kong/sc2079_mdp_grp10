"""Hamiltonian path planning -- what order to visit the obstacles in.

This is the layer the checklist grades directly:

* **B.2** "an algorithm that guides the robot ... visiting each image position
  once" -> the `nearest` strategy, the greedy nearest-neighbour of briefing
  slide 14.
* **B.3** "a shortest-TIME Hamiltonian path" -> the `exhaustive` strategy of
  slide 16, scoring every one of the 5! = 120 orderings against the *time*
  model in `config.py` rather than against raw distance.

Two things the briefing glosses over turn out to drive the whole design.

**An obstacle is not a point.** Each one offers a menu of acceptable capture
poses (see `arena.capture_poses`), and which one you pick changes the cost of
the leg coming in *and* the leg going out. So this is really a generalised TSP.
We handle the visit order with the strategies above, and for any fixed order we
solve the pose choice *exactly* with a small DP over the layers -- cheap enough
to run inside the exhaustive search.

**A Dubins path is only three segments.** On a cluttered arena that is often not
enough to get from one capture pose to the next, and the naive answer -- fall
straight through to Hybrid A* -- is far too slow to do for every pair. Instead
the graph carries `transit` poses in the open parts of the arena, and a
Floyd-Warshall pass lets a leg route through them. Two collision-free Dubins
paths joined end to end are still a drivable trajectory, so this buys most of
the missing connectivity analytically, and the search is left as a last resort.

Building the graph is the expensive part and does not depend on the strategy, so
`CostModel` builds it once and every strategy reuses it. That is what makes the
simulator's "compare all strategies" view fast.
"""

import itertools
import math
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence, Tuple

import commands as commands_module
import config as cfg
import dubins
import hybrid_astar
from arena import Arena, CapturePose, start_pose
from motion import (BACKWARD, STRAIGHT, Pose, Segment, Trajectory, footprint_centre,
                    merge_segments, normalise_angle, pose_from_footprint_centre)

STRATEGIES = ("nearest", "greedy_swap", "exhaustive")

INF = float("inf")

# Capture poses considered per obstacle, chosen for spread of approach angle
# rather than by rank -- see `Arena.select_capture_poses` for why that matters.
# There are seven approach angles, so this covers all of them plus one spare.
POSES_PER_OBSTACLE = 8

# Transit poses: a coarse lattice of "somewhere useful to be" in the open parts
# of the arena, used only as stepping stones for multi-hop legs. Positions that
# land inside a virtual obstacle are dropped, so a cluttered arena costs less
# here, not more.
TRANSIT_COORDS = (50.0, 100.0, 150.0)
TRANSIT_HEADINGS = (0.0, 1.5707963267948966, 3.141592653589793, -1.5707963267948966)

# Ceiling on Hybrid A* calls while patching whatever the analytic passes could
# not connect. The search is milliseconds when it succeeds but has to exhaust
# its budget to prove a leg impossible, and mostly it is proving.
SEARCH_BUDGET = 20

# How many already-reachable poses, nearest first, a stranded obstacle's search
# may start from before it is given up on. See `CostModel.fill_gaps`.
SEARCH_SOURCES = 3

# How many of the nearest onward poses a search out of a dead-end photo pose
# aims at, all at once. See `CostModel.reconnect`.
ONWARD_GOALS = 8


@dataclass
class Node:
    """A pose in the roadmap: the start, a place to photograph from, or a stepping stone."""

    index: int
    pose: Pose
    kind: str                            # "start" | "capture" | "transit"
    obstacle_id: Optional[int] = None    # set only for "capture"
    capture: Optional[CapturePose] = None


@dataclass
class Leg:
    """One drive from wherever the robot is to the next obstacle's capture pose."""

    obstacle_id: int
    trajectory: Trajectory
    method: str                  # the Dubins word, "via" for a multi-hop, or "hybrid_astar"
    # What the leg's whole-number commands really drive, from where the legs
    # before it leave the robot (set by `anchor_legs`). It ends a little off
    # `trajectory`'s capture pose, and it is the path the simulator draws.
    driven: Optional[Trajectory] = None

    @property
    def distance(self) -> float:
        return self.trajectory.length

    @property
    def duration(self) -> float:
        return self.trajectory.duration()


@dataclass
class Route:
    """A full plan: which obstacles, in what order, and how to drive between them."""

    strategy: str
    metric: str
    legs: List[Leg] = field(default_factory=list)
    unreachable: List[int] = field(default_factory=list)

    @property
    def order(self) -> List[int]:
        return [leg.obstacle_id for leg in self.legs]

    @property
    def total_distance(self) -> float:
        return sum(leg.distance for leg in self.legs)

    @property
    def total_duration(self) -> float:
        """Driving time plus the dwell at each obstacle while the photo is taken."""
        return sum(leg.duration for leg in self.legs) + cfg.SCAN_TIME * len(self.legs)

    @property
    def total_cost(self) -> float:
        return self.total_duration if self.metric == "time" else self.total_distance


def leg_cost(trajectory: Trajectory, metric: str = "time") -> float:
    """What B.3 minimises.

    With `metric="time"` this is seconds, from the speed and switching-penalty
    model in config.py -- turns are slower per centimetre than straights and
    every gear change costs a real pause, so the shortest path and the fastest
    path are genuinely different routes. `metric="distance"` gives plain
    centimetres, which is what a naive implementation optimises.
    """
    if metric == "time":
        return trajectory.duration()
    if metric == "distance":
        return trajectory.length
    raise ValueError("metric must be 'time' or 'distance', got %r" % (metric,))


def _start_zone_first(arena: Arena, obstacle_ids: List[int]) -> List[int]:
    """`obstacle_ids` with the blocks that crowd the start zone moved to the front.

    "Crowding" means the block's cell is within two cells of the 4x4 start
    zone in both directions (x and y both under 60cm).
    """
    near = []
    for oid in obstacle_ids:
        ob = arena.obstacle_by_id(oid)
        if ob.x < cfg.START_ZONE_SIZE + 2 * cfg.CELL_SIZE and ob.y < cfg.START_ZONE_SIZE + 2 * cfg.CELL_SIZE:
            near.append((ob.x + ob.y, oid))
    front = [oid for _, oid in sorted(near)]
    return front + [oid for oid in obstacle_ids if oid not in front]


def _plan_leg(arena: Arena, source: Pose, target: Pose, allow_search: bool,
              max_expansions: int = cfg.HA_MAX_EXPANSIONS,
              allow_backoff: bool = True) -> Optional[Tuple[str, Trajectory]]:
    """One leg: back out of the capture pose, then Dubins; Hybrid A* as a last resort.

    The back-out is not an optimisation, it is a necessity. A capture pose sits
    30cm from an obstacle face pointing straight at it, and the turning radius
    is 20cm left / 36cm right, so the tightest forward arc the robot can drive
    still ends up inside the block. Briefing slide 33 says as much: reverse first.

    Pass `allow_backoff=False` when the robot is not parked in front of anything
    -- the start pose, or a transit pose. There is nothing to reverse away from,
    and since a failed leg costs one Dubins attempt per option, skipping them is
    most of the cost of building the roadmap.
    """
    options = cfg.DEPARTURE_BACKOFF_OPTIONS if allow_backoff else (0.0,)
    for backoff in options:
        if backoff <= 0.0:
            departure, prefix = source, []
        else:
            reverse = Segment(BACKWARD, STRAIGHT, backoff, 0.0, source)
            if not all(arena.is_pose_free(p)
                       for p in reverse.iter_sample(cfg.COLLISION_SAMPLE_STEP)):
                break            # blocked behind: reversing further cannot help
            departure, prefix = reverse.end, [reverse]

        result = _dubins(arena, departure, target)
        if result is None:
            continue
        word, trajectory = result
        combined = Trajectory(merge_segments(prefix + trajectory.segments))
        return (word if not prefix else "SB+" + word, combined)

    # Dubins only drives forward, so it cannot enter a pose tucked against a
    # wall or behind another block. The same path driven backwards can: a
    # forward path from the target to the source, reversed, reaches the target
    # in reverse gear -- still analytic -- and covers most of what used to fall
    # through to the search below.
    result = _dubins(arena, target, source)
    if result is not None:
        word, trajectory = result
        return ("rev-" + word, _reversed(trajectory))

    if not allow_search:
        return None
    trajectory = hybrid_astar.plan(arena, source, target, max_expansions=max_expansions)
    return ("hybrid_astar", trajectory) if trajectory is not None else None


def _dubins(arena: Arena, start: Pose, goal: Pose) -> Optional[Tuple[str, Trajectory]]:
    """`dubins.plan` against this arena, memoised per pose pair.

    Building the roadmap asks for most pairs twice -- once as a forward leg,
    once as the reversed path of the leg the other way round -- and the
    collision check on each candidate is the expensive part.
    """
    cache = getattr(arena, "_dubins_cache", None)
    if cache is None:
        cache = {}
        setattr(arena, "_dubins_cache", cache)
    key = (start, goal)
    if key not in cache:
        cache[key] = dubins.plan(start, goal, is_free=arena.is_pose_free)
    return cache[key]


def _reversed(trajectory: Trajectory) -> Trajectory:
    """The same path driven the other way, in reverse gear.

    Each arc keeps its steering side and radius -- reversing round a circle
    stays on that circle -- so every pose it passes through, and therefore its
    collision check, is exactly the forward path's.
    """
    return Trajectory([Segment(BACKWARD, seg.steering, seg.length, seg.radius, seg.end)
                       for seg in reversed(trajectory.segments)])


class CostModel:
    """The roadmap of poses and the cost of driving between them.

    Built once per layout and shared by every strategy.
    """

    def __init__(self, arena: Arena, start: Optional[Pose] = None,
                 metric: str = "time", poses_per_obstacle: int = POSES_PER_OBSTACLE):
        self.arena = arena
        self.metric = metric
        self.start = start or start_pose()

        self.nodes: List[Node] = [Node(index=0, pose=self.start, kind="start")]
        self.nodes_by_obstacle: Dict[int, List[int]] = {}
        self.no_pose_obstacles: List[int] = []

        for obstacle in arena.obstacles:
            poses = arena.select_capture_poses(obstacle, poses_per_obstacle)
            if not poses:
                # Every pose around this face is inside a wall or another
                # obstacle's virtual box -- nothing to plan towards.
                self.no_pose_obstacles.append(obstacle.id)
                continue
            indices = []
            for capture in poses:
                indices.append(self._add(Node(index=len(self.nodes), pose=capture.pose,
                                              kind="capture", obstacle_id=obstacle.id,
                                              capture=capture)))
            self.nodes_by_obstacle[obstacle.id] = indices

        for x in TRANSIT_COORDS:
            for y in TRANSIT_COORDS:
                if not arena.is_point_free(x, y):
                    continue
                for theta in TRANSIT_HEADINGS:
                    # The lattice places the robot's body; the pose is its
                    # turning centre, a little behind that.
                    pose = pose_from_footprint_centre(x, y, theta)
                    if not arena.is_pose_free(pose):
                        continue
                    self._add(Node(index=len(self.nodes), pose=pose, kind="transit"))

        # The order obstacles are worked through decides which way a stranded
        # one gets searched for. A block crowding the start zone is reached
        # most reliably while the start is still the nearest place to search
        # from, so those go first (nearest the corner first); every other
        # obstacle keeps the order it was sent in. Ids, and so every SNAP, are
        # unchanged.
        self.obstacle_ids: List[int] = _start_zone_first(arena, list(self.nodes_by_obstacle.keys()))

        size = len(self.nodes)
        self._cost: List[List[float]] = [[INF] * size for _ in range(size)]
        self._via: List[List[int]] = [[-1] * size for _ in range(size)]
        self._direct: Dict[Tuple[int, int], Tuple[str, Trajectory]] = {}
        self._gaps_filled = False
        self._search_deadline: Optional[float] = None
        self._reconnected = False
        self._build()

    def _add(self, node: Node) -> int:
        self.nodes.append(node)
        return node.index

    # -- construction ------------------------------------------------------

    def _build(self) -> None:
        """All-pairs analytic legs, then let them chain through each other."""
        for i in range(len(self.nodes)):
            for j in range(len(self.nodes)):
                if i == j or self.nodes[j].kind == "start":
                    continue         # nothing ever needs to drive back to the start
                self._solve(i, j, allow_search=False)
        self._close_transitively()

    def _solve(self, i: int, j: int, allow_search: bool) -> float:
        result = _plan_leg(self.arena, self.nodes[i].pose, self.nodes[j].pose,
                           allow_search, max_expansions=cfg.HA_MATRIX_EXPANSIONS,
                           allow_backoff=self.nodes[i].kind == "capture")
        if result is None:
            return self._cost[i][j]
        cost = leg_cost(result[1], self.metric)
        self._direct[(i, j)] = result
        if cost < self._cost[i][j]:
            self._cost[i][j] = cost
            self._via[i][j] = -1
        return self._cost[i][j]

    def _close_transitively(self) -> None:
        """Let a leg route through intermediate poses when no direct path exists.

        Floyd-Warshall over the roadmap. Every hop is a collision-free Dubins
        path we have already computed, so joining them end to end costs nothing
        extra and yields a trajectory the robot can genuinely drive -- it simply
        passes through the intermediate pose without stopping. The detour shows
        up honestly in the cost, so a direct leg still wins whenever one exists.
        """
        cost, via, size = self._cost, self._via, len(self.nodes)
        for k in range(size):
            if self.nodes[k].kind == "start":
                continue                          # never route back through the start
            cost_k = cost[k]
            for i in range(size):
                ik = cost[i][k]
                if ik >= INF or i == k:
                    continue
                cost_i, via_i = cost[i], via[i]
                for j in range(size):
                    if j == k or j == i:
                        continue
                    through = ik + cost_k[j]
                    if through < cost_i[j] - 1e-9:
                        cost_i[j] = through
                        via_i[j] = k

    def fill_gaps(self) -> None:
        """Last resort: buy connectivity with Hybrid A* where nothing analytic worked.

        Runs at most once per model and the result is shared by every strategy.
        Only obstacles the robot cannot yet get to from the start are searched
        for, and each search starts from the few places it CAN already get to
        that are nearest that obstacle. A short search from next door succeeds
        in milliseconds; a long one from across the arena (or from another
        obstacle's photo pose, which the robot has to back out of first) tends
        to run out of expansions -- and a failed search has to exhaust its
        budget to prove the leg impossible, so failures are what the time goes
        on. As soon as one search lands, the next obstacle gets its turn.

        Bounded by wall clock as well as call count, because the two failure
        modes cost wildly different amounts and only the clock bounds the thing
        a person actually waits for. Whatever is reached in the time available
        is kept; anything still stranded is reported as unreachable.

        Each search aims at every photo pose of the obstacle at once, and stops
        at whichever it can get into: the best-ranked pose is often the one the
        car cannot fit into (straight-on under a wall, say) while a slanted one
        is easy. The clock is shared fairly: each stranded obstacle may use an
        equal share of the time left, as a hard stop inside the search, so one
        hopeless obstacle cannot starve the rest; time an obstacle does not
        need rolls on to the next.

        Searches run in two passes. The first gives every stranded obstacle a
        short search (HA_MATRIX_EXPANSIONS steps), so the easy ones are found
        before anything long runs. The second gives whatever is still stranded
        a deeper search (HA_DEEP_EXPANSIONS) on the time the first left over:
        some ways out of a tight corner are only found by a long search.
        """
        if self._gaps_filled:
            return
        self._gaps_filled = True
        deadline = time.monotonic() + cfg.SEARCH_TIME_BUDGET
        self._search_deadline = deadline

        # Two passes: a cheap search for every stranded obstacle first, then a
        # deeper one for whatever is still stranded, on the time left over.
        for cap in (cfg.HA_MATRIX_EXPANSIONS, cfg.HA_DEEP_EXPANSIONS):
            budget = SEARCH_BUDGET
            stranded = [oid for oid in self.obstacle_ids if not self._reachable_from_start(oid)]
            for position, target_id in enumerate(stranded):
                if self._reachable_from_start(target_id):
                    continue            # an earlier search opened a route to it
                waiting = sum(1 for oid in stranded[position:] if not self._reachable_from_start(oid))
                now = time.monotonic()
                share_end = now + max(0.0, deadline - now) / waiting
                reached = False
                for source_index in self._representative_sources(target_id):
                    if budget <= 0 or time.monotonic() > share_end:
                        break
                    budget -= 1
                    if self._search_into(source_index, target_id, share_end, cap):
                        reached = True
                        break
                if reached:
                    # Route the new edge through the roadmap now, so the next
                    # obstacle can start its search from here.
                    self._close_transitively()

        self._close_transitively()      # new edges open up new multi-hop routes

    def reconnect(self, dropped: Sequence[int]) -> bool:
        """Second round: obstacles the robot can reach but no full tour includes.

        `fill_gaps` only searches for obstacles nothing reaches. An obstacle can
        also be reachable and still left out of every tour: the photo pose the
        robot gets into is one it cannot drive on from (tucked in against a
        wall, say), or it can only be reached straight from the start, and so
        can another obstacle -- and only one of them can go first. So for each
        obstacle the best tour dropped, search OUT of the photo poses the robot
        can reach, to the nearest places it can carry on from; failing that,
        search back IN to it from the nearest poses the robot can reach.

        Uses what is left of `fill_gaps`'s clock, shared fairly, so planning
        stays bounded. Returns True if any new leg was found.
        """
        if self._reconnected:
            return False
        self._reconnected = True
        deadline = self._search_deadline or time.monotonic() + cfg.SEARCH_TIME_BUDGET
        budget = SEARCH_BUDGET
        joined = False
        for position, target_id in enumerate(dropped):
            now = time.monotonic()
            if now >= deadline or budget <= 0:
                break
            share_end = now + (deadline - now) / (len(dropped) - position)
            found = False
            for source_index in self._reached_poses(target_id)[:2]:
                if budget <= 0 or time.monotonic() > share_end:
                    break
                budget -= 1
                if self._search(source_index, self._onward_nodes(source_index), share_end):
                    found = True
                    break
            if not found:
                for source_index in self._representative_sources(target_id):
                    if budget <= 0 or time.monotonic() > share_end:
                        break
                    budget -= 1
                    if self._search_into(source_index, target_id, share_end):
                        found = True
                        break
            if found:
                joined = True
                self._close_transitively()
        return joined

    def _reached_poses(self, target_id: int) -> List[int]:
        """The obstacle's photo poses the robot can get to, cheapest first."""
        reached = [j for j in self.nodes_by_obstacle[target_id] if self._cost[0][j] < INF]
        return sorted(reached, key=lambda j: self._cost[0][j])

    def _onward_nodes(self, source_index: int) -> List[int]:
        """Nearest poses (up to ONWARD_GOALS) the robot can reach and go on from."""
        here = footprint_centre(self.nodes[source_index].pose)
        own = self.nodes[source_index].obstacle_id
        onward = [k for k, node in enumerate(self.nodes)
                  if k != 0 and node.obstacle_id != own and self._cost[0][k] < INF]
        onward.sort(key=lambda k: math.hypot(*(a - b for a, b in zip(
            footprint_centre(self.nodes[k].pose), here))))
        return onward[:ONWARD_GOALS]

    def _search_into(self, source_index: int, target_id: int, deadline: float,
                     max_expansions: Optional[int] = None) -> bool:
        """One Hybrid A* search from a node into any photo pose of an obstacle."""
        return self._search(source_index, self.nodes_by_obstacle[target_id], deadline,
                            max_expansions)

    def _search(self, source_index: int, goals: List[int], deadline: float,
                max_expansions: Optional[int] = None) -> bool:
        """One Hybrid A* search from a node to whichever of `goals` it reaches first."""
        if not goals:
            return False
        found = hybrid_astar.plan_any(self.arena, self.nodes[source_index].pose,
                                      [self.nodes[j].pose for j in goals],
                                      max_expansions=max_expansions or cfg.HA_MATRIX_EXPANSIONS,
                                      deadline=deadline,
                                      shoot_better_neighbours=not max_expansions
                                      or max_expansions <= cfg.HA_MATRIX_EXPANSIONS)
        if found is None:
            return False
        target_index, trajectory = goals[found[0]], found[1]
        cost = leg_cost(trajectory, self.metric)
        if cost < self._cost[source_index][target_index]:
            self._cost[source_index][target_index] = cost
            self._via[source_index][target_index] = -1
            self._direct[(source_index, target_index)] = ("hybrid_astar", trajectory)
        return True

    def _reachable_from_start(self, target_id: int) -> bool:
        return any(self._cost[0][j] < INF for j in self.nodes_by_obstacle[target_id])

    def _representative_sources(self, target_id: int) -> List[int]:
        """The start and every pose already reachable from it, nearest the target first.

        Capped at SEARCH_SOURCES: if the nearest few cannot get in, the far side
        of the arena will not do better within the expansion budget.
        """
        tx, ty = footprint_centre(self.nodes[self.nodes_by_obstacle[target_id][0]].pose)
        reachable = [i for i, node in enumerate(self.nodes)
                     if (i == 0 or self._cost[0][i] < INF) and node.obstacle_id != target_id]
        reachable.sort(key=lambda i: math.hypot(*(a - b for a, b in zip(
            footprint_centre(self.nodes[i].pose), (tx, ty)))))
        return reachable[:SEARCH_SOURCES]

    # -- queries -----------------------------------------------------------

    def cost(self, i: int, j: int) -> float:
        return self._cost[i][j]

    def trajectory(self, i: int, j: int) -> Optional[Tuple[str, Trajectory]]:
        """The drive for one leg, stitching multi-hop routes back together."""
        via = self._via[i][j]
        if via < 0:
            return self._direct.get((i, j))
        first, second = self.trajectory(i, via), self.trajectory(via, j)
        if first is None or second is None:
            return None
        return ("via", Trajectory(merge_segments(first[1].segments + second[1].segments)))

    def reachable_obstacles(self) -> List[int]:
        """Obstacles the robot can get to at all, directly or through a detour."""
        return [oid for oid in self.obstacle_ids
                if any(self._cost[0][j] < INF for j in self.nodes_by_obstacle[oid])]

    # -- the pose-choice DP ------------------------------------------------

    def evaluate_order(self, order: Sequence[int]) -> Tuple[float, List[int]]:
        """Best cost achievable for a fixed obstacle ORDER, choosing poses optimally.

        Layered shortest path: layer k holds the candidate poses of the k-th
        obstacle, and every edge is a precomputed leg cost. Exact, and O(n*K^2)
        -- cheap enough to sit inside the exhaustive search over orderings.
        """
        if not order:
            return 0.0, []

        # Layer 0: from the start node into the first obstacle's poses.
        current = {j: self._cost[0][j] for j in self.nodes_by_obstacle[order[0]]}
        back: List[Dict[int, int]] = [{}]

        for obstacle_id in order[1:]:
            nxt: Dict[int, float] = {}
            pointers: Dict[int, int] = {}
            for j in self.nodes_by_obstacle[obstacle_id]:
                best, best_i = INF, -1
                for i, so_far in current.items():
                    if so_far >= INF:
                        continue
                    total = so_far + self._cost[i][j]
                    if total < best:
                        best, best_i = total, i
                nxt[j] = best
                pointers[j] = best_i
            current, back = nxt, back + [pointers]

        end = min(current, key=lambda j: current[j], default=None)
        if end is None or current[end] >= INF:
            return INF, []

        chain = [end]
        for pointers in reversed(back[1:]):
            chain.append(pointers[chain[-1]])
        chain.reverse()
        return current[end], chain


# --------------------------------------------------------------------------
# Ordering strategies
# --------------------------------------------------------------------------


def _nearest_order(model: CostModel, obstacle_ids: Sequence[int]) -> List[int]:
    """Greedy nearest-neighbour, briefing slide 14's `nearestNeighbour()`.

    From wherever the robot is, compute the leg to every obstacle not yet
    visited, go to the cheapest, repeat. Fast and usually decent, but it can be
    led badly astray by its first choice -- which is exactly why B.3 exists.
    """
    remaining = list(obstacle_ids)
    order: List[int] = []
    current_nodes = {0: 0.0}     # node index -> cost of getting there

    while remaining:
        best = (INF, None, None)
        for obstacle_id in remaining:
            for j in model.nodes_by_obstacle[obstacle_id]:
                for i, so_far in current_nodes.items():
                    total = so_far + model.cost(i, j)
                    if total < best[0]:
                        best = (total, obstacle_id, j)
        if best[1] is None:
            break                # nothing reachable from here; the rest are dropped

        chosen = best[1]
        order.append(chosen)
        remaining.remove(chosen)
        # The greedy choice is made at the OBSTACLE level, as in slide 14, but
        # every pose of that obstacle is carried forward rather than committing
        # to the single cheapest one. Committing early is what makes a greedy
        # walk strand itself: it arrives at the pose that was cheapest to reach
        # and finds nothing reachable onward, even though a neighbouring pose on
        # the same face would have been fine.
        previous, current_nodes = current_nodes, {}
        for j in model.nodes_by_obstacle[chosen]:
            arrival = min((so_far + model.cost(i, j)
                           for i, so_far in previous.items()), default=INF)
            if arrival < INF:
                current_nodes[j] = arrival
        if not current_nodes:
            current_nodes = {best[2]: best[0]}

    return order


def _greedy_swap_order(model: CostModel, obstacle_ids: Sequence[int]) -> List[int]:
    """Nearest-neighbour, then 2-swaps until nothing improves (slide 15).

    "The nearest-neighbour algorithm returns a path SBCAD, try swapping BC, or
    CA or AD." Cheap insurance against a bad greedy first move, without paying
    for the full exhaustive search.
    """
    order = _nearest_order(model, obstacle_ids)
    best_cost, _ = model.evaluate_order(order)

    improved = True
    while improved:
        improved = False
        for a in range(len(order)):
            for b in range(a + 1, len(order)):
                candidate = list(order)
                candidate[a], candidate[b] = candidate[b], candidate[a]
                cost, _ = model.evaluate_order(candidate)
                if cost < best_cost - 1e-9:
                    order, best_cost, improved = candidate, cost, True
    return order


def _exhaustive_order(model: CostModel, obstacle_ids: Sequence[int]) -> List[int]:
    """Every ordering, keep the best -- the shortest-time path of slide 16.

    "Given that we have only 5 obstacles to visit, we can afford the cost of the
    exhaustive search." 5! = 120 orderings, each scored by the exact pose DP,
    which makes this optimal for the cost model. That is what B.3 asks for.

    If no complete tour exists -- one obstacle wedged where the robot can get in
    but not back out -- we drop to subsets of four, then three, and so on,
    rather than returning nothing. Checklist B.2 scores the images actually
    recognised, so four out of five beats giving up.

    With eight obstacles that is 8! = 40,320 orderings, and up to 8 x 7! more
    when no complete tour exists -- most of a slow plan's time. So two dynamic
    programmes over subsets come first: the cheapest way to have visited each
    set of obstacles, which gives the best cost at each size exactly, and the
    cheapest way to visit the rest of a set from each pose, which bounds any
    partial ordering from below. The orderings are then walked in the very same
    sequence as before, skipping every set and every prefix that cannot come
    within `_ORDER_SLACK` of the best cost. The first ordering at the best cost
    -- the one the plain search keeps -- is never skipped, so the answer is the
    same ordering, found from a few dozen candidates instead of thousands.
    """
    ids = list(obstacle_ids)
    count = len(ids)
    if not count:
        return []
    poses = [model.nodes_by_obstacle[oid] for oid in ids]
    cost = model._cost
    full = 1 << count

    def layer(current: Dict[int, float], k: int) -> Dict[int, float]:
        """One step of `evaluate_order`'s DP, done the same way."""
        nxt: Dict[int, float] = {}
        for j in poses[k]:
            best = INF
            for i, so_far in current.items():
                if so_far >= INF:
                    continue
                total = so_far + cost[i][j]
                if total < best:
                    best = total
            nxt[j] = best
        return nxt

    # arrive[mask][j]: cheapest cost of visiting exactly `mask`, ending on pose j.
    arrive: List[Dict[int, float]] = [{} for _ in range(full)]
    for mask in range(1, full):
        for k in range(count):
            bit = 1 << k
            if not mask & bit:
                continue
            rest_mask = mask ^ bit
            if not rest_mask:
                arrive[mask].update({j: cost[0][j] for j in poses[k]})
            else:
                arrive[mask].update(layer(arrive[rest_mask], k))

    # finish[mask][j]: cheapest cost of visiting exactly `mask` from pose j.
    finish: List[Dict[int, float]] = [{} for _ in range(full)]
    for mask in range(full):
        for owner in range(count):
            if mask & (1 << owner):
                continue
            for j in poses[owner]:
                if not mask:
                    finish[mask][j] = 0.0
                    continue
                best = INF
                for k in range(count):
                    bit = 1 << k
                    if mask & bit:
                        for nxt in poses[k]:
                            total = cost[j][nxt] + finish[mask ^ bit][nxt]
                            if total < best:
                                best = total
                finish[mask][j] = best

    def subset_best(mask: int) -> float:
        return min(arrive[mask].values(), default=INF)

    for size in range(count, 0, -1):
        masks = [sum(1 << k for k in combo) for combo in itertools.combinations(range(count), size)]
        floor = min(subset_best(mask) for mask in masks)
        if floor >= INF:
            continue
        limit = floor + _ORDER_SLACK
        best_order: List[int] = []
        best_cost = INF

        def walk(prefix: List[int], current: Dict[int, float], left: List[int]) -> None:
            nonlocal best_order, best_cost
            if not left:
                candidate = [ids[k] for k in prefix]
                total, _ = model.evaluate_order(candidate)
                if total < best_cost:
                    best_cost, best_order = total, candidate
                return
            left_mask = sum(1 << k for k in left)
            bound = min((so_far + finish[left_mask].get(j, INF)
                         for j, so_far in current.items()), default=INF)
            if bound > limit:
                return
            for position, k in enumerate(left):
                walk(prefix + [k], layer(current, k), left[:position] + left[position + 1:])

        for combo, mask in zip(itertools.combinations(range(count), size), masks):
            if subset_best(mask) > limit:
                continue
            for position, k in enumerate(combo):
                walk([k], {j: cost[0][j] for j in poses[k]},
                     list(combo[:position] + combo[position + 1:]))
        if best_order:
            return best_order
    return []


# Orderings costing more than the best by this much (seconds) cannot be the one
# the plain search keeps; it only absorbs floating-point rounding in the bound.
_ORDER_SLACK = 1e-6


_ORDERERS = {
    "nearest": _nearest_order,
    "greedy_swap": _greedy_swap_order,
    "exhaustive": _exhaustive_order,
}


# --------------------------------------------------------------------------
# Top level
# --------------------------------------------------------------------------


def plan_route(arena: Arena, strategy: str = "exhaustive",
               start: Optional[Pose] = None, metric: str = "time",
               model: Optional[CostModel] = None) -> Route:
    """Plan the full run: visit order, capture poses, and the drive between them.

    Obstacles that cannot be reached at all are reported in `Route.unreachable`
    rather than failing the whole plan. Checklist B.2 explicitly accepts a
    partial run -- "the number of images recognized within the time limit is
    accepted" -- so the robot should still go and photograph the four it can
    get to.
    """
    if strategy not in _ORDERERS:
        raise ValueError("strategy must be one of %s, got %r" % (list(STRATEGIES), strategy))

    model = model or CostModel(arena, start=start, metric=metric)
    route = Route(strategy=strategy, metric=metric)
    route.unreachable.extend(model.no_pose_obstacles)

    # The best tour the roadmap can support at all, which doubles as the test
    # for whether the roadmap is good enough. Only pay for the Hybrid A* search
    # if even an optimal ordering comes up short -- a *greedy* strategy dropping
    # an obstacle is the greedy strategy's own fault, not a hole in the roadmap,
    # and it is exactly the weakness B.3 exists to fix, so it should show up in
    # the comparison rather than be papered over.
    reachable = model.reachable_obstacles()
    optimal = _exhaustive_order(model, reachable)
    if len(optimal) < len(model.obstacle_ids):
        model.fill_gaps()
        reachable = model.reachable_obstacles()
        optimal = _exhaustive_order(model, reachable)
        # Reachable but still left out: search for a way on from it, or a way
        # in from the others, with whatever search time is left.
        dropped = [oid for oid in reachable if oid not in optimal]
        if dropped and model.reconnect(dropped):
            reachable = model.reachable_obstacles()
            optimal = _exhaustive_order(model, reachable)

    # Every strategy returns an order it can actually drive -- a partial one if
    # a complete tour is impossible -- so whatever it leaves out is reported
    # rather than silently lost.
    order = optimal if strategy == "exhaustive" else _ORDERERS[strategy](model, reachable)
    _, chain = model.evaluate_order(order)
    route.unreachable.extend(oid for oid in model.obstacle_ids if oid not in order)

    for position, node_index in enumerate(chain):
        source = 0 if position == 0 else chain[position - 1]
        entry = model.trajectory(source, node_index)
        if entry is None:                       # defensive: the DP said this was finite
            route.unreachable.append(order[position])
            continue
        method, trajectory = entry
        route.legs.append(Leg(obstacle_id=order[position], trajectory=trajectory,
                              method=method))

    anchor_legs(arena, route, model.start)
    route.unreachable = sorted(set(route.unreachable))
    return route


def anchor_legs(arena: Arena, route: Route, start: Pose) -> None:
    """Start every leg from where the previous leg's COMMANDS leave the robot.

    The STM takes whole centimetres and whole degrees, so each leg's commands
    stop a little off the planned capture pose (the heading stays within half a
    degree; the position can be ~1cm out). If the next leg is driven as
    planned from the exact pose, those errors pile up leg after leg -- the
    stress test saw 2-3cm by the end of a run, eaten straight out of the
    safety margin. So each leg is re-planned from the pose the robot really
    reaches, and the error never outlives one leg.

    The re-plan rejoins the original leg at one of its segment ends (the
    capture pose itself, or a transit pose on a multi-hop leg), with the same
    analytic moves `_plan_leg` uses and the same collision check; the cheapest
    rejoin wins. If none fits (in a cramped start zone, say), short Hybrid A*
    searches back onto the leg are tried; only if those fail too is the leg
    driven as planned from where the robot is, and its error carries on.
    """
    heading = commands_module.HeadingTracker()
    pose = start
    for index, leg in enumerate(route.legs):
        begin = leg.trajectory.start_pose()
        if (math.hypot(pose.x - begin.x, pose.y - begin.y) > 1e-6
                or abs(normalise_angle(pose.theta - begin.theta)) > 1e-9):
            rejoined = _rejoin(arena, pose, leg.trajectory, route.metric,
                               allow_backoff=index > 0)
            if rejoined is None:
                rejoined = _search_rejoin(arena, pose, leg.trajectory, allow_backoff=index > 0)
            if rejoined is not None:
                leg.method, leg.trajectory = rejoined
        driven = commands_module.leg_commands(leg.trajectory, heading, start.theta)
        if driven:
            leg.driven = commands_module.commands_to_trajectory(driven, pose)
            pose = leg.driven.end_pose()


def _rejoin(arena: Arena, pose: Pose, trajectory: Trajectory, metric: str,
            allow_backoff: bool) -> Optional[Tuple[str, Trajectory]]:
    """Cheapest collision-free way from `pose` onto `trajectory`, then along it."""
    segments = trajectory.segments
    best: Optional[Tuple[str, Trajectory]] = None
    best_cost = INF
    for k in range(len(segments)):
        result = _plan_leg(arena, pose, segments[k].end, allow_search=False,
                           allow_backoff=allow_backoff)
        if result is None:
            continue
        joined = Trajectory(merge_segments(result[1].segments + segments[k + 1:]))
        cost = leg_cost(joined, metric)
        if cost < best_cost:
            best, best_cost = ("rejoin-" + result[0], joined), cost
    return best


def _search_rejoin(arena: Arena, pose: Pose, trajectory: Trajectory,
                   allow_backoff: bool) -> Optional[Tuple[str, Trajectory]]:
    """`_rejoin`'s fallback: short Hybrid A* searches, onto the capture pose
    first (after the usual back-out tries), then onto any segment end."""
    direct = _plan_leg(arena, pose, trajectory.end_pose(), allow_search=True,
                       max_expansions=cfg.HA_MATRIX_EXPANSIONS, allow_backoff=allow_backoff)
    if direct is not None:
        return ("rejoin-" + direct[0], direct[1])
    segments = trajectory.segments
    ends = list(range(len(segments) - 1, -1, -1))        # the capture pose first
    found = hybrid_astar.plan_any(arena, pose, [segments[k].end for k in ends],
                                  max_expansions=cfg.HA_MATRIX_EXPANSIONS)
    if found is None:
        return None
    k = ends[found[0]]
    return ("rejoin-hybrid_astar", Trajectory(merge_segments(found[1].segments + segments[k + 1:])))


def compare_strategies(arena: Arena, start: Optional[Pose] = None,
                       metric: str = "time") -> Dict[str, Route]:
    """Run every strategy over one shared roadmap.

    This is what the simulator's comparison view calls to put B.2's greedy path
    and B.3's optimal path side by side on the same layout.
    """
    model = CostModel(arena, start=start, metric=metric)
    return {name: plan_route(arena, name, start=start, metric=metric, model=model)
            for name in STRATEGIES}
