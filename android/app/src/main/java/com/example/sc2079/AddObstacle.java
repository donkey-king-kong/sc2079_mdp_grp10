package com.example.sc2079;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.util.Log;
import android.widget.EditText;
import android.widget.ImageButton;
import android.text.TextUtils;
import androidx.annotation.Nullable;

import android.widget.TextView;
import android.widget.Toast;
import androidx.fragment.app.Fragment;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.button.MaterialButtonToggleGroup;
import com.example.sc2079.ui.coordinates.PlaceObstacleDialogFragment;
import com.example.sc2079.ui.Palette;
import com.example.sc2079.ui.ThemePaletteKt;
import com.example.sc2079.ui.ThemeAware;

public class AddObstacle extends Fragment implements ThemeAware {
    private android.widget.Button addObstacleButton;
    private android.widget.Button cancelButton;
    private EditText addXCoords;
    private EditText addYCoords;
    private MaterialButton addStartingPointButton;
    private MaterialButton addObstacleToggle;
    private MaterialButton removeButton;
    private android.widget.Button resetMapButton;
    private android.widget.Button saveMapButton;
    private Palette currentPalette = com.example.sc2079.ui.ThemePaletteKt.getNIGHT();


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
            gridMap = ((MainActivity) context).currentGridMapOrNull();
        }
    }

    private GridMapClass resolveGridMap() {
        if (gridMap == null && getActivity() instanceof MainActivity) {
            gridMap = ((MainActivity) getActivity()).currentGridMapOrNull();
        }
        return gridMap;
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

        // D-pad click listeners
        android.widget.Button dpadUp = addCoordsView.findViewById(R.id.dpad_up);
        android.widget.Button dpadDown = addCoordsView.findViewById(R.id.dpad_down);
        android.widget.Button dpadLeft = addCoordsView.findViewById(R.id.dpad_left);
        android.widget.Button dpadRight = addCoordsView.findViewById(R.id.dpad_right);
        android.widget.LinearLayout reverseLeft = addCoordsView.findViewById(R.id.reverse_left_button);
        android.widget.LinearLayout reverseRight = addCoordsView.findViewById(R.id.reverse_right_button);

        dpadUp.setOnClickListener(v -> {
            try { GridMapClass activeGridMap = resolveGridMap(); if (activeGridMap != null) activeGridMap.moveVehicleStraight(ObstacleData.Direction.NORTH, true); } catch (Exception e) { e.printStackTrace(); }
        });
        dpadDown.setOnClickListener(v -> {
            try { GridMapClass activeGridMap = resolveGridMap(); if (activeGridMap != null) activeGridMap.moveVehicleStraight(ObstacleData.Direction.SOUTH, true); } catch (Exception e) { e.printStackTrace(); }
        });
        dpadLeft.setOnClickListener(v -> {
            try { GridMapClass activeGridMap = resolveGridMap(); if (activeGridMap != null) activeGridMap.moveVehicleStraight(ObstacleData.Direction.WEST, true); } catch (Exception e) { e.printStackTrace(); }
        });
        dpadRight.setOnClickListener(v -> {
            try { GridMapClass activeGridMap = resolveGridMap(); if (activeGridMap != null) activeGridMap.moveVehicleStraight(ObstacleData.Direction.EAST, true); } catch (Exception e) { e.printStackTrace(); }
        });
        reverseLeft.setOnClickListener(v -> {
            try { GridMapClass activeGridMap = resolveGridMap(); if (activeGridMap != null) activeGridMap.reverseLeftVehicle(true); } catch (Exception e) { e.printStackTrace(); }
        });
        reverseRight.setOnClickListener(v -> {
            try { GridMapClass activeGridMap = resolveGridMap(); if (activeGridMap != null) activeGridMap.reverseRightVehicle(true); } catch (Exception e) { e.printStackTrace(); }
        });

        // Buttons that were in the toggle group
        addStartingPointButton = addCoordsView.findViewById(R.id.add_starting_point);
        addObstacleToggle = addCoordsView.findViewById(R.id.add_obstacle_button);
        removeButton = addCoordsView.findViewById(R.id.remove_obstacle_button);
        ImageButton btnQuickAdd = addCoordsView.findViewById(R.id.btn_quick_add);
        btnQuickAdd.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (currentSelectedButtonId == addStartingPointButton.getId()) {
                    PlaceObstacleDialogFragment.newInstance(true)
                        .show(getParentFragmentManager(), "PlaceObstacleDialog");
                } else if (currentSelectedButtonId == addObstacleToggle.getId()) {
                    PlaceObstacleDialogFragment.newInstance(false)
                        .show(getParentFragmentManager(), "PlaceObstacleDialog");
                } else {
                    Toast.makeText(
                        getActivity(),
                        "Select Vehicle or Obstacle mode first",
                        Toast.LENGTH_SHORT
                    ).show();
                }
            }
        });
        resetMapButton = getActivity().findViewById(R.id.reset_map_button);
        saveMapButton = getActivity().findViewById(R.id.save_map_button);

        resetMapButton.setOnClickListener(new View.OnClickListener(){
            @Override
            public void onClick(View view){
                Log.d("activity_main","Restarting Map!");
                Toast.makeText(getContext(), "Resetting Map back to default!", Toast.LENGTH_SHORT).show();
                GridMapClass activeGridMap = resolveGridMap();
                if (activeGridMap != null) activeGridMap.clearGridMap();
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
                    GridMapClass activeGridMap = resolveGridMap();
                    if (activeGridMap == null) {
                        Toast.makeText(getActivity(), "Grid is not ready yet. Please try again.", Toast.LENGTH_SHORT).show();
                        return;
                    }
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
                            statusReturn = activeGridMap.addNewObstacleToGrid(x_coord_add, y_coord_add);
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
                            statusReturn = activeGridMap.removeFromGrid(x_coord_add, y_coord_add, true);
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
                            statusReturn = activeGridMap.addVehicleToMap(x_coord_add, y_coord_add);
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

        currentSelectedButtonId = View.NO_ID;
        updateButtonState();

        // Apply current theme
        if (getActivity() instanceof MainActivity) {
            applyTheme(((MainActivity) getActivity()).currentPalette());
        }
        return addCoordsView;
    }

    private void updateButtonState(){
        Palette p = currentPalette;
        if (p == null) return;
        boolean isF1 = (p.getAccent() == android.graphics.Color.parseColor("#E8002D"));
        boolean vehicleActive   = addStartingPointButton.getId() == currentSelectedButtonId;
        boolean obstacleActive  = addObstacleToggle.getId()      == currentSelectedButtonId;
        boolean removeActive    = removeButton.getId()            == currentSelectedButtonId;
        GridMapClass activeGridMap = resolveGridMap();

        int inactiveBg     = isF1 ? p.getBg()          : p.getSurface2();
        int inactiveStroke = isF1 ? p.getBorderStrong() : p.getBorderStrong();
        int activeVehicleBg     = isF1 ? p.getAccentDim() : p.getAccentDim();
        int activeVehicleStroke = isF1 ? p.getAccentBorder() : p.getAccentBorder();
        int activeObstacleBg     = isF1 ? p.getAccentDim() : p.getPinkDim();
        int activeObstacleStroke = isF1 ? p.getAccentBorder() : p.getPinkBorder();
        int activeRemoveBg     = isF1 ? p.getAccentDim() : p.getRedDim();
        int activeRemoveStroke = isF1 ? p.getAccentBorder() : p.getRedBorder();

        addStartingPointButton.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
            vehicleActive ? activeVehicleBg : inactiveBg));
        addStartingPointButton.setStrokeColor(android.content.res.ColorStateList.valueOf(
            vehicleActive ? activeVehicleStroke : inactiveStroke));

        addObstacleToggle.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
            obstacleActive ? activeObstacleBg : inactiveBg));
        addObstacleToggle.setStrokeColor(android.content.res.ColorStateList.valueOf(
            obstacleActive ? activeObstacleStroke : inactiveStroke));

        removeButton.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
            removeActive ? activeRemoveBg : inactiveBg));
        removeButton.setStrokeColor(android.content.res.ColorStateList.valueOf(
            removeActive ? activeRemoveStroke : inactiveStroke));

        boolean isLight = (p.getAccent() == android.graphics.Color.parseColor("#1A3ECF"));
        int vehicleTextColor  = vehicleActive  ? (isLight ? p.getAccent() : android.graphics.Color.WHITE) : p.getTextMuted();
        int obstacleTextColor = obstacleActive ? (isLight ? p.getPink()   : android.graphics.Color.WHITE) : p.getTextMuted();
        int removeTextColor   = removeActive   ? (isLight ? p.getRed()    : android.graphics.Color.WHITE) : p.getTextMuted();

        addStartingPointButton.setTextColor(vehicleTextColor);
        addObstacleToggle.setTextColor(obstacleTextColor);
        removeButton.setTextColor(removeTextColor);

        android.content.res.ColorStateList vehicleIconTint  = android.content.res.ColorStateList.valueOf(vehicleTextColor);
        android.content.res.ColorStateList obstacleIconTint = android.content.res.ColorStateList.valueOf(obstacleTextColor);
        android.content.res.ColorStateList removeIconTint   = android.content.res.ColorStateList.valueOf(removeTextColor);
        addStartingPointButton.setIconTint(vehicleIconTint);
        addObstacleToggle.setIconTint(obstacleIconTint);
        removeButton.setIconTint(removeIconTint);

        if (activeGridMap == null) return;
        if (vehicleActive) {
            activeGridMap.setGridMode(GridMapClass.GridMode.ADD_VEHICLE);
        } else if (obstacleActive) {
            activeGridMap.setGridMode(GridMapClass.GridMode.ADD_OBSTACLE);
        } else if (removeActive) {
            activeGridMap.setGridMode(GridMapClass.GridMode.REMOVE);
        } else {
            activeGridMap.setGridMode(GridMapClass.GridMode.NONE);
        }
    }

    @Override
    public void applyTheme(Palette p) {
        if (addCoordsView == null) return;
        currentPalette = p;
        android.content.Context ctx = requireContext();

        // Root background
        addCoordsView.setBackgroundColor(p.getPanel());

        // Section labels
        android.widget.TextView lblMode = addCoordsView.findViewById(R.id.lbl_mode);
        android.widget.TextView lblCoords = addCoordsView.findViewById(R.id.lbl_coordinates);
        android.widget.TextView lblMove = addCoordsView.findViewById(R.id.lbl_move);
        android.widget.TextView txtModeHint = addCoordsView.findViewById(R.id.txt_mode_hint);
        android.widget.TextView txtCoordRange = addCoordsView.findViewById(R.id.txt_coord_range);
        if (lblMode != null) lblMode.setTextColor(p.getTextMuted());
        if (lblCoords != null) lblCoords.setTextColor(p.getTextMuted());
        if (lblMove != null) lblMove.setTextColor(p.getTextMuted());
        if (txtModeHint != null) txtModeHint.setTextColor(p.getTextMuted());
        if (txtCoordRange != null) txtCoordRange.setTextColor(p.getTextMuted());

        // X/Y labels (children of linearLayout_add_x_y_coords)
        android.widget.LinearLayout coordsLayout = addCoordsView.findViewById(R.id.linearLayout_add_x_y_coords);
        if (coordsLayout != null) {
            for (int i = 0; i < coordsLayout.getChildCount(); i++) {
                android.view.View child = coordsLayout.getChildAt(i);
                if (child instanceof android.widget.LinearLayout) {
                    android.widget.LinearLayout col = (android.widget.LinearLayout) child;
                    if (col.getChildCount() > 0 && col.getChildAt(0) instanceof android.widget.TextView) {
                        ((android.widget.TextView) col.getChildAt(0)).setTextColor(p.getTextMuted());
                    }
                }
            }
        }

        // Dividers
        android.view.View divMode = addCoordsView.findViewById(R.id.divider_mode);
        android.view.View divCoords = addCoordsView.findViewById(R.id.divider_coords);
        android.view.View divMove = addCoordsView.findViewById(R.id.divider_move);
        if (divMode != null) divMode.setBackgroundColor(p.getBorderStrong());
        if (divCoords != null) divCoords.setBackgroundColor(p.getBorderStrong());
        if (divMove != null) divMove.setBackgroundColor(p.getBorderStrong());

        // Coordinate inputs
        if (addXCoords != null) {
            addXCoords.setBackground(ThemePaletteKt.box(ctx, p.getSurface2(), p.getBorderStrong(), 8f, 2f));
            addXCoords.setTextColor(p.getText());
            addXCoords.setHintTextColor(p.getTextDim());
        }
        if (addYCoords != null) {
            addYCoords.setBackground(ThemePaletteKt.box(ctx, p.getSurface2(), p.getBorderStrong(), 8f, 2f));
            addYCoords.setTextColor(p.getText());
            addYCoords.setHintTextColor(p.getTextDim());
        }

        boolean isF1 = (p.getAccent() == android.graphics.Color.parseColor("#E8002D"));
        // Dpad buttons
        int dpadStroke = isF1 ? p.getAccentBorder() : p.getBorderStrong();
        int dpadIconColor = isF1 ? p.getAccent() : p.getText();
        android.widget.Button dUp = addCoordsView.findViewById(R.id.dpad_up);
        android.widget.Button dDown = addCoordsView.findViewById(R.id.dpad_down);
        android.widget.Button dLeft = addCoordsView.findViewById(R.id.dpad_left);
        android.widget.Button dRight = addCoordsView.findViewById(R.id.dpad_right);
        if (dUp != null)    { dUp.setBackground(ThemePaletteKt.box(ctx, p.getSurface2(), dpadStroke, 10f, 2f)); dUp.setTextColor(dpadIconColor); }
        if (dDown != null)  { dDown.setBackground(ThemePaletteKt.box(ctx, p.getSurface2(), dpadStroke, 10f, 2f)); dDown.setTextColor(dpadIconColor); }
        if (dLeft != null)  { dLeft.setBackground(ThemePaletteKt.box(ctx, p.getSurface2(), dpadStroke, 10f, 2f)); dLeft.setTextColor(dpadIconColor); }
        if (dRight != null) { dRight.setBackground(ThemePaletteKt.box(ctx, p.getSurface2(), dpadStroke, 10f, 2f)); dRight.setTextColor(dpadIconColor); }

        // DpadCenterSquare
        android.widget.TextView dpadCenter = addCoordsView.findViewById(R.id.dpadCenterSquare);
        if (dpadCenter != null) {
            dpadCenter.setBackground(ThemePaletteKt.box(ctx, p.getSurface(), dpadStroke, 10f, 2f));
            dpadCenter.setTextColor(p.getTextMuted());
        }

        // Reverse buttons
        android.widget.LinearLayout revLeft = addCoordsView.findViewById(R.id.reverse_left_button);
        android.widget.LinearLayout revRight = addCoordsView.findViewById(R.id.reverse_right_button);
        if (revLeft != null) {
            revLeft.setBackground(ThemePaletteKt.box(ctx, p.getSurface2(), dpadStroke, 10f, 2f));
            android.widget.ImageView revLeftIcon = addCoordsView.findViewById(R.id.revLeftIcon);
            android.widget.TextView revLeftLabel = addCoordsView.findViewById(R.id.revLeftLabel);
            if (revLeftIcon != null) {
                revLeftIcon.setImageTintList(android.content.res.ColorStateList.valueOf(dpadIconColor));
                revLeftIcon.setColorFilter(dpadIconColor, android.graphics.PorterDuff.Mode.SRC_IN);
            }
            if (revLeftLabel != null) revLeftLabel.setTextColor(dpadIconColor);
        }
        if (revRight != null) {
            revRight.setBackground(ThemePaletteKt.box(ctx, p.getSurface2(), dpadStroke, 10f, 2f));
            android.widget.ImageView revRightIcon = addCoordsView.findViewById(R.id.revRightIcon);
            android.widget.TextView revRightLabel = addCoordsView.findViewById(R.id.revRightLabel);
            if (revRightIcon != null) {
                revRightIcon.setImageTintList(android.content.res.ColorStateList.valueOf(dpadIconColor));
                revRightIcon.setColorFilter(dpadIconColor, android.graphics.PorterDuff.Mode.SRC_IN);
            }
            if (revRightLabel != null) revRightLabel.setTextColor(dpadIconColor);
        }

        // Action buttons
        if (cancelButton != null) {
            cancelButton.setBackground(ThemePaletteKt.box(ctx,
                isF1 ? p.getBg() : p.getRedDim(),
                p.getRedBorder(), 8f, 2f));
            cancelButton.setTextColor(p.getRed());
        }
        if (addObstacleButton != null) {
            addObstacleButton.setBackground(ThemePaletteKt.box(ctx,
                isF1 ? p.getBg() : p.getGreenDim(),
                p.getGreenBorder(), 8f, 2f));
            addObstacleButton.setTextColor(p.getGreen());
        }

        // Mode buttons (delegates to updateButtonState which reads currentPalette)
        updateButtonState();
    }


    public boolean checkCoordCorrect(EditText inputFromUser, String x_or_y){
        Log.d("checkCoordCorrect Function", "Checking Coordinates for "+x_or_y);
        GridMapClass activeGridMap = resolveGridMap();
        if (activeGridMap == null) {
            Toast.makeText(getActivity(), "Grid is not ready yet. Please try again.", Toast.LENGTH_SHORT).show();
            return false;
        }

        String input = inputFromUser.getText().toString().trim();

        if(TextUtils.isEmpty(input)){
            Toast.makeText(getActivity(), "Please enter a value for input "+x_or_y, Toast.LENGTH_SHORT).show();
            return false;
        }

        try{
            int value = Integer.parseInt(input);
            if(value >= activeGridMap.lowLimit && value <= activeGridMap.hardLimit-1){
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
