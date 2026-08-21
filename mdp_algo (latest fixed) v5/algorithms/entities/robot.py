# IN THIS FILE: TRACKING ROBOT'S CURRENT STATE & MOVEMENT HISTORY

from typing import List

from algorithms.utils.enums import Direction
from algorithms.utils.types import CellState, Position


# TO VALIDATE: Instantiate robot at (1, 1, NORTH), verify start state is correct.
class Robot:
    """
    Tracks robot's starting position and movement history.
    """
    
    def __init__(self, x: int, y: int, direction: Direction):
        """
        Initialize robot at starting position.
        
        Args:
            x, y: Grid coordinates (cell units)
            direction: Initial facing direction
        """
        self.start_position = Position(x, y, direction)
        self.current_position = Position(x, y, direction)
        self.path_history: List[Position] = [self.start_position]
    
    def get_start_state(self) -> CellState:
        """
        Get robot's starting state as CellState.
        
        Returns:
            CellState with robot's initial position
        """
        return CellState(
            self.start_position.x,
            self.start_position.y,
            self.start_position.direction
        )