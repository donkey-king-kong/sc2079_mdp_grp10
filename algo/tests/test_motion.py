"""Kinematics and trajectory bookkeeping.

Everything downstream trusts `Segment.pose_at` to say where the robot ends up,
so if these are wrong the Dubins tests will still pass (they are self-consistent)
while the robot drives somewhere else entirely.
"""

import math
import unittest

import conftest  # noqa: F401

import config as cfg
import motion
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


class Angles(unittest.TestCase):
    def test_normalise_lands_in_the_half_open_interval(self):
        for raw in (0.0, math.pi, -math.pi, 3 * math.pi, -3 * math.pi, 7.5, -7.5):
            value = normalise_angle(raw)
            self.assertTrue(-math.pi < value <= math.pi, "%r -> %r" % (raw, value))

    def test_pi_maps_to_positive_pi(self):
        # -pi and +pi are the same heading; we pick +pi so West is unambiguous.
        self.assertAlmostEqual(normalise_angle(-math.pi), math.pi, places=12)

    def test_compass_round_trip(self):
        for face in ("N", "S", "E", "W"):
            self.assertEqual(motion.heading_to_face(motion.face_to_heading(face)), face)

    def test_face_rejects_nonsense(self):
        with self.assertRaises(ValueError):
            motion.face_to_heading("Q")


class Kinematics(unittest.TestCase):
    def test_straight_forward(self):
        end = Segment(FORWARD, STRAIGHT, 50.0, 25.0, Pose(10.0, 10.0, 0.0)).end
        self.assertAlmostEqual(end.x, 60.0)
        self.assertAlmostEqual(end.y, 10.0)
        self.assertAlmostEqual(end.theta, 0.0)

    def test_straight_backward_reverses_along_the_heading(self):
        end = Segment(BACKWARD, STRAIGHT, 50.0, 25.0, Pose(60.0, 10.0, 0.0)).end
        self.assertAlmostEqual(end.x, 10.0)
        self.assertAlmostEqual(end.y, 10.0)
        self.assertAlmostEqual(end.theta, 0.0, msg="reversing must not change the heading")

    def test_quarter_turn_right_from_north(self):
        # Facing North at (10,10), a 90-degree right turn at r=25 ends facing
        # East at (35,35): the robot swings a quarter of its turning circle.
        quarter = math.pi / 2 * 25.0
        end = Segment(FORWARD, RIGHT, quarter, 25.0, Pose(10.0, 10.0, math.pi / 2)).end
        self.assertAlmostEqual(end.x, 35.0, places=9)
        self.assertAlmostEqual(end.y, 35.0, places=9)
        self.assertAlmostEqual(end.theta, 0.0, places=9)

    def test_reversing_round_a_left_circle_swings_the_nose_right(self):
        # The distinction that catches people out: LEFT is where the steering
        # points, not where the nose goes. Reversing on left lock turns the
        # heading clockwise.
        quarter = math.pi / 2 * 25.0
        segment = Segment(BACKWARD, LEFT, quarter, 25.0, Pose(100.0, 100.0, 0.0))
        self.assertAlmostEqual(segment.turn_angle, -math.pi / 2, places=9)

    def test_pose_at_is_continuous_and_clamped(self):
        segment = Segment(FORWARD, LEFT, 40.0, 25.0, Pose(50.0, 50.0, 0.4))
        self.assertEqual(segment.pose_at(0.0).as_tuple(), segment.start.as_tuple())
        self.assertEqual(segment.pose_at(999.0).as_tuple(), segment.end.as_tuple())

    def test_sampling_traces_the_declared_length(self):
        segment = Segment(FORWARD, RIGHT, 90.0, 25.0, Pose(80.0, 80.0, 1.0))
        poses = [segment.start] + segment.sample(0.5)
        walked = sum(math.hypot(b.x - a.x, b.y - a.y) for a, b in zip(poses, poses[1:]))
        self.assertAlmostEqual(walked, segment.length, delta=0.01)


class Merging(unittest.TestCase):
    def test_consecutive_like_segments_fuse(self):
        start = Pose(0.0, 0.0, 0.0)
        first = Segment(FORWARD, STRAIGHT, 10.0, 25.0, start)
        second = Segment(FORWARD, STRAIGHT, 15.0, 25.0, first.end)
        merged = merge_segments([first, second])
        self.assertEqual(len(merged), 1)
        self.assertAlmostEqual(merged[0].length, 25.0)
        # The fused segment must end exactly where the pair did.
        self.assertAlmostEqual(merged[0].end.x, second.end.x, places=9)

    def test_different_steering_is_not_fused(self):
        start = Pose(0.0, 0.0, 0.0)
        first = Segment(FORWARD, STRAIGHT, 10.0, 25.0, start)
        second = Segment(FORWARD, LEFT, 10.0, 25.0, first.end)
        self.assertEqual(len(merge_segments([first, second])), 2)

    def test_zero_length_segments_are_dropped(self):
        start = Pose(0.0, 0.0, 0.0)
        real = Segment(FORWARD, STRAIGHT, 12.0, 25.0, start)
        nothing = Segment(FORWARD, LEFT, 0.0, 25.0, real.end)
        merged = merge_segments([real, nothing])
        self.assertEqual(len(merged), 1)
        self.assertAlmostEqual(merged[0].length, 12.0)

    def test_opposing_straights_collapse_to_the_net_motion(self):
        # Stitching two legs together produces "creep forward 5cm, reverse
        # 30cm" at the joint. That is one 25cm reverse, and emitting it as two
        # commands wastes a real accelerate-and-stop cycle on the robot.
        start = Pose(100.0, 100.0, 0.0)
        forward = Segment(FORWARD, STRAIGHT, 5.0, 25.0, start)
        backward = Segment(BACKWARD, STRAIGHT, 30.0, 25.0, forward.end)
        merged = merge_segments([forward, backward])
        self.assertEqual(len(merged), 1)
        self.assertEqual(merged[0].gear, BACKWARD)
        self.assertAlmostEqual(merged[0].length, 25.0)

    def test_cancelling_preserves_the_end_pose(self):
        start = Pose(100.0, 100.0, 0.0)
        forward = Segment(FORWARD, STRAIGHT, 5.0, 25.0, start)
        backward = Segment(BACKWARD, STRAIGHT, 30.0, 25.0, forward.end)
        turn = Segment(FORWARD, LEFT, 20.0, 25.0, backward.end)
        want = turn.end
        got = Trajectory(merge_segments([forward, backward, turn])).end_pose()
        self.assertAlmostEqual(got.x, want.x, places=9)
        self.assertAlmostEqual(got.y, want.y, places=9)
        self.assertAlmostEqual(got.theta, want.theta, places=9)

    def test_exact_cancellation_keeps_the_pose(self):
        # Forward 20 then back 20 is no net motion, but the trajectory still has
        # to be able to say where the robot is.
        start = Pose(100.0, 100.0, 0.3)
        out = Segment(FORWARD, STRAIGHT, 20.0, 25.0, start)
        back = Segment(BACKWARD, STRAIGHT, 20.0, 25.0, out.end)
        merged = merge_segments([out, back])
        self.assertAlmostEqual(Trajectory(merged).length, 0.0)
        self.assertAlmostEqual(Trajectory(merged).end_pose().x, start.x, places=9)


class Timing(unittest.TestCase):
    """The time model is what makes B.3 shortest-*time* rather than shortest-path."""

    def test_turning_is_slower_per_centimetre_than_driving_straight(self):
        start = Pose(100.0, 100.0, 0.0)
        straight = Trajectory([Segment(FORWARD, STRAIGHT, 60.0, 25.0, start)])
        turning = Trajectory([Segment(FORWARD, LEFT, 60.0, 25.0, start)])
        self.assertAlmostEqual(straight.length, turning.length)
        self.assertLess(straight.duration(), turning.duration())

    def test_switching_direction_costs_time(self):
        start = Pose(100.0, 100.0, 0.0)
        one = Segment(FORWARD, STRAIGHT, 30.0, 25.0, start)
        two = Segment(BACKWARD, STRAIGHT, 30.0, 25.0, one.end)
        together = Trajectory([one, two]).duration()
        apart = Trajectory([one]).duration() + Trajectory([two]).duration()
        self.assertAlmostEqual(together - apart, cfg.DIRECTION_CHANGE_TIME, places=9)


if __name__ == "__main__":
    unittest.main()
