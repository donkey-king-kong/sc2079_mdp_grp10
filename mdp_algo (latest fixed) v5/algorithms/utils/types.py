# IN THIS FILE: POSITION, CELLSTATE

from typing import Optional

from algorithms.utils.enums import Direction


class Position:
    """
    Represents a robot's position and orientation on the grid.
    This is the base class for any position-related data.
    """
    
    def __init__(self, x: float, y: float, direction: Direction):
        self.x = x                  # Grid x-coordinate (cell units, can be float)
        self.y = y                  # Grid y-coordinate (cell units, can be float)
        self.direction = direction  # Facing direction (NORTH/EAST/SOUTH/WEST)
    
    def __eq__(self, other: object) -> bool:
        """Check if two positions are equal"""
        if not isinstance(other, Position):
            return False
        return (self.x == other.x and 
                self.y == other.y and 
                self.direction == other.direction)
    
    def __hash__(self) -> int:
        """Allow Position to be used as dictionary key or in sets"""
        return hash((self.x, self.y, self.direction))
    
    def __repr__(self) -> str:
        """String representation for debugging"""
        return f"Position(x={self.x}, y={self.y}, d={self.direction.name})"


class CellState(Position):
    """
    Enhanced position used in pathfinding algorithms.
    Adds metadata about screenshots and movement penalties.
    """
    
    def __init__(
        self, 
        x: int, 
        y: int, 
        direction: Direction,
        screenshot_id: int = -1, 
        penalty: float = 0
    ):
        super().__init__(x, y, direction)
        self.screenshot_id = screenshot_id  # Which obstacle to photograph (-1 = none)
        self.penalty = penalty              # Additional cost for using this position
    
    def set_screenshot(self, screenshot_id: int) -> None:
        """Mark this position as a photography point"""
        self.screenshot_id = screenshot_id
    
    def get_dict(self) -> dict:
        """Convert to dictionary for JSON serialization"""
        return {
            "x": self.x,
            "y": self.y,
            "d": int(self.direction),
            "s": self.screenshot_id
        }
    
    def __repr__(self) -> str:
        """String representation for debugging"""
        return f"CellState(x={self.x}, y={self.y}, d={self.direction.name}, s={self.screenshot_id})"