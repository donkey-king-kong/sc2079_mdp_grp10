"""Dubins geometry -- checked against the briefing's own worked example.

`test_slide_43_worked_example` is the important one: the briefing works an rsr
path by hand on slide 43 and prints the intermediate values, so reproducing them
to two decimal places proves the tangent construction is right rather than
merely self-consistent. If that test fails, the geometry is wrong, not the test.
"""

import math
import unittest

import conftest  # noqa: F401  (path setup)

import dubins
from motion import RIGHT, Pose, normalise_angle, turn_centre


class SlideFortyThree(unittest.TestCase):
    """Briefing slide 43: centre (10,10) facing N, target (90,90) facing E, r=20."""

    def setUp(self):
        self.start = Pose(10.0, 10.0, math.pi / 2)
        self.goal = Pose(90.0, 90.0, 0.0)
        self.radius = 20.0

    def test_turning_circle_centres(self):
        self.assertAlmostEqual(turn_centre(self.start, self.radius, RIGHT)[0], 30.0, places=6)
        self.assertAlmostEqual(turn_centre(self.start, self.radius, RIGHT)[1], 10.0, places=6)
        self.assertAlmostEqual(turn_centre(self.goal, self.radius, RIGHT)[0], 90.0, places=6)
        self.assertAlmostEqual(turn_centre(self.goal, self.radius, RIGHT)[1], 70.0, places=6)

    def test_slide_43_worked_example(self):
        candidates = dubins.plan_all(self.start, self.goal, self.radius)
        word, trajectory = candidates[0]
        self.assertEqual(word, "RSR", "slide 43 works the rsr path, and it is the shortest")

        first, straight, last = trajectory.segments
        # pt1 and pt2 from the slide. It prints pt1x as 15.85; the exact value is
        # 30 - 20*60/sqrt(7200) = 15.858, so the slide has rounded down.
        self.assertAlmostEqual(first.end.x, 15.86, places=2)
        self.assertAlmostEqual(first.end.y, 24.14, places=2)
        self.assertAlmostEqual(straight.end.x, 75.86, places=2)
        self.assertAlmostEqual(straight.end.y, 84.14, places=2)
        # |D| = 84.85 on the slide: the distance between the two circle centres,
        # which for an outer tangent is also the length of the straight run.
        self.assertAlmostEqual(straight.length, 84.85, places=2)
        self.assertEqual(last.steering, RIGHT)


class Correctness(unittest.TestCase):
    """Properties that must hold for every pose pair, not just the worked one."""

    PAIRS = [
        (Pose(20.0, 20.0, math.pi / 2), Pose(150.0, 150.0, 0.0)),
        (Pose(100.0, 100.0, 0.0), Pose(100.0, 100.0, math.pi)),
        (Pose(30.0, 170.0, -math.pi / 2), Pose(170.0, 30.0, math.pi / 2)),
        (Pose(50.0, 50.0, 1.0), Pose(60.0, 45.0, -2.0)),
        (Pose(80.0, 80.0, 0.3), Pose(85.0, 88.0, 2.9)),
    ]

    def test_every_candidate_lands_on_the_goal(self):
        # This is the real correctness check: integrate the arcs and straight
        # from the start and confirm the robot arrives at the goal pose. A sign
        # error in tangent selection cannot survive it.
        for start, goal in self.PAIRS:
            for word, trajectory in dubins.plan_all(start, goal, 25.0):
                end = trajectory.end_pose()
                with self.subTest(word=word, start=start, goal=goal):
                    self.assertAlmostEqual(end.x, goal.x, places=6)
                    self.assertAlmostEqual(end.y, goal.y, places=6)
                    self.assertAlmostEqual(normalise_angle(end.theta - goal.theta), 0.0, places=6)

    def test_candidates_are_sorted_shortest_first(self):
        for start, goal in self.PAIRS:
            lengths = [t.length for _, t in dubins.plan_all(start, goal, 25.0)]
            self.assertEqual(lengths, sorted(lengths))

    def test_length_matches_the_sampled_curve(self):
        # Guards against a segment whose declared length disagrees with the arc
        # it actually traces -- which would make every cost in the planner wrong.
        start, goal = self.PAIRS[0]
        _, trajectory = dubins.plan_all(start, goal, 25.0)[0]
        poses = trajectory.sample(0.5)
        walked = sum(math.hypot(b.x - a.x, b.y - a.y) for a, b in zip(poses, poses[1:]))
        self.assertAlmostEqual(walked, trajectory.length, delta=0.05)

    def test_ccc_appears_only_when_the_circles_are_close(self):
        # Slide 31: "The CCC path is only useful when C1 and C2 are very close,
        # i.e. the distance between them is less than 4r."
        close = dubins.plan_all(Pose(0.0, 0.0, 0.0), Pose(10.0, -5.0, math.pi), 25.0)
        self.assertTrue(any(w in ("RLR", "LRL") for w, _ in close))

        far = dubins.plan_all(Pose(0.0, 0.0, 0.0), Pose(180.0, 180.0, 0.0), 25.0)
        self.assertFalse(any(w in ("RLR", "LRL") for w, _ in far))

    def test_identical_poses_need_no_movement(self):
        # Regression: the CSC construction is degenerate here (the two turning
        # circles are exactly one diameter apart, so acos gets a value of 1),
        # and a sign slip used to turn "stay put" into two full 157cm circles.
        # The tolerance is 10 microns -- the residue is numerical, not motion.
        pose = Pose(100.0, 100.0, 0.5)
        self.assertAlmostEqual(dubins.shortest_length(pose, pose, 25.0), 0.0, delta=1e-3)

    def test_collision_callback_rejects_blocked_words(self):
        start, goal = Pose(20.0, 20.0, math.pi / 2), Pose(150.0, 150.0, 0.0)
        best = dubins.plan_all(start, goal, 25.0)[0][1]
        # Refuse every pose and nothing can be planned.
        self.assertIsNone(dubins.plan(start, goal, 25.0, lambda p: False))
        # Accept everything and we get the shortest word back.
        word, trajectory = dubins.plan(start, goal, 25.0, lambda p: True)
        self.assertAlmostEqual(trajectory.length, best.length, places=6)


if __name__ == "__main__":
    unittest.main()
