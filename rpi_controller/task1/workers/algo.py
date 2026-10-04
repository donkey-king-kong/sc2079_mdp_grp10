"""Algo request worker for Task 1."""

import threading

from task1.events import Event, EventType


class AlgoWorker(threading.Thread):
    """One HTTP request/response worker; it never owns a route."""

    def __init__(self, connector, requests, events, shutdown):
        super().__init__(name="ALGO")
        self.connector, self.requests, self.events, self.shutdown = connector, requests, events, shutdown

    def run(self):
        while True:
            request = self.requests.get()
            if request is None:
                return
            try:
                print("[ALGO TX] START_TASK obstacles=%d" % len(request.obstacles))
                result = self.connector.navigate(
                    robot=dict(request.robot), obstacles=[dict(item) for item in request.obstacles],
                    strategy=request.strategy, metric=request.metric,
                )
                commands = result.get("data", {}).get("commands") if isinstance(result, dict) else None
                if not isinstance(commands, list) or not all(isinstance(item, str) for item in commands):
                    raise ValueError("Algo NAVIGATION response has no string commands list")
                print("[ALGO RX] route received: %d commands" % len(commands))
                self.events.put(Event(EventType.ALGO_ROUTE_RECEIVED, {"result": result, "commands": commands}))
            except Exception as error:  # requests errors and malformed payloads become kernel events
                self.events.put(Event(EventType.ALGO_ERROR, {"error": str(error)}))


