"""The HTTP contract the RPi and the simulator both depend on.

Skipped when Flask is not installed, so `run_tests.py` still works on a bare
Python with nothing pip-installed.
"""

import math
import unittest

import conftest  # noqa: F401

try:
    import server
except ImportError:                                   # pragma: no cover
    server = None

LAYOUT = [
    {"id": 1, "x": 6, "y": 12, "face": "S"},
    {"id": 2, "x": 14, "y": 6, "face": "W"},
    {"id": 3, "x": 15, "y": 15, "face": "S"},
]


@unittest.skipIf(server is None, "flask is not installed")
class Api(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        server.app.config["TESTING"] = True
        cls.client = server.app.test_client()

    def post(self, path, body):
        response = self.client.post(path, json=body)
        return response.status_code, response.get_json()

    def test_health(self):
        self.assertEqual(self.client.get("/health").get_json(), {"status": "ok"})

    def test_config_exposes_what_the_ui_needs_to_draw(self):
        data = self.client.get("/api/config").get_json()
        for key in ("arena_size", "cell_size", "grid_cells", "robot_size",
                    "obstacle_inflation", "start", "strategies", "scan_time"):
            self.assertIn(key, data)
        self.assertEqual(data["arena_size"], 200.0)

    def test_random_layout_is_legal(self):
        data = self.client.get("/api/random").get_json()
        self.assertEqual(len(data["obstacles"]), 5)
        for obstacle in data["obstacles"]:
            self.assertIn(obstacle["face"], ("N", "S", "E", "W"))
            self.assertTrue(0 <= obstacle["x"] < 20 and 0 <= obstacle["y"] < 20)

    def test_plan_returns_the_documented_shape(self):
        status, data = self.post("/api/plan", {"obstacles": LAYOUT, "strategy": "exhaustive"})
        self.assertEqual(status, 200)
        for key in ("order", "commands", "legs", "trajectory", "path_cells",
                    "total_distance", "total_duration", "unreachable", "start"):
            self.assertIn(key, data)
        self.assertEqual(sorted(data["order"]), [1, 2, 3])
        self.assertEqual(data["commands"][-1], "FIN")
        self.assertGreater(len(data["trajectory"]), 10)
        # Legs must carry their own commands so the RPi can send one leg at a
        # time and wait for the photo in between.
        self.assertEqual(len(data["legs"]), 3)
        self.assertTrue(all(leg["commands"] for leg in data["legs"]))

    def test_plan_accepts_centimetre_units_and_a_custom_start(self):
        status, data = self.post("/api/plan", {
            "obstacles": [{"id": 1, "x": 100.0, "y": 100.0, "face": "S"}],
            "units": "cm",
            "start": {"x": 40.0, "y": 40.0, "theta_deg": 0.0},
        })
        self.assertEqual(status, 200)
        self.assertEqual(data["start"]["x"], 40.0)
        self.assertEqual(data["start"]["face"], "E")

    def test_compare_runs_every_strategy_on_one_layout(self):
        status, data = self.post("/api/compare", {"obstacles": LAYOUT})
        self.assertEqual(status, 200)
        self.assertEqual(set(data["results"]), {"nearest", "greedy_swap", "exhaustive"})
        best = data["results"]["exhaustive"]
        greedy = data["results"]["nearest"]
        # B.3's claim over B.2, asserted through the API the demo actually uses.
        if len(best["order"]) == len(greedy["order"]):
            self.assertLessEqual(best["total_duration"], greedy["total_duration"] + 1e-6)

    def test_navigate_speaks_the_rpi_message_format(self):
        status, data = self.post("/api/navigate", {
            "type": "START_TASK",
            "data": {"robot": {"x": 1, "y": 1, "dir": "N"},
                     "obstacles": [{"id": "1", "x": 6, "y": 12, "dir": "S"},
                                   {"id": "2", "x": 14, "y": 6, "dir": "W"}]},
        })
        self.assertEqual(status, 200)
        self.assertEqual(data["type"], "NAVIGATION")
        self.assertIn("commands", data["data"])
        self.assertIn("path", data["data"])
        self.assertTrue(all(len(cell) == 2 for cell in data["data"]["path"]))

    def test_client_errors_come_back_as_400_with_a_reason(self):
        for body in ({"obstacles": []},
                     {"obstacles": [{"id": 1, "x": 5, "y": 5, "face": "Q"}]},
                     {"obstacles": LAYOUT, "strategy": "warp"},
                     {"obstacles": LAYOUT, "metric": "vibes"}):
            status, data = self.post("/api/plan", body)
            with self.subTest(body=body):
                self.assertEqual(status, 400)
                self.assertIn("error", data)

    def test_pose_clocks_add_up_to_the_reported_total(self):
        """The animation's clock must equal the figure the plan is judged on.

        The simulator plays back the `t` on each pose. If that disagrees with
        `total_duration`, the B.3 demo shows one number on the stopwatch and a
        different one in the results panel.
        """
        status, data = self.post("/api/plan", {"obstacles": LAYOUT, "strategy": "exhaustive"})
        self.assertEqual(status, 200)
        scan = data["scan_time"]

        clock = 0.0
        for leg in data["legs"]:
            self.assertAlmostEqual(leg["starts_at"], clock, places=2)
            times = [pose["t"] for pose in leg["trajectory"]]
            self.assertEqual(times, sorted(times), "the clock must not run backwards")
            self.assertGreaterEqual(times[0], clock - 1e-6)
            self.assertAlmostEqual(times[-1], clock + leg["duration"], places=2)
            clock += leg["duration"] + scan

        self.assertAlmostEqual(clock, data["total_duration"], places=2)

    def test_legs_do_not_repeat_the_joining_pose(self):
        # Leg N+1 starts where leg N stopped; emitting that pose twice makes the
        # animation stall for a frame at every obstacle.
        _, data = self.post("/api/plan", {"obstacles": LAYOUT, "strategy": "exhaustive"})
        for previous, following in zip(data["legs"], data["legs"][1:]):
            last = previous["trajectory"][-1]
            first = following["trajectory"][0]
            self.assertNotEqual((last["x"], last["y"], last["t"]),
                                (first["x"], first["y"], first["t"]))

    def test_drive_applies_one_stm_command(self):
        """Manual driving goes through the real command parser and kinematics.

        This is what backs checklist B.1's "show the position of the robot as it
        moves forward/backward and turns" on demand.
        """
        start = {"x": 100.0, "y": 100.0, "theta_deg": 90.0}     # mid-arena, facing North
        status, data = self.post("/api/drive",
                                 {"start": start, "command": "SF050", "obstacles": []})
        self.assertEqual(status, 200)
        self.assertFalse(data["blocked"])
        self.assertAlmostEqual(data["pose"]["x"], 100.0, places=6)
        self.assertAlmostEqual(data["pose"]["y"], 150.0, places=6)
        self.assertAlmostEqual(data["pose"]["theta_deg"], 90.0, places=6)

    def test_drive_turns_are_arcs_not_pivots(self):
        # The robot cannot turn on the spot, so a turn command must move it.
        start = {"x": 100.0, "y": 100.0, "theta_deg": 0.0}
        _, data = self.post("/api/drive",
                            {"start": start, "command": "RF090", "obstacles": []})
        self.assertAlmostEqual(data["pose"]["theta_deg"], -90.0, places=6)
        moved = math.hypot(data["pose"]["x"] - 100.0, data["pose"]["y"] - 100.0)
        self.assertGreater(moved, 25.0, "a 90 degree arc at r=25 must displace the robot")

    def test_drive_refuses_a_move_into_an_obstacle(self):
        # Facing an obstacle from 40cm away: driving forward would clip its
        # virtual box, so the robot must stay put and say so.
        start = {"x": 105.0, "y": 60.0, "theta_deg": 90.0}
        _, data = self.post("/api/drive", {
            "start": start, "command": "SF060",
            "obstacles": [{"id": 1, "x": 10, "y": 10, "face": "S"}]})
        self.assertTrue(data["blocked"])
        self.assertAlmostEqual(data["pose"]["y"], 60.0, places=6)

    def test_drive_refuses_a_move_out_of_the_arena(self):
        start = {"x": 20.0, "y": 180.0, "theta_deg": 90.0}
        _, data = self.post("/api/drive",
                            {"start": start, "command": "SF060", "obstacles": []})
        self.assertTrue(data["blocked"])
        self.assertAlmostEqual(data["pose"]["y"], 180.0, places=6)

    def test_drive_rejects_a_command_it_cannot_parse(self):
        status, data = self.post("/api/drive",
                                 {"start": None, "command": "XX999", "obstacles": []})
        self.assertEqual(status, 400)
        self.assertIn("error", data)

    def test_index_serves_the_simulator(self):
        response = self.client.get("/")
        self.assertEqual(response.status_code, 200)
        self.assertIn(b"<canvas", response.data)


if __name__ == "__main__":
    unittest.main()
