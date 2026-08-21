package com.example.sc2079;

import android.Manifest;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.os.IBinder;
import android.util.AttributeSet;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;
import android.view.GestureDetector;
import android.widget.Toast;

import androidx.annotation.Nullable;
import androidx.core.app.ActivityCompat;

import com.example.sc2079.service.BluetoothService;
import com.google.android.material.button.MaterialButton;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Comparator;

public class GridMapClass extends View {
    private int gridColumns, gridRows;
    private float cellWidth, cellHeight;

    // Paints
    private Paint blackPaint = new Paint(); // obstacles
    private Paint greenPaint = new Paint(); // vehicle
    private Paint redPaint = new Paint();   // direction
    private Paint textPaint = new Paint(); // Text color in box
    private Paint verifiedPaint = new Paint(); // Verified Status
    private Paint paintObstacleVerified = new Paint();
    private GestureDetector gestureDetector;
    private ArrayList<ArrayList<ObstacleData>> gridMapData = new ArrayList<>();
    private MaterialButton add_obstacle_button;
    private boolean changeGetReadToRoll = false;
    private boolean FINDetected = false;

    int hardLimit = 20;
    int lowLimit = 0;

    //Dragging
    int oldXCoordDrag;
    int oldYCoordDrag;
    int newXCoordDrag;
    int newYCoordDrag;
    int distXCoordDrag;
    int distYCoordDrag;
    int distXDrag;
    int distYDrag;
    private int ghostX = -1;
    private int ghostY = -1;
    private ObstacleData draggedObstacleSnapshot = null;

    private boolean isDragging = false;
    private float startX, startY;
    private static final float DRAG_THRESHOLD = 10;

    private int carSize = 3;
    int vehicleHardLimitSize = carSize * carSize;
    int obstacleCount = 0;
    private ArrayList<ObstacleData> placedObstacles = new ArrayList<>();
    private boolean bound = false;

    private BluetoothService btService;
    private utilities utilitiesClass = new utilities();
    private String vehicleText = "Vehicle not placed";
    private boolean firstTimeInitalize = false;
    // Constructors
    public GridMapClass(Context context) {
        this(context, null);
        initializeGrid();
    }

    public GridMapClass(Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);
        // Initalize painting data
        blackPaint.setStyle(Paint.Style.FILL_AND_STROKE);
        redPaint.setStyle(Paint.Style.STROKE);
        redPaint.setColor(Color.RED);
        redPaint.setStrokeWidth(6f);
        redPaint.setAntiAlias(true);
        greenPaint.setStyle(Paint.Style.FILL);
        greenPaint.setColor(Color.GREEN);
        textPaint.setColor(Color.WHITE);
        textPaint.setTextAlign(Paint.Align.CENTER);
        textPaint.setTextSize(Math.min(cellWidth, cellHeight) / 2f); // scale with cell size
        textPaint.setAntiAlias(true);
        verifiedPaint.setColor(Color.GRAY);
        verifiedPaint.setStrokeWidth(6f);
        paintObstacleVerified.setColor(Color.rgb(255, 165, 0));
        gestureDetector = new GestureDetector(context, new GestureDetector.SimpleOnGestureListener(){
            @Override
            public boolean onDown(MotionEvent event){
                return true;
            }

            @Override
            public boolean onSingleTapConfirmed(MotionEvent event) {
                Log.d("GridMapClass.java", "Single Tap Confirmed!");
                int x_coord = (int) (event.getX() / cellWidth);
                int y_coord = gridRows - 1 - (int) (event.getY() / cellHeight);

                if (!checkValidObstacle(x_coord, y_coord)) {
                    return false;
                }
                // First tap will ALWAYS be vehicle type
                if (!findVehicleDataType()) {
                    addVehicleToMap(x_coord, y_coord);
                } else {
                    ObstacleData gridMapObstacle = gridMapData.get(y_coord).get(x_coord);
                    switch (gridMapObstacle.getObstacleType()) {
                        case EMPTY:
                            if (!gridMapObstacle.getOccupied()) {
                                addNewObstacleToGrid(x_coord, y_coord);
                                Log.d("GridMapClass.java", "Added Obstacle added at (" + x_coord + "," + y_coord + ")");
                            }
                            break;
                        case Vehicle:
                            // Factor for the new
                            //if(!checkChangeRightDirectionOfVehicle(true)){
                            //Log.d("GridMapClass.java", "Unable to change vehicle turning due to invalid coords");
                            //break;
                            //}
                            int[] arrayTake = findVehicleBottomLeftObstacle();
                            switch(gridMapData.get(arrayTake[1]).get(arrayTake[0]).getDirection()){
                                case NORTH:
                                    changeVehicleDirection(ObstacleData.Direction.EAST);
                                    //rotateRightBluetooth();
                                    break;
                                case EAST:
                                    changeVehicleDirection(ObstacleData.Direction.SOUTH);
                                    //rotateRightBluetooth();
                                    break;
                                case SOUTH:
                                    changeVehicleDirection(ObstacleData.Direction.WEST);
                                    //rotateRightBluetooth();
                                    break;
                                case WEST:
                                    changeVehicleDirection(ObstacleData.Direction.NORTH);
                                    //rotateRightBluetooth();
                            }

                            break;
                        case Obstacle:
                            changeDirectionOfObstacle(x_coord, y_coord, true);
                            Log.d("GridMapClass.java", "Touched Obstacle changed direction at (" + x_coord + "," + y_coord + ")");
                            break;
                    }
                }
                invalidate();
                return true;
            }
            @Override
            public boolean onDoubleTap(MotionEvent event){
                Log.d("GridMapClass.java", "Double Tap Confirmed!");
                int x_coord = (int) (event.getX() / cellWidth);
                int y_coord = gridRows - 1 - (int) (event.getY() / cellHeight);

                if (gridMapData.get(y_coord).get(x_coord).getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle) {
                    if (checkValidObstacle(x_coord, y_coord) && gridMapData.get(y_coord).get(x_coord).getOccupied()) {
                        removeFromGrid(x_coord, y_coord, true);
                    }
                } else if (gridMapData.get(y_coord).get(x_coord).getObstacleType() == ObstacleData.OBSTACLETYPE.Vehicle) {
                    // Loop thru the map to remove the vehicle type
                    removeVehicleFromGrid();
                }
                return true;
            }

            @Override
            public void onLongPress(MotionEvent event){
                Log.d("GridMapClass.java", "Long Tap Confirmed!");
                /*
                int x_coord = (int) (event.getX() / cellWidth);
                int y_coord = gridRows - 1 - (int) (event.getY() / cellHeight);
                if(gridMapData.get(y_coord).get(x_coord).getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle){
                    Log.d("GridMapClass.java", "Obstacle Validated!");
                    gridMapData.get(y_coord).get(x_coord).setVerified(true);
                }
                */
                int x_coord = (int) (event.getX() / cellWidth);
                int y_coord = gridRows - 1 - (int) (event.getY() / cellHeight);

                if (!checkValidObstacle(x_coord, y_coord)) {
                    return;
                }
                ObstacleData gridMapObstacle = gridMapData.get(y_coord).get(x_coord);
                switch (gridMapObstacle.getObstacleType()) {
                    case Obstacle:
                        changeDirectionOfObstacle(x_coord, y_coord, false);
                        Log.d("GridMapClass.java", "Touched Obstacle changed direction to turn left at (" + x_coord + "," + y_coord + ")");
                        break;
                    case Vehicle:
                        int[] arrayTake = findVehicleBottomLeftObstacle();
                        switch (gridMapData.get(arrayTake[1]).get(arrayTake[0]).getDirection()) {
                            case NORTH:
                                changeVehicleDirection(ObstacleData.Direction.WEST);
                                //rotateLeftBluetooth();
                                break;
                            case EAST:
                                changeVehicleDirection(ObstacleData.Direction.NORTH);
                                //rotateLeftBluetooth();
                                break;
                            case SOUTH:
                                changeVehicleDirection(ObstacleData.Direction.EAST);
                                //rotateLeftBluetooth();
                                break;
                            case WEST:
                                changeVehicleDirection(ObstacleData.Direction.SOUTH);
                                //rotateLeftBluetooth();
                        }
                        break;
                }

                invalidate();
            }
        });
        initializeGrid();
    }

    private final ServiceConnection conn = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder service) {
            BluetoothService.LocalBinder binder = (BluetoothService.LocalBinder) service;
            btService = binder.getService();
            bound = true;
            Intent intent = new Intent(getContext(), BluetoothService.class);
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            bound = false;
            btService = null;
        }
    };

    public void bindBluetoothService() {
        Intent intent = new Intent(getContext(), BluetoothService.class);
        getContext().bindService(intent, conn, Context.BIND_AUTO_CREATE);
    }

    // Method to unbind the service
    public void unbindBluetoothService() {
        if (bound) {
            getContext().unbindService(conn);
            bound = false;
            Log.d("GridMapClass", "Unbinding BluetoothService.");
        }
    }

    // Note that arraylist, row is y
    // column is x
    // ArrayList calls the y coord first then the x.
    private void initializeGrid(){
        for (int y = 0; y < hardLimit; y++){
            ArrayList<ObstacleData> row = new ArrayList<>();
            for (int x = 0; x < hardLimit; x++){
                ObstacleData cell = new ObstacleData(-1, -1, ObstacleData.Direction.EMPTY, false, ObstacleData.OBSTACLETYPE.EMPTY, false, obstacleCount);
                row.add(cell);
            }
            gridMapData.add(row);
        }
    }

    public void setBluetoothService(BluetoothService service) {
        this.btService = service;
        Log.d("GridMapClass", "BluetoothService instance set.");
        Log.d("Lolol", utilities.convertBooleanToString(this.btService == null));
    }

    public boolean clearGridMap() {
        for (int y = 0; y < hardLimit; y++) {
            for (int x = 0; x < hardLimit; x++) {
                removeFromGrid(x, y, true);
            }
        }
        // 2. THE FIX: Reset the counting logic
        vehicleText = "Vehicle not placed"; // This "unlocks" the car for the next run
        placedObstacles.clear(); // Empty the list so findVehicleDataType/placedObstacles is fresh
        obstacleCount = 0;       // Reset the counter back to zero
        firstTimeInitalize = false; // Reset the vehicle status text too
        Log.d("GridMapClass,java", "Grid Map Cleared");
        return true;
    }

    public boolean addGridMapSaved(ArrayList<ArrayList<ObstacleData>> loadedData) {
        for (int y = 0; y < hardLimit; y++) {
            for (int x = 0; x < hardLimit; x++) {
                ObstacleData retrievedInfo = loadedData.get(y).get(x);
                changeObstacleData(x,y,retrievedInfo.getOccupied(),retrievedInfo.getDirection(),retrievedInfo.getObstacleType(),retrievedInfo.getVerified(),retrievedInfo.getObstacleNumber());
            }
        }
        gridMapData = loadedData;

        // placedObstacles
        for (int y = 0; y < hardLimit; y++) {
            for (int x = 0; x < hardLimit; x++) {
                if(gridMapData.get(y).get(x).getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle){
                    placedObstacles.add(gridMapData.get(y).get(x));
                }
            }
        }
        return true;
    }


    public void setGridColumns(int gridColumns){
        this.gridColumns = gridColumns;
        calculateDimensions();
    }

    public int getGridColumns(){
        return gridColumns;
    }

    public void setGridRows(int gridRows){
        this.gridRows = gridRows;
        calculateDimensions();
    }

    public int getGridRows(){
        return gridRows;
    }

    public ArrayList<ArrayList<ObstacleData>> returnGridMap(){
        return gridMapData;
    }

    private void calculateDimensions(){
        if (gridColumns < 1 || gridRows < 1){
            return;
        }
        cellWidth = (float) getWidth() / gridColumns;
        cellHeight = (float) getHeight() / gridRows;
        textPaint.setTextSize(Math.min(cellWidth, cellHeight) / 2f);
        invalidate();
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh){
        super.onSizeChanged(w, h, oldw, oldh);
        calculateDimensions();
    }

    @Override
    protected void onDraw(Canvas canvas){
        canvas.drawColor(Color.parseColor("#7CFC00"));
        if (gridColumns == 0 || gridRows == 0) {
            return;
        }

        // Draw grid first
        for (int i = 0; i <= gridColumns; i++) {
            canvas.drawLine(i * cellWidth, 0, i * cellWidth, getHeight(), blackPaint);
        }
        for (int j = 0; j <= gridRows; j++) {
            canvas.drawLine(0, j * cellHeight, getWidth(), j * cellHeight, blackPaint);
        }

        for (int y = 0; y < gridRows; y++) {
            for (int x = 0; x < gridColumns; x++) {
                ObstacleData obstacle = gridMapData.get(y).get(x);
                float top = (gridRows - 1 - y) * cellHeight;
                float bottom = top + cellHeight;
                float left = x * cellWidth;
                float right = left + cellWidth;
                if (obstacle.getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle) {
                    if (obstacle.getOccupied()) {
                        if (obstacle.getVerified()) {
                            canvas.drawRect(left, top, right, bottom, paintObstacleVerified);
                        } else {
                            canvas.drawRect(left, top, right, bottom, blackPaint);
                        }
                        float originalSize = textPaint.getTextSize();
                        if (obstacle.getVerified()) {
                            textPaint.setTextSize(originalSize * 1.5f);
                        }
                        float textX = left + (cellWidth / 2f);
                        float textY = top + (cellHeight / 2f) - ((textPaint.descent() + textPaint.ascent()) / 2);
                        if(obstacle.getVerified()){
                            canvas.drawText(utilities.convertObstacleIdToObstacleString(obstacle.getObstacleNumber()), textX, textY, textPaint);
                        }else{
                            canvas.drawText(String.valueOf(obstacle.getObstacleNumber()), textX, textY, textPaint);
                        }
                        textPaint.setTextSize(originalSize);
                        if (obstacle.getVerified()) {
                            drawDirectionalArrow(canvas, obstacle, left, top, verifiedPaint);
                        } else {
                            drawDirectionalArrow(canvas, obstacle, left, top, redPaint);
                        }
                    }
                } else if (obstacle.getObstacleType() == ObstacleData.OBSTACLETYPE.Vehicle) {
                    canvas.drawRect(left, top, right, bottom, greenPaint);
                    drawDirectionalArrow(canvas, obstacle, left, top, redPaint);
                } else if (obstacle.getObstacleType() == ObstacleData.OBSTACLETYPE.passedObstacle) {
                    canvas.drawRect(left, top, right, bottom, verifiedPaint);
                }
            }
        }
        if (isDragging && ghostX != -1 && ghostY != -1 && draggedObstacleSnapshot != null) {
            if (draggedObstacleSnapshot.getObstacleType() == ObstacleData.OBSTACLETYPE.Vehicle) {
                // Draw a 3x3 green ghost for the car
                for (int i = 0; i < 3; i++) {
                    for (int j = 0; j < 3; j++) {
                        float gLeft = (ghostX + i) * cellWidth;
                        float gTop = (gridRows - 1 - (ghostY + j)) * cellHeight;
                        canvas.drawRect(gLeft, gTop, gLeft + cellWidth, gTop + cellHeight, greenPaint);
                    }
                }
            } else {
                float gTop = (gridRows - 1 - ghostY) * cellHeight;
                float gLeft = ghostX * cellWidth;
                float gRight = gLeft + cellWidth;
                float gBottom = gTop + cellHeight;

                // Draw the square for the obstacle being dragged
                canvas.drawRect(gLeft, gTop, gRight, gBottom, blackPaint);

                // Draw the directional arrow on the ghost
                drawDirectionalArrow(canvas, draggedObstacleSnapshot, gLeft, gTop, redPaint);
                String idText = String.valueOf(draggedObstacleSnapshot.getObstacleNumber());
                // Draw the ID number on the ghost
                float tX = gLeft + (cellWidth / 2f);
                float tY = gTop + (cellHeight / 2f) - ((textPaint.descent() + textPaint.ascent()) / 2);
                canvas.drawText(idText, tX, tY, textPaint);
            }
        }
    }

    private void drawDirectionalArrow(Canvas canvas, ObstacleData obstacle, float left, float top, Paint paintColor) {
        float right = left + cellWidth;
        float bottom = top + cellHeight;
        float startX, startY, endX, endY;

        switch (obstacle.getDirection()) {
            case NORTH:
                startX = left;
                startY = top;
                endX = right;
                endY = top;
                break;
            case SOUTH:
                startX = left;
                startY = bottom;
                endX = right;
                endY = bottom;
                break;
            case EAST:
                startX = right;
                startY = top;
                endX = right;
                endY = bottom;
                break;
            case WEST:
                startX = left;
                startY = top;
                endX = left;
                endY = bottom;
                break;
            default:
                return;
        }
        canvas.drawLine(startX, startY, endX, endY, paintColor);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        gestureDetector.onTouchEvent(event);
        int currentXCoordRef = (int) (event.getX() / cellWidth);
        int currentYCoordRef = gridRows - 1 - (int) (event.getY() / cellHeight);
        switch (event.getAction()) {
            // New Coordinate about to be placed down
            case MotionEvent.ACTION_UP:
                if (isDragging && draggedObstacleSnapshot != null) {
                    Log.d("GridMapClass.java", "Action Up Motion Event Detected");
                    // --- VEHICLE DRAG LOGIC ---
                    if (draggedObstacleSnapshot.getObstacleType() == ObstacleData.OBSTACLETYPE.Vehicle) {
                        // First, remove the car from its OLD position
                        removeVehicleFromGrid();

                        // Try to add the car to the NEW position
                        // addVehicleToMap already contains the 3x3 occupancy/bounds checks!
                        int result = addVehicleToMap(currentXCoordRef, currentYCoordRef);

                        if (result == 1) {
                            // Restore the original direction (NORTH, SOUTH, etc.)
                            changeVehicleDirection(draggedObstacleSnapshot.getDirection());
                        } else {
                            // result 3 usually means "Occupied" in your addVehicleToMap logic
                            String reason = (result == 3) ? "Space occupied by obstacle!" : "Out of bounds!";
                            Toast.makeText(getContext(), "Cannot move vehicle: " + reason, Toast.LENGTH_SHORT).show();
                            addVehicleToMap(oldXCoordDrag, oldYCoordDrag);
                            changeVehicleDirection(draggedObstacleSnapshot.getDirection());
                        }
                    }
                    else {
                        if (currentXCoordRef >= 0 && currentXCoordRef < gridColumns && currentYCoordRef >= 0 && currentYCoordRef < gridRows){
                            // --- OBSTACLE DRAG LOGIC ---
                            boolean isTargetOccupied = gridMapData.get(currentYCoordRef).get(currentXCoordRef).getOccupied();
                            boolean isSameCell = (currentXCoordRef == oldXCoordDrag && currentYCoordRef == oldYCoordDrag);

                            if (isTargetOccupied && !isSameCell) {
                                // ILLEGAL MOVE: Snap back to old position
                                Toast.makeText(getContext(), "Space occupied!", Toast.LENGTH_SHORT).show();
                                changeObstacleData(
                                        oldXCoordDrag, oldYCoordDrag,
                                        true,
                                        draggedObstacleSnapshot.getDirection(),
                                        draggedObstacleSnapshot.getObstacleType(),
                                        draggedObstacleSnapshot.getVerified(),
                                        draggedObstacleSnapshot.getObstacleNumber() // This is the saved ID!
                                );
                            } else {
                                // VALID MOVE: Place in new cell
                                changeObstacleData(
                                        currentXCoordRef, currentYCoordRef,
                                        true,
                                        draggedObstacleSnapshot.getDirection(),
                                        draggedObstacleSnapshot.getObstacleType(),
                                        draggedObstacleSnapshot.getVerified(),
                                        draggedObstacleSnapshot.getObstacleNumber()
                                );
                                //Registering the new position in the list
                                ObstacleData newlyPlaced = gridMapData.get(currentYCoordRef).get(currentXCoordRef);
                                if (!placedObstacles.contains(newlyPlaced))placedObstacles.add(newlyPlaced);
                            }
                        } else {
                            // Dropped outside grid
                                removeFromGrid(oldXCoordDrag, oldYCoordDrag, true);
                        }
                    }
                    // Reset state
                    isDragging = false;
                    draggedObstacleSnapshot = null;
                    ghostX = -1;
                    ghostY = -1;
                    reformatObstacleDataArray();
                    invalidate();
                }
                break;
            // Old Coordinate about to be dragged
            case MotionEvent.ACTION_DOWN:
                // Lock the ENTIRE grid if the car is not Inactive or Unplaced
                if (!vehicleText.equals("Inactive") && !vehicleText.equals("Vehicle not placed")) {
                    return false;
                }
                startX = event.getX();
                startY = event.getY();
                isDragging = false;
                Log.d("GridMapClass.java", "Action Down Motion Event Detected");
                oldXCoordDrag = (int) (event.getX() / cellWidth);
                oldYCoordDrag = gridRows - 1 - (int) (event.getY() / cellHeight);
                // FIX: Create a NEW object so it doesn't get wiped by removeFromGrid
                ObstacleData original = gridMapData.get(oldYCoordDrag).get(oldXCoordDrag);

                if (original.getObstacleType() == ObstacleData.OBSTACLETYPE.Vehicle) {
                    if (!vehicleText.equals("Inactive")) {
                        Toast.makeText(getContext(), "Mission in progress! Car locked.", Toast.LENGTH_SHORT).show();
                        // We set ghost to -1 so no ghost is drawn
                        ghostX = -1;
                        ghostY = -1;
                        return false; // This prevents ACTION_MOVE from ever triggering
                    }
                    // We are dragging the car!
                    // We snapshot the bottom-left corner of the car
                    int[] vehiclePos = findVehicleBottomLeftObstacle();
                    oldXCoordDrag = vehiclePos[0];
                    oldYCoordDrag = vehiclePos[1];

                    // Create a snapshot representing the vehicle
                    draggedObstacleSnapshot = new ObstacleData(oldXCoordDrag, oldYCoordDrag,
                            original.getDirection(), true, ObstacleData.OBSTACLETYPE.Vehicle, false, -1);
                } else if (original.getOccupied()) {
                    draggedObstacleSnapshot = new ObstacleData(
                            original.getXCoord(),
                            original.getYCoord(),
                            original.getDirection(),
                            original.getOccupied(),
                            original.getObstacleType(),
                            original.getVerified(),
                            original.getObstacleNumber() // This is the ID we are "freezing"
                    );
                }
                // Set ghost to starting position
                ghostX = oldXCoordDrag;
                ghostY = oldYCoordDrag;
                break;
            // While being dragged
            case MotionEvent.ACTION_MOVE:

                float dx = Math.abs(event.getX() - startX);
                float dy = Math.abs(event.getY() - startY);

                // Detect drag start
                if (!isDragging && (dx > DRAG_THRESHOLD || dy > DRAG_THRESHOLD)) {
                    isDragging = true;
                    // If you want the old spot to go blank
                    // immediately when you start dragging:
                    if (draggedObstacleSnapshot != null &&
                            draggedObstacleSnapshot.getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle) {
                        removeFromGrid(oldXCoordDrag, oldYCoordDrag, false);
                    }
                }

                if (isDragging) {

                    // Convert touch position to grid coordinates
                    int newX = (int) (event.getX() / cellWidth);
                    int newY = gridRows - 1 - (int) (event.getY() / cellHeight);

                    // Clamp bounds (IMPORTANT to avoid crashes)
                    if (newX >= 0 && newX < 20 && newY >= 0 && newY < 20) {

                        // Only update if finger moved to a NEW cell
                        if (newX != ghostX || newY != ghostY) {
                            ghostX = newX;
                            ghostY = newY;
                        // This forces onDraw to show the obstacle at the new ghost spot
                            invalidate();
                        }
                    }
                }

                Log.d("GridMapClass.java", "Dragging...");

                break;
        }
        return true;
    }

    // Add new Obstacle
    public int addNewObstacleToGrid(int x_coord, int y_coord) {
        if (checkValidObstacle(x_coord, y_coord)) {
            ObstacleData gridMapObstacle = gridMapData.get(y_coord).get(x_coord);
            if (!gridMapObstacle.getOccupied()) {
                if (obstacleCount == 0) {
                    obstacleCount += 1;
                } else {
                    Log.d("Who that hunk", "obstacleCount " + Integer.toString(obstacleCount));
                    Log.d("Check placed obstacle", "More than 1 obstacle detected, Place obstacle " + Integer.toString(placedObstacles.size() - 1));
                    obstacleCount = placedObstacles.get(placedObstacles.size() - 1).getObstacleNumber() + 1;
                    Log.d("Check placed obstacle", "Place obstacle " + Integer.toString(obstacleCount));
                }
                changeObstacleData(x_coord, y_coord, true, ObstacleData.Direction.NORTH, ObstacleData.OBSTACLETYPE.Obstacle, false, obstacleCount);
                placedObstacles.add(gridMapObstacle);
                invalidate();
                return 1;
            } else {
                return 2;
            }
        } else {
            return 0;
        }
    }

    // Updates the data to anything
    public boolean updateVehiclePassed(int old_x_coord, int old_y_coord, int new_x_coord, int new_y_coord) {
        if (checkValidObstacle(old_x_coord, old_y_coord) && checkValidObstacle(new_x_coord, new_y_coord)) {
            ObstacleData gridMapObstacleOld = gridMapData.get(old_y_coord).get(old_x_coord);
            ObstacleData gridMapObstacleNew = gridMapData.get(new_y_coord).get(new_x_coord);
            if (!gridMapObstacleNew.getOccupied()) {
                changeObstacleData(new_x_coord, new_y_coord, gridMapObstacleOld.getOccupied(), gridMapObstacleOld.getDirection(), gridMapObstacleOld.getObstacleType(), gridMapObstacleOld.getVerified(), gridMapObstacleOld.getObstacleNumber());
                changeObstacleData(old_x_coord, old_y_coord, false, ObstacleData.Direction.EMPTY, ObstacleData.OBSTACLETYPE.passedObstacle, true, 0);
                invalidate();
                return true;
            }
        }
        return false;
    }
    public void rearrangeObstacleData(int x_coord, int y_coord){
        placedObstacles.remove(gridMapData.get(y_coord).get(x_coord));
        obstacleCount -= 1;

        // Reassign obstacle numbers
        for (int i = 0; i < placedObstacles.size(); i++) {
            ObstacleData obstacle = placedObstacles.get(i);
            obstacle.setObstacleNumber(i + 1);
        }
    }

    public void reformatObstacleDataArray(){
        placedObstacles.sort(Comparator.comparingInt(ObstacleData::getObstacleNumber));
    }
    // Remove the grid data in its entirety
    public int removeFromGrid(int x_coord, int y_coord, boolean updatePlacedObstacle) {
        if (x_coord >= 0 && y_coord >= 0 && x_coord < gridColumns && y_coord < gridRows) {

            // If this cell is part of a vehicle, remove the WHOLE vehicle
            if (gridMapData.get(y_coord).get(x_coord).getObstacleType() == ObstacleData.OBSTACLETYPE.Vehicle) {
                if (updatePlacedObstacle) {
                Log.d("GridMapClass.java", "Vehicle cell targeted. Removing entire 3x3 vehicle.");
                return removeVehicleFromGrid();
            } else {
                // This part is called by the internal loop (ACTION_MOVE/UP).
                // Just clear the single cell to avoid recursion.
                changeObstacleData(x_coord, y_coord, false, ObstacleData.Direction.EMPTY, ObstacleData.OBSTACLETYPE.EMPTY, false, 0);
                invalidate();
                return 1;
            }
        }
            if (gridMapData.get(y_coord).get(x_coord).getOccupied() || gridMapData.get(y_coord).get(x_coord).getObstacleType() == ObstacleData.OBSTACLETYPE.passedObstacle) {
                if (gridMapData.get(y_coord).get(x_coord).getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle) {
                    // Remove from history
                    if(updatePlacedObstacle){
                        rearrangeObstacleData(x_coord, y_coord);
                    }
                }
                changeObstacleData(x_coord, y_coord, false, ObstacleData.Direction.EMPTY, ObstacleData.OBSTACLETYPE.EMPTY, false, 0);
                invalidate();
                return 1;
            } else {
                return 2;
            }
        } else {
            return 0;
        }
    }

    public int removeVehicleFromGrid() {
        for (int y = 0; y < hardLimit; y++) {
            for (int x = 0; x < hardLimit; x++) {
                if (gridMapData.get(y).get(x).getObstacleType() == ObstacleData.OBSTACLETYPE.Vehicle) {
                    removeFromGrid(x, y, false);
                }
            }
        }
        return 1;
    }

    // Initial Vehicle addition
    public int addVehicleToMap(int x_coord, int y_coord) {
        if (x_coord + carSize > gridColumns || y_coord + carSize > gridRows) {
            Log.d("GridMapClass.java", "Invalid Location to place car!");
            return 0;
        } else if (findVehicleDataType()) {
            Log.d("GridMapClass.java", "Car is already placed on the map!");
            return 2;
        } else {
            for (int x = x_coord; x < x_coord + carSize; x++) {
                for (int y = y_coord; y < y_coord + carSize; y++) {
                    if (gridMapData.get(y).get(x).getOccupied()) {
                        Log.d("GridMapClass.java", "Vehicle is unable to be placed due to an obstacle at");
                        return 3;
                    }
                }
            }
            for (int x = x_coord; x < x_coord + carSize; x++) {
                for (int y = y_coord; y < y_coord + carSize; y++) {
                    changeObstacleData(x, y, true, ObstacleData.Direction.NORTH, ObstacleData.OBSTACLETYPE.Vehicle, false, -1);
                    invalidate();
                }
            }
            return 1;
        }
    }
    public int changeDirectionOfObstacleFlexible(int x_coord, int y_coord, ObstacleData.Direction takeInNewDirection){
        ObstacleData gridMapObstacle = gridMapData.get(y_coord).get(x_coord);
        if(gridMapObstacle.getObstacleType() == ObstacleData.OBSTACLETYPE.EMPTY || gridMapObstacle.getObstacleType() == ObstacleData.OBSTACLETYPE.passedObstacle){
            return 2;
        }
        if(gridMapObstacle.getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle){
            changeObstacleData(x_coord, y_coord, gridMapObstacle.getOccupied(), takeInNewDirection, gridMapObstacle.getObstacleType(), gridMapObstacle.getVerified(), gridMapObstacle.getObstacleNumber());
            invalidate();
            return 1;
        }else if(gridMapObstacle.getObstacleType() == ObstacleData.OBSTACLETYPE.Vehicle){
            changeVehicleDirection(takeInNewDirection);
            invalidate();
            return 3;
        }else{
            return 4;
        }

    }
    public boolean changeDirectionOfObstacle(int x_coord, int y_coord, boolean leftRight) {
        ObstacleData gridMapObstacle = gridMapData.get(y_coord).get(x_coord);
        if (leftRight) {
            // Turn right
            switch (gridMapObstacle.direction) {
                case NORTH:
                    changeObstacleData(x_coord, y_coord, gridMapObstacle.getOccupied(), ObstacleData.Direction.EAST, gridMapObstacle.getObstacleType(), gridMapObstacle.getVerified(), gridMapObstacle.getObstacleNumber());
                    break;
                case EAST:
                    changeObstacleData(x_coord, y_coord, gridMapObstacle.getOccupied(), ObstacleData.Direction.SOUTH, gridMapObstacle.getObstacleType(), gridMapObstacle.getVerified(), gridMapObstacle.getObstacleNumber());
                    break;
                case SOUTH:
                    changeObstacleData(x_coord, y_coord, gridMapObstacle.getOccupied(), ObstacleData.Direction.WEST, gridMapObstacle.getObstacleType(), gridMapObstacle.getVerified(), gridMapObstacle.getObstacleNumber());
                    break;
                case WEST:
                    changeObstacleData(x_coord, y_coord, gridMapObstacle.getOccupied(), ObstacleData.Direction.NORTH, gridMapObstacle.getObstacleType(), gridMapObstacle.getVerified(), gridMapObstacle.getObstacleNumber());
                    break;
                default:
                    return false;
            }
        } else {
            // turn Left
            switch (gridMapObstacle.direction) {
                case NORTH:
                    changeObstacleData(x_coord, y_coord, gridMapObstacle.getOccupied(), ObstacleData.Direction.WEST, gridMapObstacle.getObstacleType(), gridMapObstacle.getVerified(), gridMapObstacle.getObstacleNumber());
                    break;
                case EAST:
                    changeObstacleData(x_coord, y_coord, gridMapObstacle.getOccupied(), ObstacleData.Direction.NORTH, gridMapObstacle.getObstacleType(), gridMapObstacle.getVerified(), gridMapObstacle.getObstacleNumber());
                    break;
                case SOUTH:
                    changeObstacleData(x_coord, y_coord, gridMapObstacle.getOccupied(), ObstacleData.Direction.EAST, gridMapObstacle.getObstacleType(), gridMapObstacle.getVerified(), gridMapObstacle.getObstacleNumber());
                    break;
                case WEST:
                    changeObstacleData(x_coord, y_coord, gridMapObstacle.getOccupied(), ObstacleData.Direction.SOUTH, gridMapObstacle.getObstacleType(), gridMapObstacle.getVerified(), gridMapObstacle.getObstacleNumber());
                    break;
            }
        }
        return true;
    }

    public void changeObstacleData(int x_coord, int y_coord, boolean occupiedStatus, ObstacleData.Direction directionData, ObstacleData.OBSTACLETYPE obstacleType, boolean verified, int obstacle_number) {
        ObstacleData gridMapObstacle = gridMapData.get(y_coord).get(x_coord);
        gridMapObstacle.setxCoord(x_coord);
        gridMapObstacle.setYCoord(y_coord);
        gridMapObstacle.setDirection(directionData);
        gridMapObstacle.setOccupied(occupiedStatus);
        gridMapObstacle.setObstacleType(obstacleType);
        gridMapObstacle.setVerified(verified);
        gridMapObstacle.setObstacleNumber(obstacle_number);
        //gridMapObstacle.printObstacleData();
    }

    // Boolean to check if vehicle exist
    public boolean findVehicleDataType() {
        for (int y = 0; y < gridRows; y++) {
            for (int x = 0; x < gridColumns; x++) {
                if (gridMapData.get(y).get(x).getObstacleType() == ObstacleData.OBSTACLETYPE.Vehicle) {
                    Log.d("GridMapClass.java", "I found the vehicle data at " + x + "," + y);
                    return true;
                }
            }
        }
        return false;
    }

    // Boolean to check if vehicle has coordinates
    public int[] findVehicleBottomLeftObstacle() {
        for (int y = 0; y < gridRows; y++) {
            for (int x = 0; x < gridColumns; x++) {
                if (gridMapData.get(y).get(x).getObstacleType() == ObstacleData.OBSTACLETYPE.Vehicle) {
                    return new int[]{x, y};
                }
            }
        }
        Log.d("GridMapClass.java", "Unable to find vehicle");
        return new int[]{-2, -2};
    }

    public boolean moveVehicleStraight(ObstacleData.Direction vehicleDirection, boolean sendToBluetooth) throws JSONException {
        // Find Vehicle (Check Direction)
        if (!findVehicleDataType()) {
            Log.d("GridMapClass.java", "Unable to move joystick as no vehicle in placed");
            return false;
        }
        // Vehicle origin
        int checkObstacleAhead;
        int staticCoord;
        int[] bottomLeftObstacleCoords = findVehicleBottomLeftObstacle();
        ObstacleData bottomLeftObstacle = gridMapData.get(bottomLeftObstacleCoords[1]).get(bottomLeftObstacleCoords[0]);
        // Since vehicle found at bottom left first, determine the boxes from there
        // if straight, use top 3 cols
        // if right, use right box
        // if left, use left box
        // if bottom, use bottom box
        switch (vehicleDirection) {
            case NORTH:
                if (bottomLeftObstacle.getDirection() == ObstacleData.Direction.WEST && checkChangeRightDirectionOfVehicle(false)) {
                    // Turn from West to North
                    turnRightDirectionOfVehicle(ObstacleData.Direction.WEST, false, sendToBluetooth);
                    if(sendToBluetooth){
                        strafeRightBluetooth();
                    }

                } else if (bottomLeftObstacle.getDirection() == ObstacleData.Direction.EAST && checkChangeLeftDirectionOfVehicle(false)) {
                    // Turn from East to North
                    turnLeftDirectionOfVehicle(ObstacleData.Direction.EAST, false, sendToBluetooth);
                    if(sendToBluetooth){
                        strafeLeftBluetooth();
                    }
                } else if (bottomLeftObstacle.getDirection() == ObstacleData.Direction.SOUTH) {
                    checkObstacleAhead = bottomLeftObstacle.y_coord + carSize;
                    staticCoord = bottomLeftObstacle.x_coord;
                    moveVehicleNorth(bottomLeftObstacle, checkObstacleAhead, staticCoord);
                    if(sendToBluetooth){
                        reverseBluetooth();
                    }
                } else if (bottomLeftObstacle.getDirection() == ObstacleData.Direction.NORTH) {
                    // Go Straight
                    checkObstacleAhead = bottomLeftObstacle.y_coord + carSize;
                    staticCoord = bottomLeftObstacle.x_coord;
                    moveVehicleNorth(bottomLeftObstacle, checkObstacleAhead, staticCoord);
                    if(sendToBluetooth){
                        forwardDirectionBluetooth();
                    }
                }
                break;
            case EAST:
                if (bottomLeftObstacle.getDirection() == ObstacleData.Direction.NORTH && checkChangeRightDirectionOfVehicle(false)) {
                    // Turn from North to East
                    turnRightDirectionOfVehicle(ObstacleData.Direction.NORTH, false, sendToBluetooth);
                    if(sendToBluetooth){
                        strafeRightBluetooth();
                    }
                } else if (bottomLeftObstacle.getDirection() == ObstacleData.Direction.SOUTH && checkChangeLeftDirectionOfVehicle(false)) {
                    // Turn from South to East
                    turnLeftDirectionOfVehicle(ObstacleData.Direction.SOUTH, false, sendToBluetooth);
                    if(sendToBluetooth){
                        strafeLeftBluetooth();
                    }
                } else if (bottomLeftObstacle.getDirection() == ObstacleData.Direction.WEST) {
                    checkObstacleAhead = bottomLeftObstacle.x_coord + carSize;
                    staticCoord = bottomLeftObstacle.y_coord;
                    moveVehicleEast(bottomLeftObstacle, checkObstacleAhead, staticCoord);
                    if(sendToBluetooth){
                        reverseBluetooth();
                    }
                } else if (bottomLeftObstacle.getDirection() == ObstacleData.Direction.EAST) {
                    // Go Straight
                    checkObstacleAhead = bottomLeftObstacle.x_coord + carSize;
                    staticCoord = bottomLeftObstacle.y_coord;
                    moveVehicleEast(bottomLeftObstacle, checkObstacleAhead, staticCoord);
                    if(sendToBluetooth){
                        forwardDirectionBluetooth();
                    }
                }
                break;
            case SOUTH:
                if (bottomLeftObstacle.getDirection() == ObstacleData.Direction.EAST && checkChangeRightDirectionOfVehicle(false)) {
                    // Turn from East to South
                    turnRightDirectionOfVehicle(ObstacleData.Direction.EAST, false, sendToBluetooth);
                    if(sendToBluetooth){
                        strafeRightBluetooth();
                    }
                } else if (bottomLeftObstacle.getDirection() == ObstacleData.Direction.WEST && checkChangeLeftDirectionOfVehicle(false)) {
                    // Turn from West to South
                    turnLeftDirectionOfVehicle(ObstacleData.Direction.WEST, false, sendToBluetooth);
                    if(sendToBluetooth){
                        strafeLeftBluetooth();
                    }
                } else if (bottomLeftObstacle.getDirection() == ObstacleData.Direction.NORTH) {
                    checkObstacleAhead = bottomLeftObstacle.y_coord - 1;
                    staticCoord = bottomLeftObstacle.x_coord;
                    moveVehicleSouth(bottomLeftObstacle, checkObstacleAhead, staticCoord);
                    if(sendToBluetooth){
                        reverseBluetooth();
                    }
                } else if (bottomLeftObstacle.getDirection() == ObstacleData.Direction.SOUTH) {
                    // Go Straight
                    checkObstacleAhead = bottomLeftObstacle.y_coord - 1;
                    staticCoord = bottomLeftObstacle.x_coord;
                    moveVehicleSouth(bottomLeftObstacle, checkObstacleAhead, staticCoord);
                    if(sendToBluetooth){
                        forwardDirectionBluetooth();
                    }
                }

                break;
            case WEST:
                if (bottomLeftObstacle.getDirection() == ObstacleData.Direction.SOUTH && checkChangeRightDirectionOfVehicle(false)) {
                    // Turn from South to West
                    turnRightDirectionOfVehicle(ObstacleData.Direction.SOUTH, false, sendToBluetooth);
                    if(sendToBluetooth){
                        strafeRightBluetooth();
                    }
                } else if (bottomLeftObstacle.getDirection() == ObstacleData.Direction.NORTH && checkChangeLeftDirectionOfVehicle(false)) {
                    // Turn from North to West
                    turnLeftDirectionOfVehicle(ObstacleData.Direction.NORTH, false, sendToBluetooth);
                    if(sendToBluetooth){
                        strafeLeftBluetooth();
                    }
                } else if (bottomLeftObstacle.getDirection() == ObstacleData.Direction.EAST) {
                    checkObstacleAhead = bottomLeftObstacle.x_coord - 1;
                    staticCoord = bottomLeftObstacle.y_coord;
                    moveVehicleWest(bottomLeftObstacle, checkObstacleAhead, staticCoord);
                    if(sendToBluetooth){
                        reverseBluetooth();
                    }
                } else if (bottomLeftObstacle.getDirection() == ObstacleData.Direction.WEST) {
                    // Go Straight
                    checkObstacleAhead = bottomLeftObstacle.x_coord - 1;
                    staticCoord = bottomLeftObstacle.y_coord;
                    moveVehicleWest(bottomLeftObstacle, checkObstacleAhead, staticCoord);
                    if(sendToBluetooth){
                        forwardDirectionBluetooth();
                    }
                }
                break;
            case EMPTY:
                break;
            default:
                return false;
        }

        // Attempt to check if above 3 coords do not have obstacle or are at the end of the vehicle
        return true;
    }

    // Check if obstacle used is indeed valid
    public boolean checkValidObstacle(int x_coord, int y_coord) {
        if (x_coord >= lowLimit && y_coord >= lowLimit && x_coord < gridColumns && y_coord < gridRows) {
            return true;
        }
        return false;
    }

    public boolean moveVehicleNorth(ObstacleData bottomLeftObstacle, int checkObstacleAhead, int staticCoord) {
        // Check if at edge
        if (checkObstacleAhead > gridRows - 1) {
            Log.d("GridMapClass.java", "Edge Reached! Unable to move anymore");
            return false;
        }
        for (int i = staticCoord; i < staticCoord + carSize; i++) {
            Log.d("GridMapClass.java", "Checking Coords " + i + ", " + checkObstacleAhead);
            if (gridMapData.get(checkObstacleAhead).get(i).getOccupied()) {
                Log.d("GridMapClass.java", "Obstacle Detected at Coords " + i + ", " + checkObstacleAhead);
                return false;
            }
        }
        Log.d("GridMapClass.java", "Moving North!");
        for (int i = staticCoord; i < staticCoord + carSize; i++) {
            updateVehiclePassed(i, bottomLeftObstacle.y_coord, i, checkObstacleAhead);
        }
        return true;
    }

    public boolean moveVehicleEast(ObstacleData bottomLeftObstacle, int checkObstacleAhead, int staticCoord) {
        // Check if at edge
        if (checkObstacleAhead > gridColumns - 1) {
            Log.d("GridMapClass.java", "Edge Reached! Unable to move anymore");
            return false;
        }

        for (int i = staticCoord; i < staticCoord + carSize; i++) {
            Log.d("GridMapClass.java", "Checking Coords " + i + ", " + checkObstacleAhead);
            if (gridMapData.get(i).get(checkObstacleAhead).getOccupied()) {
                Log.d("GridMapClass.java", "Obstacle Detected at Coords " + i + ", " + checkObstacleAhead);
                return false;
            }
        }
        Log.d("GridMapClass.java", "Moving East!");
        for (int i = staticCoord; i < staticCoord + carSize; i++) {
            updateVehiclePassed(bottomLeftObstacle.x_coord, i, checkObstacleAhead, i);
        }
        return true;
    }

    public boolean moveVehicleSouth(ObstacleData bottomLeftObstacle, int checkObstacleAhead, int staticCoord) {
        // Check if at edge
        if (checkObstacleAhead < 0) {
            Log.d("GridMapClass.java", "Edge Reached! Unable to move anymore");
            return false;
        }
        for (int i = staticCoord; i < staticCoord + carSize; i++) {
            Log.d("GridMapClass.java", "Checking Coords " + i + ", " + checkObstacleAhead);
            if (gridMapData.get(checkObstacleAhead).get(i).getOccupied()) {
                Log.d("GridMapClass.java", "Obstacle Detected at Coords " + i + ", " + checkObstacleAhead);
                return false;
            }
        }
        Log.d("GridMapClass.java", "Moving South!");
        for (int i = staticCoord; i < staticCoord + carSize; i++) {
            updateVehiclePassed(i, bottomLeftObstacle.y_coord + carSize - 1, i, checkObstacleAhead);
        }
        return true;
    }

    public boolean moveVehicleWest(ObstacleData bottomLeftObstacle, int checkObstacleAhead, int staticCoord) {
        // Check if at edge
        if (checkObstacleAhead < 0) {
            Log.d("GridMapClass.java", "Edge Reached! Unable to move anymore");
            return false;
        }
        for (int i = staticCoord; i < staticCoord + carSize; i++) {
            Log.d("GridMapClass.java", "Checking Coords " + checkObstacleAhead + ", " + i);
            if (gridMapData.get(i).get(checkObstacleAhead).getOccupied()) {
                Log.d("GridMapClass.java", "Obstacle Detected at Coords " + checkObstacleAhead + ", " + i);
                return false;
            }
        }
        Log.d("GridMapClass.java", "Moving West!");
        for (int i = staticCoord; i < staticCoord + carSize; i++) {
            updateVehiclePassed(bottomLeftObstacle.x_coord + carSize - 1, i, bottomLeftObstacle.x_coord - 1, i);
        }
        return true;
    }

    public boolean checkChangeLeftDirectionOfVehicle(boolean reverseTrue) {
        int countVehicleObstacle = 0;
        for (int y = 0; y < hardLimit; y++) {
            for (int x = 0; x < hardLimit; x++) {
                if (gridMapData.get(y).get(x).getObstacleType() == ObstacleData.OBSTACLETYPE.Vehicle) {
                    if (vehicleHardLimitSize > countVehicleObstacle) {
                        switch (gridMapData.get(y).get(x).direction) {
                            case NORTH:
                                if (reverseTrue) {
                                    if (x - carSize < 0 || y - carSize < 0) {
                                        return false;
                                    }
                                    if (gridMapData.get(y - carSize).get(x - carSize).getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle) {
                                        Log.d("GridMapClass.java", "Obstacle in the way for left turn from North! Unable to turn");
                                        return false;
                                    }
                                } else {
                                    if (x - carSize < 0 || y + carSize >= gridRows) {
                                        return false;
                                    }
                                    if (gridMapData.get(y + carSize).get(x - carSize).getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle) {
                                        Log.d("GridMapClass.java", "Obstacle in the way for left turn from North! Unable to turn");
                                        return false;
                                    }
                                }
                                break;
                            case EAST:
                                if (reverseTrue) {
                                    if (x - carSize < 0 || y + carSize >= gridRows) {
                                        return false;
                                    }
                                    if (gridMapData.get(y + carSize).get(x - carSize).getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle) {
                                        Log.d("GridMapClass.java", "Obstacle in the way for left turn from East! Unable to turn");
                                        return false;
                                    }
                                } else {
                                    if (x + carSize >= gridColumns || y + carSize >= gridRows) {
                                        return false;
                                    }
                                    if (gridMapData.get(y + carSize).get(x + carSize).getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle) {
                                        Log.d("GridMapClass.java", "Obstacle in the way for left turn from East! Unable to turn");
                                        return false;
                                    }
                                }
                                break;
                            case SOUTH:
                                if (reverseTrue) {
                                    if (x + carSize >= gridColumns || y + carSize >= gridRows) {
                                        return false;
                                    }
                                    if (gridMapData.get(y + carSize).get(x + carSize).getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle) {
                                        Log.d("GridMapClas.java", "Obstacle in the way for left turn from South! Unable to turn");
                                        return false;
                                    }
                                } else {
                                    if (x + carSize >= gridColumns || y - carSize < 0) {
                                        return false;
                                    }
                                    if (gridMapData.get(y - carSize).get(x + carSize).getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle) {
                                        Log.d("GridMapClas.java", "Obstacle in the way for left turn from South! Unable to turn");
                                        return false;
                                    }
                                }

                                break;
                            case WEST:
                                if (reverseTrue) {
                                    if (x + carSize >= gridColumns || y - carSize < 0) {
                                        return false;
                                    }
                                    if (gridMapData.get(y - carSize).get(x + carSize).getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle) {
                                        Log.d("GridMapClas.java", "Obstacle in the way for left turn from West! Unable to turn");
                                        return false;
                                    }
                                } else {
                                    if (x - carSize < 0 || y - carSize < 0) {
                                        return false;
                                    }
                                    if (gridMapData.get(y - carSize).get(x - carSize).getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle) {
                                        Log.d("GridMapClas.java", "Obstacle in the way for left turn from West! Unable to turn");
                                        return false;
                                    }
                                }

                                break;
                        }
                    }
                }
            }
        }
        return true;
    }

    public boolean turnRightDirectionOfVehicle(ObstacleData.Direction obstacleFacingWhichDirection, boolean reverseTrue, boolean sendToBluetooth) {
        switch (obstacleFacingWhichDirection) {
            case NORTH:
                if (reverseTrue) {
                    forLoopUp(ObstacleData.Direction.SOUTH);
                    forLoopUp(ObstacleData.Direction.EAST);
                    changeVehicleDirection(ObstacleData.Direction.WEST);
                    if(sendToBluetooth) {
                        reverseRightBluetooth();
                    }
                } else {
                    forLoopUp(ObstacleData.Direction.NORTH);
                    forLoopUp(ObstacleData.Direction.EAST);
                    changeVehicleDirection(ObstacleData.Direction.EAST);
                }
                break;
            case EAST:
                if (reverseTrue) {
                    forLoopUp(ObstacleData.Direction.WEST);
                    forLoopUp(ObstacleData.Direction.SOUTH);
                    changeVehicleDirection(ObstacleData.Direction.NORTH);
                    if(sendToBluetooth) {
                        reverseRightBluetooth();
                    }
                } else {
                    forLoopUp(ObstacleData.Direction.EAST);
                    forLoopUp(ObstacleData.Direction.SOUTH);
                    changeVehicleDirection(ObstacleData.Direction.SOUTH);
                }
                break;
            case SOUTH:
                if (reverseTrue) {
                    forLoopUp(ObstacleData.Direction.NORTH);
                    forLoopUp(ObstacleData.Direction.WEST);
                    changeVehicleDirection(ObstacleData.Direction.EAST);
                    if(sendToBluetooth) {
                        reverseRightBluetooth();
                    }
                } else {
                    forLoopUp(ObstacleData.Direction.SOUTH);
                    forLoopUp(ObstacleData.Direction.WEST);
                    changeVehicleDirection(ObstacleData.Direction.WEST);
                }
                break;
            case WEST:
                if (reverseTrue) {
                    forLoopUp(ObstacleData.Direction.EAST);
                    forLoopUp(ObstacleData.Direction.NORTH);
                    changeVehicleDirection(ObstacleData.Direction.SOUTH);
                    if(sendToBluetooth) {
                        reverseRightBluetooth();
                    }
                } else {
                    forLoopUp(ObstacleData.Direction.WEST);
                    forLoopUp(ObstacleData.Direction.NORTH);
                    changeVehicleDirection(ObstacleData.Direction.NORTH);
                }
        }
        return true;
    }

    public boolean turnLeftDirectionOfVehicle(ObstacleData.Direction obstacleFacingWhichDirection, boolean reverseTrue, boolean sendToBluetooth) {
        switch (obstacleFacingWhichDirection) {
            case NORTH:
                if (reverseTrue) {
                    forLoopUp(ObstacleData.Direction.SOUTH);
                    forLoopUp(ObstacleData.Direction.WEST);
                    changeVehicleDirection(ObstacleData.Direction.EAST);
                    if(sendToBluetooth){
                        reverseLeftBluetooth();
                    }

                } else {
                    forLoopUp(ObstacleData.Direction.NORTH);
                    forLoopUp(ObstacleData.Direction.WEST);
                    changeVehicleDirection(ObstacleData.Direction.WEST);
                }
                break;
            case EAST:
                if (reverseTrue) {
                    forLoopUp(ObstacleData.Direction.WEST);
                    forLoopUp(ObstacleData.Direction.NORTH);
                    changeVehicleDirection(ObstacleData.Direction.SOUTH);
                    if(sendToBluetooth) {
                        reverseLeftBluetooth();
                    }
                } else {
                    forLoopUp(ObstacleData.Direction.EAST);
                    forLoopUp(ObstacleData.Direction.NORTH);
                    changeVehicleDirection(ObstacleData.Direction.NORTH);
                }
                break;
            case SOUTH:
                if (reverseTrue) {
                    forLoopUp(ObstacleData.Direction.NORTH);
                    forLoopUp(ObstacleData.Direction.EAST);
                    changeVehicleDirection(ObstacleData.Direction.WEST);
                    if(sendToBluetooth) {
                        reverseLeftBluetooth();
                    }
                } else {
                    forLoopUp(ObstacleData.Direction.SOUTH);
                    forLoopUp(ObstacleData.Direction.EAST);
                    changeVehicleDirection(ObstacleData.Direction.EAST);
                }
                break;
            case WEST:
                if (reverseTrue) {
                    forLoopUp(ObstacleData.Direction.EAST);
                    forLoopUp(ObstacleData.Direction.SOUTH);
                    changeVehicleDirection(ObstacleData.Direction.NORTH);
                    if(sendToBluetooth) {
                        reverseLeftBluetooth();
                    }
                } else {
                    forLoopUp(ObstacleData.Direction.WEST);
                    forLoopUp(ObstacleData.Direction.SOUTH);
                    changeVehicleDirection(ObstacleData.Direction.SOUTH);
                }
        }
        return true;
    }
    public boolean reverseLeftVehicle(boolean sendToBluetooth){
        int[] vehicleBottomData = findVehicleBottomLeftObstacle();
        if(vehicleBottomData[0] == -2 && vehicleBottomData[1] == -2){
            return false;
        }
        //if(checkChangeLeftDirectionOfVehicle(true)) {
        turnLeftDirectionOfVehicle(gridMapData.get(vehicleBottomData[1]).get(vehicleBottomData[0]).getDirection(), true, sendToBluetooth);
        return true;
        //}else{
        //return false;
        //}
    }

    public boolean reverseRightVehicle(boolean sendToBluetooth){
        int[] vehicleBottomData = findVehicleBottomLeftObstacle();
        if(vehicleBottomData[0] == -2 && vehicleBottomData[1] == -2){
            return false;
        }
        //if(checkChangeRightDirectionOfVehicle(true)) {
        turnRightDirectionOfVehicle(gridMapData.get(vehicleBottomData[1]).get(vehicleBottomData[0]).getDirection(), true, sendToBluetooth);
        return true;
        //}else{
        //return false;
        //}
    }

    public boolean forLoopUp(ObstacleData.Direction directionToMove) {
        int[] findBottomLeftObstacle;
        ObstacleData bottomLeftObstacle;
        int checkObstacleAhead;
        int staticCoord;
        for (int x = 0; x < carSize; x++) {
            findBottomLeftObstacle = findVehicleBottomLeftObstacle();
            bottomLeftObstacle = gridMapData.get(findBottomLeftObstacle[1]).get(findBottomLeftObstacle[0]);
            switch (directionToMove) {
                case NORTH:
                    checkObstacleAhead = bottomLeftObstacle.y_coord + carSize;
                    staticCoord = bottomLeftObstacle.x_coord;
                    moveVehicleNorth(bottomLeftObstacle, checkObstacleAhead, staticCoord);
                    break;
                case EAST:
                    checkObstacleAhead = bottomLeftObstacle.x_coord + carSize;
                    staticCoord = bottomLeftObstacle.y_coord;
                    moveVehicleEast(bottomLeftObstacle, checkObstacleAhead, staticCoord);
                    break;
                case SOUTH:
                    checkObstacleAhead = bottomLeftObstacle.y_coord - 1;
                    staticCoord = bottomLeftObstacle.x_coord;
                    moveVehicleSouth(bottomLeftObstacle, checkObstacleAhead, staticCoord);
                    break;
                case WEST:
                    checkObstacleAhead = bottomLeftObstacle.x_coord - 1;
                    staticCoord = bottomLeftObstacle.y_coord;
                    moveVehicleWest(bottomLeftObstacle, checkObstacleAhead, staticCoord);
                    break;
            }
        }
        return true;
    }

    public boolean changeVehicleDirection(ObstacleData.Direction newDirection) {
        int countVehicleObstacle = 0;
        for (int y = 0; y < hardLimit; y++) {
            for (int x = 0; x < hardLimit; x++) {
                ObstacleData gridMapObstacle = gridMapData.get(y).get(x);
                if (gridMapObstacle.getObstacleType() == ObstacleData.OBSTACLETYPE.Vehicle) {
                    if (vehicleHardLimitSize > countVehicleObstacle) {
                        changeObstacleData(x, y, gridMapObstacle.getOccupied(), newDirection, gridMapObstacle.getObstacleType(), gridMapObstacle.getVerified(), gridMapObstacle.getObstacleNumber());
                    }
                }
            }
        }
        invalidate();
        return true;
    }

    public boolean checkChangeRightDirectionOfVehicle(boolean reverseTrue) {
        int countVehicleObstacle = 0;
        for (int y = 0; y < hardLimit; y++) {
            for (int x = 0; x < hardLimit; x++) {
                if (gridMapData.get(y).get(x).getObstacleType() == ObstacleData.OBSTACLETYPE.Vehicle) {
                    if (vehicleHardLimitSize > countVehicleObstacle) {
                        Log.d("GridMapClass.java", "X Coord: " + x + " Y Coord: " + y);
                        Log.d("GridMapClass.java", "gridColumn "+gridColumns);
                        switch (gridMapData.get(y).get(x).direction) {
                            case NORTH:
                                if (reverseTrue) {
                                    Log.d("GridMapClass.java", "Reverse True");
                                    if (x + carSize >= gridColumns || y - carSize < 0) {
                                        return false;
                                    }
                                    if (gridMapData.get(y - carSize).get(x + carSize).getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle) {
                                        Log.d("GridMapClass.java", "Obstacle in the way! Unable to turn");
                                        return false;
                                    }
                                } else {
                                    if (x + carSize >= gridColumns || y + carSize >= gridRows) {
                                        return false;
                                    }
                                    if (gridMapData.get(y + carSize).get(x + carSize).getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle) {
                                        Log.d("GridMapClass.java", "Obstacle in the way! Unable to turn");
                                        return false;
                                    }
                                }
                                break;
                            case EAST:
                                if (reverseTrue) {
                                    if (x - carSize < 0 || y - carSize < 0) {
                                        return false;
                                    }
                                    if (gridMapData.get(y - carSize).get(x - carSize).getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle) {
                                        Log.d("GridMapClas.java", "Obstacle in the way! Unable to turn");
                                        return false;
                                    }
                                } else {
                                    if (x + carSize >= gridColumns || y - carSize < 0) {
                                        return false;
                                    }
                                    if (gridMapData.get(y - carSize).get(x + carSize).getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle) {
                                        Log.d("GridMapClas.java", "Obstacle in the way! Unable to turn");
                                        return false;
                                    }
                                }
                                break;
                            case SOUTH:
                                if (reverseTrue) {
                                    if (x - carSize < 0 || y + carSize >= gridRows) {
                                        return false;
                                    }
                                    if (gridMapData.get(y + carSize).get(x - carSize).getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle) {
                                        Log.d("GridMapClas.java", "Obstacle in the way! Unable to turn");
                                        return false;
                                    }
                                } else {
                                    if (x - carSize < 0 || y - carSize < 0) {
                                        return false;
                                    }
                                    if (gridMapData.get(y - carSize).get(x - carSize).getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle) {
                                        Log.d("GridMapClas.java", "Obstacle in the way! Unable to turn");
                                        return false;
                                    }
                                }

                                break;
                            case WEST:
                                if (reverseTrue) {
                                    if (x + carSize >= gridColumns || y + carSize >= gridRows) {
                                        return false;
                                    }
                                    if (gridMapData.get(y + carSize).get(x + carSize).getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle) {
                                        Log.d("GridMapClas.java", "Obstacle in the way! Unable to turn");
                                        return false;
                                    }
                                } else {
                                    if (x - carSize < 0 || y + carSize >= gridRows) {
                                        return false;
                                    }
                                    if (gridMapData.get(y + carSize).get(x - carSize).getObstacleType() == ObstacleData.OBSTACLETYPE.Obstacle) {
                                        Log.d("GridMapClas.java", "Obstacle in the way! Unable to turn");
                                        return false;
                                    }
                                }

                                break;
                        }
                    }
                }
            }
        }
        return true;
    }

    public String getImmediateVehicleDirection(){
        int[] bottomVehicleData = findVehicleBottomLeftObstacle();
        if(bottomVehicleData[0] == -2 && bottomVehicleData[1] ==-2){
            return "EMPTY";
        }else{
            return ObstacleData.Direction.getStringFromDirection(gridMapData.get(bottomVehicleData[1]).get(bottomVehicleData[0]).getDirection());
        }
    }

    public String getImmediateVehicleCoord(){
        int[] bottomVehicleData = findVehicleBottomLeftObstacle();
        if(bottomVehicleData[0] == -2 && bottomVehicleData[1] ==-2){
            return "(EMPTY)";
        }else{
            return "("+bottomVehicleData[0]+","+bottomVehicleData[1]+")";
        }
    }
    public String getImmediateVehicleStatus(){
        int[] bottomVehicleData = findVehicleBottomLeftObstacle();

        if(bottomVehicleData[0] != -2 && !firstTimeInitalize) {
            vehicleText = "Inactive";
            firstTimeInitalize = true;
            Log.d("GridMapClas.java", "Passed inactive");
        }
        return vehicleText;
    }
    public void receiveStatusMessageBluetooth(String msg, boolean joystickMovementDetected) {
        String jsonObject = msg.replace("'", "\"");
        String vehicleValue = null;
        String vehicleStatus = "notPlaced";
        try {
            JSONObject jsonThis = new JSONObject(jsonObject);
            vehicleStatus = jsonThis.getString("cat");
            vehicleValue = jsonThis.getString("value");
        } catch (JSONException e) {
            Log.d("JSON EXCEPTION at status", String.valueOf(e));
            e.printStackTrace();
        }

        if (vehicleValue == null) {
            vehicleText = "Vehicle not placed";
            return;
        }

        switch (vehicleValue) {
            case "ready-to-roll":
                Log.d("GridMapClas.java", "ready to roll");
                vehicleText = "Active";
                break;
            case "ready-to-scan":
                vehicleText = "Looking for Target " + vehicleValue;
                break;
            case "all-images-scan":
                vehicleText = "All images found";
                break;
            case "API returned non 200 status code":
                vehicleText = "API returned non 200 status code";
                break;
            case "Failed to convert raw Android message":
                vehicleText = "Failed to convert raw Android message";
                break;
            case "Failed to capture image for obstacle":
                vehicleText = "Failed to capture image for obstacle";
                break;
            case "Failed to stitch images":
                vehicleText = "Failed to stitch images";
                break;
            case "Android coordinate queue is empty":
                vehicleText = "Android coordinate queue is empty";
                break;
            default:
                if(!joystickMovementDetected) {
                    // Handle dynamic patterns
                    if (vehicleValue.startsWith("Failed to capture image for obstacle ")) {
                        vehicleText = vehicleValue;
                    } else if (vehicleValue.startsWith("Retrying SNAP command for obstacle ")) {
                        vehicleText = vehicleValue;
                    } else if (vehicleValue.startsWith("STM completed ")) {
                        vehicleText = vehicleValue;
                        channelMovementFromStm(vehicleValue);
                    } else if (vehicleValue.startsWith("STM failed to complete ")) {
                        vehicleText = vehicleValue;
                    } else if (vehicleValue.startsWith("STM retrying command ")) {
                        vehicleText = vehicleValue;
                    } else {
                        vehicleText = "Vehicle not placed";
                    }
                }
        }
    }

    public int receiveLocationMessageBluetooth(String msg){
        JSONObject jsonThis;
        String x;
        String y;
        String d;
        Log.d("at receive Location", msg);
        try{
            jsonThis = new JSONObject(msg).getJSONObject("value");
            x = jsonThis.getString("x");
            y = jsonThis.getString("y");
            d = jsonThis.getString("d");
        }catch(JSONException e){
            Log.d("JSON EXCEPTION at receive location", String.valueOf(e));
            e.printStackTrace();
            return -2;
        }
        Log.d("GridMapClas.java", "X Coord "+ x);
        Log.d("GridMapClas.java", "Y Coord "+ y);
        Log.d("GridMapClas.java", "Direction Coords "+ d);
        return 1;

    }

    public String receiveStichImageMessageBluetooth(String msg){
        JSONObject jsonThis;
        String stichValue;

        try{
            jsonThis = new JSONObject(msg);
            stichValue = jsonThis.getString("value");
        }catch(JSONException e){
            Log.d("JSON EXCEPTION at stitch", String.valueOf(e));
            e.printStackTrace();
            return "-1";
        }
        if(stichValue.equals("starting stitch")){
            vehicleText = "starting stitch";
            FINDetected = true;
            return "2";
        }else if(stichValue.equals("ending stitch")){
            vehicleText = "ending stitch";
            return "3";
        }else{

            return stichValue;
        }

    }

    public int receiveHealthMessageBluetooth(String msg){
        JSONObject jsonThis;
        String healthValue;

        try{
            jsonThis = new JSONObject(msg);
            healthValue = jsonThis.getString("value");
        }catch(JSONException e){
            Log.d("JSON EXCEPTION at health", String.valueOf(e));
            e.printStackTrace();
            return -2;
        }
        if(healthValue.equals("Image Rec API is down")){
            vehicleText = "Image Rec API is down";
            return 1;
        }else if(healthValue.equals("Algo API is down")){
            vehicleText = "Algo API is down";
            return 0;
        }
        return -1;

    }

    public void strafeRightBluetooth() throws JSONException {
        if (btService != null) {
            Log.d("Sending Message", "Sending Message");
            // btService.write(strafeRightString.getBytes(StandardCharsets.UTF_8));
            utilitiesClass.vehiclePoint = utilitiesClass.strafeRightString;
            // JSONObject messageSent = utilities.convertStringToJson(jsonCraftVehicleMovement);
            btService.write(utilitiesClass.getJsonCraftVehicleMovement().getBytes(StandardCharsets.UTF_8));

        } else {
            Log.d("Sending Message", "Unable to send Message to strafe right!");
        }
    }

    public void strafeLeftBluetooth() {
        if (btService != null) {
            Log.d("Sending Message", "Sending Message");
            utilitiesClass.vehiclePoint = utilitiesClass.strafeLeftString;
            btService.write(utilitiesClass.getJsonCraftVehicleMovement().getBytes(StandardCharsets.UTF_8));
        } else {
            Log.d("Sending Message", "Unable to send Message to strafe Left!");
        }
    }

    public void rotateRightBluetooth() {
        if (btService != null) {
            Log.d("Sending Message", "Sending Message");
            utilitiesClass.vehiclePoint = utilitiesClass.rotateRightString;
            btService.write(utilitiesClass.getJsonCraftVehicleMovement().getBytes(StandardCharsets.UTF_8));
        } else {
            Log.d("Sending Message", "Unable to send Message to rotate right!");
        }
    }

    public void rotateLeftBluetooth() {
        if (btService != null) {
            Log.d("Sending Message", "Sending Message");
            utilitiesClass.vehiclePoint = utilitiesClass.rotateLeftString;
            btService.write(utilitiesClass.getJsonCraftVehicleMovement().getBytes(StandardCharsets.UTF_8));
        } else {
            Log.d("Sending Message", "Unable to send Message to rotate Left!");
        }
    }

    public void reverseBluetooth() {
        if (btService != null) {
            Log.d("Sending Message", "Sending Message to reverse backwards");
            utilitiesClass.vehiclePoint = utilitiesClass.reverseString;
            btService.write(utilitiesClass.getJsonCraftVehicleMovement().getBytes(StandardCharsets.UTF_8));
        } else {
            Log.d("Sending Message", "Unable to send Message to reverse!");
        }
    }

    public void reverseLeftBluetooth() {
        if (btService != null) {
            Log.d("Sending Message", "Sending Message to reverse left");
            utilitiesClass.vehiclePoint = utilitiesClass.reverseLeftString;
            btService.write(utilitiesClass.getJsonCraftVehicleMovement().getBytes(StandardCharsets.UTF_8));
        } else {
            Log.d("Sending Message", "Unable to send Message to reverse Left!");
        }
    }

    public void reverseRightBluetooth() {
        if (btService != null) {
            Log.d("Sending Message", "Sending Message to reverse right");
            utilitiesClass.vehiclePoint = utilitiesClass.reverseRightString;
            btService.write(utilitiesClass.getJsonCraftVehicleMovement().getBytes(StandardCharsets.UTF_8));
        } else {
            Log.d("Sending Message", "Unable to send Message to reverse Right!");
        }
    }

    public void forwardDirectionBluetooth() {
        if (btService != null) {
            Log.d("Sending Message", "Sending Message");
            utilitiesClass.vehiclePoint = utilitiesClass.forwardString;
            btService.write(utilitiesClass.getJsonCraftVehicleMovement().getBytes(StandardCharsets.UTF_8));
        } else {
            Log.d("Sending Message", "Unable to send Message to Forward!");
        }
    }

    public void sendArenaDataBluetooth(){
        if (btService != null) {
            Log.d("Sending Message", "Sending Message");
            StringBuilder stringBuilder = new StringBuilder();
            int[] bottomLeftVehicleData = findVehicleBottomLeftObstacle();

            if(bottomLeftVehicleData[1] == -2 && bottomLeftVehicleData[0] == -2){
                stringBuilder.append("\"NO MAP DATA\"");
                utilitiesClass.arenaData = stringBuilder.toString();
                btService.write(utilitiesClass.getJsonCraftSendArena().getBytes(StandardCharsets.UTF_8));
                return;
            }
            ObstacleData bottomLeftVehicle = gridMapData.get(bottomLeftVehicleData[1]).get(bottomLeftVehicleData[0]);
            stringBuilder.append("{\"obstacles\":");
            stringBuilder.append("[");

            // For obstacle
            if(placedObstacles.size() == 0){
                stringBuilder.append("]");
                utilitiesClass.arenaData = stringBuilder.toString();
                btService.write(utilitiesClass.getJsonCraftSendArena().getBytes(StandardCharsets.UTF_8));
                return;
            }
            reformatObstacleDataArray();
            for(int i=0; i< placedObstacles.size(); i++){
                // change direction to number
                stringBuilder.append("{\"x\": "+placedObstacles.get(i).getXCoord()+",");
                stringBuilder.append("\"y\": "+placedObstacles.get(i).getYCoord()+",");
                stringBuilder.append("\"d\": "+placedObstacles.get(i).getDirection().getIntFromDirection()+",");
                stringBuilder.append("\"id\": "+placedObstacles.get(i).getObstacleNumber()+"}");

                if(i < placedObstacles.size()-1){
                    stringBuilder.append(",");
                }
            }
            stringBuilder.append("],");
            stringBuilder.append("\"robot_x\": "+bottomLeftVehicle.getXCoord()+",");
            stringBuilder.append("\"robot_y\": "+bottomLeftVehicle.getYCoord()+",");
            stringBuilder.append("\"robot_direction\": "+bottomLeftVehicle.getDirection().getIntFromDirection()+"}");

            utilitiesClass.arenaData = stringBuilder.toString();
            btService.write(utilitiesClass.getJsonCraftSendArena().getBytes(StandardCharsets.UTF_8));
            // {cat: 'sendArena', 'value': [{}]}
            // 1st value is vehicle origin left, the rest are obstacle data
        } else {
            Log.d("Sending Message", "Unable to send Message to send Arena Data to Bluetooth!");
        }
    }

    public void sendStichSignalBluetooth(){
        if (btService != null) {
            Log.d("Sending Message", "Sending Message");
            btService.write(utilitiesClass.getJsonCraftSendStichSignal().getBytes(StandardCharsets.UTF_8));
        } else {
            Log.d("Sending Message", "Unable to send Message to send stich images again!");
        }
    }
    public void sendBeginFastestPathBluetooth() {
        if (btService != null) {
            Log.d("Sending Message", "Sending Message");
            utilitiesClass.vehiclePoint = utilitiesClass.beginFastestString;
            btService.write(utilitiesClass.getJsonCraftVehicleMovement().getBytes(StandardCharsets.UTF_8));
        } else {
            Log.d("Sending Message", "Unable to send Message to send to begin fastest path data!");
        }
    }

    public void sendBeginExplorationBluetooth() {
        if (btService != null) {
            Log.d("Sending Message", "Sending Message");
            utilitiesClass.vehiclePoint = utilitiesClass.beginExplorationString;
            btService.write(utilitiesClass.getJsonCraftVehicleMovement().getBytes(StandardCharsets.UTF_8));
        } else {
            Log.d("Sending Message", "Unable to send Message to send to begin fastest path data!");
        }
    }

    public int receiveVerifiedObstacleBluetooth(String msg) {
        Log.d("Verified", "Verified Message!");

        try {
            JSONObject json = new JSONObject(msg);
            Log.d("Verified", "Tried Message successful!");
            String cat = json.getString("cat");
            Log.d("cat: ", cat);

            // Handle special case: image-rec messages
            if (cat.equals("image-rec")) {
                String value = json.getString("value");
                if (value.startsWith("Capturing image for obstacle ")) {
                    Log.d("ImageRec", value);
                    vehicleText = value;
                    // You could extract the ID if needed:
                    return 2; // New return code to indicate "capturing image" status
                }
            }
            // "value" is a single JSONObject, not an array
            JSONObject obstacle = json.getJSONObject("value");
            Log.d("Verified", "Can get val");

            String x = obstacle.getString("x");
            String y = obstacle.getString("y");
            String direction = obstacle.getString("d");
            String imageId = obstacle.getString("image-id");
            String obstacleId = obstacle.getString("obstacle-id");

            Log.d("Sending Message", "Obstacle: x=" + x + ", y=" + y + ", direction=" + direction
                    + ", image=" + imageId + ", id=" + obstacleId);

            if (imageId.equals("-1")) {
                Log.d("Sending Message", "No Image ID Detected");
                return -1;
            }else if(imageId.equals("marker")){
                Log.d("Sending Message", "Bullseye Detected");
                return  -3;
            }

            if (gridMapData.get(Integer.parseInt(y)).get(Integer.parseInt(x)).getOccupied()) {
                Log.d("About Verified", "it exists");
                gridMapData.get(Integer.parseInt(y)).get(Integer.parseInt(x)).setVerified(true);
                gridMapData.get(Integer.parseInt(y)).get(Integer.parseInt(x)).setObstacleNumber(Integer.parseInt(imageId));
                rearrangeObstacleData(Integer.parseInt(x), Integer.parseInt(y));
                invalidate();
                return 1;
            } else {
                Log.d("About Verified", "Obstacle Does not exist!");
                return 0;
            }

        } catch (JSONException e) {
            Log.d("JSON EXCEPTION at verified obstacle", String.valueOf(e));
            e.printStackTrace();
            Log.d("Error This", "What went wrong");
        }
        return -2;
    }

    public void channelMovementFromStm(String msg){
        Log.d("Verified", "Verified Message!");
        // Update with x y d stmcommand
        try {
            if(msg.contains("SNAPS")){
                Log.d("GridMapClass.java", "Snap Detected");
                return;
            }else if(msg.contains("FIN")){
                Log.d("GridMapClass.java", "FIN Detected");
                return;
            }
            int[] getVehicleBottomData = findVehicleBottomLeftObstacle();
            if(getVehicleBottomData[1] == -2 && getVehicleBottomData[0] == -2){
                return;
            }
            ObstacleData.Direction getVehicleDirection = gridMapData.get(getVehicleBottomData[1]).get(getVehicleBottomData[0]).getDirection();
            // Get "cat"

            int numOfTimesToMove = utilities.returnMiddleSTMValues(msg);
            Log.d("value: ", msg);
            if(msg.contains(utilitiesClass.returnFirstTwoSTMValue(utilitiesClass.forwardString))){
                for(int i =0; i< numOfTimesToMove/5; i++){
                    if(!changeGetReadToRoll) {
                        changeGetReadToRoll = true;
                        Log.d("STM Movement Record", "Straight from STM!");
                        moveVehicleStraight(getVehicleDirection, false);
                    }else{
                        changeGetReadToRoll = false;
                    }
                }
            }else if(msg.contains(utilitiesClass.returnFirstTwoSTMValue(utilitiesClass.reverseLeftString))){
                Log.d("STM Movement Record", "Reverse Left from STM!");
                reverseLeftVehicle(false);
            }else if(msg.contains(utilitiesClass.returnFirstTwoSTMValue(utilitiesClass.reverseRightString))){
                Log.d("STM Movement Record", "Reverse Right from STM!");
                reverseRightVehicle(false);
            }else if(msg.contains(utilitiesClass.returnFirstTwoSTMValue(utilitiesClass.strafeLeftString))){
                Log.d("STM Movement Record", "Turn Left from STM!");
                turnLeftDirectionOfVehicle(getVehicleDirection, false, false);
            }else if(msg.contains(utilitiesClass.returnFirstTwoSTMValue(utilitiesClass.strafeRightString))){
                Log.d("STM Movement Record", "Turn Right from STM!");
                turnRightDirectionOfVehicle(getVehicleDirection, false, false);
            }else if(msg.contains(utilitiesClass.returnFirstTwoSTMValue(utilitiesClass.reverseString))){
                for(int i =0; i< numOfTimesToMove/5; i++) {
                    if(!changeGetReadToRoll){
                        Log.d("STM Movement Record", "Reverse from STM!");
                        changeGetReadToRoll = true;
                        moveVehicleStraight(ObstacleData.Direction.getOppositeDirection(getVehicleDirection), false);
                    }else{
                        changeGetReadToRoll = false;
                    }
                }
            }else{
                Log.d("STM Movement Record", "No movement detected");
            }
        } catch (JSONException e) {
            Log.d("JSON EXCEPTION at channel movement", String.valueOf(e));
            e.printStackTrace();
        }
    }

    public boolean checkFINStatus(){
        Log.d("GridMapClass.java", "FIN detected "+FINDetected);
        return FINDetected;
    }

    public void updateFINStatus(boolean updateFIN){
        Log.d("GridMapClass.java", "FIN updated "+updateFIN);
        FINDetected = updateFIN;
    }
}



