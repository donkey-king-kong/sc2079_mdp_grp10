import requests


class AlgoConnector:
    def __init__(self, host="127.0.0.1", port=5001, timeout=15):
        self.base_url = f"http://{host}:{port}"
        self.timeout = timeout

    def check_health(self):
        try:
            response = requests.get(
                f"{self.base_url}/health",
                timeout=self.timeout,
            )

            response.raise_for_status()

            data = response.json()
            return data.get("status") == "ok"

        except requests.RequestException as e:
            print(f"[ALGO] Health check failed: {e}")
            return False

    def navigate(self, robot, obstacles, strategy="exhaustive", metric="time"):
        payload = {
            "type": "START_TASK",
            "data": {
                "robot": robot,
                "obstacles": obstacles,
                "strategy": strategy,
                "metric": metric,
            },
        }

        response = requests.post(
            f"{self.base_url}/api/navigate",
            json=payload,
            timeout=self.timeout,
        )

        response.raise_for_status()

        data = response.json()

        if data.get("type") != "NAVIGATION":
            raise ValueError(
                f"Unexpected Algo response type: {data.get('type')}"
            )

        return data