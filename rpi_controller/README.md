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
- Wait for STM acknowledgement before sending the next command.
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
RPi → SF010\n
STM → A\n
```

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
