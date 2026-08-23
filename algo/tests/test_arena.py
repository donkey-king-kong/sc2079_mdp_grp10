"""Arena model: where the robot may be, and where it must stand for a photo.

`test_slide_8_target_pose` is the anchor -- the briefing states the answer
explicitly, so this pins our capture geometry to the coursework's rather than to
whatever we happened to implement.
"""

import math
import random
import unittest

import conftest  # noqa: F401

import arena as arena_module
import config as cfg
from arena import Arena, Obstacle
from motion import Pose


class CapturePoses(unittest.TestCase):
    def test_slide_8_target_pose(self):
        # Briefing slide 8: "If an image is located at (a, b, S), we should
        # target the robot at (a-10, b-45, pi/2)" -- bottom-left corner form.
        a, b = 100.0, 100.0
        obstacle = Obstacle(1, a, b, "S")
        best = Arena([obstacle]).capture_poses(obstacle)[0]
        x, y, theta = arena_module.centre_to_bottom_left(best.pose)
        self.assertAlmostEqual(x, a - 10.0, places=6)
        self.assertAlmostEqual(y, b - 45.0, places=6)
        self.assertAlmostEqual(theta, math.pi / 2, places=6)

    def test_robot_faces_back_at_the_image_on_every_face(self):
        expected = {"N": -math.pi / 2, "S": math.pi / 2, "E": math.pi, "W": 0.0}
        for face, heading in expected.items():
            obstacle = Obstacle(1, 100.0, 100.0, face)
            pose = Arena([obstacle]).capture_poses(obstacle)[0].pose
            self.assertAlmostEqual(pose.theta, heading, places=6, msg="face %s" % face)

    def test_every_pose_is_a_legal_place_to_be(self):
        layout = [Obstacle(1, 100.0, 100.0, "N"), Obstacle(2, 40.0, 150.0, "E")]
        arena = Arena(layout)
        for obstacle in layout:
            for capture in arena.capture_poses(obstacle):
                self.assertTrue(arena.is_pose_free(capture.pose))

    def test_camera_distance_respects_checklist_a2(self):
        # Checklist A.2: the image sits 20-50cm from the robot's midpoint.
        obstacle = Obstacle(1, 100.0, 100.0, "W")
        fx, fy = obstacle.face_centre()
        for capture in Arena([obstacle]).capture_poses(obstacle):
            distance = math.hypot(capture.pose.x - fx, capture.pose.y - fy)
            self.assertGreaterEqual(distance, cfg.CAPTURE_MIN_DISTANCE - 1e-9)
            self.assertLessEqual(distance, cfg.CAPTURE_MAX_DISTANCE + 1e-9)

    def test_selection_spreads_across_approach_angles(self):
        # The whole point of `select_capture_poses`: taking the top N by rank
        # gives N poses on the same heading, which is nearly useless to a planner
        # whose reachability is decided by heading.
        obstacle = Obstacle(1, 100.0, 100.0, "N")
        arena = Arena([obstacle])
        chosen = arena.select_capture_poses(obstacle, 5)
        self.assertEqual(len(chosen), 5)
        self.assertEqual(len({c.angle for c in chosen}), 5, "angles should all differ")
        self.assertEqual(chosen[0].angle, 0.0, "the ideal head-on pose still comes first")

    def test_boxed_in_face_offers_nothing(self):
        # An obstacle in the corner with its image against the wall: there is
        # physically nowhere legal to stand, and we must say so rather than
        # inventing a pose inside the wall.
        obstacle = Obstacle(1, 0.0, 0.0, "W")
        self.assertEqual(Arena([obstacle]).capture_poses(obstacle), [])


class Collision(unittest.TestCase):
    """Briefing slide 36's virtual obstacles, with the robot treated as a point."""

    def setUp(self):
        self.obstacle = Obstacle(1, 100.0, 100.0, "N")
        self.arena = Arena([self.obstacle])

    def test_inside_the_virtual_obstacle_is_blocked(self):
        self.assertFalse(self.arena.is_point_free(105.0, 105.0))   # the block itself
        self.assertFalse(self.arena.is_point_free(90.0, 105.0))    # inflated skirt

    def test_just_outside_the_virtual_obstacle_is_free(self):
        # The box spans [85, 125] on each axis: 10cm block plus 15cm each side.
        self.assertTrue(self.arena.is_point_free(84.9, 105.0))
        self.assertTrue(self.arena.is_point_free(125.1, 105.0))

    def test_boundary_margin_keeps_the_footprint_inside_the_arena(self):
        margin = cfg.BOUNDARY_MARGIN
        self.assertTrue(self.arena.is_point_free(margin, margin))
        self.assertFalse(self.arena.is_point_free(margin - 0.1, margin))
        self.assertFalse(self.arena.is_point_free(cfg.ARENA_SIZE - margin + 0.1, 100.0))

    def test_trajectory_check_looks_at_the_whole_swept_path(self):
        from motion import FORWARD, STRAIGHT, Segment, Trajectory
        # A straight run whose endpoints are both clear but which passes right
        # through the obstacle must be rejected.
        through = Trajectory([Segment(FORWARD, STRAIGHT, 80.0, 25.0, Pose(60.0, 105.0, 0.0))])
        self.assertTrue(self.arena.is_point_free(60.0, 105.0))
        self.assertTrue(self.arena.is_point_free(140.0, 105.0))
        self.assertFalse(self.arena.is_trajectory_free(through))


class WireFormat(unittest.TestCase):
    def test_cell_units_are_the_default(self):
        obstacles = arena_module.parse_obstacles([{"id": 3, "x": 8, "y": 5, "face": "S"}])
        self.assertEqual((obstacles[0].x, obstacles[0].y), (80.0, 50.0))
        self.assertEqual(obstacles[0].id, 3)

    def test_cm_units_pass_straight_through(self):
        obstacles = arena_module.parse_obstacles(
            [{"id": 1, "x": 85.0, "y": 55.0, "face": "N"}], units="cm")
        self.assertEqual((obstacles[0].x, obstacles[0].y), (85.0, 55.0))

    def test_dir_is_accepted_as_well_as_face(self):
        # The RPi says "dir", the simulator says "face"; translating on their
        # behalf is cheaper than making them change.
        self.assertEqual(arena_module.parse_obstacles([{"x": 1, "y": 1, "dir": "w"}])[0].face, "W")

    def test_bad_face_is_rejected_loudly(self):
        with self.assertRaises(ValueError):
            arena_module.parse_obstacles([{"x": 1, "y": 1, "face": "Q"}])

    def test_ids_are_assigned_when_missing(self):
        obstacles = arena_module.parse_obstacles([{"x": 1, "y": 1, "face": "N"},
                                                  {"x": 5, "y": 5, "face": "S"}])
        self.assertEqual([o.id for o in obstacles], [1, 2])


class RandomLayouts(unittest.TestCase):
    def test_generated_layouts_are_legal_and_solvable(self):
        for seed in range(6):
            layout = arena_module.random_layout(rng=random.Random(seed))
            arena = Arena(layout)
            with self.subTest(seed=seed):
                self.assertEqual(len(layout), cfg.NUM_OBSTACLES)
                self.assertEqual(len({(o.x, o.y) for o in layout}), cfg.NUM_OBSTACLES)
                for obstacle in layout:
                    self.assertTrue(arena.capture_poses(obstacle),
                                    "obstacle %d has nowhere to shoot from" % obstacle.id)
                self.assertTrue(arena.is_pose_free(arena_module.start_pose()),
                                "the robot must be able to sit at the start")

    def test_obstacles_keep_clear_of_the_start_zone(self):
        # An obstacle abutting the start zone walls the robot in: it leaves a
        # band too narrow for a 25cm turning radius to turn round in.
        for seed in range(6):
            for obstacle in arena_module.random_layout(rng=random.Random(seed)):
                self.assertFalse(
                    obstacle.x - cfg.OBSTACLE_INFLATION < cfg.START_KEEP_CLEAR
                    and obstacle.y - cfg.OBSTACLE_INFLATION < cfg.START_KEEP_CLEAR)


if __name__ == "__main__":
    unittest.main()
