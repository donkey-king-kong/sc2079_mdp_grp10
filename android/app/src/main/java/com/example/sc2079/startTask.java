package com.example.sc2079;

import android.content.ContentUris;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.MediaStore;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;
import android.widget.ToggleButton;
import java.util.ArrayList;
import java.util.Locale;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.fragment.app.Fragment;

public class startTask extends Fragment {
    private ToggleButton startExplorationButton;
    private ToggleButton startFastestButton;
    private ToggleButton startStichButton;
    private Button viewSavedImagesButton;
    View addStartTaskView;
    private GridMapClass gridMap;
    private boolean startTraverseMap = false;
    private boolean startFastestRound = false;
    private boolean startSendStich = false;
    private TextView calculateObstacleTimerView;
    private TextView fastestTimeTimerView;
    public static Handler timerHandler = new Handler(Looper.getMainLooper());
    private int timerReflectOnText = 0;
    private Runnable timerRunnable;


    public startTask(GridMapClass gridMap){
        this.gridMap = gridMap;

    }

    @Nullable
    @Override
    public View onCreateView(LayoutInflater inflater, @Nullable ViewGroup container, Bundle savedInstanceState) {
        Log.d("onCreateView Function in startTask", "Entering onCreateView");
        addStartTaskView = inflater.inflate(R.layout.activate_tasks, container, false);
        super.onCreate(savedInstanceState);
        startExplorationButton = addStartTaskView.findViewById(R.id.beginExplorationButton);
        startFastestButton = addStartTaskView.findViewById(R.id.beginFastestButton);
        startStichButton = addStartTaskView.findViewById(R.id.beginStichButton);
        setTaskButtonColors(startExplorationButton, "#2563A8");
        setTaskButtonColors(startFastestButton, "#2563A8");
        setTaskButtonColors(startStichButton, "#5A2D8A");
        startExplorationButton.setChecked(startTraverseMap);
        startFastestButton.setChecked(startFastestRound);
        startStichButton.setChecked(startSendStich);
        viewSavedImagesButton = addStartTaskView.findViewById(R.id.viewSavedImagesButton);
        calculateObstacleTimerView = addStartTaskView.findViewById(R.id.calculateObstacleTimer);
        fastestTimeTimerView = addStartTaskView.findViewById(R.id.fastestTimeTimer);
        timerRunnable = new Runnable() {
            @Override
            public void run() {
                Log.d("Timer", "This runs every 1 second");
                timerReflectOnText += 1;
                // Calculate minutes and seconds
                int minutes = timerReflectOnText / 60;
                int seconds = timerReflectOnText % 60;

                // Format as MM:SS (e.g., 01:05)
                String time = String.format(Locale.getDefault(), "%02d:%02d", minutes, seconds);

                if(startTraverseMap){
                    calculateObstacleTimerView.setText(time);
                }
                if(startFastestRound){
                    fastestTimeTimerView.setText(time);
                }
                if(gridMap.checkFINStatus()){
                    startTraverseMap = false;
                    startFastestRound = false;
                    startExplorationButton.setChecked(false);
                    startFastestButton.setChecked(false);
                    timerReflectOnText = 0;
                    gridMap.updateFINStatus(false);
                    timerHandler.removeCallbacks(timerRunnable);
                }


                if (startSendStich && "Stitched Images Completed".equals(gridMap.getImmediateVehicleStatus())) {
                    startSendStich = false;
                    startStichButton.setChecked(false);
                }
                if (startTraverseMap || startFastestRound || startSendStich) {
                    timerHandler.postDelayed(this, 1000);
                }
            }
        };
        startExplorationButton.setOnClickListener(new View.OnClickListener()
        {
            @Override
            public void onClick(View view) {
                if (!startTraverseMap) {
                    gridMap.sendArenaDataBluetooth();
                    // gridMap.sendBeginExplorationBluetooth();
                    startTraverseMap = true;
                    gridMap.updateFINStatus(false);
                    showTaskStatus("Task 1 in Progress...");
                    timerHandler.removeCallbacks(timerRunnable);
                    timerReflectOnText = 0;
                    timerHandler.postDelayed(timerRunnable, 1000);
                } else {
                    startTraverseMap = false;
                    showTaskStatus("Task 1 Stopped");
                    timerReflectOnText = 0;
                    timerHandler.removeCallbacks(timerRunnable);
                    gridMap.updateFINStatus(false);
                }
            }
        });

        startFastestButton.setOnClickListener(new View.OnClickListener()
        {
            @Override
            public void onClick(View view){
                if (!startFastestRound) {
                    gridMap.sendArenaDataBluetooth();
                    // gridMap.sendBeginExplorationBluetooth();
                    startFastestRound = true;
                    gridMap.updateFINStatus(false);
                    showTaskStatus("Task 2 in Progress...");
                    timerHandler.removeCallbacks(timerRunnable);
                    timerReflectOnText = 0;
                    timerHandler.postDelayed(timerRunnable, 1000);
                } else{
                    startFastestRound = false;
                    showTaskStatus("Task 2 Stopped");
                    timerReflectOnText = 0;
                    timerHandler.removeCallbacks(timerRunnable);
                    gridMap.updateFINStatus(false);
                }
            }
        });


        startStichButton.setOnClickListener(new View.OnClickListener()
        {
            @Override
            public void onClick(View view){
            if (!startSendStich) {
                gridMap.sendStichSignalBluetooth();
                // gridMap.sendBeginExplorationBluetooth();
                startSendStich = true;
                showTaskStatus("Stitched Images in Progress...");
                timerHandler.removeCallbacks(timerRunnable);
                timerHandler.postDelayed(timerRunnable, 1000);
            }else{
                startSendStich = false;
                showTaskStatus("Stitched Images Stopped");
                if (!startTraverseMap && !startFastestRound) timerHandler.removeCallbacks(timerRunnable);
            }
        }
        });

        viewSavedImagesButton.setOnClickListener(new View.OnClickListener()
        {
            @Override
            public void onClick(View view){
                showSavedImagesDialog();
            }
        });

            return addStartTaskView;
    }

    private void setTaskButtonColors(ToggleButton button, String startColor) {
        button.setBackgroundTintList(new ColorStateList(
                new int[][] {new int[] {android.R.attr.state_checked}, new int[] {}},
                new int[] {Color.parseColor("#C62828"), Color.parseColor(startColor)}));
    }

    private void showTaskStatus(String status) {
        gridMap.setTaskStatus(status);
        TextView statusView = requireActivity().findViewById(R.id.give_vehicle_status_now);
        statusView.setText(status);
    }

    private void showSavedImagesDialog() {
        ArrayList<SavedImage> savedImages = loadSavedImages();

        if (savedImages.isEmpty()) {
            Toast.makeText(requireContext(), "No saved stitched images found", Toast.LENGTH_SHORT).show();
            return;
        }

        String[] imageNames = new String[savedImages.size()];
        for (int i = 0; i < savedImages.size(); i++) {
            imageNames[i] = savedImages.get(i).name;
        }

        new AlertDialog.Builder(requireContext())
                .setTitle("Saved Stitched Images")
                .setItems(imageNames, (dialog, which) -> openSavedImage(savedImages.get(which).uri))
                .setNegativeButton("Close", null)
                .show();
    }

    private ArrayList<SavedImage> loadSavedImages() {
        ArrayList<SavedImage> savedImages = new ArrayList<>();
        String[] projection = {
                MediaStore.Images.Media._ID,
                MediaStore.Images.Media.DISPLAY_NAME
        };
        String selection = MediaStore.Images.Media.RELATIVE_PATH + "=?";
        String[] selectionArgs = {"Pictures/SC2079/"};
        String sortOrder = MediaStore.Images.Media.DATE_ADDED + " DESC";

        try (Cursor cursor = requireContext().getContentResolver().query(
                MediaStore.Images.Media.EXTERNAL_CONTENT_URI,
                projection,
                selection,
                selectionArgs,
                sortOrder
        )) {
            if (cursor == null) {
                return savedImages;
            }

            int idColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media._ID);
            int nameColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.DISPLAY_NAME);

            while (cursor.moveToNext()) {
                long id = cursor.getLong(idColumn);
                String name = cursor.getString(nameColumn);
                Uri uri = ContentUris.withAppendedId(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, id);
                savedImages.add(new SavedImage(name, uri));
            }
        }

        return savedImages;
    }

    private void openSavedImage(Uri imageUri) {
        Intent intent = new Intent(Intent.ACTION_VIEW);
        intent.setDataAndType(imageUri, "image/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);

        try {
            startActivity(intent);
        } catch (Exception e) {
            Toast.makeText(requireContext(), "Unable to open image", Toast.LENGTH_SHORT).show();
            Log.e("startTask", "Unable to open saved stitched image", e);
        }
    }

    private static class SavedImage {
        final String name;
        final Uri uri;

        SavedImage(String name, Uri uri) {
            this.name = name;
            this.uri = uri;
        }
    }
}
