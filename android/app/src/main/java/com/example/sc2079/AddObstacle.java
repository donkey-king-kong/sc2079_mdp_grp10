package com.example.sc2079;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageButton;
import android.util.Log;
import android.widget.EditText;
import android.text.TextUtils;
import androidx.annotation.Nullable;

import android.widget.TextView;
import android.widget.Toast;
import androidx.fragment.app.Fragment;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.button.MaterialButtonToggleGroup;
import android.widget.PopupMenu;
import android.view.MenuItem;

public class AddObstacle extends Fragment{
    private ImageButton addObstacleButton;
    private ImageButton cancelButton;
    private EditText addXCoords;
    private EditText addYCoords;
    private MaterialButton addStartingPointButton;
    private MaterialButton addObstacleToggle;
    private MaterialButton removeObstacleButton;
    private MaterialButton removeVehicleButton;
    private MaterialButton changeDirectionButton;
    private MaterialButton resetMapButton;
    private MaterialButton saveMapButton;


    View addCoordsView;
    View changeSaveView;
    private int currentSelectedButtonId = View.NO_ID;
    ObstacleData.Direction selectedDirectionID;


    private GridMapClass gridMap;
    private boolean activatedMap = false;

    public AddObstacle(GridMapClass gridMap){
        this.gridMap = gridMap;
    }

    @Nullable
    @Override
    public View onCreateView(LayoutInflater inflater, @Nullable ViewGroup container, Bundle savedInstanceState){
        Log.d("onCreateView Function in AddObstacle", "Entering onCreateView");
        addCoordsView = inflater.inflate(R.layout.add_coordinates, container, false);
        super.onCreate(savedInstanceState);
        // buttons to look out for
        /*
        addObstacleButton = addCoordsView.findViewById(R.id.addObstacleButton);
        cancelButton= addCoordsView.findViewById(R.id.cancelButton);
        //MaterialButtonToggleGroup toggleGroup = addCoordsView.findViewById(R.id.toggleGroup_Options);

        // EditText to look out for
        addXCoords = addCoordsView.findViewById(R.id.addXCoords);
        addYCoords = addCoordsView.findViewById(R.id.addYCoords);*/

        // Buttons and EditTexts
        addObstacleButton = addCoordsView.findViewById(R.id.addObstacleButton);
        cancelButton= addCoordsView.findViewById(R.id.cancelButton);
        addXCoords = addCoordsView.findViewById(R.id.addXCoords);
        addYCoords = addCoordsView.findViewById(R.id.addYCoords);

        // Buttons that were in the toggle group
        addStartingPointButton = addCoordsView.findViewById(R.id.add_starting_point);
        addObstacleToggle = addCoordsView.findViewById(R.id.add_obstacle_button);
        removeObstacleButton = addCoordsView.findViewById(R.id.remove_obstacle_button);
        removeVehicleButton = addCoordsView.findViewById(R.id.remove_vehicle_button);
        changeDirectionButton = addCoordsView.findViewById(R.id.change_direction_button);
        resetMapButton = getActivity().findViewById(R.id.reset_map_button);
        saveMapButton = getActivity().findViewById(R.id.save_map_button);

        resetMapButton.setOnClickListener(new View.OnClickListener(){
            @Override
            public void onClick(View view){
                Log.d("activity_main","Restarting Map!");
                Toast.makeText(getContext(), "Resetting Map back to default!", Toast.LENGTH_SHORT).show();
                gridMap.clearGridMap();
            }
        });
        /*
        saveMapButton.setOnClickListener(new View.OnClickListener(){
            @Override
            public void onClick(View view){
                Toast.makeText(getContext(), "Saving Current Map Configuration!", Toast.LENGTH_SHORT).show();
                gridMap.clearGridMap();
            }
        });
         */

        View.OnClickListener selectionListener = new View.OnClickListener(){
            @Override
            public void onClick(View v){
                currentSelectedButtonId = v.getId();
                updateButtonState();
            }
        };

        addStartingPointButton.setOnClickListener(selectionListener);
        addObstacleToggle.setOnClickListener(selectionListener);
        removeObstacleButton.setOnClickListener(selectionListener);
        removeVehicleButton.setOnClickListener(selectionListener);
        changeDirectionButton.setOnClickListener(new View.OnClickListener(){
            @Override
            public void onClick(View view) {
                currentSelectedButtonId = view.getId();
                updateButtonState();
                if (!activatedMap) {
                    PopupMenu popup = new PopupMenu(getContext(), changeDirectionButton);
                    popup.getMenuInflater().inflate(R.menu.direction_menu, popup.getMenu());
                    popup.setOnMenuItemClickListener(new PopupMenu.OnMenuItemClickListener() {
                        @Override
                        public boolean onMenuItemClick(MenuItem item) {
                            int id = item.getItemId();

                            if (id == R.id.north) {
                                changeDirectionButton.setText("Change Direction to Up");
                                selectedDirectionID = ObstacleData.Direction.NORTH;
                                return true;
                            } else if (id == R.id.south) {
                                changeDirectionButton.setText("Change Direction to Down");
                                selectedDirectionID = ObstacleData.Direction.SOUTH;
                                return true;
                            } else if (id == R.id.east) {
                                changeDirectionButton.setText("Change Direction to Right");
                                selectedDirectionID = ObstacleData.Direction.EAST;
                                return true;
                            } else if (id == R.id.west) {
                                changeDirectionButton.setText("Change Direction to Left");
                                selectedDirectionID = ObstacleData.Direction.WEST;
                                return true;
                            }
                            return false;
                        }
                    });
                    popup.show();
                    activatedMap = true;
                }else{
                    activatedMap = false;
                }
            }
        });
        addObstacleButton.setOnClickListener(new View.OnClickListener()
        {
            @Override
            public void onClick(View view){
                int statusReturn;

                if(currentSelectedButtonId == View.NO_ID){
                    Log.d("add_coordinate.xml","No Option Selected");
                    Toast.makeText(getActivity(), "Please select an option first!", Toast.LENGTH_SHORT).show();
                }else{
                    String buttonName = getResources().getResourceEntryName(currentSelectedButtonId);
                    boolean checkXCoords = checkCoordCorrect(addXCoords, "X");
                    boolean checkYCoords = checkCoordCorrect(addYCoords, "Y");
                    if(checkXCoords){
                        Log.d("add_coordinate.xml", "Result of X Coord: " + utilities.convertBooleanToString(checkXCoords));
                    }else{
                        return;
                    }
                    if(checkYCoords){
                        Log.d("add_coordinate.xml", "Result of Y Coord: " + utilities.convertBooleanToString(checkYCoords));
                    }else{
                        return;
                    }


                    int x_coord_add = utilities.convertEditTextToInt(addXCoords);
                    int y_coord_add = utilities.convertEditTextToInt(addYCoords);

                    switch(buttonName){
                        case "add_obstacle_button":
                            Log.d("add_coordinate.xml","Clicked add_obstacle_button");
                            statusReturn = gridMap.addNewObstacleToGrid(x_coord_add, y_coord_add);
                            switch(statusReturn){
                                case 0:
                                    Toast.makeText(getContext(), "Invalid Coordinates added! Please reenter input", Toast.LENGTH_SHORT).show();
                                    break;
                                case 1:
                                    Toast.makeText(getContext(), "Obstacle added at (" + x_coord_add + "," + y_coord_add + ")", Toast.LENGTH_SHORT).show();
                                    break;
                                case 2:
                                    Toast.makeText(getContext(), "Obstacle already added at (" + x_coord_add + "," + y_coord_add + ") was not added successfully", Toast.LENGTH_SHORT).show();
                                    break;
                                default:
                                    Toast.makeText(getContext(), "Unknown Error Occurred!", Toast.LENGTH_SHORT).show();
                                    break;
                            }
                            break;

                        case "remove_obstacle_button":
                            Log.d("add_coordinate.xml","Clicked remove_obstacle_button Button");
                            statusReturn = gridMap.removeFromGrid(x_coord_add, y_coord_add, true);
                            switch(statusReturn){
                                case 0:
                                    Toast.makeText(getContext(), "Invalid Coordinates added! Please reenter input", Toast.LENGTH_SHORT).show();
                                    break;
                                case 1:
                                    Toast.makeText(getActivity(),"Obstacle successfully removed at (" + utilities.convertIntToString(x_coord_add) + "," + utilities.convertIntToString(y_coord_add)+")",Toast.LENGTH_SHORT).show();
                                    break;
                                case 2:
                                    Toast.makeText(getActivity(),"Obstacle already removed at (" + utilities.convertIntToString(x_coord_add) + "," + utilities.convertIntToString(y_coord_add)+")",Toast.LENGTH_SHORT).show();
                                    break;
                                case 3:
                                    Toast.makeText(getActivity(),"Obstacle dragged to new location",Toast.LENGTH_SHORT).show();
                                    break;
                                default:
                                    Toast.makeText(getContext(), "Unknown Error Occurred!", Toast.LENGTH_SHORT).show();
                                    break;
                            }
                            break;

                        case "add_starting_point":
                            Log.d("add_coordinate.xml","Clicked add_starting_point Button");
                            statusReturn = gridMap.addVehicleToMap(x_coord_add, y_coord_add);
                            switch(statusReturn){
                                case 0:
                                    Toast.makeText(getContext(), "Invalid Coordinates to add vehicle! Please reenter input", Toast.LENGTH_SHORT).show();
                                    break;
                                case 1:
                                    Toast.makeText(getContext(), "Vehicle successfully added at (" + x_coord_add + "," + y_coord_add + ")", Toast.LENGTH_SHORT).show();
                                    break;
                                case 2:
                                    Toast.makeText(getContext(), "Vehicle already added at (" + x_coord_add + "," + y_coord_add + ") was not added successfully", Toast.LENGTH_SHORT).show();
                                    break;
                                case 3:
                                    Toast.makeText(getContext(), "Obstacle blocking vehicle!", Toast.LENGTH_SHORT).show();
                                default:
                                    Toast.makeText(getContext(), "Unknown Error Occurred!", Toast.LENGTH_SHORT).show();
                                    break;
                            }
                            break;
                        case "remove_vehicle_button":
                            statusReturn = gridMap.removeVehicleFromGrid();
                            switch(statusReturn){
                                case 1:
                                    Toast.makeText(getContext(), "Vehicle successfully removed!", Toast.LENGTH_SHORT).show();
                                    break;
                                case 2:
                                    Toast.makeText(getContext(), "Vehicle already removed!", Toast.LENGTH_SHORT).show();
                                    break;
                                default:
                                    Toast.makeText(getContext(), "Unknown Error Occurred!", Toast.LENGTH_SHORT).show();
                                    break;
                            }
                            Log.d("add_coordinate.xml","Clicked remove_obstacle Button");
                            break;
                        case "change_direction_button":
                            Log.d("add_coordinate.xml","Clicked change_direction Button");
                            if(selectedDirectionID == null){
                                Toast.makeText(getContext(), "No direction was specified!", Toast.LENGTH_SHORT).show();
                                return;
                            }
                            statusReturn = gridMap.changeDirectionOfObstacleFlexible(x_coord_add, y_coord_add, selectedDirectionID);
                            switch(statusReturn){
                                case 1:
                                    Toast.makeText(getContext(), "Obstacle direction changed to " + selectedDirectionID + " at (" + x_coord_add + "," + y_coord_add + ")", Toast.LENGTH_SHORT).show();
                                    break;
                                case 2:
                                    Toast.makeText(getContext(), "Obstacle not present at (" + x_coord_add + "," + y_coord_add + ")", Toast.LENGTH_SHORT).show();
                                    break;
                                case 3:
                                    Toast.makeText(getContext(), "Vehicle direction changed to " + selectedDirectionID + " at (" + x_coord_add + "," + y_coord_add + ")", Toast.LENGTH_SHORT).show();
                                    break;
                                case 4:
                                    Toast.makeText(getContext(), "Unknown Error Occurred!", Toast.LENGTH_SHORT).show();
                                    break;
                            }
                            break;
                    }
                }
            }
        });

        // When pressing cancel button
        cancelButton.setOnClickListener(new View.OnClickListener()
        {
            public void onClick(View v){
                // Clean all inputs and reset
                Log.d("add_coordinate.xml","Clicked cancelBtn");
                addXCoords.getText().clear();
                addYCoords.getText().clear();
                currentSelectedButtonId = View.NO_ID;
                updateButtonState();
            }
        });
        return addCoordsView;
    }

    private void updateButtonState(){
        addStartingPointButton.setActivated(addStartingPointButton.getId() == currentSelectedButtonId);
        addObstacleToggle.setActivated(addObstacleToggle.getId() == currentSelectedButtonId);
        removeObstacleButton.setActivated(removeObstacleButton.getId() == currentSelectedButtonId);
        removeVehicleButton.setActivated(removeVehicleButton.getId() == currentSelectedButtonId);
        changeDirectionButton.setActivated(changeDirectionButton.getId() == currentSelectedButtonId);

        if (currentSelectedButtonId == addStartingPointButton.getId()) {
            gridMap.setGridMode(GridMapClass.GridMode.ADD_VEHICLE);
        } else if (currentSelectedButtonId == addObstacleToggle.getId()) {
            gridMap.setGridMode(GridMapClass.GridMode.ADD_OBSTACLE);
        } else if (currentSelectedButtonId == removeObstacleButton.getId() || currentSelectedButtonId == removeVehicleButton.getId()) {
            gridMap.setGridMode(GridMapClass.GridMode.REMOVE);
        } else if (currentSelectedButtonId == changeDirectionButton.getId()) {
            gridMap.setGridMode(GridMapClass.GridMode.CHANGE_DIRECTION);
        } else {
            gridMap.setGridMode(GridMapClass.GridMode.NONE);
        }
    }


    public boolean checkCoordCorrect(EditText inputFromUser, String x_or_y){
        Log.d("checkCoordCorrect Function", "Checking Coordinates for "+x_or_y);

        String input = inputFromUser.getText().toString().trim();

        if(TextUtils.isEmpty(input)){
            Toast.makeText(getActivity(), "Please enter a value for input "+x_or_y, Toast.LENGTH_SHORT).show();
            return false;
        }

        try{
            int value = Integer.parseInt(input);
            if(value >= gridMap.lowLimit && value <= gridMap.hardLimit-1){
                return true;
            } else{
                Toast.makeText(getActivity(), "Value must be between 0 and 19 for input "+x_or_y, Toast.LENGTH_SHORT).show();
                return false;
            }
        }catch(NumberFormatException e){
            Toast.makeText(getActivity(), "Invalid number format for input "+x_or_y, Toast.LENGTH_SHORT).show();
            return false;
        }
    }
}
