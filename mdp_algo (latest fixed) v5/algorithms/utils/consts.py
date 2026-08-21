# IN THIS FILE: ALL CONSTANTS (CALIBRATED FOR 90-DEGREE LOGIC)

# -----------------------------------------------------------------------------
# 1. GRID & ARENA DIMENSIONS
# -----------------------------------------------------------------------------
GRID_SIZE = 20          # 20x20 grid
CELL_SIZE = 10          # Each cell = 10cm
MAP_WIDTH = 200         # Physical arena width (cm)
MAP_HEIGHT = 200        # Physical arena height (cm)

# -----------------------------------------------------------------------------
# 2. ROBOT DIMENSIONS (VIRTUAL FOOTPRINT)
# -----------------------------------------------------------------------------
# We assume a safe 30x30cm buffer for the robot to avoid corners.
ROBOT_WIDTH = 30        
ROBOT_HEIGHT = 30       
OBSTACLE_SIZE = 10
# Optimal distance from camera to obstacle face (in cm)
# Adjust this based on your actual camera focus (usually 20-30cm)
ROBOT_CAMERA_DISTANCE = 20  

# -----------------------------------------------------------------------------
# 3. MOVEMENT PHYSICS (CRITICAL FOR 90° TURNS)
# -----------------------------------------------------------------------------
# Turning Radius in Cells (3 cells = 30cm).
# This creates a safe 90-degree arc (displacement of 3x and 3y).
TURN_RADIUS = 4         

# Step size for straight movement (A* expands 1 cell at a time)
STRAIGHT_STEP = 1       

# Legacy tuples (kept for compatibility, but updated to symmetric 3x3)
# If your A* or Generator imports these, they will now use the correct 90° geometry.
TURN_FORWARD_LEFT = (3, 3)   
TURN_FORWARD_RIGHT = (3, 3)  
TURN_BACKWARD_LEFT = (3, 3)  
TURN_BACKWARD_RIGHT = (3, 3) 

# -----------------------------------------------------------------------------
# 4. A* SEARCH PENALTIES
# -----------------------------------------------------------------------------
# Safety buffer: How many cells away from an obstacle is "too close"?
# Robot radius is 1.5 cells (15cm). We add slight buffer -> 2 cells.
EXPANDED_CELL = 2       

# Costs
TURN_COST = 20          # Penalty for making a turn (favors straight paths)
SAFE_COST = 1000        # Huge penalty for entering the safety buffer of an obstacle
SCREENSHOT_COST = 50    # Penalty for taking a photo from a weird angle (if applicable)

# -----------------------------------------------------------------------------
# 5. BOUNDARY CONSTRAINTS
# -----------------------------------------------------------------------------
# Physical limits in CM (Center of robot)
MIN_BOUNDARY = 15       # 15cm from wall
MAX_BOUNDARY = 185      # 185cm from wall

# Grid Index limits (Center-point logic)
# Valid indices are 1 to 18. Indices 0 and 19 are virtual walls.
MIN_PADDING = 1         
MAX_PADDING = 18