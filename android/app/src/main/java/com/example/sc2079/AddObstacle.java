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

public class AddObstacle extends Fragment{
    private ImageButton addObstacleButton;
    private ImageButton cancelButton;
    private EditText addXCoords;
    private EditText addYCoords;
    private MaterialButton addStartingPointButton;
    private MaterialButton addObstacleToggle;
    private MaterialButton removeButton;
    private MaterialButton resetMapButton;
    private MaterialButton saveMapButton;


    View addCoordsView;
    View changeSaveView;
    private int currentSelectedButtonId = View.NO_ID;

    private GridMapClass gridMap;

    public AddObstacle() {}

    public AddObstacle(GridMapClass gridMap){
        this.gridMap = gridMap;
    }

    @Override
    public void onAttach(android.content.Context context) {
        super.onAttach(context);
        if (gridMap == null && context instanceof MainActivity) {
            gridMap = ((MainActivity) context).gridMapObj;
        }
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
        removeButton = addCoordsView.findViewById(R.id.remove_obstacle_button);
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
                if (currentSelectedButtonId == v.getId()) {
                    currentSelectedButtonId = View.NO_ID;
                } else {
                    currentSelectedButtonId = v.getId();
                }
                updateButtonState();
            }
        };

        addStartingPointButton.setOnClickListener(selectionListener);
        addObstacleToggle.setOnClickListener(selectionListener);
        removeButton.setOnClickListener(selectionListener);
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
                            Log.d("add_coordinate.xml","Clicked remove Button");
                            statusReturn = gridMap.removeFromGrid(x_coord_add, y_coord_add, true);
                            switch(statusReturn){
                                case 0:
                                    Toast.makeText(getContext(), "Invalid Coordinates! Please reenter input", Toast.LENGTH_SHORT).show();
                                    break;
                                case 1:
                                    Toast.makeText(getActivity(),"Successfully removed at (" + utilities.convertIntToString(x_coord_add) + "," + utilities.convertIntToString(y_coord_add)+")",Toast.LENGTH_SHORT).show();
                                    break;
                                case 2:
                                    Toast.makeText(getActivity(),"Nothing to remove at (" + utilities.convertIntToString(x_coord_add) + "," + utilities.convertIntToString(y_coord_add)+")",Toast.LENGTH_SHORT).show();
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
        boolean vehicleActive   = addStartingPointButton.getId() == currentSelectedButtonId;
        boolean obstacleActive  = addObstacleToggle.getId()      == currentSelectedButtonId;
        boolean removeActive    = removeButton.getId()            == currentSelectedButtonId;

        // Swap background drawables so active state is visually distinct
        addStartingPointButton.setBackground(
            androidx.core.content.ContextCompat.getDrawable(requireContext(),
                vehicleActive ? R.drawable.btn_mode_vehicle_on : R.drawable.btn_mode_vehicle_off));
        addObstacleToggle.setBackground(
            androidx.core.content.ContextCompat.getDrawable(requireContext(),
                obstacleActive ? R.drawable.btn_mode_obstacle_on : R.drawable.btn_mode_obstacle_off));
        removeButton.setBackground(
            androidx.core.content.ContextCompat.getDrawable(requireContext(),
                removeActive ? R.drawable.btn_mode_remove_on : R.drawable.btn_mode_remove_off));

        // Text color for active vs inactive
        int activeVehicleColor  = androidx.core.content.ContextCompat.getColor(requireContext(), R.color.ds_accent);
        int activePinkColor     = androidx.core.content.ContextCompat.getColor(requireContext(), R.color.ds_pink);
        int activeRedColor      = androidx.core.content.ContextCompat.getColor(requireContext(), R.color.ds_red);
        int inactiveColor       = androidx.core.content.ContextCompat.getColor(requireContext(), R.color.ds_text_muted);
        addStartingPointButton.setTextColor(vehicleActive  ? activeVehicleColor : inactiveColor);
        addObstacleToggle.setTextColor     (obstacleActive ? activePinkColor    : inactiveColor);
        removeButton.setTextColor          (removeActive   ? activeRedColor     : inactiveColor);

        if (vehicleActive) {
            gridMap.setGridMode(GridMapClass.GridMode.ADD_VEHICLE);
        } else if (obstacleActive) {
            gridMap.setGridMode(GridMapClass.GridMode.ADD_OBSTACLE);
        } else if (removeActive) {
            gridMap.setGridMode(GridMapClass.GridMode.REMOVE);
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
