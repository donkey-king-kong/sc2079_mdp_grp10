-------------------------------------------------------------------------------------
# CURRENT FILE STRUCTURE 

algorithm/
├── entities/              # core data structures
│   ├── __init__.py
│   ├── obstacle.py       # obstacle class
│   ├── robot.py          # robot state
│   └── grid.py           # map & grid representation
│
├── pathfinding/          # algorithms we wanna use
│   ├── __init__.py
│   ├── astar.py          # A* pathfinding
│   ├── hamiltonian.py    # TSP solving
│   └── validation.py     # Collision detection
│
├── utils/                # helpers (hybrid)
│   ├── __init__.py
│   ├── consts.py         # all constants
│   ├── enums.py          # direction & movement enums
│   └── helpers.py        # utility functions
│
├── commands/             # for robot control
│   ├── __init__.py
│   └── generator.py      # converting path to STM commands
│
├── tests /
├── main.py               # FastAPI/Flask server - KIV
├── requirements.txt
└── README.md

-------------------------------------------------------------------------------------

kiv, not sure if i did it correctly
┌─────────────────────────────────────────────────────────┐
│                   YOUR ALGORITHM SERVER                 │
│                  (FastAPI on port 5000)                 │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  Endpoint: POST /path                                   │
│  Input:    JSON with obstacles                          │
│  Output:   JSON with commands + path + distance         │
│                                                         │
│  Endpoint: GET /status                                  │
│  Output:   {"status": "ok", ...}                        │
│                                                         │
└─────────────────────────────────────────────────────────┘