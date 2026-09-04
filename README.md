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
| `data.robot` | no | Robot start position as a grid cell, same 20x20 grid as the tablet. `x`/`y` are the bottom-left cell of the robot, `dir` is the facing (`N`/`S`/`E`/`W`). Defaults to the start zone, facing north. |
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
