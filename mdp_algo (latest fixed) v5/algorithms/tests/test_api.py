import json

import requests

url = "http://localhost:5000/path"

data = {
    "obstacles": [
        {"id": 1, "x": 5, "y": 10, "d": 0},   # NORTH
        {"id": 2, "x": 15, "y": 5, "d": 2},   # EAST
        {"id": 3, "x": 10, "y": 15, "d": 4},  # SOUTH
    ],
    "robot_x": 1,
    "robot_y": 1,
    "robot_dir": 0
}

response = requests.post(url, json=data)
print(json.dumps(response.json(), indent=2))