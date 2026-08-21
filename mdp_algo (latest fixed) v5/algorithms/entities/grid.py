# algorithms/entities/grid.py

from typing import List
from algorithms.entities.obstacle import Obstacle
# Ensure you updated consts.py with MIN_PADDING=1, MAX_PADDING=18
from algorithms.utils.consts import (
    GRID_SIZE, 
    EXPANDED_CELL, 
    MIN_PADDING, 
    MAX_PADDING
) 

class Grid:
    """
    Represents the 20x20 arena grid.
    Manages obstacles and validates robot positions.
    """
    
    def __init__(self, size_x: int = GRID_SIZE, size_y: int = GRID_SIZE):
        self.size_x = size_x
        self.size_y = size_y
        self.obstacles: List[Obstacle] = []
    
    def add_obstacle(self, obstacle: Obstacle) -> None:
        self.obstacles.append(obstacle)
    
    def reset_obstacles(self) -> None:
        self.obstacles.clear()
    
    def is_within_bounds(self, x: int, y: int) -> bool:
        """
        Check if position is within arena boundaries.
        Uses valid indices 1 to 18 (keeping 1 cell buffer from physical walls).
        """
        return MIN_PADDING <= x <= MAX_PADDING and MIN_PADDING <= y <= MAX_PADDING
    
    def is_reachable(self, x: int, y: int, turn: bool = False) -> bool:
        """
        Check if robot can occupy position (x, y) without collision.
        """
        # Check arena boundaries first
        if not self.is_within_bounds(x, y):
            return False
        
        # Calculate required clearance
        # If turning, we might need more space, but EXPANDED_CELL=1 is usually enough
        clearance = EXPANDED_CELL 
        
        # Check distance from all obstacles
        for obs in self.obstacles:
            dx = abs(obs.x - x)
            dy = abs(obs.y - y)
            
            # REMOVED: if dx + dy < 4: return False 
            # REASON: This diamond check blocks valid 30cm shooting positions.
            
            # Use Square Bounding Box Check (Better for square robots)
            # If robot is within 'clearance' cells of the obstacle in BOTH axes, it's a hit.
            if dx <= clearance and dy <= clearance: 
                return False
                
        return True
    
    def get_all_viewing_positions(self):
        """Helper for Hamiltonian Solver"""
        return [obs.get_valid_viewing_positions(self) for obs in self.obstacles]