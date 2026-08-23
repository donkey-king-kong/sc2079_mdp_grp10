# Algorithm subsystem &mdash; SC2079 Group 10

Path planning for the MDP robot. The robot starts in the corner of a 2m x 2m
arena, has to drive to five obstacles, park in front of the face of each one
that carries an image, photograph it, and do the whole thing in the shortest
time it can.

This module covers **section B of the project deliverables checklist**:

| Item | Requirement | Where it lives |
| --- | --- | --- |
| **B.1** | Movement area simulator: the 2m x 2m arena, the start zone, obstacles and image positions, with the robot animated forward/backward and turning on a grid map | `static/index.html` + `server.py` |
| **B.2** | Hamiltonian path: an algorithm that starts in the start zone and visits each image position once | `planner.py`, strategy `nearest` |
| **B.3** | Shortest-**time** Hamiltonian path | `planner.py`, strategy `exhaustive` |

Code comments cite slide numbers from `algarithms_briefing_25S2.pdf` wherever a
formula or a constant comes from the briefing.

## Running it

```bash
cd algo
python3 -m venv venv && source venv/bin/activate
pip install -r requirements.txt          # flask is the only dependency
python3 server.py                        # http://localhost:5001
```

Then open <http://localhost:5001>. Press **Randomise layout**, then **Plan
path**, then **Play**.

> Port 5001, not 5000: macOS gives 5000 to the AirPlay Receiver, so a Mac fails
> to bind it. Override with `python3 server.py --port 8000` or `PORT=8000`.

Tests:

```bash
python3 run_tests.py                     # no dependencies needed at all
python3 -m pytest tests/ -v              # same tests, nicer output (pip install pytest)
python3 -m pytest tests/test_dubins.py -v -k slide_43     # one test

node tests/playback.js                   # the simulator's animation (needs the server running)
```

`tests/playback.js` is separate because it needs Node, and because the animation
is the one part that can fail *silently* -- the robot just sits at the start line
with no error anywhere. It loads the real page script against a stub canvas,
pumps synthetic frames through the real animation loop, and checks the robot
drives the whole path. Run it after touching `static/index.html`.

## Demonstrating the checklist items

**B.1** &mdash; open the simulator. The canvas is the 200cm x 200cm area drawn as
the same 20x20 grid the Android tablet uses, with the 40cm start zone shaded
green. Click a cell to add an obstacle; click it again to rotate which face
carries the image (the thick amber edge). Drag to move, shift-click to delete.
Press **Play** and the robot drives the planned path, forwards, backwards and
turning, with a live clock and the STM commands highlighted as they execute.

If a supervisor asks to see a particular motion rather than waiting for the run
to produce it, the **Manual drive** buttons move the robot one STM command at a
time &mdash; forward, backward, and left/right arcs in both gears. They go
through the same parser, the same kinematics and the same collision check as the
planner, so a move that would clip a virtual obstacle or leave the arena is
refused and says so. Note there is no on-the-spot turn: every turn is an arc at
the 25cm turning radius, which is the honest behaviour of the real chassis.

Tick **Virtual obstacles** to show the inflated no-go regions the planner
actually reasons about, and **Capture poses** to show every pose it considered
standing at. Between them they explain any "unreachable" result on the spot,
which is worth having in front of you when a supervisor asks.

**B.2** &mdash; choose **Nearest neighbour** and plan. This is the greedy
`nearestNeighbour()` of briefing slide 14: from wherever the robot is, price the
leg to every obstacle it has not visited, take the cheapest, repeat. The
**Images recognised** counter starts at zero and counts up as each photo is
taken, which is what the checklist scores: "the number of images recognized
within the time limit is accepted".

**B.3** &mdash; choose **Exhaustive** and plan, or press **Compare B.2 vs B.3**
to run all three strategies on the same layout and see them side by side. The
exhaustive search is slide 16's: score all 5! = 120 orderings and keep the best.
On most layouts it comes in several seconds faster than the greedy path, and on
awkward ones it reaches obstacles the greedy strands itself before.

## How a plan is built

Four layers, called in this order.

### 1. `arena.py` &mdash; where should the robot stand?

Each obstacle shows its image on one of N/S/E/W. The robot has to end up in
front of that face, pointing at it. Briefing slide 8 gives the ideal pose: for
an image at `(a, b, S)`, put the robot's bottom-left corner at `(a-10, b-45)`
facing north &mdash; 30cm out from the face along its normal, squared up to it.
`tests/test_arena.py` pins our geometry to exactly that.

That single pose is often unreachable, so each obstacle publishes a **menu** of
acceptable poses: a fan of standoffs (25&ndash;45cm) and approach angles
(0&deg; to &plusmn;45&deg;) around the face midpoint, always turned to point
back at the image. The briefing allows this &mdash; "the center of the robot
does not have to be aligned exactly with the center of the image" (slide 8), and
the camera has a conical field of view (slide 4) &mdash; and checklist A.2's
"20-50cm from the midpoint of the robot" is the real acceptance test, which
every pose in the menu satisfies.

**The approach angle is what matters.** Whether a path exists depends almost
entirely on the heading the robot arrives on, so the planner is given a spread
of angles rather than the best few poses by rank, which would all share one
heading. An obstacle in a 20cm strip under the top wall cannot be entered
head-on at any standoff and is easy to enter at 45&deg;.

Collision checking follows slide 36: inflate each 10cm obstacle by half a robot
into a 40cm "virtual obstacle", inset the walls by the same 15cm, and treat the
robot as a point at its centre.

### 2. `dubins.py` and `hybrid_astar.py` &mdash; how does it get there?

The robot cannot turn on the spot, so a straight line between two poses is not a
path it can follow.

**Dubins** (`dubins.py`) is the analytic answer: for a car that only drives
forward at a fixed turning radius, the shortest path between two poses is one of
six shapes &mdash; `LSL RSR LSR RSL RLR LRL` (slide 18). All six are built and
the shortest collision-free one wins. It is microseconds, and provably optimal
when nothing is in the way. Slide 43's worked `rsr` example is reproduced to two
decimal places in `tests/test_dubins.py`.

**Hybrid A\*** (`hybrid_astar.py`) is the fallback for legs no Dubins path can
serve. It searches continuous poses with motion primitives that include reverse,
so it can three-point-turn into a tight spot, and de-duplicates states on a
coarse lattice. Its heuristic is an obstacle-aware distance field from a single
Dijkstra sweep out from the goal, and it splices in an analytic Dubins shot when
one becomes available, so it lands on the goal pose exactly.

Two details matter more than they look:

- **Reversing out of a capture pose is mandatory, not an optimisation.** The
  robot finishes a photo 30cm from an obstacle face pointing straight at it, and
  the turning radius is 25cm &mdash; so every forward-only path out drives into
  the block it just photographed. Slide 33 says the same thing. Each leg
  therefore tries backing straight out first, shortest reverse that works.
- **Three segments is not always enough.** A Dubins path cannot express "along
  the bottom, up the right-hand side, then in", which a cluttered arena needs
  constantly. So the roadmap carries `transit` poses in the open parts of the
  arena and a Floyd-Warshall pass lets a leg route through them: two
  collision-free Dubins paths joined end to end are still a drivable trajectory.
  This recovers most of the missing connectivity analytically and leaves the
  search as a genuine last resort.

### 3. `planner.py` &mdash; what order?

An obstacle is not a point &mdash; it is a menu of poses, and which one you pick
changes the cost of the leg in *and* the leg out. So this is a generalised TSP.
The visit order comes from one of three strategies; for any fixed order the pose
choice is then solved **exactly** by a small DP over the layers, cheap enough to
run inside the exhaustive search.

| Strategy | What it does | Checklist |
| --- | --- | --- |
| `nearest` | greedy nearest-neighbour (slide 14) | **B.2** |
| `greedy_swap` | greedy, then 2-swaps until nothing improves (slide 15) | &mdash; |
| `exhaustive` | all 120 orderings, keep the best (slide 16) | **B.3** |

The all-pairs cost matrix is the expensive part and does not depend on the
strategy, so it is built once and shared. That is what makes "compare all
strategies" cost barely more than one plan.

"Best" here means optimal over the capture poses the roadmap carries (8 per
obstacle, chosen for spread of approach angle) and the leg costs in the matrix
— not over the full 35-pose menu. Widening `POSES_PER_OBSTACLE` makes it
closer to globally optimal and quadratically slower. On an open layout the
greedy order is often already optimal and B.2 and B.3 agree; over 60 random
layouts the exhaustive search was strictly faster on about 70% of them.

**The cost is time, not distance.** `leg_cost()` scores a leg with the model in
`config.py`: straights at 40cm/s, arcs at 25cm/s, plus a real penalty every time
the gear or the steering has to change, plus the dwell at each obstacle while
the photo is taken. Distance and time genuinely disagree &mdash; a path made of
six alternating micro-turns can be shorter than one long straight and much
slower to drive. This is what makes B.3 *shortest-time* rather than
shortest-path. Pass `"metric": "distance"` to compare against the naive version.

### 4. `commands.py` &mdash; geometry to STM strings

```
SF100  straight forward 100cm      LF090  forward, steering left, 90 degrees
SB050  straight backward 50cm      RF090  forward, steering right, 90 degrees
SNAP3  photograph obstacle 3       LB090  reverse, steering left, 90 degrees
FIN    path complete               RB090  reverse, steering right, 90 degrees
```

**This grammar is not specified anywhere upstream.** Field widths, whether a
turn carries an angle or an arc length, whether `L` means the steering goes left
or the nose swings left &mdash; all of it is a convention to agree with whoever
owns the STM board. It all lives in `config.py`, and `parse()` /
`commands_to_trajectory()` exist so the tests can drive a planned path out to
strings and replay it back, which is the only way to catch a bad field width
before the robot drives into a wall.

Segments are merged before being emitted: Hybrid A\* produces one segment per
5cm primitive, and sending forty `SF005`s instead of one `SF200` means forty
accelerate-and-stop cycles &mdash; far slower and far less accurate on real
hardware.

## `config.py` is the single calibration point

Every tunable constant lives there with the slide it came from written next to
it. It is the only file that should need touching when calibrating against the
real robot. The ones most likely to be wrong:

- `TURNING_RADIUS` (25cm from slide 4, and larger the faster the robot goes)
- `CAPTURE_STANDOFF` (30cm, derived from slide 8)
- `SPEED_STRAIGHT`, `SPEED_TURN`, `DIRECTION_CHANGE_TIME`, `STEERING_CHANGE_TIME`
  &mdash; **measure these with a stopwatch**; they are estimates, and they are
  what B.3 optimises against
- `COMMAND_NUM_WIDTH`, `SNAP_TO_90_TURNS`, `MAX_TURN_COMMAND_DEG` &mdash; must
  match the STM firmware

Coordinates are centimetres with the origin at the arena's bottom-left. A robot
pose is `(x, y, theta)` about the robot's **centre**, not the bottom-left corner
the briefing uses on slide 7; `arena.bottom_left_to_centre()` converts at the
boundary. `theta` is radians, East = 0, counter-clockwise.

Note `START_X`/`START_Y` are 20, not 15. Slide 7's corner position puts the
robot exactly on the boundary margin, where the first left turn dips a
fraction of a millimetre outside the arena and every left-handed path out of the
start zone is rejected. The start zone is 40cm and the planning footprint 30cm,
so centring the robot in it costs nothing and buys 5cm of slack.

## Talking to the rest of the system

`server.py` is both the simulator backend and the service the RPi calls, on
purpose: the week-7 demo and the week-8 robot run go through identical planning
code.

| Endpoint | Purpose |
| --- | --- |
| `GET /health` | liveness, for the RPi to check the laptop is up |
| `GET /api/config` | the constants, so the UI draws the world the planner believes in |
| `GET /api/random` | a random legal layout, for demos |
| `POST /api/plan` | the real contract |
| `POST /api/compare` | every strategy on one layout, for the B.2 vs B.3 view |
| `POST /api/navigate` | the same planner in the RPi's existing message format |

```bash
curl -X POST http://localhost:5001/api/plan \
  -H 'Content-Type: application/json' \
  -d '{"obstacles":[{"id":1,"x":8,"y":5,"face":"S"},
                    {"id":2,"x":14,"y":6,"face":"W"}],
       "strategy":"exhaustive"}'
```

`x`/`y` are indices into the 20x20 grid the tablet draws (send `"units":"cm"` to
use centimetres of the obstacle's bottom-left corner instead); `face` &mdash; or
`dir`, both are accepted &mdash; is the side carrying the image.

The response carries `order`, `commands`, `total_duration`, `unreachable`, and
`legs`, each leg with its own `commands` and `trajectory` so the RPi can send
one leg, wait for the photo, and then send the next. Every pose carries a `t`
stamp from the planner's own time model, so the simulator's clock and the
reported total are the same number by construction.

`POST /api/navigate` takes `{"type":"START_TASK","data":{"robot":…,"obstacles":…}}`
and returns `{"type":"NAVIGATION","data":{"commands":…,"path":…}}`, matching the
message format the RPi code already uses.

## Tests document the invariants

Read `tests/` before changing anything in `dubins.py`, `hybrid_astar.py` or
`planner.py`. The ones that matter most:

- `test_dubins.py::test_slide_43_worked_example` &mdash; reproduces the
  briefing's hand-worked `rsr` path, so the geometry is pinned to the
  coursework's rather than merely being self-consistent.
- `test_dubins.py::test_every_candidate_lands_on_the_goal` &mdash; integrates
  each path from the start pose and checks it arrives at the goal. A sign error
  in tangent selection cannot survive this.
- `test_planner.py::test_exhaustive_is_never_worse_than_greedy` &mdash; the
  whole of B.3's claim over B.2.
- `test_planner.py::test_every_leg_is_collision_free` &mdash; a plan that clips
  an obstacle is worse than no plan.
- `test_commands.py::RoundTrip` &mdash; a planned route survives being turned
  into command strings and replayed.

## Known gaps

- **Speeds are estimated, not measured.** Everything in the time model is a
  guess until someone stopwatches the robot. The *relative* ordering it produces
  is already better than optimising distance, but the absolute seconds are not
  trustworthy yet. This is the highest-value thing to fix.
- **No recovery behaviour for a camera miss.** Briefing slides 37&ndash;39
  describe what to do when the robot finds a bull's-eye instead of an image, or
  no obstacle at all: reverse and go round the block, or roam. That needs the
  live camera loop before it can be written, and it is not part of section B.
- **Hard layouts can end up partial.** On a badly cluttered arena the planner
  may reach only four of the five obstacles, and says so in `unreachable`
  rather than failing outright. Checklist B.2 scores the images actually
  recognised, so this is the right behaviour, but it is worth knowing about.

  Measured over 60 random layouts (`random_layout`, seeds 1000&ndash;1059,
  all three strategies each): planning takes **3.3s on average, 8.8s worst
  case**, and the exhaustive search reaches all five obstacles on **45 of 60**
  against the greedy walk's 37. Where both reach the same number, exhaustive is
  strictly faster on 34 of 48 and saves **9.5% of the run time** on average.

  The knob here is `SEARCH_TIME_BUDGET` (4s), the wall-clock ceiling on the
  Hybrid A* rescue pass. Raising it recovers a few more obstacles at the cost of
  a longer wait: with the budget lifted entirely, those same 60 layouts give 48
  of 60, but the worst case goes from 8.8s to 18.1s. Planning happens once,
  before the robot moves, so it is worth turning up if a supervisor is watching
  one specific layout.
- **`exhaustive` does not scale past about 8 obstacles.** 120 orderings is
  nothing; 10 obstacles would be 3.6 million. Task 1 only ever has five. If that
  changes, replace `_exhaustive_order` with a Held-Karp DP over subsets.
- **Task 2 (fastest car) is not implemented.** It is not covered by the briefing
  or by section B of the checklist.
