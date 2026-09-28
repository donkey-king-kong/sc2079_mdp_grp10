"""Dubins geometry -- checked against the briefing's own worked example.

`test_slide_43_worked_example` is the important one: the briefing works an rsr
path by hand on slide 43 and prints the intermediate values, so reproducing them
to two decimal places proves the tangent construction is right rather than
merely self-consistent. If that test fails, the geometry is wrong, not the test.
"""

import math
import unittest

import conftest  # noqa: F401  (path setup)

import config as cfg
import dubins
from motion import LEFT, RIGHT, Pose, normalise_angle, turn_centre


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


class AsymmetricSteering(unittest.TestCase):
    """Our robot turns 20cm left and 36cm right; the geometry must honour both.

    The briefing only ever works a symmetric radius, so there is no slide to
    check against. What pins this down instead is the same integration check as
    above -- every candidate must drive from start to goal -- plus a check that
    each arc really is on its own side's circle.
    """

    RADII = (20.0, 36.0)
    PAIRS = Correctness.PAIRS + [
        (Pose(100.0, 100.0, 0.0), Pose(100.0, 100.0, 0.0)),
        (Pose(40.0, 100.0, 0.0), Pose(160.0, 100.0, math.pi)),
        (Pose(100.0, 100.0, math.pi / 2), Pose(130.0, 100.0, -math.pi / 2)),
        (Pose(100.0, 100.0, math.pi / 2), Pose(70.0, 100.0, -math.pi / 2)),
    ]

    def test_every_candidate_lands_on_the_goal(self):
        for start, goal in self.PAIRS:
            candidates = dubins.plan_all(start, goal, self.RADII)
            with self.subTest(start=start, goal=goal):
                self.assertTrue(candidates, "some word must always connect two poses")
            for word, trajectory in candidates:
                end = trajectory.end_pose()
                with self.subTest(word=word, start=start, goal=goal):
                    self.assertAlmostEqual(end.x, goal.x, places=6)
                    self.assertAlmostEqual(end.y, goal.y, places=6)
                    self.assertAlmostEqual(normalise_angle(end.theta - goal.theta), 0.0, places=6)

    def test_every_word_is_constructed_somewhere(self):
        # The mixed-radius cases (LSR, RSL, and both CCC words) are where a
        # generalisation would go wrong, so they must actually be exercised.
        seen = {word for start, goal in self.PAIRS
                for word, _ in dubins.plan_all(start, goal, self.RADII)}
        self.assertEqual(seen, set(dubins.WORDS))

    def test_each_arc_is_on_its_own_sides_circle(self):
        left, right = self.RADII
        for start, goal in self.PAIRS:
            for word, trajectory in dubins.plan_all(start, goal, self.RADII):
                for segment in trajectory.segments:
                    with self.subTest(word=word, steering=segment.steering):
                        if segment.steering == LEFT:
                            self.assertEqual(segment.radius, left)
                        elif segment.steering == RIGHT:
                            self.assertEqual(segment.radius, right)

    def test_length_matches_the_sampled_curve(self):
        for start, goal in self.PAIRS[:5]:
            for word, trajectory in dubins.plan_all(start, goal, self.RADII):
                poses = trajectory.sample(0.5)
                walked = sum(math.hypot(b.x - a.x, b.y - a.y) for a, b in zip(poses, poses[1:]))
                with self.subTest(word=word, start=start, goal=goal):
                    self.assertAlmostEqual(walked, trajectory.length, delta=0.05)

    def test_equal_radii_reduce_to_the_symmetric_construction(self):
        # A (25, 25) pair must give exactly what the briefing's single radius
        # gives -- the generalisation adds nothing when the sides agree.
        for start, goal in Correctness.PAIRS:
            symmetric = [(w, round(t.length, 9)) for w, t in dubins.plan_all(start, goal, 25.0)]
            paired = [(w, round(t.length, 9)) for w, t in dubins.plan_all(start, goal, (25.0, 25.0))]
            self.assertEqual(symmetric, paired)

    def test_a_u_turn_prefers_the_tight_side(self):
        # Reversing heading 60cm to the right is a right-hand U-turn on a 36cm
        # circle -- too wide to fit, so it needs a detour -- while 40cm to the
        # left is exactly one left-hand half circle. The asymmetry must show.
        start = Pose(100.0, 100.0, math.pi / 2)
        to_left = dubins.shortest_length(start, Pose(60.0, 100.0, -math.pi / 2), self.RADII)
        to_right = dubins.shortest_length(start, Pose(140.0, 100.0, -math.pi / 2), self.RADII)
        self.assertAlmostEqual(to_left, math.pi * 20.0, places=6)
        self.assertGreater(to_right, to_left)

    def test_default_radius_is_the_calibrated_robot(self):
        start, goal = Correctness.PAIRS[0]
        configured = dubins.plan_all(start, goal, (cfg.TURNING_RADIUS_LEFT, cfg.TURNING_RADIUS_RIGHT))
        default = dubins.plan_all(start, goal)
        self.assertEqual([(w, t.length) for w, t in configured],
                         [(w, t.length) for w, t in default])


if __name__ == "__main__":
    unittest.main()
