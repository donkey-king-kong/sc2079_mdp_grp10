# SC2079 MDP - Group 10

## Algorithm (`algo/`)

Path planning for Task 1: work out which order to visit the five obstacles in,
where to park to photograph each one, and what to send the STM board. Covers
checklist section B &mdash; B.1 movement area simulator, B.2 Hamiltonian path,
B.3 shortest-time Hamiltonian path.

```bash
cd algo
python3 -m venv venv && source venv/bin/activate
pip install -r requirements.txt
python3 server.py            # simulator at http://localhost:5001
```

The same Flask server is both the simulator used for the checklist demo and the
service the Raspberry Pi calls during a run, so the demo and the robot execute
identical planning code. See [`algo/README.md`](algo/README.md) for the API
contract, the design, and what still needs calibrating against the real robot.

### For the RPi team: endpoints, request and response

The RPi only needs two endpoints. Everything else under `/api/` exists for the
browser simulator and is not part of the RPi contract.

| Endpoint | When | Purpose |
| --- | --- | --- |
| `GET /health` | before a run | Confirms the laptop is up. Returns `{"status": "ok"}`. |
| `POST /api/navigate` | once, after the tablet sends the arena | Plans the whole run and returns the command list. |

`/api/plan` is the same planner behind a different request shape, used by the
simulator. Do **not** send the `START_TASK` message there: it expects
`obstacles` at the top level and will answer 400.

**Request body** for `POST /api/navigate` (`Content-Type: application/json`):

```json
{
  "type": "START_TASK",
  "data": {
    "robot":     {"x": 1, "y": 1, "dir": "N"},
    "obstacles": [
      {"id": 1, "x": 8,  "y": 5,  "dir": "S"},
      {"id": 2, "x": 14, "y": 6,  "dir": "W"}
    ],
    "strategy":  "exhaustive",
    "metric":    "time"
  }
}
```

| Field | Required | Meaning |
| --- | --- | --- |
| `type` | no | Ignored by the server. Kept for the RPi's own message routing. |
| `data.robot` | no | **Ignored** (logged only). The robot is always pushed into the bottom-left corner facing north, and the server plans from that one configured start (`START_X`/`START_Y` in `algo/config.py`), exactly as the simulator does. |
| `data.obstacles` | **yes** | One entry per obstacle. `x`/`y` are the grid cell of the obstacle, `dir` (or `face`) is the side carrying the image. `id` is echoed back in `SNAP<id>` and `order`; it defaults to the 1-based index if missing. Must be a non-empty list. |
| `data.strategy` | no | `nearest`, `greedy_swap` or `exhaustive` (default). Use `exhaustive`. |
| `data.metric` | no | `time` (default) or `distance`. Use `time`. |

Planning takes about 3s on average and up to 9s on a cluttered layout, so give
the HTTP call a timeout of at least 15s.

**Response body**, HTTP 200 (this is the real reply to the request above):

```json
{
  "type": "NAVIGATION",
  "data": {
    "commands":       ["SF050", "RF117", "SF050", "LF012", "SNAP2",
                       "SB015", "LF180", "LF059", "SF079", "LF180", "LF001", "SNAP1",
                       "FIN"],
    "path":           [[2, 2], [2, 3], [2, 4], [2, 5], "..."],
    "order":          [2, 1],
    "unreachable":    [],
    "total_duration": 21.03
  }
}
```

| Field | Meaning |
| --- | --- |
| `data.commands` | The full run, in order, as strings. Send the driving ones to the STM one at a time; on `SNAP<id>` take the photo; on `FIN` stop. See the table below for what each string means. This is the only field the RPi needs. |
| `data.path` | The robot's route as grid cells, de-duplicated, for drawing on the tablet. |
| `data.order` | Obstacle ids in visiting order. |
| `data.unreachable` | Obstacle ids the planner could not find a path to. They have no `SNAP` in `commands`. Empty on a normal layout. |
| `data.total_duration` | Estimated run time in seconds, from the planner's speed model. |

**Errors**: a malformed request gets HTTP 400 with `{"error": "<reason>"}`, for
example a missing `obstacles` list or a `dir` that is not one of N/S/E/W. A
planner crash gets HTTP 500 with the same shape. Anything other than a 200 with
`"type": "NAVIGATION"` should be treated as no plan.

The reference implementation of the client side is
`rpi_controller/connectors/algo.py`, and `rpi_controller/manager.py` shows the
`commands` list being walked and routed to the STM or the camera.

### Commands sent to the RPi

The planner's output is a list of short ASCII strings. The RPi forwards the
driving ones to the STM in order, and treats `SNAP` and `FIN` as its own cues.
Every number is three digits, zero-padded.

| Command | Example | Number | Meaning |
| --- | --- | --- | --- |
| `SF<cm>` | `SF100` | distance in cm | Drive straight forward 100cm. |
| `SB<cm>` | `SB015` | distance in cm | Drive straight backward 15cm. Every leg after a photo starts with one of these, to back away from the obstacle before turning. |
| `LF<deg>` | `LF090` | angle in degrees | Drive forward with the steering at full left lock until the heading has turned 90&deg; anticlockwise. |
| `RF<deg>` | `RF045` | angle in degrees | Drive forward with the steering at full right lock until the heading has turned 45&deg; clockwise. |
| `LB<deg>` | `LB090` | angle in degrees | Reverse with the steering at full left lock through 90&deg;. The nose swings clockwise. |
| `RB<deg>` | `RB090` | angle in degrees | Reverse with the steering at full right lock through 90&deg;. The nose swings anticlockwise. |
| `SNAP<id>` | `SNAP3` | obstacle id | The robot is parked facing the image on obstacle 3. Take the photo and run recognition before sending the next command. Not sent to the STM. |
| `FIN` | `FIN` | none | The run is complete. Not sent to the STM. |

Notes for whoever is on the receiving end:

- Turns are the **angle the heading changes by**, not an arc length, and
  assume the STM holds full lock at a 25cm turning radius. `LF090` from a
  standstill ends 25cm ahead and 25cm to the left, facing left.
- First letter is the steering (`L`/`R`, or `S` for straight), second is the
  gear (`F`/`B`).
- Turns can be any angle, e.g. `RF074`. No single turn exceeds 180&deg;; a
  longer sweep is split into consecutive commands.
- A real run, one leg per line:

  ```
  RF074 SF036 RF016 SNAP1
  SB015 LF091 SF064 LF134 SNAP5
  SB015 LF037 SF027 RF007 SNAP3
  SB015 LF160 SF055 RF100 SNAP4
  SB015 RF038 SF055 LF008 SNAP2
  FIN
  ```

This is the list that comes back as `data.commands` from `/api/navigate`. The
prefixes, field width and turn limits are a team convention, not a spec; they
live in `algo/config.py` and are a one-line change if the STM firmware wants
something different.

## Android App

The Android application serves as the main remote controller and monitoring interface for the MDP robot. It handles Bluetooth communication with the Raspberry Pi, visualizes the 20x20 arena map, and issues commands for tasks like exploration and image recognition.

### Getting Started

1. **Prerequisites**: Download and install [Android Studio](https://developer.android.com/studio).
2. **Open Project**: Launch Android Studio, click **Open**, and select the `android/` folder from this repository.
3. **Sync Gradle**: Allow Android Studio to download the necessary dependencies and sync the project.
4. **Testing UI**: You can run the app on the built-in Android Emulator to test the Grid Map, layouts, and navigation.
5. **Testing Bluetooth (Important)**: The Android Emulator **does not support Bluetooth**. To test the actual connection to the Raspberry Pi/robot, you must connect a physical Android device and deploy the app via USB Debugging.

### Build & Run

1. In Android Studio, wait for the indexing and Gradle sync to finish (indicated by the progress bar at the bottom).
2. Select your target device from the device dropdown menu in the top toolbar (either your connected physical device or a created Virtual Device).
3. Click the **Run** button (green play icon ▶️) or press `Control + R` (Mac).
4. Android Studio will compile the app (APK) and automatically launch it on your selected device.

Or via command line (tablet connected via USB with USB debugging enabled):

```bash
cd android
./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

### AMD Tool Setup (Windows)

The app is designed to work with the **Android Module Debugger (AMD)** tool running on a Windows laptop. The AMD tool sends arena and robot position data to the tablet over Bluetooth.

**Required one-time setup:**

1. Copy `android/scripts/defaultJson.cs` to the AMD tool's `scripts/` folder on the Windows laptop.
2. In AMD: **Settings → Custom Scripts** → select `defaultJson.cs`.
3. In AMD: **Settings → Default Arena Settings** → set arena to **20×20**.
4. Pair the tablet's Bluetooth to the Windows laptop before launching the app.

> `defaultJson.cs` sends obstacle grid and robot position in the JSON format the tablet expects. Without it, the tablet will not receive robot position updates.

### Manual & Auto Buttons

| Button | Behaviour |
|--------|-----------|
| **Manual** | Sends a single `sendArena` request to AMD — tablet receives current obstacle grid and robot position immediately. |
| **Auto** | Polls `sendArena` every 2 seconds automatically. Button turns green when active. Stops when toggled off or when the app backgrounds. |

### Bluetooth Message Formats

| Keyword | Handler | Format |
|---------|---------|--------|
| `location` | Move vehicle on grid | `{"location":"update","value":{"x":"5","y":"3","d":"N"}}` |
| `"grid"` | Place obstacles on grid | `{"grid":"<hex string>"}` |
| `image-rec` | Mark obstacle as verified | contains `image-rec` |
| `status` | Update robot status text | contains `status` |
| `health` | API health check | contains `health` |

## STM32 Firmware
 
Firmware for the Wheeltec STM32F407VET6 board (C30D, rev 23.0). It drives the
robot (two rear DC motors with encoders and a front steering servo), reads the
ICM-20948 gyro, and executes the movement commands the RPi forwards from the
planner.
 
### Toolchain and opening the project
 
| | |
| --- | --- |
| IDE | STM32CubeIDE 1.9.0 (Help → About STM32CubeIDE) |
| MCU | STM32F407VET6, STM32 HAL + FreeRTOS (CMSIS-RTOS2) |
| Hardware configuration | The `.ioc` file (opens in CubeMX inside CubeIDE) |
 
1. In STM32CubeIDE: **File → Open Projects from File System…**, choose the STM
   project folder, **Finish**.
2. Build with **Project → Build Project** (the hammer icon).
3. Flash: Run with the ST-Link Debugger or via FlyMCU.
4. After power-on, **keep the robot still for about 5 s** while the gyro
   calibrates ("Calibrating Gyro..." on the OLED). The robot is ready when the
   OLED shows the dashboard.
If you change the `.ioc` and regenerate code, CubeMX rewrites `main.c` except
inside the `USER CODE BEGIN` / `END` blocks. The motion code lives in separate
files, which CubeMX never touches.
 
### Code layout
 
| File | Contents | Edit when |
| --- | --- | --- |
| `Core/Inc/robot_config.h` | **Every tunable value**, plus the build switches | Calibration |
| `Core/Src/motion.c`, `Core/Inc/motion.h` | Straights, turns, wheel speed control, heading | Logic changes |
| `Core/Src/field_tools.c`, `Core/Inc/field_tools.h` | Knob-and-button calibration modes, `TD` log | Rarely |
| `Core/Src/main.c` | CubeMX setup and the FreeRTOS tasks (commands, motor loop, gyro, OLED, ultrasonic) | Rarely |
 
How it works, briefly: each rear wheel has its own speed controller. Every
move speeds up, cruises, slows to a slow approach and brakes early by an amount
based on its measured speed. Straights steer with the servo to hold their
heading and their line. Turns go to full steering lock and stop on the gyro.
The heading is **absolute for the whole run**: each move aims for its true
direction, so small errors are corrected by the next move instead of adding up.
 
### Calibration values: `robot_config.h`
 
**Build switches** (top of the file) – check these before every run:
 
| Switch | Task run | Calibration |
| --- | --- | --- |
| `ARENA_OUTDOOR` | `0` indoor (lab), `1` outdoor (tiled corridor) | the floor being calibrated |
| `FIELD_TOOLS` | **`0`** (the user button can't start a test move) | `1` |
| `DEBUG_REPLIES` | `0` (short replies) | `0` or `1` |
 
**Floor-dependent values** have one set for indoor and one for outdoor. The
outdoor set is still a copy of indoor; the compiler prints a warning if you
select it before it has been calibrated.
 
| Value | Indoor | What it is | How to calibrate |
| --- | --- | --- | --- |
| `TICKS_PER_CM` | 75.5 | Encoder ticks per cm | `SD +100`: tape distance vs. `d=` |
| `STOP_T` | 0.050 s | Straights: roll after the stop decision | `SD +20`: tape-measured overshoot |
| `TURN_STOP_T` | 0.075 s | Turns: rotation after the stop decision | `TN` turns: average `e=` |
| `TURN_RADIUS_L` / `_R` | 19.7 / 36.0 cm | Turn radii (speed profile timing only) | `LF180` / `RF180` diameter, see below |
| `SERVO_TRUE_CENTRE` | 1500 µs | Straight-ahead servo value | `t=` in the `TD` log trends away from 0 |
| `MOTOR_FF_BASE`, `MOTOR_KICK_PWM` | 2100, 3300 | Rolling PWM and start burst | only if moves stall or stutter |
| `SPEED_PROFILE` | 2 | 65 cm/s straights, 45 cm/s turns | set before calibrating |
 
Values that are the same on every floor (steering locks, gains, geometry) are
further down the file, with comments explaining each.
 
**Calibration tools** (`FIELD_TOOLS 1`, no cable needed): the knob and the user
button run test moves.
- **`SD`** mode: the knob picks a distance (−100…+100 cm); tap the button to drive.
- **`TN`** mode: the knob picks a turn (`LF090`, `RB045`, `LF180`, …); tap to run.
- Hold the button ~1 s to switch modes; tap during a move to stop it.
- Afterwards, send **`TD`** over serial to print every result with full
  diagnostics.
### Robot measurements
 
| Quantity | Value |
| --- | --- |
| Wheelbase (hub to hub) | 14.75 cm |
| Rear track, centre to centre (outer edges) | 15.7 cm (18.5 cm) |
| Wheel diameter / tyre width | 6.7 cm / 2.8 cm |
| Encoder scale | 75.5 ticks per cm |
| Servo: straight ahead / left lock / right lock | 1500 / 940 / 2080 µs (mechanical limits ≈ 915 / 2100) |
| Cruise speed: straights / turns | 65 cm/s / 45 cm/s (plus ≈ 0.5 s fixed per move) |
| **Turning radius at the rear-axle centre** | **left ≈ 20–21 cm, right ≈ 36 cm** (reverse turns the same) |
 
The turning radii are **provisional until calibrated on the task floor**, and
the left radius grows slightly with speed. They are very different on each
side, so the planner needs a separate radius per direction; its current
25 cm assumption no longer applies.
 
To measure a radius: put a tape measure on the floor against the outer edge of
the rear tyre **on the inside** of the turn, run `TN LF180` (or `RF180`) and
read where that tyre edge lands. The distance moved is the diameter of that
edge's circle, so **radius = distance ÷ 2 + 9.25 cm** (half the outer track,
converting from the tyre edge to the rear-axle centre).
 
### Communication with the RPi
 
USART3 at **115200 baud, 8N1** (the board's UART3 port; PD8 / PD9).
 
**Commands**: one per line, ending in `\n` (or `\r`), at most 14 characters.
Numbers can be any width; the planner's 3-digit format (`SF050`) is fine.
 
| Command | Meaning |
| --- | --- |
| `SF<cm>`, `SB<cm>` (also `S`, `F`, `B`) | Straight forward / backward |
| `LF<deg>`, `RF<deg>`, `LB<deg>`, `RB<deg>` | Turn at full lock, forward or reverse, by `<deg>` of heading change |
| `ZH` | "Zero heading": the robot's current direction becomes north for the run. **Send once after placing the robot, before the first move** (and again after any re-placement). If no `ZH` arrives, the first move sets north itself |
| `!` | Emergency stop: a single character, no newline needed. Stops the current move immediately |
| `D` | Task done (shown on the OLED) |
| `TD`, `TV<us>` | Calibration only (`FIELD_TOOLS 1`): print the result log / set the knob value |
 
**Replies**: every command gets exactly one reply, ending in `\n`. **Every reply
starts with `A`.**
 
| Reply | Meaning |
| --- | --- |
| `A d=99.9 e=+0.4` | Straight finished: distance travelled (cm), final heading error (°, + = left) |
| `A a=90.2 e=-0.1` | Turn finished: angle turned (°), final heading error (°) |
| `A ZH` | Heading zeroed |
| `A ERR <command>` | Command not recognised |
| … ` ABORT` | The move was stopped by `!` or the user button |
| … ` TO` | The move timed out (e.g. the robot was blocked) |
 
A reply ending in `ABORT` or `TO` means **the move did not finish**.
 
**Moves are blocking**: the reply is sent only once the robot has stopped, so
**wait for each reply before sending the next command**. A move normally takes
1–3 s. The STM gives up after roughly 2.5 s + distance ÷ 12 cm/s for a
straight (≈ 11 s for `SF100`), or 2.3 s + arc length ÷ 14 cm/s for a turn
(≈ 10 s for `RF180`), so a **read timeout of 20 s** on the RPi covers every
move.
 
### Task-day checklist (STM)
 
1. In `robot_config.h`: `FIELD_TOOLS 0`; `ARENA_OUTDOOR` to match the announced
   arena. Build and flash.
2. Freshly charged battery, with the battery plug secured.
3. Power on and keep the robot still until the OLED shows the dashboard (~5 s).
4. Place the robot with **both rear tyres against a ruler** lined up with a
   grid line (the rear axle sets the direction; the front wheels steer).
5. The RPi sends `ZH`, then the planned commands.
