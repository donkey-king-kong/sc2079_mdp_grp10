#!/usr/bin/env python3
"""Replay STM commands on the robot's measured geometry and report clearance.

An independent check on the planner in algo/: this file deliberately imports
nothing from it, so a mistake in the planner's geometry cannot hide itself
here. Standard library only (the laptop has no internet, so no matplotlib);
plots are written as SVG, which any browser opens.

    python tools/replay/replay.py selftest
    python tools/replay/replay.py replay run.json --svg run.svg
    python tools/replay/replay.py compare --algo-dir algo --svg-dir out/

`run.json` is {"obstacles": [{"id": 1, "x": 5, "y": 7, "face": "S"}, ...],
"units": "cell" | "cm", "commands": ["RF090", "SF020", ...],
"start": {"x": 9.45, "y": 3.35, "theta_deg": 90}}. `start` is optional and is
the rear-axle centre in cm; it defaults to the robot pushed into the corner.

Conventions: cm, origin at the arena's bottom-left, x East, y North, heading in
degrees anticlockwise from East. A pose is the centre of the REAR AXLE, which
is the point the turning circle is centred on (Ackermann, rear-wheel drive).
"""

import argparse
import json
import math
import os
import subprocess
import sys

# --------------------------------------------------------------------------
# Measured robot (ground truth from the STM side; all from the rear-axle centre)
# --------------------------------------------------------------------------

RADIUS_LEFT = 20.2          # full lock, measured at the rear-axle centre
RADIUS_RIGHT = 36.2         # reverse turns use the same radius as forward ones
FRONT = 22.0                # front-most point (ultrasonic mount) ahead of the axle
REAR = 3.35                 # rear-most point (rear tyres' back edge) behind it
WIDTH = 21.4                # front wheels at full lock, the widest the body gets

# Pushed into the bottom-left corner facing North: left side of the front
# wheels (18.9cm across when straight) on x = 0, rear tyres' back edge on y = 0.
START = (18.9 / 2.0, REAR, 90.0)

ARENA = 200.0
OBSTACLE = 10.0
CELL = 10.0
START_ZONE = 40.0
MARGIN = 3.0                # the clearance a plan is supposed to keep

# Time model, measured: cruise speed, acceleration, deceleration (cm/s, cm/s^2)
STRAIGHT_PROFILE = (65.0, 100.0, 60.0)
TURN_PROFILE = (45.0, 60.0, 60.0)
COMMAND_OVERHEAD = 0.7      # s fixed cost per motion command
SCAN_TIME = 2.0             # s parked per photo (the planner's own figure)

# Replay sampling: far finer than the planner, so it is the reference.
SAMPLE_CM = 0.25
SAMPLE_DEG = 0.5

FACE_NORMAL = {"N": (0.0, 1.0), "S": (0.0, -1.0), "E": (1.0, 0.0), "W": (-1.0, 0.0)}


# --------------------------------------------------------------------------
# Geometry
# --------------------------------------------------------------------------

def outline(pose):
    """Corners of the body rectangle, anticlockwise, for a rear-axle pose."""
    x, y, th = pose
    c, s = math.cos(math.radians(th)), math.sin(math.radians(th))
    half = WIDTH / 2.0
    local = ((FRONT, -half), (FRONT, half), (-REAR, half), (-REAR, -half))
    return [(x + a * c - b * s, y + a * s + b * c) for a, b in local]


def _point_to_box(px, py, box):
    x0, y0, x1, y1 = box
    dx = max(x0 - px, 0.0, px - x1)
    dy = max(y0 - py, 0.0, py - y1)
    return math.hypot(dx, dy)


def _point_to_body(px, py, pose):
    x, y, th = pose
    c, s = math.cos(math.radians(th)), math.sin(math.radians(th))
    a = (px - x) * c + (py - y) * s
    b = -(px - x) * s + (py - y) * c
    half = WIDTH / 2.0
    da = max(-REAR - a, 0.0, a - FRONT)
    db = max(-half - b, 0.0, b - half)
    return math.hypot(da, db)


def box_clearance(pose, box):
    """Signed distance between the body and an axis-aligned box.

    Positive: the gap in cm. Negative: they overlap, by the minimum distance
    that would separate them (separating-axis theorem).
    """
    corners = outline(pose)
    x0, y0, x1, y1 = box
    box_corners = ((x0, y0), (x1, y0), (x1, y1), (x0, y1))
    th = math.radians(pose[2])
    axes = ((1.0, 0.0), (0.0, 1.0), (math.cos(th), math.sin(th)),
            (-math.sin(th), math.cos(th)))
    overlap = math.inf
    for ax, ay in axes:
        p = [cx * ax + cy * ay for cx, cy in corners]
        q = [cx * ax + cy * ay for cx, cy in box_corners]
        depth = min(max(p), max(q)) - max(min(p), min(q))
        if depth <= 0.0:
            overlap = None
            break
        overlap = min(overlap, depth)
    if overlap is not None:
        return -overlap
    # Disjoint convex polygons: the gap is realised at a vertex of one of them.
    return min(min(_point_to_box(cx, cy, box) for cx, cy in corners),
               min(_point_to_body(cx, cy, pose) for cx, cy in box_corners))


def wall_clearance(pose):
    """Distance from the body to the nearest wall (negative = outside)."""
    return min(min(x, ARENA - x, y, ARENA - y) for x, y in outline(pose))


def obstacle_box(ob):
    return (ob["x_cm"], ob["y_cm"], ob["x_cm"] + OBSTACLE, ob["y_cm"] + OBSTACLE)


def normalise_obstacles(raw, units="cell"):
    scale = CELL if units == "cell" else 1.0
    result = []
    for i, item in enumerate(raw):
        face = str(item.get("face", item.get("dir", "N"))).upper()[:1]
        result.append({"id": int(item.get("id", i + 1)), "face": face,
                       "x_cm": float(item["x"]) * scale, "y_cm": float(item["y"]) * scale})
    return result


# --------------------------------------------------------------------------
# Kinematics
# --------------------------------------------------------------------------

def parse(command):
    command = command.strip().upper()
    if command == "FIN":
        return ("FIN", None)
    for prefix in ("SNAP", "SF", "SB", "LF", "RF", "LB", "RB"):
        if command.startswith(prefix) and command[len(prefix):].isdigit():
            return (prefix, int(command[len(prefix):]))
    raise ValueError("unrecognised command %r" % command)


def motion(pose, kind, value, fraction):
    """Pose after `fraction` (0..1) of one motion command, from `pose`."""
    x, y, th = pose
    if kind in ("SF", "SB"):
        d = value * fraction * (1.0 if kind == "SF" else -1.0)
        return (x + d * math.cos(math.radians(th)), y + d * math.sin(math.radians(th)), th)
    steer = 1.0 if kind[0] == "L" else -1.0
    gear = 1.0 if kind[1] == "F" else -1.0
    radius = RADIUS_LEFT if steer > 0 else RADIUS_RIGHT
    # The turning centre is on the rear-axle line, one radius to the steering side.
    cx = x - steer * radius * math.sin(math.radians(th))
    cy = y + steer * radius * math.cos(math.radians(th))
    delta = steer * gear * value * fraction
    c, s = math.cos(math.radians(delta)), math.sin(math.radians(delta))
    rx, ry = x - cx, y - cy
    return (cx + rx * c - ry * s, cy + rx * s + ry * c, th + delta)


def samples_for(kind, value):
    if kind in ("SF", "SB"):
        return max(1, int(math.ceil(value / SAMPLE_CM)))
    radius = RADIUS_LEFT if kind[0] == "L" else RADIUS_RIGHT
    arc = math.radians(value) * radius
    return max(1, int(math.ceil(max(value / SAMPLE_DEG, arc / SAMPLE_CM))))


def profile_time(distance, profile):
    """Trapezoidal (or, if too short to reach cruise, triangular) move time."""
    v, a, d = profile
    if distance <= 0:
        return 0.0
    ramp = v * v / (2 * a) + v * v / (2 * d)
    if distance >= ramp:
        return v / a + v / d + (distance - ramp) / v
    peak = math.sqrt(2 * distance * a * d / (a + d))
    return peak / a + peak / d


def command_time(kind, value):
    if kind in ("SF", "SB"):
        return profile_time(value, STRAIGHT_PROFILE) + COMMAND_OVERHEAD
    radius = RADIUS_LEFT if kind[0] == "L" else RADIUS_RIGHT
    return profile_time(math.radians(value) * radius, TURN_PROFILE) + COMMAND_OVERHEAD


def replay(start, obstacles, commands):
    """Drive the commands; return per-command rows, the sampled poses, and totals."""
    boxes = [(ob["id"], obstacle_box(ob)) for ob in obstacles]
    pose = start
    rows, path = [], [start]
    clock = 0.0
    for index, command in enumerate(commands):
        kind, value = parse(command)
        row = {"i": index, "cmd": command, "start": pose}
        if kind == "FIN":
            continue
        if kind == "SNAP":
            clock += SCAN_TIME
            row.update(snap_report(pose, obstacles, value))
            row.update(end=pose, t=clock)
            rows.append(row)
            continue
        n = samples_for(kind, value)
        worst_ob, worst_ob_id, worst_ob_pose = math.inf, None, pose
        worst_wall, worst_wall_pose = math.inf, pose
        worst_zone = math.inf       # wall clearance while still in the start zone
        for k in range(0, n + 1):
            p = motion(pose, kind, value, k / n)
            for oid, box in boxes:
                cl = box_clearance(p, box)
                if cl < worst_ob:
                    worst_ob, worst_ob_id, worst_ob_pose = cl, oid, p
            wl = wall_clearance(p)
            if in_start_zone(p):
                # The corner placement itself sits on the walls, so leaving the
                # start zone is reported apart from the rest of the run.
                worst_zone = min(worst_zone, wl)
            elif wl < worst_wall:
                worst_wall, worst_wall_pose = wl, p
            if k:
                path.append(p)
        end = motion(pose, kind, value, 1.0)
        clock += command_time(kind, value)
        row.update(obstacle=worst_ob, obstacle_id=worst_ob_id, obstacle_pose=worst_ob_pose,
                   wall=worst_wall, wall_pose=worst_wall_pose, zone=worst_zone,
                   end=end, t=clock)
        rows.append(row)
        pose = end
    return rows, path, clock


def in_start_zone(pose):
    """Is the rear-axle centre inside the 40x40 start zone?"""
    return pose[0] < START_ZONE and pose[1] < START_ZONE


def snap_report(pose, obstacles, obstacle_id):
    """Where the robot really is when it takes a photo: camera distance and angle."""
    ob = next((o for o in obstacles if o["id"] == obstacle_id), None)
    if ob is None:
        return {"snap": "obstacle %s not in layout" % obstacle_id}
    nx, ny = FACE_NORMAL[ob["face"]]
    fx = ob["x_cm"] + OBSTACLE / 2 + nx * OBSTACLE / 2
    fy = ob["y_cm"] + OBSTACLE / 2 + ny * OBSTACLE / 2
    mid = (FRONT - REAR) / 2.0
    th = math.radians(pose[2])
    mx, my = pose[0] + mid * math.cos(th), pose[1] + mid * math.sin(th)
    dist = math.hypot(fx - mx, fy - my)
    bearing = math.degrees(math.atan2(fy - my, fx - mx))
    off = (bearing - pose[2] + 180.0) % 360.0 - 180.0
    # Is the robot on the image side of the face at all?
    in_front = (mx - fx) * nx + (my - fy) * ny > 0
    return {"snap": "image %.1fcm from body middle, %.1f deg off heading%s"
                    % (dist, off, "" if in_front else ", BEHIND THE FACE")}


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------

def fmt_pose(p):
    return "(%6.1f,%6.1f,%6.1f)" % (p[0], p[1], ((p[2] + 180.0) % 360.0) - 180.0)


def print_table(rows, total_time, out=sys.stdout):
    out.write("  #  cmd      obstacle clr (id)   wall clr (in start zone)  end pose (rear axle x, y, deg)\n")
    for r in rows:
        if "snap" in r:
            out.write("%3d  %-7s  %s\n" % (r["i"], r["cmd"], r["snap"]))
            continue
        flag = ""
        worst = min(r["obstacle"], r["wall"])
        if worst < 0:
            flag = "  <-- COLLISION" if r["obstacle"] < 0 else "  <-- OUTSIDE ARENA"
        elif worst < MARGIN:
            flag = "  <-- under %.0fcm margin" % MARGIN
        ob = "%8.2f (%s)" % (r["obstacle"], r["obstacle_id"]) if r["obstacle_id"] else "       -    "
        zone = "(%6.2f)" % r["zone"] if r["zone"] < math.inf else " " * 8
        out.write("%3d  %-7s  %-18s %8.2f %s        %s%s\n"
                  % (r["i"], r["cmd"], ob, r["wall"], zone, fmt_pose(r["end"]), flag))
    motion_rows = [r for r in rows if "snap" not in r]
    if motion_rows:
        ob = min(motion_rows, key=lambda r: r["obstacle"])
        wl = min(motion_rows, key=lambda r: r["wall"])
        out.write("min obstacle clearance %.2f cm at #%d %s; min wall clearance %.2f cm at #%d %s"
                  " (outside the start zone), %.2f cm inside it\n"
                  % (ob["obstacle"], ob["i"], ob["cmd"], wl["wall"], wl["i"], wl["cmd"],
                     min(r["zone"] for r in motion_rows)))
    out.write("run time %.1f s (measured time model, incl. %.1f s per photo)\n" % (total_time, SCAN_TIME))


def summarise(rows):
    """(min obstacle, min wall outside the start zone, min wall inside it)."""
    motion_rows = [r for r in rows if "snap" not in r]
    if not motion_rows:
        return math.inf, math.inf, math.inf
    return (min(r["obstacle"] for r in motion_rows), min(r["wall"] for r in motion_rows),
            min(r["zone"] for r in motion_rows))


def write_svg(path_file, obstacles, rows, path, title=""):
    scale, pad = 3.0, 30.0
    size = ARENA * scale + 2 * pad

    def X(x):
        return pad + x * scale

    def Y(y):
        return pad + (ARENA - y) * scale

    def poly(points, style):
        return '<polygon points="%s" %s/>' % (
            " ".join("%.1f,%.1f" % (X(px), Y(py)) for px, py in points), style)

    parts = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
             'viewBox="0 0 %d %d" font-family="sans-serif" font-size="11">'
             % (size, size + 20, size, size + 20),
             '<rect width="100%" height="100%" fill="white"/>']
    for i in range(0, 21):
        v = i * CELL
        parts.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#eee"/>'
                     % (X(v), Y(0), X(v), Y(ARENA)))
        parts.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#eee"/>'
                     % (X(0), Y(v), X(ARENA), Y(v)))
    parts.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="#dcfce7"/>'
                 % (X(0), Y(START_ZONE), START_ZONE * scale, START_ZONE * scale))
    parts.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="none" stroke="black" stroke-width="2"/>'
                 % (X(0), Y(ARENA), ARENA * scale, ARENA * scale))
    for ob in obstacles:
        x0, y0, x1, y1 = obstacle_box(ob)
        parts.append(poly(((x0, y0), (x1, y0), (x1, y1), (x0, y1)), 'fill="#334155"'))
        nx, ny = FACE_NORMAL[ob["face"]]
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        ex, ey = cx + nx * OBSTACLE / 2, cy + ny * OBSTACLE / 2
        tx, ty = -ny * OBSTACLE / 2, nx * OBSTACLE / 2
        parts.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#f59e0b" stroke-width="5"/>'
                     % (X(ex - tx), Y(ey - ty), X(ex + tx), Y(ey + ty)))
        parts.append('<text x="%.1f" y="%.1f" fill="white" text-anchor="middle">%d</text>'
                     % (X(cx), Y(cy) + 4, ob["id"]))
    # Outline every ~4cm of travel, so the swept area is visible.
    travelled, last = 0.0, None
    for p in path:
        if last is not None:
            travelled += math.hypot(p[0] - last[0], p[1] - last[1]) + abs(p[2] - last[2]) * 0.2
        if last is None or travelled >= 4.0:
            parts.append(poly(outline(p), 'fill="none" stroke="#0f766e" stroke-opacity="0.25"'))
            travelled = 0.0
        last = p
    parts.append('<polyline points="%s" fill="none" stroke="#2563eb" stroke-width="1.5"/>'
                 % " ".join("%.1f,%.1f" % (X(p[0]), Y(p[1])) for p in path))
    motion_rows = [r for r in rows if "snap" not in r]
    if motion_rows:
        ob = min(motion_rows, key=lambda r: r["obstacle"])
        wl = min(motion_rows, key=lambda r: r["wall"])
        if wl["wall"] == math.inf:          # never left the start zone
            wl = dict(wl, wall_pose=path[0])
        parts.append(poly(outline(ob["obstacle_pose"]), 'fill="#ef4444" fill-opacity="0.25" stroke="#dc2626" stroke-width="2"'))
        parts.append(poly(outline(wl["wall_pose"]), 'fill="#a855f7" fill-opacity="0.2" stroke="#9333ea" stroke-width="2"'))
        title += "   min obstacle %.1fcm (#%d %s, red)   min wall %.1fcm (#%d %s, purple)" % (
            ob["obstacle"], ob["i"], ob["cmd"], wl["wall"], wl["i"], wl["cmd"])
    parts.append(poly(outline(path[0]), 'fill="none" stroke="black" stroke-width="1.5"'))
    parts.append('<text x="%.1f" y="%.1f">%s</text>' % (pad, size + 12, title))
    parts.append("</svg>")
    with open(path_file, "w", encoding="utf-8") as f:
        f.write("\n".join(parts))


# --------------------------------------------------------------------------
# Subcommands
# --------------------------------------------------------------------------

def selftest():
    """Yesterday's measured test vectors, relative to the heading before the turn."""
    def rel(command, ahead):
        kind, value = parse(command)
        x, y, th = motion((0.0, 0.0, 0.0), kind, value, 1.0)
        # (forward, left) displacement of a point `ahead` cm in front of the axle
        return (x + ahead * math.cos(math.radians(th)) - ahead,
                y + ahead * math.sin(math.radians(th)))

    vectors = [("LF090", 0.0, (20.2, 20.2)), ("RF090", 0.0, (36.2, -36.2)),
               ("LF180", 0.0, (0.0, 40.4)), ("LB090", 0.0, (-20.2, 20.2)),
               ("LF090", 7.4, (12.8, 27.6)), ("RF090", 7.4, (28.8, -43.6)),
               ("LF180", 7.4, (-14.8, 40.4))]
    ok = True
    for command, ahead, want in vectors:
        got = rel(command, ahead)
        good = abs(got[0] - want[0]) < 0.05 and abs(got[1] - want[1]) < 0.05
        ok &= good
        print("%-5s point %.1fcm ahead: fwd %6.2f left %6.2f  want %6.1f %6.1f  %s"
              % (command, ahead, got[0], got[1], want[0], want[1], "ok" if good else "FAIL"))
    # Geometry sanity: parked 5cm south of a block, facing it, clearance is 5.
    box = (100.0, 100.0, 110.0, 110.0)
    got = box_clearance((105.0, 100.0 - 5.0 - FRONT, 90.0), box)
    good = abs(got - 5.0) < 1e-9
    ok &= good
    print("clearance facing a block 5cm away: %.3f  %s" % (got, "ok" if good else "FAIL"))
    got = wall_clearance(START)
    print("start pose wall clearance: %.2f (left side at full-lock width)" % got)
    print("selftest", "PASSED" if ok else "FAILED")
    return 0 if ok else 1


def parse_start(text):
    if not text:
        return START
    x, y, th = (float(v) for v in text.split(","))
    return (x, y, th)


def cmd_replay(args):
    with open(args.run, encoding="utf-8") as f:
        run = json.load(f)
    obstacles = normalise_obstacles(run["obstacles"], run.get("units", "cell"))
    start = parse_start(args.start)
    if not args.start and run.get("start"):
        s = run["start"]
        start = (float(s["x"]), float(s["y"]), float(s.get("theta_deg", 90.0)))
    rows, path, total = replay(start, obstacles, run["commands"])
    print("start (rear axle) %s" % fmt_pose(start))
    print_table(rows, total)
    if args.svg:
        write_svg(args.svg, obstacles, rows, path, os.path.basename(args.run))
        print("wrote", args.svg)
    return 0


_PLAN_SNIPPET = r"""
import json, math, sys, time
sys.path.insert(0, '.')
import arena as A, commands as C, planner as P
raw = json.loads(sys.stdin.read())
layout = A.Arena(A.parse_obstacles(raw, 'cell'))
began = time.time()
route = P.plan_route(layout, 'exhaustive')
elapsed = time.time() - began
s = A.start_pose()
print(json.dumps({'commands': C.route_to_commands(route), 'order': route.order,
                  'unreachable': route.unreachable, 'planning_seconds': elapsed,
                  'planner_time': route.total_duration,
                  'start': [s.x, s.y, math.degrees(s.theta)]}))
"""


def plan_with(algo_dir, python, raw):
    proc = subprocess.run([python, "-B", "-c", _PLAN_SNIPPET], cwd=algo_dir,
                          input=json.dumps(raw), capture_output=True, text=True, timeout=600)
    if proc.returncode:
        raise RuntimeError(proc.stderr.strip().splitlines()[-1] if proc.stderr else "planner failed")
    return json.loads(proc.stdout.strip().splitlines()[-1])


def cmd_compare(args):
    with open(args.layouts, encoding="utf-8") as f:
        layouts = json.load(f)
    if args.svg_dir:
        os.makedirs(args.svg_dir, exist_ok=True)
    header = ("layout  reached  plan_s  run_s   obst_clr  wall_clr  zone_clr"
              "  | from planner's own start: obst_clr  wall_clr")
    print("algo dir:", os.path.abspath(args.algo_dir))
    print(header)
    totals = []
    for index, item in enumerate(layouts):
        name, raw = item["name"], item["obstacles"]
        plan = plan_with(args.algo_dir, args.python, raw)
        obstacles = normalise_obstacles(raw, "cell")
        rows, path, total = replay(START, obstacles, plan["commands"])
        ob, wl, zone = summarise(rows)
        own = tuple(plan["start"])
        rows2, _, _ = replay(own, obstacles, plan["commands"])
        ob2, wl2, zone2 = summarise(rows2)
        print("%-7s %d/%d    %6.1f  %6.1f  %8.2f  %8.2f  %8.2f  |                          %8.2f  %8.2f"
              % (name, len(plan["order"]), len(raw), plan["planning_seconds"], total, ob, wl, zone,
                 ob2, min(wl2, zone2)))
        totals.append((len(plan["order"]), plan["planning_seconds"], ob, wl, zone))
        if args.svg_dir:
            svg = os.path.join(args.svg_dir, "%s.svg" % name)
            write_svg(svg, obstacles, rows, path, "%s  order %s" % (name, plan["order"]))
        if args.verbose:
            print_table(rows, total)
            print("commands:", " ".join(plan["commands"]))
    reached = sum(t[0] for t in totals)
    possible = sum(len(item["obstacles"]) for item in layouts)
    print("TOTAL reached %d/%d, worst planning %.1f s, worst obstacle clearance %.2f, "
          "worst wall clearance %.2f (%.2f in the start zone)"
          % (reached, possible, max(t[1] for t in totals), min(t[2] for t in totals),
             min(t[3] for t in totals), min(t[4] for t in totals)))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="mode", required=True)
    sub.add_parser("selftest", help="check the kinematics against the measured test vectors")
    p = sub.add_parser("replay", help="replay one command list")
    p.add_argument("run", help="JSON file with obstacles, commands and optional start")
    p.add_argument("--start", help="rear-axle start 'x,y,deg' (default: corner placement)")
    p.add_argument("--svg", help="write a plot here")
    p = sub.add_parser("compare", help="plan every layout with an algo/ directory and replay it")
    p.add_argument("--algo-dir", required=True)
    p.add_argument("--layouts", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "layouts.json"))
    p.add_argument("--python", default=sys.executable, help="interpreter for the planner")
    p.add_argument("--svg-dir", help="write one plot per layout here")
    p.add_argument("-v", "--verbose", action="store_true", help="per-command tables too")
    args = parser.parse_args()
    if args.mode == "selftest":
        return selftest()
    if args.mode == "replay":
        return cmd_replay(args)
    return cmd_compare(args)


if __name__ == "__main__":
    sys.exit(main())
