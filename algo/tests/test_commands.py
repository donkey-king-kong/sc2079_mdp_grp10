"""STM command generation, and the round trip back to geometry.

The round-trip tests are the point. The command grammar is a convention nobody
upstream specifies, so the only way to know a field width or an L/R sense is
right is to drive a known trajectory out to strings, replay the strings, and
check the robot ends up where it started from.
"""

import math
import unittest

import conftest  # noqa: F401

import commands
import config as cfg
import planner
from arena import Arena, Obstacle, start_pose
from motion import BACKWARD, FORWARD, LEFT, RIGHT, STRAIGHT, Pose, Segment, Trajectory


class Formatting(unittest.TestCase):
    def test_straight_commands(self):
        pose = Pose(0.0, 0.0, 0.0)
        self.assertEqual(commands.segment_to_commands(
            Segment(FORWARD, STRAIGHT, 100.0, 25.0, pose)), ["SF100"])
        self.assertEqual(commands.segment_to_commands(
            Segment(BACKWARD, STRAIGHT, 25.0, 25.0, pose)), ["SB025"])

    def test_turn_commands_carry_degrees_not_arc_length(self):
        pose = Pose(0.0, 0.0, 0.0)
        quarter = math.pi / 2 * 25.0        # 39.3cm of arc, but a 90 degree turn
        self.assertEqual(commands.segment_to_commands(
            Segment(FORWARD, RIGHT, quarter, 25.0, pose)), ["RF090"])
        self.assertEqual(commands.segment_to_commands(
            Segment(BACKWARD, LEFT, quarter, 25.0, pose)), ["LB090"])

    def test_fields_are_zero_padded_to_the_agreed_width(self):
        pose = Pose(0.0, 0.0, 0.0)
        command = commands.segment_to_commands(Segment(FORWARD, STRAIGHT, 5.0, 25.0, pose))[0]
        self.assertEqual(len(command), 2 + cfg.COMMAND_NUM_WIDTH)
        self.assertEqual(command, "SF005")

    def test_oversized_values_clamp_rather_than_wrap(self):
        # A 1200cm run cannot fit a 3-digit field. Clamping gives 999; wrapping
        # would silently give 200 and drive the robot a sixth of the distance.
        pose = Pose(0.0, 0.0, 0.0)
        cfg_limit = 10 ** cfg.COMMAND_NUM_WIDTH - 1
        produced = commands.segment_to_commands(Segment(FORWARD, STRAIGHT, 1200.0, 25.0, pose))
        self.assertTrue(all(int(c[2:]) <= cfg_limit for c in produced))

    def test_negligible_segments_produce_nothing(self):
        pose = Pose(0.0, 0.0, 0.0)
        self.assertEqual(commands.segment_to_commands(
            Segment(FORWARD, STRAIGHT, 0.2, 25.0, pose)), [])
        self.assertEqual(commands.segment_to_commands(
            Segment(FORWARD, LEFT, 0.001, 25.0, pose)), [])

    def test_long_turns_split_into_firmware_sized_chunks(self):
        # Two arcs around the same circle merge into one segment, which can be a
        # 226-degree sweep; plenty of firmwares will not take that in one go.
        pose = Pose(0.0, 0.0, 0.0)
        produced = commands.segment_to_commands(
            Segment(FORWARD, RIGHT, math.radians(226) * 25.0, 25.0, pose))
        self.assertEqual(produced, ["RF180", "RF046"])
        self.assertAlmostEqual(sum(int(c[2:]) for c in produced), 226, delta=1)


class Parsing(unittest.TestCase):
    def test_round_trip_of_each_command_kind(self):
        for text, kind, value in [("SF100", "SF", 100.0), ("SB025", "SB", 25.0),
                                  ("LF090", "LF", 90.0), ("RB045", "RB", 45.0),
                                  ("SNAP3", "SNAP", 3.0)]:
            self.assertEqual(commands.parse(text), (kind, value))

    def test_finish_has_no_magnitude(self):
        self.assertEqual(commands.parse("FIN"), ("FIN", None))

    def test_snap_is_not_mistaken_for_a_drive_command(self):
        # "SNAP3" starts with "S"; longest-prefix matching has to win here.
        self.assertEqual(commands.parse("SNAP3")[0], cfg.CMD_SNAP)

    def test_garbage_is_rejected(self):
        for text in ("", "XX010", "SF", "SFABC"):
            with self.assertRaises(ValueError, msg=repr(text)):
                commands.parse(text)


class RoundTrip(unittest.TestCase):
    def replay(self, trajectory, start):
        return commands.commands_to_trajectory(
            commands.trajectory_to_commands(trajectory), start)

    def test_a_handmade_path_survives_the_round_trip(self):
        start = Pose(30.0, 30.0, math.pi / 2)
        first = Segment(FORWARD, STRAIGHT, 60.0, 25.0, start)
        second = Segment(FORWARD, RIGHT, math.pi / 2 * 25.0, 25.0, first.end)
        third = Segment(BACKWARD, LEFT, math.pi / 4 * 25.0, 25.0, second.end)
        original = Trajectory([first, second, third])

        replayed = self.replay(original, start)
        want, got = original.end_pose(), replayed.end_pose()
        self.assertAlmostEqual(got.x, want.x, delta=0.5)
        self.assertAlmostEqual(got.y, want.y, delta=0.5)
        self.assertAlmostEqual(got.theta, want.theta, delta=0.02)

    def test_a_planned_route_survives_the_round_trip(self):
        # The real check: rounding to whole centimetres and whole degrees must
        # not accumulate into a miss over a five-obstacle run.
        layout = [Obstacle(1, 60.0, 120.0, "S"), Obstacle(2, 140.0, 60.0, "W"),
                  Obstacle(3, 150.0, 150.0, "S")]
        route = planner.plan_route(Arena(layout), "exhaustive")
        pose = start_pose()
        for leg in route.legs:
            replayed = self.replay(leg.trajectory, pose)
            want, got = leg.trajectory.end_pose(), replayed.end_pose()
            drift = math.hypot(got.x - want.x, got.y - want.y)
            with self.subTest(obstacle=leg.obstacle_id):
                self.assertLess(drift, 2.0, "rounding drift of %.2fcm per leg" % drift)
            pose = want          # the robot is corrected at each photo


class RouteCommands(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        layout = [Obstacle(1, 60.0, 120.0, "S"), Obstacle(2, 140.0, 60.0, "W"),
                  Obstacle(3, 150.0, 150.0, "S")]
        cls.route = planner.plan_route(Arena(layout), "exhaustive")
        cls.commands = commands.route_to_commands(cls.route)

    def test_ends_with_the_finish_marker(self):
        self.assertEqual(self.commands[-1], cfg.CMD_FINISH)

    def test_one_snap_per_obstacle_in_visit_order(self):
        snaps = [int(c[len(cfg.CMD_SNAP):]) for c in self.commands
                 if c.startswith(cfg.CMD_SNAP)]
        self.assertEqual(snaps, self.route.order)

    def test_every_command_parses(self):
        for command in self.commands:
            commands.parse(command)      # raises if the grammar is inconsistent

    def test_describe_covers_every_command(self):
        text = commands.describe(self.commands)
        self.assertIn("photograph obstacle", text)
        self.assertTrue(text.endswith("finish"))


if __name__ == "__main__":
    unittest.main()
