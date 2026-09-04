

"""HTTP front end: the simulator's backend and the RPi's planning service.

Deliberately one program for both jobs. The week-7 checklist demo and the
week-8 robot run go through exactly the same planning code, so anything that
works in the simulator works on the arena -- and anything that breaks on the
arena can be reproduced in the browser.

Endpoints
---------

``GET  /``              the simulator UI (checklist B.1)
``GET  /api/config``    the tunable constants, so the UI draws what the planner
                        actually believes about the world
``GET  /api/random``    a random legal obstacle layout, for demos
``POST /api/plan``      the real contract -- plan a run, return commands
``POST /api/compare``   run every strategy on one layout, for the B.2 vs B.3 view
``POST /api/navigate``  the same planner behind the RPi's message format
``POST /api/drive``     apply one STM command to a pose -- manual driving in the UI

Request body for ``/api/plan``::

    {
      "obstacles": [{"id": 1, "x": 8, "y": 5, "face": "S"}, ...],
      "units":    "cell",           // "cell" (default, 20x20 grid) or "cm"
      "strategy": "exhaustive",     // "nearest" | "greedy_swap" | "exhaustive"
      "metric":   "time",           // "time" (default) or "distance"
      "start":    {"x": 20, "y": 20, "theta_deg": 90}    // optional, cm
    }

`x`/`y` on an obstacle are the BOTTOM-LEFT corner; `face` (or `dir`) is the
side the image is on, N/S/E/W. Response is documented in `_plan_response`.
"""

import math
import time
import traceback
from typing import Any, Dict, List, Optional

from flask import Flask, jsonify, request, send_from_directory

import arena as arena_module
import commands as commands_module
import config as cfg
import planner
from motion import Pose, heading_to_face, normalise_angle

app = Flask(__name__, static_folder="static", static_url_path="/static")

# How finely trajectories are sampled for the browser to animate. Finer than
# this just makes the JSON bigger without the eye noticing.
ANIMATION_STEP = 3.0


# --------------------------------------------------------------------------
# Serialisation
# --------------------------------------------------------------------------


def _pose_dict(pose: Pose, clock: Optional[float] = None) -> Dict[str, Any]:
    """One pose, plus the simulated clock reading if this is part of a run.

    The clock comes from the planner's own time model rather than being
    re-derived in the browser, so the animation and the headline figure agree.
    """
    data = {
        "x": round(pose.x, 2),
        "y": round(pose.y, 2),
        "theta": round(pose.theta, 5),
        "theta_deg": round(math.degrees(pose.theta), 2),
        "face": heading_to_face(pose.theta),
    }
    if clock is not None:
        data["t"] = round(clock, 3)
    return data


def _cells(poses) -> List[List[int]]:
    """Trajectory as 20x20 grid cells, de-duplicated -- what the Android map draws."""
    cells: List[List[int]] = []
    for pose in poses:
        cell = [arena_module.cm_to_cell(pose.x), arena_module.cm_to_cell(pose.y)]
        if not cells or cells[-1] != cell:
            cells.append(cell)
    return cells


def _leg_dict(leg: planner.Leg, start_time: float) -> Dict[str, Any]:
    """One leg, with every pose stamped with the clock reading it happens at."""
    timed = leg.trajectory.sample_with_time(ANIMATION_STEP, start_time)
    return {
        "obstacle_id": leg.obstacle_id,
        "method": leg.method,
        "distance": round(leg.distance, 2),
        "duration": round(leg.duration, 3),
        "starts_at": round(start_time, 3),
        "commands": commands_module.trajectory_to_commands(leg.trajectory),
        "end": _pose_dict(leg.trajectory.end_pose(), start_time + leg.duration),
        # Drop the first pose of each leg after the first: it is the previous
        # leg's last pose, and duplicating it makes the animation stall.
        "trajectory": [_pose_dict(p, t) for p, t in (timed[1:] if start_time else timed)],
    }


def _plan_response(route: planner.Route, layout: arena_module.Arena,
                   start: Pose, elapsed: float) -> Dict[str, Any]:
    """The full plan, in the shape the simulator and the RPi both consume."""
    legs = []
    clock = 0.0
    for leg in route.legs:
        legs.append(_leg_dict(leg, clock))
        # Driving time, then parked while the photo is taken.
        clock += leg.duration + cfg.SCAN_TIME

    poses = [start]
    for leg in route.legs:
        poses.extend(leg.trajectory.sample(ANIMATION_STEP))

    return {
        "strategy": route.strategy,
        "metric": route.metric,
        "order": route.order,
        "unreachable": route.unreachable,
        "commands": commands_module.route_to_commands(route),
        "legs": legs,
        "trajectory": [_pose_dict(p) for p in poses],
        "path_cells": _cells(poses),
        "total_distance": round(route.total_distance, 2),
        "total_duration": round(route.total_duration, 2),
        "total_cost": round(route.total_cost, 3),
        "scan_time": cfg.SCAN_TIME,
        "within_time_limit": route.total_duration <= cfg.TASK_TIME_LIMIT,
        "planning_seconds": round(elapsed, 3),
        "start": _pose_dict(start),
        "obstacles": [ob.to_dict() for ob in layout.obstacles],
    }


# --------------------------------------------------------------------------
# Request parsing
# --------------------------------------------------------------------------


class BadRequest(Exception):
    """A client error worth reporting as 400 rather than a stack trace."""


def _read_layout(payload: Dict[str, Any]):
    raw = payload.get("obstacles")
    if not isinstance(raw, list) or not raw:
        raise BadRequest("'obstacles' must be a non-empty list")
    try:
        obstacles = arena_module.parse_obstacles(raw, payload.get("units", "cell"))
    except (KeyError, TypeError, ValueError) as exc:
        raise BadRequest(str(exc))
    return arena_module.Arena(obstacles)


def _read_start(payload: Dict[str, Any]) -> Pose:
    """Start pose in cm. Accepts `theta` (radians) or `theta_deg`, or a face letter."""
    raw = payload.get("start")
    if not raw:
        return arena_module.start_pose()
    try:
        if "theta" in raw:
            theta = float(raw["theta"])
        elif "theta_deg" in raw:
            theta = math.radians(float(raw["theta_deg"]))
        else:
            theta = arena_module.face_to_heading(str(raw.get("face", raw.get("dir", "N"))))
        return Pose(float(raw["x"]), float(raw["y"]), normalise_angle(theta))
    except (KeyError, TypeError, ValueError) as exc:
        raise BadRequest("bad 'start': %s" % (exc,))


def _read_strategy(payload: Dict[str, Any]) -> str:
    strategy = payload.get("strategy", "exhaustive")
    if strategy not in planner.STRATEGIES:
        raise BadRequest("'strategy' must be one of %s" % (list(planner.STRATEGIES),))
    return strategy


def _read_metric(payload: Dict[str, Any]) -> str:
    metric = payload.get("metric", "time")
    if metric not in ("time", "distance"):
        raise BadRequest("'metric' must be 'time' or 'distance'")
    return metric


# --------------------------------------------------------------------------
# Routes
# --------------------------------------------------------------------------


@app.errorhandler(BadRequest)
def _handle_bad_request(exc: BadRequest):
    return jsonify({"error": str(exc)}), 400


@app.route("/")
def index():
    return send_from_directory(app.static_folder, "index.html")


@app.route("/health")
def health():
    """Cheap liveness check, so the RPi can confirm the laptop is up before a run."""
    return jsonify({"status": "ok"})


@app.route("/api/config")
def api_config():
    """Everything the UI needs to draw the same world the planner is planning in."""
    return jsonify({
        "arena_size": cfg.ARENA_SIZE,
        "cell_size": cfg.CELL_SIZE,
        "grid_cells": cfg.GRID_CELLS,
        "obstacle_size": cfg.OBSTACLE_SIZE,
        "start_zone_size": cfg.START_ZONE_SIZE,
        "robot_size": cfg.ROBOT_SIZE,
        "turning_radius": cfg.TURNING_RADIUS,
        "obstacle_inflation": cfg.OBSTACLE_INFLATION,
        "boundary_margin": cfg.BOUNDARY_MARGIN,
        "capture_standoff": cfg.CAPTURE_STANDOFF,
        "num_obstacles": cfg.NUM_OBSTACLES,
        "task_time_limit": cfg.TASK_TIME_LIMIT,
        "scan_time": cfg.SCAN_TIME,
        "speed_straight": cfg.SPEED_STRAIGHT,
        "speed_turn": cfg.SPEED_TURN,
        "strategies": list(planner.STRATEGIES),
        "start": _pose_dict(arena_module.start_pose()),
    })


@app.route("/api/random")
def api_random():
    """A random legal layout. Guaranteed solvable -- see `arena.random_layout`."""
    try:
        count = int(request.args.get("count", cfg.NUM_OBSTACLES))
    except ValueError:
        raise BadRequest("'count' must be an integer")
    if not 1 <= count <= 12:
        raise BadRequest("'count' must be between 1 and 12")
    obstacles = arena_module.random_layout(count)
    return jsonify({"obstacles": [ob.to_dict() for ob in obstacles]})


@app.route("/api/plan", methods=["POST"])
def api_plan():
    """Plan one run. This is the endpoint the RPi calls before the robot moves."""
    payload = request.get_json(silent=True) or {}
    layout = _read_layout(payload)
    start = _read_start(payload)
    strategy = _read_strategy(payload)
    metric = _read_metric(payload)

    began = time.time()
    route = planner.plan_route(layout, strategy, start=start, metric=metric)
    return jsonify(_plan_response(route, layout, start, time.time() - began))


@app.route("/api/compare", methods=["POST"])
def api_compare():
    """Every strategy on one layout -- the simulator's B.2 vs B.3 view.

    All three share a single cost matrix, so this costs barely more than one
    plan and the comparison is apples to apples.
    """
    payload = request.get_json(silent=True) or {}
    layout = _read_layout(payload)
    start = _read_start(payload)
    metric = _read_metric(payload)

    began = time.time()
    routes = planner.compare_strategies(layout, start=start, metric=metric)
    elapsed = time.time() - began
    return jsonify({
        "metric": metric,
        "planning_seconds": round(elapsed, 3),
        "results": {name: _plan_response(route, layout, start, elapsed)
                    for name, route in routes.items()},
    })


@app.route("/api/navigate", methods=["POST"])
def api_navigate():
    """The same planner, wrapped in the RPi's existing message format.

    Lets the RPi teammate point at this service without rewriting their
    message handling::

        {"type": "START_TASK",
         "data": {"robot": {"x": 1, "y": 1, "dir": "N"},
                  "obstacles": [{"id": "00", "x": 8, "y": 5, "dir": "S"}, ...]}}

    comes back as::

        {"type": "NAVIGATION",
         "data": {"commands": [...], "path": [[x, y], ...], "order": [...]}}
    """
    payload = request.get_json(silent=True) or {}
    data = payload.get("data", payload)
    layout = _read_layout(data)

    start = arena_module.start_pose()
    robot = data.get("robot")
    if robot:
        # The RPi speaks in grid cells like the tablet does; convert to the
        # centre-of-robot centimetres the planner works in.
        start = arena_module.bottom_left_to_centre(
            arena_module.cell_to_cm(float(robot.get("x", 0))),
            arena_module.cell_to_cm(float(robot.get("y", 0))),
            arena_module.face_to_heading(str(robot.get("dir", robot.get("face", "N")))),
        )

    began = time.time()
    route = planner.plan_route(layout, _read_strategy(data), start=start,
                              metric=_read_metric(data))
    response = _plan_response(route, layout, start, time.time() - began)
    return jsonify({
        "type": "NAVIGATION",
        "data": {
            "commands": response["commands"],
            "path": response["path_cells"],
            "order": response["order"],
            "unreachable": response["unreachable"],
            "total_duration": response["total_duration"],
        },
    })

@app.route("/api/drive", methods=["POST"])
def api_drive():
    """Apply a single STM command to a pose and report where the robot ends up.

    This is what the simulator's manual drive buttons call, so checklist B.1's
    "show the position of the robot as it moves forward/backward and turns" can
    be demonstrated on demand rather than only via whatever reverses a given
    plan happens to contain.

    It deliberately goes through `commands_to_trajectory`, the same parser the
    round-trip tests use, so the buttons drive the robot with the exact strings
    that would be sent to the STM board -- and through the same collision check
    the planner uses, so a blocked move is reported rather than driven.

    Body: ``{"start": {...}, "command": "SF010", "obstacles": [...]}``
    """
    payload = request.get_json(silent=True) or {}
    command = str(payload.get("command", "")).strip().upper()
    try:
        commands_module.parse(command)
    except ValueError as exc:
        raise BadRequest(str(exc))

    start = _read_start(payload) if payload.get("start") else arena_module.start_pose()
    raw = payload.get("obstacles") or []
    try:
        layout = arena_module.Arena(arena_module.parse_obstacles(raw, payload.get("units", "cell")))
    except (KeyError, TypeError, ValueError) as exc:
        raise BadRequest(str(exc))

    trajectory = commands_module.commands_to_trajectory([command], start)
    blocked = next((pose for pose in trajectory.iter_sample(cfg.COLLISION_SAMPLE_STEP)
                    if not layout.is_pose_free(pose)), None)

    return jsonify({
        "command": command,
        "blocked": blocked is not None,
        # On a blocked move the robot stays put: better to show the demonstrator
        # why it will not go than to drive it through a wall.
        "pose": _pose_dict(start if blocked is not None else trajectory.end_pose()),
        "trajectory": ([] if blocked is not None
                       else [_pose_dict(p) for p in trajectory.sample(ANIMATION_STEP)]),
        "distance": round(trajectory.length, 2),
        "description": commands_module.describe([command]),
    })


@app.errorhandler(500)
def _handle_error(exc):                                  # pragma: no cover
    traceback.print_exc()
    return jsonify({"error": "internal error: %s" % (exc,)}), 500


def main() -> None:
    import argparse
    import os

    parser = argparse.ArgumentParser(description="SC2079 Group 10 algorithm server")
    parser.add_argument("--host", default=os.environ.get("HOST", cfg.HOST))
    parser.add_argument("--port", type=int, default=int(os.environ.get("PORT", cfg.PORT)))
    args = parser.parse_args()

    print("Simulator:  http://localhost:%d" % args.port)
    print("API:        http://%s:%d/api/plan  (reachable from the RPi over the LAN)"
          % (args.host, args.port))
    app.run(host=args.host, port=args.port, debug=False, threaded=True)


if __name__ == "__main__":
    main()
