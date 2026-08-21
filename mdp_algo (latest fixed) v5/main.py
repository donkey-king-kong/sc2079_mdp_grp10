import uvicorn
from typing import List, Optional
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

from algorithms.commands.generator import CommandGenerator
from algorithms.entities.grid import Grid
from algorithms.entities.obstacle import Obstacle
from algorithms.entities.robot import Robot
from algorithms.pathfinding.hamiltonian import HamiltonianSolver
from algorithms.pathfinding.astar import AStar
from algorithms.utils.enums import Direction

app = FastAPI(title="MDP Algorithm Server")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# --- HELPER: DIRECTION TRANSLATOR ---
def translate_direction(d: int) -> int:
    """
    Translates Android's direction format to Algorithm's (0, 2, 4, 6).
    Android (ObstacleData.java): 1=N, 2=E, 3=S, 4=W  ->  Algo: 0=N, 2=E, 4=S, 6=W
    """
    #android_to_algo = {1: 0, 2: 2, 3: 4, 4: 6}  
    #if d in android_to_algo:
    #    return android_to_algo[d]
    #if d in [0, 2, 4, 6]:
     #   return d  
    return d  
#
# --- PYDANTIC MODELS ---

class ObstacleInput(BaseModel):
    id: int
    x: int
    y: int
    d: int

class AlgorithmInput(BaseModel):
    obstacles: List[ObstacleInput]
    robot_x: Optional[int] = 1   
    robot_y: Optional[int] = 1   
    robot_dir: Optional[int] = 0 
    retrying: Optional[bool] = False

class BullseyeInput(BaseModel):
    obstacles: List[ObstacleInput] 
    robot_x: int                   
    robot_y: int                   
    robot_dir: int                 
    target_obstacle_id: int        
    checked_faces: List[int]       

class PathPoint(BaseModel):
    x: int
    y: int
    d: int
    s: int

class SnapPosition(BaseModel):
    x: int
    y: int
    d: int

class AlgorithmData(BaseModel):
    commands: List[str]
    path: List[PathPoint]
    distance: float
    snap_positions: List[SnapPosition]

class AlgorithmOutput(BaseModel):
    data: AlgorithmData


# --- CORE ORCHESTRATORS ---

def run_algorithm(obstacles_data: List[dict], robot_x: int, robot_y: int, robot_dir: int, retrying: bool) -> dict:
    grid = Grid()
    
    print("\n--- 🧠 [ALGORITHM INTERNAL LOGIC] ---")
    for obs in obstacles_data:
        algo_dir = translate_direction(obs["d"])
        print(f"   [GRID] Adding Obstacle {obs['id']} at ({obs['x']}, {obs['y']}) | Android Dir: {obs['d']} -> Algo Dir: {algo_dir}")
        grid.add_obstacle(Obstacle(obs["x"], obs["y"], Direction(algo_dir), obs["id"]))
    
    algo_robot_dir = translate_direction(robot_dir)
    robot = Robot(robot_x, robot_y, Direction(algo_robot_dir))
    
    print(f"   [TSP] Running Hamiltonian Solver for {len(obstacles_data)} obstacles...")
    solver = HamiltonianSolver(grid, robot)
    permutation, total_cost = solver.find_optimal_order(retrying=retrying)
    print(f"   [TSP] 🎯 Optimal Visit Sequence (0=Start, 1..N=Obstacles): {permutation}")
    print(f"   [TSP] 💰 Minimum Total Cost: {total_cost:.2f}")
    
    full_path = solver.generate_full_path(permutation)
    print(f"   [ASTAR] 🗺️ Generated full contiguous path with {len(full_path)} coordinate steps.")
    print("--------------------------------------\n")
    
    cmd_gen = CommandGenerator()
    raw_commands = cmd_gen.generate_commands(full_path)
    
    path_points = [{"x": s.x, "y": s.y, "d": int(s.direction), "s": s.screenshot_id} for s in full_path]
    snap_positions = [{"x": s.x, "y": s.y, "d": int(s.direction)} for s in full_path if s.screenshot_id != -1]
    
    return {
        "data": {
            "commands": raw_commands,
            "path": path_points,
            "distance": total_cost,
            "snap_positions": snap_positions
        }
    }

def run_bullseye_recovery(obstacles_data: List[dict], robot_x: int, robot_y: int, robot_dir: int, target_id: int, checked_faces: List[int]) -> dict:
    grid = Grid()
    target_obstacle = None
    
    translated_checked_faces = [translate_direction(f) for f in checked_faces]
    
    print("\n--- 🧠 [BULLSEYE RECOVERY LOGIC] ---")
    print(f"   [RECOVERY] Target Obstacle ID: {target_id}")
    print(f"   [RECOVERY] Ignoring Faces (Algo Dirs): {translated_checked_faces}")
    
    for obs in obstacles_data:
        algo_dir = translate_direction(obs["d"])
        obstacle_obj = Obstacle(obs["x"], obs["y"], Direction(algo_dir), obs["id"])
        grid.add_obstacle(obstacle_obj)
        if obs["id"] == target_id:
            target_obstacle = obstacle_obj
            
    algo_robot_dir = translate_direction(robot_dir)
    robot = Robot(robot_x, robot_y, Direction(algo_robot_dir))
    astar = AStar(grid)
    
    recovery_path = []
    new_start_state = robot.get_start_state()
    
    # 1. FIND RECOVERY PATH
    if target_obstacle:
        candidates = target_obstacle.get_valid_viewing_positions(grid, specific_face=False, ignored_faces=translated_checked_faces)
        print(f"   [ASTAR] Found {len(candidates)} valid alternative viewing positions for Obstacle {target_id}.")
        
        best_path = None
        best_cost = float('inf')
        
        for pos in candidates:
            path = astar.search(robot.get_start_state(), pos)
            if path:
                cost = astar.cost_cache[(robot.get_start_state(), pos)]
                if cost < best_cost:
                    best_cost = cost
                    best_path = path
                    
        if best_path:
            recovery_path = best_path
            new_start_state = best_path[-1] 
            new_start_state.screenshot_id = target_id
            print(f"   [ASTAR] ✅ Best recovery path found! Cost: {best_cost:.2f}.")
            print(f"   [ASTAR] 📍 New Snapshot Position: (x: {new_start_state.x}, y: {new_start_state.y}, dir: {new_start_state.direction})")
        else:
            print(f"   [ASTAR] ⚠️ FATAL: Cannot reach any other sides of Obstacle {target_id}. Giving up on this obstacle.")
            
        # SILENCE TARGET SO TSP SOLVER IGNORES IT
        target_obstacle.get_viewing_positions = lambda *args, **kwargs: []
        target_obstacle.get_valid_viewing_positions = lambda *args, **kwargs: []
        
    # 2. REROUTE REMAINING SEQUENCE
    robot.x, robot.y, robot.direction = new_start_state.x, new_start_state.y, new_start_state.direction
    
    print(f"   [TSP] Re-calculating Hamiltonian path for remaining unvisited obstacles...")
    solver = HamiltonianSolver(grid, robot)
    permutation, total_cost = solver.find_optimal_order()
    remaining_path = solver.generate_full_path(permutation)
    print(f"   [TSP] 🎯 New Remaining Visit Sequence: {permutation}")
    print("--------------------------------------\n")
    
    # 3. STITCH PATHS
    full_combined_path = []
    if recovery_path:
        full_combined_path.extend(recovery_path)
        if remaining_path:
            full_combined_path.extend(remaining_path[1:])
    else:
        full_combined_path.extend(remaining_path)

    cmd_gen = CommandGenerator()
    raw_commands = cmd_gen.generate_commands(full_combined_path)

    path_points = [{"x": s.x, "y": s.y, "d": int(s.direction), "s": s.screenshot_id} for s in full_combined_path]
    snap_positions = [{"x": s.x, "y": s.y, "d": int(s.direction)} for s in full_combined_path if s.screenshot_id != -1]

    return {
        "data": {
            "commands": raw_commands,
            "path": path_points,
            "distance": total_cost + (best_cost if best_path else 0),
            "snap_positions": snap_positions
        }
    }


# --- API ENDPOINTS ---

@app.get("/status")
def health_check():
    return {"status": "ok", "message": "Algorithm server is running"}

@app.post("/path", response_model=AlgorithmOutput)
def compute_path(input_data: AlgorithmInput):
    try:
        print("\n" + "="*60)
        print("🚀 [API] INBOUND REQUEST: /path")
        print(f"🤖 Robot Start: (x: {input_data.robot_x}, y: {input_data.robot_y}, dir: {input_data.robot_dir})")
        print(f"📦 Obstacles Count: {len(input_data.obstacles)}")
        for obs in input_data.obstacles:
            print(f"   -> ID: {obs.id:2d} | X: {obs.x:2d}, Y: {obs.y:2d} | Dir: {obs.d}")
        print("="*60)

        obstacles_data = [{"id": o.id, "x": o.x, "y": o.y, "d": o.d} for o in input_data.obstacles]
        result = run_algorithm(obstacles_data, input_data.robot_x, input_data.robot_y, input_data.robot_dir, input_data.retrying)
        
        print("="*60)
        print("✅ [API] OUTBOUND RESPONSE: /path")
        print(f"📜 STM Commands ({len(result['data']['commands'])}): {result['data']['commands']}")
        print(f"📸 Snap Positions ({len(result['data']['snap_positions'])}):")
        for snap in result['data']['snap_positions']:
            print(f"   -> [CAMERA] Park at: (x: {snap['x']}, y: {snap['y']}, dir: {snap['d']})")
        
        # Format the full coordinate path for visibility
        path_str_list = [f"({p['x']},{p['y']},{p['d']})" for p in result['data']['path']]
        compact_path = " -> ".join(path_str_list)
        print(f"🛣️  Full Grid Coordinate Path:\n   {compact_path}")
        print("="*60 + "\n")

        return result
    except Exception as e:
        import traceback
        traceback.print_exc() 
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/bullseye", response_model=AlgorithmOutput)
def compute_bullseye_recovery(input_data: BullseyeInput):
    try:
        print("\n" + "="*60)
        print("🎯 [API] INBOUND REQUEST: /bullseye (DYNAMIC RECOVERY)")
        print(f"🤖 Robot Stopped At: (x: {input_data.robot_x}, y: {input_data.robot_y}, dir: {input_data.robot_dir})")
        print(f"🚨 Target Obstacle: {input_data.target_obstacle_id}")
        print(f"❌ Failed Faces: {input_data.checked_faces}")
        print("="*60)

        obstacles_data = [{"id": o.id, "x": o.x, "y": o.y, "d": o.d} for o in input_data.obstacles]
        result = run_bullseye_recovery(obstacles_data, input_data.robot_x, input_data.robot_y, input_data.robot_dir, input_data.target_obstacle_id, input_data.checked_faces)
        
        print("="*60)
        print("✅ [API] OUTBOUND RESPONSE: /bullseye")
        print(f"📜 STM Recovery Commands ({len(result['data']['commands'])}): {result['data']['commands']}")
        
        path_str_list = [f"({p['x']},{p['y']},{p['d']})" for p in result['data']['path']]
        compact_path = " -> ".join(path_str_list)
        print(f"🛣️  Full Recovery Coordinate Path:\n   {compact_path}")
        print("="*60 + "\n")

        return result
    except Exception as e:
        import traceback
        traceback.print_exc() 
        raise HTTPException(status_code=500, detail=str(e))

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=5000)