# Raspberry Pi Controller

The Raspberry Pi acts as the central coordinator for the MDP robot, connecting the Android app, Algo server, STM32 and imaging system.

See [controller migration notes](CHANGELOG.md) for the current layout and the
mapping from earlier controller work.

## Architecture

```text
Android ──Bluetooth──> Raspberry Pi <──HTTP──> Algo
                            │
                            ├──UART──> STM32
                            │
                            └───────> Imaging
```

## Responsibilities

- Receive commands and arena data from Android.
- Convert Android arena data into the Algo API format.
- Request navigation commands from Algo.
- Route movement commands (`SF`, `SB`, `LF`, `RF`, `LB`, `RB`) to STM32.
- Use dedicated STM TX/RX workers: TX serializes commands and RX classifies replies/events.
- Intercept `SNAP` commands and trigger imaging.
- Handle `FIN` when navigation is complete.

## Structure

```text
rpi_controller/
├── connectors/
│   ├── algo.py
│   ├── android.py   # Android parsing, STM mapping, and image sending
│   ├── bluetooth.py
│   ├── imaging.py
│   └── stm.py
├── dummy_test/       # manual hardware and integration checks
├── imaging/          # Picamera2 capture, YOLO inference, tracking, and stitching
├── scripts/          # host-side migration and image-pull tools
├── task1/
│   ├── runtime.py    # Task 1 state machine and lifecycle wiring
│   ├── events.py     # Task 1 event/request definitions
│   └── workers/      # Android, STM, Algo, camera/inference, and transfer workers
├── task1.py          # Android-enabled Task 1 launcher
└── requirements.txt
```

## Communication

**Android ↔ RPi:** Bluetooth Classic SPP/RFCOMM via `/dev/rfcomm0`.

```text
<FW010> → SF010
```

**RPi ↔ Algo:** HTTP API on port `5001`. Algo returns commands such as:

```text
SF050
RF117
SNAP2
SB015
FIN
```

**RPi ↔ STM32:** UART via `/dev/ttyUSB0` at `115200` baud.

```text
RPi → ZH\n                 # once at the start of every movement run
STM → A ZH\n
RPi → SF010\n
STM → A d=10.0 e=+0.1\n
```

The Task1 STM protocol accepted by `connectors/stm.py` is:

```text
Movement: SF<n>, SB<n>, LF<n>, LB<n>, RF<n>, RB<n>
Control:  ZH, D, !
Field tools (only if enabled in firmware): TD, TV<800..2200>
```

`!` is an immediate one-byte emergency stop, with no newline or independent
reply; the interrupted move later responds with `A ... ABORT`. All normal
commands receive one `A` response. `TD` additionally produces numbered log
records before its final `A TD <count> results` response; RX exposes them as
events rather than assigning them to another command.

**Imaging:** `SNAPx` commands call:

```python
capture_and_predict(obstacle_id)
```

The result contains `obstacle_id`, `image_id`, and `confidence`.

## Run Task 1 from scratch

### 1. Create the new RPi checkout — host PC

From this `rpi_controller/` directory, confirm the RPi connection settings in
`scripts/migrate.py`, then run:

```bash
python3 scripts/migrate.py
```

This creates `/home/mdp/rpi_refactor` without changing either existing RPi
checkout. It syncs the current controller source, then copies `.venv/` and
every `imaging/model*` directory from
`/home/mdp/rpi_controller_mt_android`.

### 2. Start Algo — host PC

On the PC connected to the RPi network:

```bash
cd ../algo
python3 -m venv .venv          # first time only
source .venv/bin/activate
pip install -r requirements.txt
python3 server.py --host 0.0.0.0 --port 5001
```

Note the PC's IP address. It is the `ALGO_HOST` value used on the RPi.

### 3. Configure and run Task 1 — RPi

```bash
ssh mdp@10.42.0.1
cd /home/mdp/rpi_refactor
source .venv/bin/activate
python3 task1.py
```

Before running, update `task1.py` if needed:

- `ALGO_HOST` — host PC's Algo-server IP address
- `STM_PORT` — usually `/dev/ttyACM0`
- `CV_MODEL_FILENAME` — a file under `imaging/model/`
- `BLUETOOTH_DEVICE` — usually `/dev/rfcomm0`
- `RESET_BLUETOOTH_ON_START` — match the RPi Bluetooth service setup

Android sends the robot and obstacle layout. The runtime uses dedicated STM,
Algo, Android, camera, inference, and transfer workers; the Task 1 kernel is
the only component that advances mission state.

### 4. Pull captured images — host PC

After the run, return to this directory on the host PC:

```bash
python3 scripts/pull_images.py
```

It copies `/home/mdp/rpi_refactor/imaging/data/` from the RPi to local
`imaging/data/`.

Manual checks are under `dummy_test/`.
```
source .venv/bin/activate
python dummy_test/<any-file>.py
```


## Status

Software-tested:
- Android and Bluetooth message parsing
- Android → Algo arena conversion
- Algo API integration and command routing
- STM command/ACK protocol
- Imaging integration interface
- Task 1 runtime integration flow

Physical end-to-end testing with the Android app, STM hardware and actual imaging system is still pending.
