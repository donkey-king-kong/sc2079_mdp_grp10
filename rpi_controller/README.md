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
├── manager.py
├── protocol.py
├── router.py
├── requirements.txt
└── test_*.py
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

The imaging team can plug the camera and recognition model into this interface.

## Setup

```bash
pip install -r rpi_controller/requirements.txt
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