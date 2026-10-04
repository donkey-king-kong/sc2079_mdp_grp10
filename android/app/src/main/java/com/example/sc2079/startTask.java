package com.example.sc2079;

import android.content.ContentUris;
import android.content.Context;
import android.content.Intent;
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
import java.lang.Thread;
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


    public startTask() {}

    public startTask(GridMapClass gridMap){
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
        if (getContext() instanceof MainActivity) {
            GridMapClass current = ((MainActivity) getContext()).currentGridMapOrNull();
            if (current != null) return current;
        }
        return gridMap;
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
                if(resolveGridMap().checkFINStatus()){
                    if(startTraverseMap) {
                        startTraverseMap = false;
                    }else if(startFastestRound){
                        startFastestRound = false;
                    }
                    timerReflectOnText = 0;
                    resolveGridMap().updateFINStatus(false);
                    timerHandler.removeCallbacks(timerRunnable);
                }


                // Re-post with delay for repeating
                timerHandler.postDelayed(this, 1000);
            }
        };
        startExplorationButton.setOnClickListener(new View.OnClickListener()
        {
            @Override
            public void onClick(View view) {
                if (!startTraverseMap) {
                    resolveGridMap().sendArenaDataBluetooth();
                    try {
                        Thread.sleep(1000);
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                    // gridMap.sendBeginExplorationBluetooth();
                    startTraverseMap = true;
                    timerHandler.removeCallbacks(timerRunnable);
                    timerReflectOnText = 0;
                    timerHandler.postDelayed(timerRunnable, 1000);
                } else {
                    startTraverseMap = false;
                    timerReflectOnText = 0;
                    timerHandler.removeCallbacks(timerRunnable);
                    resolveGridMap().updateFINStatus(false);
                }
            }
        });

        startFastestButton.setOnClickListener(new View.OnClickListener()
        {
            @Override
            public void onClick(View view){
                if (!startFastestRound) {
                    resolveGridMap().sendArenaDataBluetooth();
                    try {
                        Thread.sleep(1000);
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                    // gridMap.sendBeginExplorationBluetooth();
                    startFastestRound = true;
                    timerHandler.removeCallbacks(timerRunnable);
                    timerReflectOnText = 0;
                    timerHandler.postDelayed(timerRunnable, 1000);
                } else{
                    startFastestRound = false;
                    timerReflectOnText = 0;
                    timerHandler.removeCallbacks(timerRunnable);
                    resolveGridMap().updateFINStatus(false);
                }
            }
        });


        startStichButton.setOnClickListener(new View.OnClickListener()
        {
            @Override
            public void onClick(View view){
            if (!startSendStich) {
                resolveGridMap().sendStichSignalBluetooth();
                try {
                    Thread.sleep(1000);
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
                // gridMap.sendBeginExplorationBluetooth();
                startSendStich = true;
            }else{
                startSendStich = false;
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
