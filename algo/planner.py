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
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence, Tuple

import config as cfg
import dubins
import hybrid_astar
from arena import Arena, CapturePose, start_pose
from motion import BACKWARD, STRAIGHT, Pose, Segment, Trajectory, merge_segments

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


def _plan_leg(arena: Arena, source: Pose, target: Pose, allow_search: bool,
              max_expansions: int = cfg.HA_MAX_EXPANSIONS,
              allow_backoff: bool = True) -> Optional[Tuple[str, Trajectory]]:
    """One leg: back out of the capture pose, then Dubins; Hybrid A* as a last resort.

    The back-out is not an optimisation, it is a necessity. A capture pose sits
    30cm from an obstacle face pointing straight at it, and the turning radius
    is 25cm, so the tightest forward arc the robot can drive still ends up
    inside the block. Briefing slide 33 says as much: reverse first.

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
            reverse = Segment(BACKWARD, STRAIGHT, backoff, cfg.TURNING_RADIUS, source)
            if not all(arena.is_pose_free(p)
                       for p in reverse.iter_sample(cfg.COLLISION_SAMPLE_STEP)):
                break            # blocked behind: reversing further cannot help
            departure, prefix = reverse.end, [reverse]

        result = dubins.plan(departure, target, cfg.TURNING_RADIUS, arena.is_pose_free)
        if result is None:
            continue
        word, trajectory = result
        combined = Trajectory(merge_segments(prefix + trajectory.segments))
        return (word if not prefix else "SB+" + word, combined)

    if not allow_search:
        return None
    trajectory = hybrid_astar.plan(arena, source, target, max_expansions=max_expansions)
    return ("hybrid_astar", trajectory) if trajectory is not None else None


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
                    self._add(Node(index=len(self.nodes), pose=Pose(x, y, theta),
                                   kind="transit"))

        self.obstacle_ids: List[int] = list(self.nodes_by_obstacle.keys())

        size = len(self.nodes)
        self._cost: List[List[float]] = [[INF] * size for _ in range(size)]
        self._via: List[List[int]] = [[-1] * size for _ in range(size)]
        self._direct: Dict[Tuple[int, int], Tuple[str, Trajectory]] = {}
        self._gaps_filled = False
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
        One search per stranded pair -- a failed search has to exhaust its
        budget to prove the leg impossible, so we do not work down the menu.

        Bounded by wall clock as well as call count, because the two failure
        modes cost wildly different amounts and only the clock bounds the thing
        a person actually waits for. Whatever is reached in the time available
        is kept; anything still stranded is reported as unreachable.
        """
        if self._gaps_filled:
            return
        self._gaps_filled = True
        budget = SEARCH_BUDGET
        deadline = time.monotonic() + cfg.SEARCH_TIME_BUDGET

        for target_id in self.obstacle_ids:
            for source_index in self._representative_sources(target_id):
                if budget <= 0 or time.monotonic() > deadline:
                    break
                if self._pair_is_connected(source_index, target_id):
                    continue
                for target_index in self.nodes_by_obstacle[target_id][:2]:
                    if budget <= 0 or time.monotonic() > deadline:
                        break
                    budget -= 1
                    if self._solve(source_index, target_index, allow_search=True) < INF:
                        break

        self._close_transitively()      # new edges open up new multi-hop routes

    def _representative_sources(self, target_id: int) -> List[int]:
        """The start node, plus the best pose of every other obstacle."""
        sources = [0]
        for other_id, indices in self.nodes_by_obstacle.items():
            if other_id != target_id and indices:
                sources.append(indices[0])
        return sources

    def _pair_is_connected(self, source_index: int, target_id: int) -> bool:
        """Does any known leg run from this source into this obstacle?"""
        source_obstacle = self.nodes[source_index].obstacle_id
        sources = ([source_index] if source_obstacle is None
                   else self.nodes_by_obstacle[source_obstacle])
        return any(self._cost[i][j] < INF
                   for i in sources for j in self.nodes_by_obstacle[target_id])

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
    recognised, so four out of five beats giving up. Even at the worst size that
    is 325 permutations in total, which costs nothing.
    """
    ids = list(obstacle_ids)
    for size in range(len(ids), 0, -1):
        best_order: List[int] = []
        best_cost = INF
        for subset in itertools.combinations(ids, size):
            for candidate in itertools.permutations(subset):
                cost, _ = model.evaluate_order(candidate)
                if cost < best_cost:
                    best_cost, best_order = cost, list(candidate)
        if best_order:
            return best_order
    return []


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

    route.unreachable = sorted(set(route.unreachable))
    return route


def compare_strategies(arena: Arena, start: Optional[Pose] = None,
                       metric: str = "time") -> Dict[str, Route]:
    """Run every strategy over one shared roadmap.

    This is what the simulator's comparison view calls to put B.2's greedy path
    and B.3's optimal path side by side on the same layout.
    """
    model = CostModel(arena, start=start, metric=metric)
    return {name: plan_route(arena, name, start=start, metric=metric, model=model)
            for name in STRATEGIES}
