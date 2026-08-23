"""Route planning: the invariants B.2 and B.3 are actually graded on.

The important ones are `test_exhaustive_is_never_worse_than_greedy` (that is the
entire claim B.3 makes over B.2) and `test_every_leg_is_collision_free` (a plan
that clips an obstacle is worse than no plan).
"""

import math
import random
import unittest

import conftest  # noqa: F401

import config as cfg
import planner
from arena import Arena, Obstacle, random_layout, start_pose

# A fixed, open layout. Deliberately not random, so a failure here is always the
# same failure and can be debugged.
LAYOUT = [
    Obstacle(1, 60.0, 120.0, "S"),
    Obstacle(2, 140.0, 60.0, "W"),
    Obstacle(3, 150.0, 150.0, "S"),
    Obstacle(4, 60.0, 60.0, "E"),
    Obstacle(5, 100.0, 170.0, "S"),
]


class Routes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.arena = Arena(LAYOUT)
        # One shared cost model: building it is the expensive part and it does
        # not depend on the strategy.
        cls.model = planner.CostModel(cls.arena)

    def route(self, strategy):
        return planner.plan_route(self.arena, strategy, model=self.model)

    def test_every_strategy_visits_each_obstacle_exactly_once(self):
        for strategy in planner.STRATEGIES:
            order = self.route(strategy).order
            with self.subTest(strategy=strategy):
                self.assertEqual(sorted(order), [1, 2, 3, 4, 5])

    def test_exhaustive_is_never_worse_than_greedy(self):
        # This is the whole of B.3's claim over B.2. Exhaustive searches every
        # ordering the greedy could have picked, so it cannot come out behind.
        greedy = self.route("nearest")
        best = self.route("exhaustive")
        self.assertGreaterEqual(len(best.order), len(greedy.order))
        if len(best.order) == len(greedy.order):
            self.assertLessEqual(best.total_cost, greedy.total_cost + 1e-6)

    def test_two_swap_is_never_worse_than_plain_greedy(self):
        greedy = self.route("nearest")
        swapped = self.route("greedy_swap")
        self.assertLessEqual(swapped.total_cost, greedy.total_cost + 1e-6)

    def test_legs_chain_end_to_end(self):
        # Each leg must begin exactly where the previous one stopped, or the
        # commands sent to the STM describe a path with teleports in it.
        route = self.route("exhaustive")
        pose = start_pose()
        for leg in route.legs:
            begin = leg.trajectory.start_pose()
            self.assertAlmostEqual(begin.x, pose.x, places=6)
            self.assertAlmostEqual(begin.y, pose.y, places=6)
            self.assertAlmostEqual(begin.theta, pose.theta, places=6)
            pose = leg.trajectory.end_pose()

    def test_every_leg_is_collision_free(self):
        for strategy in planner.STRATEGIES:
            for leg in self.route(strategy).legs:
                with self.subTest(strategy=strategy, obstacle=leg.obstacle_id):
                    self.assertTrue(self.arena.is_trajectory_free(leg.trajectory))

    def test_each_leg_ends_pointing_at_its_obstacle(self):
        # The point of the whole exercise: the camera has to be able to see the
        # image when the robot stops.
        for leg in self.route("exhaustive").legs:
            obstacle = self.arena.obstacle_by_id(leg.obstacle_id)
            fx, fy = obstacle.face_centre()
            end = leg.trajectory.end_pose()
            distance = math.hypot(fx - end.x, fy - end.y)
            bearing = math.atan2(fy - end.y, fx - end.x)
            off_axis = abs(math.atan2(math.sin(bearing - end.theta),
                                      math.cos(bearing - end.theta)))
            with self.subTest(obstacle=leg.obstacle_id):
                self.assertLessEqual(distance, cfg.CAPTURE_MAX_DISTANCE + 1e-6)
                self.assertGreaterEqual(distance, cfg.CAPTURE_MIN_DISTANCE - 1e-6)
                self.assertLess(off_axis, 1e-6, "the robot must point at the image")

    def test_total_duration_includes_the_scan_dwell(self):
        route = self.route("exhaustive")
        driving = sum(leg.duration for leg in route.legs)
        self.assertAlmostEqual(route.total_duration,
                               driving + cfg.SCAN_TIME * len(route.legs), places=6)

    def test_run_fits_inside_the_task_time_limit(self):
        self.assertLess(self.route("exhaustive").total_duration, cfg.TASK_TIME_LIMIT)

    def test_unknown_strategy_is_rejected(self):
        with self.assertRaises(ValueError):
            planner.plan_route(self.arena, "teleport", model=self.model)


class CostModelBehaviour(unittest.TestCase):
    def setUp(self):
        self.arena = Arena(LAYOUT)
        self.model = planner.CostModel(self.arena)

    def test_the_roadmap_carries_transit_poses(self):
        # Without them a leg can only ever be three segments, which is not
        # enough to get around a cluttered arena.
        kinds = {node.kind for node in self.model.nodes}
        self.assertEqual(kinds, {"start", "capture", "transit"})

    def test_nothing_routes_back_into_the_start(self):
        for i in range(len(self.model.nodes)):
            self.assertEqual(self.model.cost(i, 0), planner.INF)

    def test_pose_dp_agrees_with_the_legs_it_produces(self):
        order = [4, 2, 3, 5, 1]
        cost, chain = self.model.evaluate_order(order)
        self.assertEqual(len(chain), len(order))
        walked = self.model.cost(0, chain[0])
        for a, b in zip(chain, chain[1:]):
            walked += self.model.cost(a, b)
        self.assertAlmostEqual(walked, cost, places=6)

    def test_multi_hop_legs_are_still_one_continuous_trajectory(self):
        # Legs stitched through a transit pose must come back as a single
        # drivable path, not two disconnected halves.
        for i in range(len(self.model.nodes)):
            for j in self.model.nodes_by_obstacle[3]:
                entry = self.model.trajectory(i, j)
                if entry is None or entry[0] != "via":
                    continue
                segments = entry[1].segments
                for first, second in zip(segments, segments[1:]):
                    end, begin = first.end, second.start
                    self.assertAlmostEqual(end.x, begin.x, places=6)
                    self.assertAlmostEqual(end.y, begin.y, places=6)
                return      # one example is enough to prove the stitching


class Degenerate(unittest.TestCase):
    def test_single_obstacle(self):
        arena = Arena([Obstacle(1, 100.0, 100.0, "S")])
        route = planner.plan_route(arena, "exhaustive")
        self.assertEqual(route.order, [1])
        self.assertEqual(route.unreachable, [])

    def test_unreachable_obstacle_is_reported_not_fatal(self):
        # Checklist B.2 scores the images actually recognised, so an obstacle
        # with nowhere to stand must not sink the other four.
        layout = [Obstacle(1, 100.0, 100.0, "S"), Obstacle(2, 0.0, 190.0, "W")]
        route = planner.plan_route(Arena(layout), "exhaustive")
        self.assertIn(1, route.order)
        self.assertIn(2, route.unreachable)

    def test_no_obstacles_gives_an_empty_route(self):
        route = planner.plan_route(Arena([]), "exhaustive")
        self.assertEqual(route.order, [])
        self.assertEqual(route.total_distance, 0.0)


class RandomLayouts(unittest.TestCase):
    def test_random_layouts_plan_without_crashing_or_clipping(self):
        for seed in range(4):
            arena = Arena(random_layout(rng=random.Random(seed)))
            route = planner.plan_route(arena, "exhaustive")
            with self.subTest(seed=seed):
                self.assertGreater(len(route.legs), 0)
                self.assertEqual(len(set(route.order)), len(route.order))
                for leg in route.legs:
                    self.assertTrue(arena.is_trajectory_free(leg.trajectory))


if __name__ == "__main__":
    unittest.main()
