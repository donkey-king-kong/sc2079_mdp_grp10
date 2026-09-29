"""Single calibration point for the Algorithm subsystem.

Every tunable number lives here, with the slide of `algarithms_briefing_25S2.pdf`
it came from written next to it. When the robot is finally on the arena and the
numbers are wrong, this is the only file you should need to touch.

Coordinate conventions used by the whole package
------------------------------------------------
* Units are centimetres. Origin is the arena's BOTTOM-LEFT corner, x to the
  East, y to the North (briefing slide 7).
* An obstacle's position is its BOTTOM-LEFT corner, as in the briefing.
* A robot pose is ``(x, y, theta)`` about the robot's **turning centre** -- the
  point it rotates about, `TURNING_CENTRE_OFFSET` behind the middle of its
  footprint -- not its bottom-left corner. The briefing uses the corner on
  slide 7; centre maths is far less error-prone for Dubins curves, so
  `arena.bottom_left_to_centre()` converts at the boundary. Anything about the
  robot's *body* (collisions, camera distance, grid cell) is measured from the
  footprint centre instead, via `motion.footprint_centre()`.
* ``theta`` is radians, East = 0, counter-clockwise positive, normalised to
  (-pi, pi]. So N = +pi/2, W = pi, S = -pi/2 (briefing slide 7).
"""

import math

# --------------------------------------------------------------------------
# Arena (briefing slides 3 and 5)
# --------------------------------------------------------------------------

ARENA_SIZE = 200.0          # 200cm x 200cm square area
CELL_SIZE = 10.0            # the Android grid uses 10cm cells ...
GRID_CELLS = 20             # ... so the map is 20x20 cells (slide 9, option 2)

OBSTACLE_SIZE = 10.0        # each obstacle is a 10cm x 10cm block (slide 3)
NUM_OBSTACLES = 5           # the task always has exactly five (slide 3)

START_ZONE_SIZE = 40.0      # 40cm x 40cm start zone at the bottom-left (slide 3)

# Slide 3 gives the true footprint as 20cm x 21cm, but slide 7 recommends
# planning with 30cm x 30cm so that the margin absorbs steering error. We plan
# with the recommended figure.
ROBOT_SIZE = 30.0
ROBOT_HALF = ROBOT_SIZE / 2.0

# Our robot's real outline, measured from the centre of the REAR AXLE (the
# point every planner pose describes -- see TURNING_CENTRE_OFFSET below):
#   front-most point, the ultrasonic mount ........... 22.0cm ahead
#   rear-most point, the rear tyres' back edge ....... 3.35cm behind
#   width, the front wheels at full lock ............. 21.4cm (18.9 straight,
#                                                      rear tyres 18.65)
# So the body is 25.35cm x 21.4cm. The widest steering case is used for the
# whole outline because the wheels are at full lock on every turn.
ROBOT_FRONT = 22.0
ROBOT_REAR = 3.35
ROBOT_WIDTH = 21.4

# Keep-clear square around the start zone, used when generating demo layouts.
# An obstacle whose virtual box abuts the start zone leaves the robot in a
# 10cm-tall band, and a 20-36cm turning radius cannot turn round in one -- the
# robot is walled in before it has moved. Real arenas leave the start clear;
# generated ones should too. Half a footprint of slack past the zone is enough.
START_KEEP_CLEAR = START_ZONE_SIZE + ROBOT_HALF

# Where the robot starts: THE one setting, used by the simulator (/api/plan) and
# the RPi (/api/navigate) alike. Nobody measures anything on the day -- the
# robot is pushed into the bottom-left corner of the arena facing North, its
# left side on the left edge and the back of its rear tyres on the bottom edge.
# The front wheels are the widest part when straight (18.9cm across), so the
# REAR-AXLE CENTRE, which is what a pose describes, is at (18.9/2, ROBOT_REAR).
START_X = 18.9 / 2.0            # 9.45cm
START_Y = ROBOT_REAR            # 3.35cm
START_THETA = math.pi / 2.0

# That pose is inside the safety margin -- the robot touches both walls -- so it
# gets one exception. While the rear axle is still inside the start zone, each
# wall only has to stay as clear as it was at the start, less this tolerance;
# everywhere else the full margin applies. So the robot may leave the corner but
# never get closer to a wall than it started, and cannot creep along a wall out
# of the zone. The tolerance covers the rear corner of the 21.4cm-wide outline
# swinging out ~0.1cm as a right turn begins.
START_WALL_TOLERANCE = 0.5

# --------------------------------------------------------------------------
# Kinematics (briefing slide 4)
# --------------------------------------------------------------------------

# Slide 4 says "a turning radius of about 25cm but it is a larger radius if robot
# moves faster", and assumes the same radius both ways. OUR robot does not turn
# symmetrically: measured at the centre of the rear axle, full left lock gives a
# 20.2cm radius and full right lock 36.2cm, whatever the sweep. That is a fault
# of this chassis (steering linkage), not a choice -- so the planner models the
# two sides separately rather than planning with one figure it would then drive
# wrongly.
#
# The radius belongs to the steering side, not to the direction the nose swings:
# LB (reverse, steering left) is also driven round the 20.2cm circle, RB round
# the 36.2cm one. Re-measure both if the steering is ever trimmed; with equal values
# the planner reduces exactly to the symmetric construction of slides 27-31.
TURNING_RADIUS_LEFT = 20.2
TURNING_RADIUS_RIGHT = 36.2

# Our robot does not turn about the middle of its footprint: with Ackermann
# steering and rear-wheel drive the turning circle is always centred on the line
# through the REAR AXLE, so the point it turns about is the rear-axle centre.
# That point is the only one on the chassis whose velocity always lies along the
# heading, which is what the Dubins/Hybrid A* kinematics assume (slides 27-33),
# so it is the point every planner pose describes -- and the radii above are
# measured about it. The body is centred this far AHEAD of the pose (derived
# from the outline, 9.325cm), so the nose swings wider than the tail on every
# turn, and the capture standoff and camera distance are taken from there.
TURNING_CENTRE_OFFSET = (ROBOT_FRONT - ROBOT_REAR) / 2.0

# --------------------------------------------------------------------------
# Obstacle avoidance (briefing slide 36)
# --------------------------------------------------------------------------

# Collisions are judged on the robot's real outline (ROBOT_FRONT/REAR/WIDTH),
# ROTATED with its heading, against the real 10cm blocks and the walls -- see
# footprint.py. Every pose along a path must keep at least this much clear of
# every obstacle and every wall. It absorbs the robot's 1-3cm turn error and the
# ribbon cable that can stick out a little. One knob for both.
SAFETY_MARGIN = 3.0

# How finely a trajectory is sampled for that check: at most this far (cm, at
# the rear axle) and at most this much rotation between samples.
COLLISION_SAMPLE_STEP = 1.0
COLLISION_SAMPLE_ANGLE = math.radians(2.0)


def _sweep_pad() -> float:
    """Half the furthest any point of the body moves between two samples.

    A point that moves d between two samples is never more than d/2 from where
    it was sampled, so checking the samples at SAFETY_MARGIN + this keeps the
    whole continuous sweep at least SAFETY_MARGIN clear. The worst point is the
    corner farthest from the turning circle's centre.
    """
    reach = max(ROBOT_FRONT, ROBOT_REAR)
    worst = COLLISION_SAMPLE_STEP                    # a straight moves every point this far
    for radius in (TURNING_RADIUS_LEFT, TURNING_RADIUS_RIGHT):
        corner = math.hypot(reach, radius + ROBOT_WIDTH / 2.0)
        worst = max(worst, corner * min(COLLISION_SAMPLE_STEP / radius, COLLISION_SAMPLE_ANGLE))
    return worst / 2.0


SWEEP_PAD = _sweep_pad()                         # ~0.72cm with the values above

# Briefing slide 36's "virtual obstacles" -- each block inflated so the robot can
# be treated as a dot -- are no longer the collision test, because no single
# inflation is right for a rotating rectangle. They survive as a cheap, slightly
# optimistic picture of where the body's MIDDLE can go: the Hybrid A* distance
# heuristic, the reachability flood fill, layout generation and the simulator's
# overlay. Half the width plus the margin is the nearest the middle can ever be.
OBSTACLE_INFLATION = ROBOT_WIDTH / 2.0 + SAFETY_MARGIN          # 13.7cm
VIRTUAL_OBSTACLE_SIZE = OBSTACLE_SIZE + 2 * OBSTACLE_INFLATION   # 37.4cm
BOUNDARY_MARGIN = OBSTACLE_INFLATION

# --------------------------------------------------------------------------
# Where to park for a photo (briefing slides 4 and 8)
# --------------------------------------------------------------------------

# Slide 8's worked target: an image at (a, b, S) wants the robot's bottom-left
# corner at (a - 10, b - 45), i.e. its CENTRE at (a + 5, b - 30). That is 30cm
# from the obstacle face along the face normal, laterally centred on the block.
# Every standoff here is to the middle of the footprint, not the turning centre
# -- the turning centre parks TURNING_CENTRE_OFFSET further back.
CAPTURE_STANDOFF = 30.0

# The single ideal pose is often unreachable (a 20-36cm turning radius plus a wall
# or a neighbouring obstacle), so every obstacle offers a *menu* of acceptable
# poses and the planner takes the first one it can actually drive to. Ordered
# best-first: the head of the list is slide 8's pose.
#
# Slide 4 wants the camera ~20cm from the image; with a 30cm footprint the
# camera sits 15cm ahead of the centre, so a 35cm standoff puts it there. Both
# 30 and 35 are comfortably inside checklist A.2's "20-50cm from the midpoint
# of the robot", which is the real acceptance criterion.
CAPTURE_STANDOFF_OPTIONS = (30.0, 35.0, 25.0, 40.0, 45.0)

# "The center of the robot does not have to be aligned exactly with the center
# of the image/obstacle" (slide 8), and slide 4 notes the camera has a conical
# field of vision -- so the robot may sit off to one side of the face normal and
# still see the image, as long as it is pointing back at the face.
#
# This is the single most important knob for reachability. Every pose in the
# menu shares a heading if you only vary the standoff, and whether a Dubins path
# exists depends almost entirely on the APPROACH HEADING. Offering the planner
# a fan of approach angles is what turns "no path found" into a path.
CAPTURE_ANGLE_OPTIONS = (0.0, 15.0, -15.0, 30.0, -30.0, 45.0, -45.0)

# How a compromise pose is scored against the ideal, for menu ordering.
# An oblique view is harder for the camera than an unusual standoff, so
# degrees are penalised about three times as hard per centimetre.
CAPTURE_ANGLE_PENALTY_SCALE = 30.0
CAPTURE_STANDOFF_PENALTY_SCALE = 10.0

# Checklist A.2: the image must end up 20-50cm from the robot's midpoint.
CAPTURE_MIN_DISTANCE = 20.0
CAPTURE_MAX_DISTANCE = 50.0

# Briefing slide 33: "After the robot has recognized an image at an obstacle,
# this obstacle is blocking the robot -- needs to reverse first." The robot
# finishes a photo parked 30cm from a face, pointing straight at it, and its
# turning radius is 20cm left / 36cm right, so EVERY forward-only path out of a capture pose
# drives into the block it just photographed. Before planning the next leg we
# therefore back straight out by one of these distances and plan the Dubins
# path from there. 0.0 is tried first so the start pose costs nothing extra.
DEPARTURE_BACKOFF_OPTIONS = (0.0, 15.0, 30.0)

# --------------------------------------------------------------------------
# Hybrid A* fallback (used only when every Dubins candidate is blocked)
# --------------------------------------------------------------------------

HA_STEP = 5.0               # arc length of one motion primitive, cm
HA_THETA_BINS = 24          # 15 degrees per bin
HA_XY_RESOLUTION = 5.0      # cm per lattice cell used for de-duplicating states
HA_GOAL_XY_TOLERANCE = 4.0  # cm
HA_GOAL_THETA_TOLERANCE = math.radians(10.0)
HA_REVERSE_COST = 2.0       # multiplier: reversing is slow and drifts
HA_GEAR_CHANGE_COST = 8.0   # cm-equivalent penalty for shifting fwd <-> rev
HA_STEER_CHANGE_COST = 2.0  # cm-equivalent penalty for a steering change
HA_MAX_EXPANSIONS = 60000   # hard stop so a hopeless goal cannot hang a demo
# While filling holes in the cost matrix we run the search dozens of times and
# most of those legs turn out to be genuinely impossible. A tighter cap keeps a
# nasty layout from turning a 0.3s plan into a 90s one; the full budget above is
# reserved for the final, committed path.
HA_MATRIX_EXPANSIONS = 2500

# Wall-clock ceiling on the whole gap-filling pass. A call-count budget is a
# poor bound because the cost of one search varies by two orders of magnitude --
# tens of milliseconds when it succeeds, over a second when it has to exhaust
# itself proving a leg impossible. Bounding the time directly is what keeps a
# nasty layout from turning a 2s plan into an 18s one. Raise it if you would
# rather wait than lose an obstacle; planning happens once, before the run.
SEARCH_TIME_BUDGET = 4.0    # seconds

# --------------------------------------------------------------------------
# Time model -- this is what makes B.3 "shortest-TIME" and not "shortest-path"
# --------------------------------------------------------------------------
#
# Turning is slower per centimetre than driving straight on the real chassis,
# and every gear/steering change costs a real pause while the servo swings.
# Measure these with a stopwatch on the actual robot and put the numbers here;
# until then they are sane estimates and the *relative* ordering they produce
# is already better than pure distance.

SPEED_STRAIGHT = 40.0       # cm/s driving straight
SPEED_TURN = 25.0           # cm/s along the arc while steering
DIRECTION_CHANGE_TIME = 0.5  # s to shift between forward and reverse
STEERING_CHANGE_TIME = 0.35  # s for the steering servo to swing over
SCAN_TIME = 2.0             # s parked at an obstacle taking the photo

# The run time the simulator measures itself against.
#
# ASSUMPTION, NOT A SPECIFICATION. Checklist B.2 says "the recognition of the 5
# images should be completed within the time limit" but never states what that
# limit is, and the algorithms briefing does not either. 6 minutes is the usual
# Task 1 allowance -- confirm it with your supervisor and correct this. Nothing
# in the planner depends on it; it only decides whether the simulator shows the
# run as inside or outside the limit.
TASK_TIME_LIMIT = 360.0

# --------------------------------------------------------------------------
# STM command grammar
# --------------------------------------------------------------------------
#
# NOTHING upstream specifies this -- it is a per-team convention. Agree it with
# whoever owns the STM board, then encode it here. The defaults follow the
# convention used by the reference repo (SC2079-MDP-Group-29):
#
#   SF100  straight forward 100cm        LF090  forward-left through 90 degrees
#   SB050  straight backward 50cm        RF090  forward-right through 90 degrees
#                                        LB090  reverse-left through 90 degrees
#                                        RB090  reverse-right through 90 degrees
#   SNAP1  take a photo of obstacle 1    FIN    path complete
#
CMD_STRAIGHT_FORWARD = "SF"
CMD_STRAIGHT_BACKWARD = "SB"
CMD_LEFT_FORWARD = "LF"
CMD_RIGHT_FORWARD = "RF"
CMD_LEFT_BACKWARD = "LB"
CMD_RIGHT_BACKWARD = "RB"
CMD_SNAP = "SNAP"
CMD_FINISH = "FIN"

COMMAND_NUM_WIDTH = 3       # zero-padded field width, e.g. 090

# If True, turn commands are rounded to the nearest 90 degrees, because some
# STM firmwares only implement quarter turns. Leave False while the firmware
# accepts arbitrary angles -- snapping throws away path accuracy.
SNAP_TO_90_TURNS = False

# Segments shorter than this are dropped rather than emitted as "SF000".
MIN_COMMAND_DISTANCE = 1.0      # cm
MIN_COMMAND_ANGLE = math.radians(1.0)

# Two arcs around the same circle merge into one command, which can legitimately
# come out as a 300-degree sweep. Plenty of STM firmwares only accept a quarter
# or half turn, so long arcs are split into repeated commands that add up to the
# same sweep. Set to 0 to disable splitting. Agree the real limit with whoever
# owns the STM board.
MAX_TURN_COMMAND_DEG = 180.0

# Likewise for very long straights, which some firmwares clamp.
MAX_STRAIGHT_COMMAND_CM = 0.0   # 0 = no limit

# --------------------------------------------------------------------------
# Server
# --------------------------------------------------------------------------

HOST = "0.0.0.0"            # listen on the LAN so the RPi can reach us

# NOT 5000: macOS binds that to the AirPlay Receiver by default, so a Mac gives
# you "Address already in use" before the server ever starts. Override with the
# PORT environment variable or --port if 5001 clashes with something too.
PORT = 5001
