"""Single calibration point for the Algorithm subsystem.

Every tunable number lives here, with the slide of `algarithms_briefing_25S2.pdf`
it came from written next to it. When the robot is finally on the arena and the
numbers are wrong, this is the only file you should need to touch.

Coordinate conventions used by the whole package
------------------------------------------------
* Units are centimetres. Origin is the arena's BOTTOM-LEFT corner, x to the
  East, y to the North (briefing slide 7).
* An obstacle's position is its BOTTOM-LEFT corner, as in the briefing.
* A robot pose is ``(x, y, theta)`` about the robot's **centre**, not its
  bottom-left corner. The briefing uses the corner on slide 7; centre maths is
  far less error-prone for Dubins curves, so `arena.bottom_left_to_centre()`
  converts at the boundary.
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

# Keep-clear square around the start zone, used when generating demo layouts.
# An obstacle whose virtual box abuts the start zone leaves the robot in a
# 10cm-tall band, and a 25cm turning radius cannot turn round in one -- the
# robot is walled in before it has moved. Real arenas leave the start clear;
# generated ones should too. Half a footprint of slack past the zone is enough.
START_KEEP_CLEAR = START_ZONE_SIZE + ROBOT_HALF

# Robot starts in the start zone facing North.
#
# Slide 7 puts the bottom-left corner at (0, 0), i.e. the centre at (15, 15).
# That is exactly BOUNDARY_MARGIN from both walls, which makes the very first
# left turn a boundary violation before the robot has moved a centimetre --
# the turning circle dips a couple of millimetres past x = 15 and every
# left-handed Dubins word out of the start zone is rejected. Since the start
# zone is 40cm and the planning footprint is 30cm, centring the robot in it
# costs nothing, keeps it entirely inside the zone, and buys 5cm of slack on
# each wall. Set these back to ROBOT_HALF if your robot really is corner-parked.
START_X = 20.0
START_Y = 20.0
START_THETA = math.pi / 2.0

# --------------------------------------------------------------------------
# Kinematics (briefing slide 4)
# --------------------------------------------------------------------------

# "There is a turning radius of about 25cm but it is a larger radius if robot
# moves faster." This and CAPTURE_STANDOFF are the two values most likely to
# need re-measuring against the real robot.
TURNING_RADIUS = 25.0

# --------------------------------------------------------------------------
# Obstacle avoidance (briefing slide 36)
# --------------------------------------------------------------------------

# "A simple way is to make virtual obstacles and consider the robot as a dot.
# The robot's footprint is 30cm x 30cm so the virtual obstacle should be
# 40cm x 40cm" -> inflate the 10cm block by 15cm on every side.
OBSTACLE_INFLATION = ROBOT_HALF                 # 15cm
VIRTUAL_OBSTACLE_SIZE = OBSTACLE_SIZE + 2 * OBSTACLE_INFLATION   # 40cm

# Same idea for the walls: the robot's centre can never be closer than half a
# footprint to the arena boundary.
BOUNDARY_MARGIN = ROBOT_HALF                    # 15cm

# How finely a trajectory is sampled when checking it for collisions. 2cm is
# well under the 15cm of slack the inflation gives us, so nothing can tunnel
# through a corner between samples.
COLLISION_SAMPLE_STEP = 3.0

# --------------------------------------------------------------------------
# Where to park for a photo (briefing slides 4 and 8)
# --------------------------------------------------------------------------

# Slide 8's worked target: an image at (a, b, S) wants the robot's bottom-left
# corner at (a - 10, b - 45), i.e. its CENTRE at (a + 5, b - 30). That is 30cm
# from the obstacle face along the face normal, laterally centred on the block.
CAPTURE_STANDOFF = 30.0

# The single ideal pose is often unreachable (a 25cm turning radius plus a wall
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
# turning radius is 25cm, so EVERY forward-only path out of a capture pose
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
