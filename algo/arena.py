"""The arena: obstacles, no-go regions, and where the robot should stand.

Two jobs live here.

1. **Collision checking.** Following briefing slide 36 we do not model the
   robot's rectangle at all. Instead every 10cm obstacle is inflated by half a
   robot footprint on each side into a 40cm "virtual obstacle", the walls are
   inset by the same 15cm, and the robot is treated as a single point at its
   centre. If the centre stays out of every virtual obstacle, the real 30x30
   robot cannot touch the real 10x10 block.

2. **Capture poses.** Each obstacle shows its image on one of N/S/E/W. The
   robot has to end up standing off that face, pointing back at it. The single
   ideal pose from slide 8 is frequently unreachable -- a 25cm turning radius
   next to a wall leaves no room -- so each obstacle publishes a *menu* of
   acceptable poses and the planner takes the first one it can actually reach.
"""

import math
import random
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Sequence, Set, Tuple

import config as cfg
from motion import Pose, face_to_heading, normalise_angle

FACES = ("N", "S", "E", "W")

# Outward unit normal of each obstacle face, and the tangent we slide along
# when trying laterally-offset capture poses.
_FACE_NORMAL = {"N": (0.0, 1.0), "S": (0.0, -1.0), "E": (1.0, 0.0), "W": (-1.0, 0.0)}
_FACE_TANGENT = {"N": (1.0, 0.0), "S": (1.0, 0.0), "E": (0.0, 1.0), "W": (0.0, 1.0)}


def bottom_left_to_centre(x: float, y: float, theta: float) -> Pose:
    """Briefing slide 7 gives robot poses by bottom-left corner; we use centres."""
    return Pose(x + cfg.ROBOT_HALF, y + cfg.ROBOT_HALF, normalise_angle(theta))


def centre_to_bottom_left(pose: Pose) -> Tuple[float, float, float]:
    return (pose.x - cfg.ROBOT_HALF, pose.y - cfg.ROBOT_HALF, pose.theta)


def cell_to_cm(cell: float) -> float:
    """Grid cell index (the Android 20x20 map) -> cm of its lower-left edge."""
    return cell * cfg.CELL_SIZE


def cm_to_cell(value: float) -> int:
    return int(math.floor(value / cfg.CELL_SIZE))


@dataclass(frozen=True)
class Obstacle:
    """A 10x10cm block with the target image on one face.

    `x`/`y` are the BOTTOM-LEFT corner in cm, matching the briefing. `face` is
    the side the image is on, so the robot must approach from that direction.
    """

    id: int
    x: float
    y: float
    face: str

    @property
    def centre(self) -> Tuple[float, float]:
        return (self.x + cfg.OBSTACLE_SIZE / 2.0, self.y + cfg.OBSTACLE_SIZE / 2.0)

    @property
    def image_heading(self) -> float:
        """Direction the image points, i.e. the face's outward normal."""
        return face_to_heading(self.face)

    def face_centre(self) -> Tuple[float, float]:
        """Midpoint of the face carrying the image."""
        cx, cy = self.centre
        nx, ny = _FACE_NORMAL[self.face]
        half = cfg.OBSTACLE_SIZE / 2.0
        return (cx + nx * half, cy + ny * half)

    def cell(self) -> Tuple[int, int]:
        return (cm_to_cell(self.x), cm_to_cell(self.y))

    def to_dict(self) -> Dict:
        cx, cy = self.cell()
        return {"id": self.id, "x": cx, "y": cy, "face": self.face,
                "x_cm": self.x, "y_cm": self.y}


@dataclass(frozen=True)
class CapturePose:
    """One acceptable place to stand to photograph an obstacle."""

    obstacle_id: int
    pose: Pose
    standoff: float     # cm from the obstacle face to the robot centre
    angle: float        # degrees off the face normal; 0 is dead in front
    rank: int           # 0 = the ideal pose from slide 8; higher = more of a compromise


class Arena:
    """Holds the obstacle layout and answers "can the robot be here?"."""

    def __init__(self, obstacles: Sequence[Obstacle]):
        self.obstacles: List[Obstacle] = list(obstacles)
        # Pre-compute the inflated no-go box for each obstacle: cheaper than
        # recomputing it for every one of the tens of thousands of collision
        # queries a single plan makes.
        self._blocked: List[Tuple[float, float, float, float]] = [
            (
                ob.x - cfg.OBSTACLE_INFLATION,
                ob.y - cfg.OBSTACLE_INFLATION,
                ob.x + cfg.OBSTACLE_SIZE + cfg.OBSTACLE_INFLATION,
                ob.y + cfg.OBSTACLE_SIZE + cfg.OBSTACLE_INFLATION,
            )
            for ob in self.obstacles
        ]
        self._min_xy = cfg.BOUNDARY_MARGIN
        self._max_xy = cfg.ARENA_SIZE - cfg.BOUNDARY_MARGIN

    # -- collision ---------------------------------------------------------

    def in_bounds(self, x: float, y: float) -> bool:
        """Is the robot's centre far enough from every wall?"""
        return self._min_xy <= x <= self._max_xy and self._min_xy <= y <= self._max_xy

    def is_point_free(self, x: float, y: float) -> bool:
        """Slide 36's test: robot as a dot against the 40x40 virtual obstacles."""
        if not self.in_bounds(x, y):
            return False
        for x0, y0, x1, y1 in self._blocked:
            if x0 < x < x1 and y0 < y < y1:
                return False
        return True

    def is_pose_free(self, pose: Pose) -> bool:
        return self.is_point_free(pose.x, pose.y)

    def is_trajectory_free(self, trajectory, step: float = cfg.COLLISION_SAMPLE_STEP) -> bool:
        return all(self.is_pose_free(p) for p in trajectory.iter_sample(step))

    # -- capture poses -----------------------------------------------------

    def capture_poses(self, obstacle: Obstacle) -> List[CapturePose]:
        """Every reachable pose from which this obstacle's image can be shot.

        The menu is an arc of positions around the face's midpoint: `standoff`
        centimetres away, `angle` degrees off the face normal, always turned to
        point back at the image. Slide 8's ideal pose is the one at 30cm and 0
        degrees, and it is always first.

        Ordered best-first by how much of a compromise each pose is, so the
        planner tries the well-aligned ones before the oblique ones. Poses that
        would sit inside a wall or another obstacle's virtual box are dropped
        here, so the planner never wastes a Dubins call on them.
        """
        fx, fy = obstacle.face_centre()
        outward = obstacle.image_heading

        scored = []
        for standoff in cfg.CAPTURE_STANDOFF_OPTIONS:
            for angle in cfg.CAPTURE_ANGLE_OPTIONS:
                penalty = (abs(angle) / cfg.CAPTURE_ANGLE_PENALTY_SCALE
                           + abs(standoff - cfg.CAPTURE_STANDOFF)
                           / cfg.CAPTURE_STANDOFF_PENALTY_SCALE)
                scored.append((penalty, standoff, angle))
        scored.sort(key=lambda item: (item[0], abs(item[2]), item[1]))

        results: List[CapturePose] = []
        for rank, (_penalty, standoff, angle) in enumerate(scored):
            bearing = normalise_angle(outward + math.radians(angle))
            x = fx + standoff * math.cos(bearing)
            y = fy + standoff * math.sin(bearing)
            if not self.is_point_free(x, y):
                continue
            # Checklist A.2 accepts the image 20-50cm from the robot's midpoint.
            if not (cfg.CAPTURE_MIN_DISTANCE <= standoff <= cfg.CAPTURE_MAX_DISTANCE):
                continue
            # Turn to face back down the bearing, at the image.
            heading = normalise_angle(bearing + math.pi)
            results.append(CapturePose(obstacle.id, Pose(x, y, heading),
                                       standoff, angle, rank))
        return results

    def select_capture_poses(self, obstacle: Obstacle, count: int) -> List[CapturePose]:
        """A small, *angularly diverse* subset of the capture menu.

        Taking the best `count` poses by rank looks reasonable and is a trap:
        the ranking prefers a straight-on view, so the top handful are all the
        same approach heading at slightly different standoffs. Whether a Dubins
        path exists depends almost entirely on the approach heading, so that
        subset is nearly useless to the planner -- an obstacle in a 20cm-tall
        strip under the top wall cannot be entered head-on at any standoff, but
        can be entered at 45 degrees.

        So: take the best pose at each distinct approach angle first, then spend
        whatever is left on the next-best poses overall.
        """
        menu = self.capture_poses(obstacle)
        chosen: List[CapturePose] = []
        seen_angles = set()
        for pose in menu:
            if len(chosen) >= count:
                break
            if pose.angle not in seen_angles:
                seen_angles.add(pose.angle)
                chosen.append(pose)
        for pose in menu:
            if len(chosen) >= count:
                break
            if pose not in chosen:
                chosen.append(pose)
        return chosen

    def all_capture_poses(self) -> Dict[int, List[CapturePose]]:
        return {ob.id: self.capture_poses(ob) for ob in self.obstacles}

    def obstacle_by_id(self, obstacle_id: int) -> Optional[Obstacle]:
        for ob in self.obstacles:
            if ob.id == obstacle_id:
                return ob
        return None

    # -- serialisation -----------------------------------------------------

    def to_dict(self) -> Dict:
        return {
            "size": cfg.ARENA_SIZE,
            "cell_size": cfg.CELL_SIZE,
            "obstacles": [ob.to_dict() for ob in self.obstacles],
        }


# --------------------------------------------------------------------------
# Building an Arena from the wire format
# --------------------------------------------------------------------------


def parse_obstacles(raw: Iterable[Dict], units: str = "cell") -> List[Obstacle]:
    """Turn the JSON the RPi/Android sends into `Obstacle`s.

    `units="cell"` (the default) reads `x`/`y` as indices into the 20x20 grid
    the Android map draws, which is what the tablet actually sends.
    `units="cm"` reads them as the obstacle's bottom-left corner in cm.

    The face key may be `face` or `dir`, since the two subsystems name it
    differently; both are accepted rather than making the RPi translate.
    """
    obstacles: List[Obstacle] = []
    for index, item in enumerate(raw):
        face = str(item.get("face", item.get("dir", "N"))).upper()[:1]
        if face not in FACES:
            raise ValueError("obstacle %r has face %r, expected one of N/S/E/W"
                             % (item.get("id", index), item.get("face", item.get("dir"))))
        x, y = float(item["x"]), float(item["y"])
        if units == "cell":
            x, y = cell_to_cm(x), cell_to_cm(y)
        elif units != "cm":
            raise ValueError("units must be 'cell' or 'cm', got %r" % (units,))
        obstacles.append(Obstacle(id=int(item.get("id", index + 1)), x=x, y=y, face=face))
    return obstacles


def start_pose() -> Pose:
    return Pose(cfg.START_X, cfg.START_Y, cfg.START_THETA)


def reachable_region(arena: "Arena", origin: Pose,
                     resolution: float = 5.0) -> Set[Tuple[int, int]]:
    """Flood-fill of the free cells the robot's centre can reach from `origin`.

    Ignores the turning radius, so it is an optimistic test -- but it is exact
    about walls and virtual obstacles, which is what actually strands a robot.
    Used to reject random layouts that are not solvable at all.
    """
    n = int(math.ceil(cfg.ARENA_SIZE / resolution))

    def free(cx: int, cy: int) -> bool:
        return (0 <= cx < n and 0 <= cy < n
                and arena.is_point_free((cx + 0.5) * resolution, (cy + 0.5) * resolution))

    start = (int(origin.x // resolution), int(origin.y // resolution))
    if not free(*start):
        return set()

    seen = {start}
    stack = [start]
    while stack:
        cx, cy = stack.pop()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            cell = (cx + dx, cy + dy)
            if cell not in seen and free(*cell):
                seen.add(cell)
                stack.append(cell)
    return seen


def _ideal_pose_in_bounds(obstacle: Obstacle) -> bool:
    """Is slide 8's ideal capture pose actually inside the arena?

    An obstacle three cells from the right-hand wall with its image facing East
    pushes the ideal pose past the boundary, leaving only a sliver of oblique,
    wall-hugging poses the robot can barely steer into. The real arena never
    does this -- briefing slide 5: "No image will be in a location not
    accessible by the robot" -- so neither should a generated one.
    """
    fx, fy = obstacle.face_centre()
    nx, ny = _FACE_NORMAL[obstacle.face]
    return (cfg.BOUNDARY_MARGIN <= fx + nx * cfg.CAPTURE_STANDOFF <= cfg.ARENA_SIZE - cfg.BOUNDARY_MARGIN
            and cfg.BOUNDARY_MARGIN <= fy + ny * cfg.CAPTURE_STANDOFF <= cfg.ARENA_SIZE - cfg.BOUNDARY_MARGIN)


def _blocks_start_zone(obstacle: Obstacle) -> bool:
    """Does this obstacle's virtual box crowd the start zone?"""
    return (obstacle.x - cfg.OBSTACLE_INFLATION < cfg.START_KEEP_CLEAR
            and obstacle.y - cfg.OBSTACLE_INFLATION < cfg.START_KEEP_CLEAR)


def _start_can_escape(arena: "Arena") -> bool:
    """Can the robot actually drive out of the start pose?

    The flood fill below treats the robot as a point, so it happily reports a
    10cm-tall corridor as reachable -- but a car with a 25cm turning radius
    cannot turn round in one, and the real robot would be stuck on the spot.
    This asks the question properly, by trying to plan a real path to a spread
    of poses around the arena.
    """
    import dubins           # local import: dubins has no need to know about arenas
    origin = start_pose()
    for x in (60.0, 100.0, 140.0):
        for y in (60.0, 100.0, 140.0):
            if not arena.is_point_free(x, y):
                continue
            for theta in (0.0, math.pi / 2, math.pi, -math.pi / 2):
                if dubins.plan(origin, Pose(x, y, theta), cfg.TURNING_RADIUS,
                               arena.is_pose_free) is not None:
                    return True
    return False


def _layout_is_solvable(obstacles: Sequence[Obstacle], resolution: float = 5.0) -> bool:
    """Can the robot reach a capture pose for every obstacle from the start?"""
    arena = Arena(obstacles)
    if not _start_can_escape(arena):
        return False
    region = reachable_region(arena, start_pose(), resolution)
    if not region:
        return False
    for obstacle in obstacles:
        poses = arena.capture_poses(obstacle)
        if not poses:
            return False
        if not any((int(p.pose.x // resolution), int(p.pose.y // resolution)) in region
                   for p in poses):
            return False
    return True


def random_layout(count: int = cfg.NUM_OBSTACLES,
                  rng: Optional[random.Random] = None,
                  max_attempts: int = 400) -> List[Obstacle]:
    """A random but *legal and solvable* obstacle layout, for demoing.

    Three things make a layout unusable, and all three are rejected here:

    * an obstacle crowding the start zone, or close enough to a wall that its
      image faces into one -- there is nowhere legal to stand, or no room to
      drive away afterwards;
    * two obstacles closer than 40cm centre-to-centre. Their 40cm virtual boxes
      would merge into one wall, which is briefing slide 34's "a straight line
      path needs a 30cm width between two obstacles";
    * a layout that passes both of those and is still not solvable, because the
      obstacles between them fence off a corner of the arena.

    The last check is a flood fill, so what comes back is always something the
    planner can actually plan a full five-obstacle run for.
    """
    rng = rng or random.Random()
    # 40cm centre-to-centre, i.e. 4 cells, is the point at which two virtual
    # obstacles stop overlapping.
    min_separation_cells = int(round(cfg.VIRTUAL_OBSTACLE_SIZE / cfg.CELL_SIZE))

    for _ in range(max_attempts):
        chosen: List[Obstacle] = []
        cells: List[Tuple[int, int]] = []
        for _try in range(300):
            if len(chosen) == count:
                break
            # Skip the outermost ring: an obstacle there facing outwards has
            # its capture pose inside the wall.
            cx = rng.randrange(1, cfg.GRID_CELLS - 1)
            cy = rng.randrange(1, cfg.GRID_CELLS - 1)
            if any(abs(cx - ox) < min_separation_cells and abs(cy - oy) < min_separation_cells
                   for ox, oy in cells):
                continue
            candidate = Obstacle(len(chosen) + 1, cell_to_cm(cx), cell_to_cm(cy),
                                 rng.choice(FACES))
            if not _ideal_pose_in_bounds(candidate) or _blocks_start_zone(candidate):
                continue
            cells.append((cx, cy))
            chosen.append(candidate)

        if len(chosen) == count and _layout_is_solvable(chosen):
            return chosen

    raise RuntimeError("could not generate a legal layout of %d obstacles" % count)
