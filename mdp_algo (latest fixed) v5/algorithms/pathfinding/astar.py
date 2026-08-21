import heapq
from typing import Dict, List, Optional, Tuple
from algorithms.entities.grid import Grid
from algorithms.utils.consts import TURN_RADIUS, TURN_COST
from algorithms.utils.enums import Direction
from algorithms.utils.types import CellState

class AStarNode:
    def __init__(self, state: CellState, g_cost: float, h_cost: float, parent: Optional['AStarNode'] = None):
        self.state = state
        self.g_cost = g_cost
        self.h_cost = h_cost
        self.f_cost = g_cost + h_cost
        self.parent = parent
    
    def __lt__(self, other):
        return self.f_cost < other.f_cost

class AStar:
    def __init__(self, grid: Grid):
        self.grid = grid
        self.cost_cache: Dict[Tuple[CellState, CellState], float] = {}

    def heuristic(self, current: CellState, goal: CellState) -> float:
        return ((current.x - goal.x) ** 2 + (current.y - goal.y) ** 2) ** 0.5

    def get_neighbors(self, state: CellState) -> List[Tuple[CellState, float]]:
        neighbors = []
        x, y, d = state.x, state.y, state.direction
        
        # Dynamically pulls from constants (which is now 4 for your 36cm turn)
        r = TURN_RADIUS 

        # --- 1. STRAIGHT MOVEMENT ---
        dx, dy = 0, 0
        if d == Direction.NORTH: dy = 1
        elif d == Direction.SOUTH: dy = -1
        elif d == Direction.EAST: dx = 1
        elif d == Direction.WEST: dx = -1
        
        for sign in [1, -1]:
            nx, ny = x + (dx * sign), y + (dy * sign)
            if self.grid.is_reachable(nx, ny):
                neighbors.append((CellState(nx, ny, d), 1))

        # --- 2. 90-DEGREE TURNS ---
        def apply_turn(turn_type):
            if turn_type == 'FL':
                return {0:(-r,r,6), 2:(r,r,0), 4:(r,-r,2), 6:(-r,-r,4)}[d]
            elif turn_type == 'FR':
                return {0:(r,r,2), 2:(r,-r,4), 4:(-r,-r,6), 6:(-r,r,0)}[d]
            elif turn_type == 'BL':
                return {0:(-r,-r,2), 2:(-r,r,4), 4:(r,r,6), 6:(r,-r,0)}[d]
            elif turn_type == 'BR':
                return {0:(r,-r,6), 2:(-r,-r,0), 4:(-r,r,2), 6:(r,r,4)}[d]

        for turn in ['FL', 'FR', 'BL', 'BR']:
            tdx, tdy, new_d = apply_turn(turn)
            nx, ny = x + tdx, y + tdy
            
            if not self.grid.is_reachable(nx, ny, turn=True):
                continue

            # --- SWEEP CLEARANCE CHECK ---
            # Dynamically verifies the entire swept bounding box of the turn arc!
            safe = True
            step_x = 1 if tdx > 0 else -1
            step_y = 1 if tdy > 0 else -1
            
            check_points = []
            for i in range(r + 1):
                for j in range(r + 1):
                    if i == 0 and j == 0: continue # Already checked start
                    
                    # Shave off the extreme outer opposite corners to allow natural arcs
                    if i == 0 and j == r: continue 
                    if i == r and j == 0: continue 
                    
                    check_points.append((i * step_x, j * step_y))
            
            for cx, cy in check_points:
                if not self.grid.is_reachable(x + cx, y + cy, turn=True):
                    safe = False
                    break
                    
            if not safe:
                continue
            
            neighbors.append((CellState(nx, ny, Direction(new_d)), TURN_COST + r))

        return neighbors

    def search(self, start: CellState, goal: CellState) -> List[CellState]:
        open_set = []
        heapq.heappush(open_set, AStarNode(start, 0, self.heuristic(start, goal)))
        closed_set = set()
        g_scores = {start: 0}
        
        while open_set:
            current_node = heapq.heappop(open_set)
            curr = current_node.state
            
            if curr.x == goal.x and curr.y == goal.y and curr.direction == goal.direction:
                self.cost_cache[(start, goal)] = current_node.g_cost
                return self._reconstruct_path(current_node)
            
            if curr in closed_set: continue
            closed_set.add(curr)
            
            for next_s, cost in self.get_neighbors(curr):
                if next_s in closed_set: continue
                tentative_g = g_scores[curr] + cost
                if next_s not in g_scores or tentative_g < g_scores[next_s]:
                    g_scores[next_s] = tentative_g
                    heapq.heappush(open_set, AStarNode(next_s, tentative_g, self.heuristic(next_s, goal), current_node))
                    
        return []

    def _reconstruct_path(self, node):
        path = []
        while node:
            path.append(node.state)
            node = node.parent
        return path[::-1]