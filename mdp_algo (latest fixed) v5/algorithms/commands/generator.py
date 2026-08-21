# algorithms/commands/generator.py
from typing import List
from algorithms.utils.enums import Direction
from algorithms.utils.types import CellState

class CommandGenerator:
    def generate_commands(self, path: List[CellState]) -> List[str]:
        commands = []
        for i in range(1, len(path)):
            prev = path[i-1]
            curr = path[i]
            
            if prev.direction == curr.direction:
                # STRAIGHT
                dist = max(abs(curr.x - prev.x), abs(curr.y - prev.y)) * 10 # Convert cells to cm
                if dist == 0: continue # Skip if no movement (e.g. start node duplicate)
                
                # Check Forward or Backward
                # If facing North (y+) and y increased -> Forward
                is_forward = False
                if prev.direction == Direction.NORTH and curr.y > prev.y: is_forward = True
                elif prev.direction == Direction.SOUTH and curr.y < prev.y: is_forward = True
                elif prev.direction == Direction.EAST and curr.x > prev.x: is_forward = True
                elif prev.direction == Direction.WEST and curr.x < prev.x: is_forward = True
                
                # If simplified straight logic fails (due to A* diagonals in old code), verify carefully.
                # Since new A* is strict grid, this holds.
                cmd = "FW" if is_forward else "BW"
                # We append 10cm chunks, will compress later
                commands.append(f"{cmd}10") 
                
            else:
                # TURNING (90 Degree)
                # Left Turn Check: (Old - 2) % 8 == New? No, North(0)->West(6). (0-2)%8 = 6. Yes.
                # Right Turn Check: (Old + 2) % 8 == New? North(0)->East(2). Yes.
                
                diff = (int(curr.direction) - int(prev.direction)) % 8
                
                if diff == 2: # Right Turn (0->2, 2->4...)
                    commands.append("FR90")
                elif diff == 6: # Left Turn (0->6, 6->4...)
                    commands.append("FL90")
                elif diff == 4: # 180 Turn (Rare)
                    commands.append("FR90")
                    commands.append("FR90")
            
            # SNAPSHOT
            if curr.screenshot_id != -1:
                commands.append(f"SNAP{curr.screenshot_id}")
                
        commands.append("FIN")
        return self.compress_commands(commands)

    def compress_commands(self, commands: List[str]) -> List[str]:
        # Same as your existing logic, but robust for FW/BW
        compressed = []
        if not commands: return []
        
        curr_cmd = commands[0]
        curr_val = 0
        
        # Helper to parse "FW10" -> ("FW", 10)
        def parse(c):
            if c.startswith("FW") or c.startswith("BW"):
                return c[:2], int(c[2:])
            return c, 0

        for i in range(len(commands)):
            cmd = commands[i]
            type_str, val = parse(cmd)
            
            # Start of list
            if i == 0:
                curr_val = val
                continue
            
            prev_type, _ = parse(commands[i-1])
            
            if type_str == prev_type and val > 0: # Mergeable
                curr_val += val
            else:
                # Flush previous
                prev_real_type, _ = parse(commands[i-1])
                if prev_real_type in ["FW", "BW"]:
                    # Split into 90 max
                    while curr_val > 90:
                        compressed.append(f"{prev_real_type}90")
                        curr_val -= 90
                    if curr_val > 0:
                        compressed.append(f"{prev_real_type}{curr_val:02d}")
                else:
                    compressed.append(commands[i-1])
                
                curr_val = val
                
        # Flush last
        last_type, _ = parse(commands[-1])
        if last_type in ["FW", "BW"]:
             while curr_val > 90:
                compressed.append(f"{last_type}90")
                curr_val -= 90
             if curr_val > 0:
                compressed.append(f"{last_type}{curr_val:02d}")
        else:
            compressed.append(commands[-1])
            
        return compressed