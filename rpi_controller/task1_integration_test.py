"""Tonight's Android-free Task 1 integration entrypoint."""

from connectors.algo import AlgoConnector
from connectors.imaging import ImagingConnector
from connectors.stm import STMConnector
from task1_runtime import MissionState, Task1Runtime


ROBOT = {"x": 1, "y": 1, "dir": "N"}
OBSTACLES = [
    {"id": 1, "x": 5, "y": 13, "dir": "W"},
]


def main():
    stm = STMConnector()
    runtime = None
    try:
        connection = stm.connect()
        runtime = Task1Runtime(
            AlgoConnector(), connection, imaging=ImagingConnector(),
        )
        runtime.start()
        runtime.kernel.start(ROBOT, OBSTACLES)
        state = runtime.kernel.run()
        print("[KERNEL] mission ended: %s" % state.value)
        return 0 if state is MissionState.COMPLETE else 1
    except KeyboardInterrupt:
        print("[SHUTDOWN] Ctrl+C received")
        return 130
    finally:
        if runtime:
            runtime.stop()
        stm.disconnect()
        print("[SHUTDOWN] clean")


if __name__ == "__main__":
    raise SystemExit(main())
