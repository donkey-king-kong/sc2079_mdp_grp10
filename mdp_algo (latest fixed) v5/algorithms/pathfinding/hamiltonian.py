import itertools
import numpy as np
from python_tsp.exact import solve_tsp_dynamic_programming
from typing import List, Tuple
from algorithms.entities.grid import Grid
from algorithms.entities.robot import Robot
from algorithms.pathfinding.astar import AStar
from algorithms.utils.types import CellState
from algorithms.utils.enums import Direction

class HamiltonianSolver:
    def __init__(self, grid: Grid, robot: Robot):
        self.grid = grid
        self.robot = robot
        self.astar = AStar(grid)
    
    def generate_cost_matrix(self, positions: List[CellState]) -> np.ndarray:
        n = len(positions)
        cost_matrix = np.zeros((n, n))
        
        for i in range(n):
            for j in range(n):
                if i == j:
                    cost_matrix[i][j] = 0
                    continue
                if j == 0:
                    cost_matrix[i][j] = 0
                    continue
                
                # Check for Dummy "Trapped/Silenced" nodes
                if positions[i].x == -99 or positions[j].x == -99:
                    cost_matrix[i][j] = 1e9
                    continue

                path = self.astar.search(positions[i], positions[j])
                if path:
                    cost = self.astar.cost_cache[(positions[i], positions[j])]
                    if hasattr(positions[j], 'penalty'):
                        cost += positions[j].penalty
                    cost_matrix[i][j] = cost
                else:
                    cost_matrix[i][j] = 1e9

        return cost_matrix
    
    def find_optimal_order(self, retrying: bool = False) -> Tuple[List[int], float]:
        start_state = self.robot.get_start_state()
        viewing_positions = [start_state]
        
        for obstacle in self.grid.obstacles:
            candidates = obstacle.get_viewing_positions(retrying=retrying) 
            valid_positions = [pos for pos in candidates if self.grid.is_reachable(pos.x, pos.y)]
            
            selected_pos = None
            if valid_positions:
                 # SMART SELECTION: Test if it's actually reachable!
                 for pos in valid_positions:
                     if self.astar.search(start_state, pos):
                         selected_pos = pos
                         break
                 if not selected_pos: 
                     selected_pos = valid_positions[0]
            
            if not selected_pos:
                viewing_positions.append(CellState(-99, -99, Direction.NORTH)) 
            else:
                viewing_positions.append(selected_pos)
        
        cost_matrix = self.generate_cost_matrix(viewing_positions)
        n_obstacles = len(self.grid.obstacles)
        
        best_permutation = None
        best_distance = float('inf')
        
        # MAX-SUBSET: Prevent paths from breaking if a node is trapped
        for subset_size in range(n_obstacles, 0, -1):
            found_valid_subset = False
            min_subset_dist = float('inf')
            best_subset_perm = None
            
            for comb in itertools.combinations(range(1, n_obstacles + 1), subset_size):
                subset_indices = [0] + list(comb)
                sub_matrix = cost_matrix[np.ix_(subset_indices, subset_indices)]
                perm, dist = solve_tsp_dynamic_programming(sub_matrix)
                
                if dist < 1e9:
                    found_valid_subset = True
                    if dist < min_subset_dist:
                        min_subset_dist = dist
                        best_subset_perm = [subset_indices[i] for i in perm]
                        
            if found_valid_subset:
                best_permutation = best_subset_perm
                best_distance = min_subset_dist
                break
                
        if best_permutation is None:
            return [0], 0
            
        if best_permutation[0] != 0:
            start_idx = best_permutation.index(0)
            best_permutation = best_permutation[start_idx:] + best_permutation[:start_idx]
        
        return best_permutation, best_distance
    
    def generate_full_path(self, permutation: List[int]) -> List[CellState]:
        start_state = self.robot.get_start_state()
        viewing_positions = [start_state]
        
        # Must exactly mirror the selection logic above to prevent gaps
        for obstacle in self.grid.obstacles:
            candidates = obstacle.get_viewing_positions()
            valid_positions = [pos for pos in candidates if self.grid.is_reachable(pos.x, pos.y)]
            
            selected_pos = None
            if valid_positions:
                 for pos in valid_positions:
                     if self.astar.search(start_state, pos):
                         selected_pos = pos
                         break
                 if not selected_pos: selected_pos = valid_positions[0]
            
            viewing_positions.append(selected_pos if selected_pos else CellState(-99, -99, Direction.NORTH))
        
        full_path = []
        for i in range(len(permutation) - 1):
            from_idx = permutation[i]
            to_idx = permutation[i + 1]
            if to_idx == 0: continue

            segment = self.astar.search(viewing_positions[from_idx], viewing_positions[to_idx])
            
            # If still empty due to extreme traps, skip cleanly to avoid crashes
            if not segment: continue

            if i == 0: full_path.extend(segment)
            else: full_path.extend(segment[1:])
            
            if to_idx > 0 and full_path:
                full_path[-1].screenshot_id = self.grid.obstacles[to_idx - 1].obstacle_id
        
        return full_path