# System Contracts v0.1

## 1. RPi <-> Android
Bluetooth 

Framing : JSON Lines, every message is one JSON Object terminated by `\n`

Structure:
```json
{
    "cat":"...",
    "value":"...",
}
```
### Android -> RPi
---
Category:
- Send Arena
- Manual STM Command
- Mission Control

### RPi -> Android
---
Category:
- Robot Status
- Robot Pose


## 2. RPi <-> Algo


## 3. RPi <-> STM32
 