# IN THIS FILE: DIRECTIONS, MOVEMENT TYPES, and maybe states ?
from enum import Enum


class Direction(int, Enum):
    """
    Robot facing direction and obstacle image facing.
    Uses even numbers to allow easy rotation cost calculation.
    """
    NORTH = 0
    EAST = 2
    SOUTH = 4
    WEST = 6
    
    def __int__(self):
        return self.value
    
    @staticmethod
    def rotation_cost(d1: 'Direction', d2: 'Direction') -> int:
        """
        Calculate the cost of rotating from direction d1 to d2.
        Since directions are 0, 2, 4, 6, rotation cost is the minimum
        angular distance around the compass.
        
        Examples:
            NORTH (0) → EAST (2): cost = 2
            NORTH (0) → WEST (6): cost = 2 (not 6, shorter to go counter-clockwise)
            NORTH (0) → SOUTH (4): cost = 4
        """
        diff = abs(int(d1) - int(d2))
        return min(diff, 8 - diff)


class Movement(Enum):
    """
    Robot movement types.
    Value is the command prefix for STM commands.
    """
    FORWARD = "FW"
    BACKWARD = "BW"
    FORWARD_LEFT = "FL"
    FORWARD_RIGHT = "FR"
    BACKWARD_LEFT = "BL"
    BACKWARD_RIGHT = "BR"