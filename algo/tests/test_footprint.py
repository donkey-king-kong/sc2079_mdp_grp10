"""Collision checking on the robot's real, rotated outline.

`test_planned_paths_keep_the_margin_between_samples` is the one that matters:
it re-checks planned paths far more finely than the planner does and confirms
SWEEP_PAD really does keep SAFETY_MARGIN along the whole sweep.
"""

import math
import unittest

import conftest  # noqa: F401

import config as cfg
import footprint
import planner
from arena import Arena, Obstacle
from motion import Pose, pose_from_footprint_centre

BLOCK = (100.0, 100.0, 110.0, 110.0)

LAYOUT = [
    Obstacle(1, 60.0, 120.0, "S"),
    Obstacle(2, 140.0, 60.0, "W"),
    Obstacle(3, 150.0, 150.0, "S"),
    Obstacle(4, 60.0, 60.0, "E"),
    Obstacle(5, 100.0, 170.0, "S"),
]


class Outline(unittest.TestCase):
    def test_outline_is_measured_from_the_rear_axle(self):
        corners = footprint.outline(Pose(0.0, 0.0, 0.0))
        xs = [x for x, _ in corners]
        ys = [y for _, y in corners]
        self.assertAlmostEqual(max(xs), cfg.ROBOT_FRONT)
        self.assertAlmostEqual(min(xs), -cfg.ROBOT_REAR)
        self.assertAlmostEqual(max(ys) - min(ys), cfg.ROBOT_WIDTH)

    def test_clearance_facing_a_block(self):
        # Facing North, the nose 5cm below the block's south face.
        pose = Pose(105.0, 100.0 - 5.0 - cfg.ROBOT_FRONT, math.pi / 2)
        self.assertAlmostEqual(footprint.box_clearance(pose, BLOCK), 5.0, places=9)

    def test_overlap_is_negative(self):
        pose = Pose(105.0, 100.0 + 2.0 - cfg.ROBOT_FRONT, math.pi / 2)
        self.assertAlmostEqual(footprint.box_clearance(pose, BLOCK), -2.0, places=9)

    def test_wall_clearance(self):
        pose = Pose(100.0, 10.0, math.pi / 2)       # rear edge 10 - 3.35 above y = 0
        self.assertAlmostEqual(footprint.wall_clearance(pose), 10.0 - cfg.ROBOT_REAR, places=9)


class RotatedCheck(unittest.TestCase):
    def setUp(self):
        self.arena = Arena([Obstacle(1, 100.0, 100.0, "N")])

    def test_rotating_on_the_spot_can_hit_a_block(self):
        # The body's middle 17cm left of the block. Facing East, the nose is
        # 4.3cm clear; turned 45 degrees, a front corner swings to within 0.5cm.
        # The old point-and-inflated-box test could not tell these apart.
        self.assertTrue(self.arena.is_point_free(83.0, 105.0))
        self.assertTrue(self.arena.is_pose_free(pose_from_footprint_centre(83.0, 105.0, 0.0)))
        self.assertFalse(self.arena.is_pose_free(pose_from_footprint_centre(83.0, 105.0, math.pi / 4)))

    def test_margin_applies_to_walls(self):
        just_clear = cfg.ROBOT_REAR + cfg.SAFETY_MARGIN + cfg.SWEEP_PAD + 0.01
        self.assertTrue(self.arena.is_pose_free(Pose(50.0, just_clear, math.pi / 2)))
        self.assertFalse(self.arena.is_pose_free(Pose(50.0, just_clear - 0.1, math.pi / 2)))


class Sweep(unittest.TestCase):
    def test_planned_paths_keep_the_margin_between_samples(self):
        arena = Arena(LAYOUT)
        route = planner.plan_route(arena, "exhaustive")
        self.assertTrue(route.legs)
        boxes = [(ob.x, ob.y, ob.x + cfg.OBSTACLE_SIZE, ob.y + cfg.OBSTACLE_SIZE)
                 for ob in LAYOUT]
        worst = math.inf
        for leg in route.legs:
            for segment in leg.trajectory.segments:
                # 20x finer than the planner samples, and no angle cap: this
                # is the continuous sweep as near as makes no difference.
                n = max(1, int(math.ceil(segment.length / 0.05)))
                for i in range(n + 1):
                    pose = segment.pose_at(segment.length * i / n)
                    worst = min(worst, footprint.wall_clearance(pose),
                                *(footprint.box_clearance(pose, box) for box in boxes))
        self.assertGreaterEqual(worst, cfg.SAFETY_MARGIN - 1e-6)


if __name__ == "__main__":
    unittest.main()
