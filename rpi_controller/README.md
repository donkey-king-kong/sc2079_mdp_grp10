# Raspberry Pi Controller

The Raspberry Pi acts as the central coordinator for the MDP robot, connecting the Android app, Algo server, STM32 and imaging system.

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
│   ├── android.py
│   ├── bluetooth.py
│   ├── imaging.py
│   └── stm.py
├── dummy_test/       # manual hardware and integration checks
├── imaging/          # Picamera2 capture and local YOLO inference
├── manager.py        # controller entry point
├── migrate.py        # PC → RPi source sync
├── protocol.py / router.py
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

## Setup

On the RPi:

```bash
cd rpi_controller
source .venv/bin/activate
pip install -r requirements.txt
python manager.py
```

## Android-free Task 1 integration

Tonight's narrow integration runtime does not start Bluetooth or imaging. It
uses one Algo HTTP worker plus separate STM TX and RX workers; the Task 1
kernel is the sole owner of route state and sends the next STM movement only
after the STM RX worker reports that the prior movement has stopped.

```bash
python3 task1_integration_test.py
```

The hardcoded robot and obstacle layout is in `task1_integration_test.py`.
The Algo server must be available at `http://127.0.0.1:5001` and the STM USB
serial adapter must be `/dev/ttyUSB0`. PC image transfer is optional; configure
`MDP_PC_HOST`, `MDP_PC_USER`, `MDP_PC_DEST`, and optionally `MDP_PC_SSH_KEY`.

YOLO weights belong at `imaging/models/best.pt`.

To sync source, run this on the PC (keeps the RPi `.venv` and model):

```bash
python migrate.py
```

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
- Manager integration flow

Physical end-to-end testing with the Android app, STM hardware and actual imaging system is still pending.
