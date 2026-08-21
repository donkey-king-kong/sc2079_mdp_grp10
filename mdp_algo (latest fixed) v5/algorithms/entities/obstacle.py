from typing import List
from algorithms.utils.consts import (
    CELL_SIZE, 
    OBSTACLE_SIZE,
    ROBOT_CAMERA_DISTANCE, 
    EXPANDED_CELL,
    SCREENSHOT_COST
)
from algorithms.utils.enums import Direction
from algorithms.utils.types import CellState

class Obstacle:
    def __init__(self, x: int, y: int, direction: Direction, obstacle_id: int):
        self.x = x
        self.y = y
        self.direction = direction
        self.obstacle_id = obstacle_id
    
    def get_viewing_positions(self, retrying: bool = False, specific_face: bool = True, ignored_faces: List[int] = None) -> List[CellState]:
        positions = []
        if ignored_faces is None:
            ignored_faces = []
            
        if not retrying:
            dist_cells = (ROBOT_CAMERA_DISTANCE + OBSTACLE_SIZE // 2) // CELL_SIZE
            # REVERTED: Back to 3 (Close parking). 
            # A* Sweep Check will automatically force the robot to reverse out if it's too tight!
            if dist_cells < 3: dist_cells = 3 
        else:
            # Retry: Step back just 1 block
            dist_cells = ((ROBOT_CAMERA_DISTANCE + OBSTACLE_SIZE // 2) // CELL_SIZE) + 1
            if dist_cells < 4: dist_cells = 4 

        offset_1 = dist_cells
        offset_2 = dist_cells + 1

        # --- BULLSEYE FILTER LOGIC ---
        if specific_face:
            faces_to_check = [self.direction]
        else:
            # Check all 4 sides, BUT filter out the ones we already know are bullseyes
            faces_to_check = [d for d in [Direction.NORTH, Direction.SOUTH, Direction.EAST, Direction.WEST] if d.value not in ignored_faces]

        for face in faces_to_check:
            if face == Direction.NORTH:
                target_d = Direction.SOUTH
                positions.append(CellState(self.x, self.y + offset_1, target_d, self.obstacle_id, 0))
                positions.append(CellState(self.x, self.y + offset_2, target_d, self.obstacle_id, 5))
                positions.append(CellState(self.x - 1, self.y + offset_1, target_d, self.obstacle_id, SCREENSHOT_COST))
                positions.append(CellState(self.x + 1, self.y + offset_1, target_d, self.obstacle_id, SCREENSHOT_COST))

            elif face == Direction.SOUTH:
                target_d = Direction.NORTH
                positions.append(CellState(self.x, self.y - offset_1, target_d, self.obstacle_id, 0))
                positions.append(CellState(self.x, self.y - offset_2, target_d, self.obstacle_id, 5))
                positions.append(CellState(self.x - 1, self.y - offset_1, target_d, self.obstacle_id, SCREENSHOT_COST))
                positions.append(CellState(self.x + 1, self.y - offset_1, target_d, self.obstacle_id, SCREENSHOT_COST))

            elif face == Direction.EAST:
                target_d = Direction.WEST
                positions.append(CellState(self.x + offset_1, self.y, target_d, self.obstacle_id, 0))
                positions.append(CellState(self.x + offset_2, self.y, target_d, self.obstacle_id, 5))
                positions.append(CellState(self.x + offset_1, self.y - 1, target_d, self.obstacle_id, SCREENSHOT_COST))
                positions.append(CellState(self.x + offset_1, self.y + 1, target_d, self.obstacle_id, SCREENSHOT_COST))

            elif face == Direction.WEST:
                target_d = Direction.EAST
                positions.append(CellState(self.x - offset_1, self.y, target_d, self.obstacle_id, 0))
                positions.append(CellState(self.x - offset_2, self.y, target_d, self.obstacle_id, 5))
                positions.append(CellState(self.x - offset_1, self.y - 1, target_d, self.obstacle_id, SCREENSHOT_COST))
                positions.append(CellState(self.x - offset_1, self.y + 1, target_d, self.obstacle_id, SCREENSHOT_COST))

        return positions

    def get_valid_viewing_positions(self, grid, retrying: bool = False, specific_face: bool = True, ignored_faces: List[int] = None) -> List[CellState]:
        candidates = self.get_viewing_positions(retrying, specific_face, ignored_faces)
        return [pos for pos in candidates if grid.is_reachable(pos.x, pos.y)]