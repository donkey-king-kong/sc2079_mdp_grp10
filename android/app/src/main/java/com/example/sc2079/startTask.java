package com.example.sc2079;

import android.content.ContentUris;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.drawable.Drawable;
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

import com.example.sc2079.ui.Palette;
import com.example.sc2079.ui.ThemeAware;
import com.example.sc2079.ui.ThemePaletteKt;

public class startTask extends Fragment implements ThemeAware {
    private Button startExplorationButton;
    private Button startFastestButton;
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
    private View rootView;
    private enum TaskState { IDLE, RUNNING, STOPPED }
    private TaskState task1State = TaskState.IDLE;
    private TaskState task2State = TaskState.IDLE;


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
                    if (calculateObstacleTimerView != null) calculateObstacleTimerView.setText(time);
                }
                if(startFastestRound){
                    if (fastestTimeTimerView != null) fastestTimeTimerView.setText(time);
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
        startExplorationButton.setOnClickListener(v -> {
            Palette p = (getActivity() instanceof MainActivity)
                ? ((MainActivity) getActivity()).currentPalette()
                : com.example.sc2079.ui.ThemePaletteKt.getNIGHT();
            switch (task1State) {
                case IDLE:
                    resolveGridMap().sendArenaDataBluetooth();
                    startTraverseMap = true;
                    timerHandler.removeCallbacks(timerRunnable);
                    timerReflectOnText = 0;
                    timerHandler.postDelayed(timerRunnable, 1000);
                    applyTask1State(TaskState.RUNNING, p);
                    break;
                case RUNNING:
                    startTraverseMap = false;
                    timerHandler.removeCallbacks(timerRunnable);
                    resolveGridMap().updateFINStatus(false);
                    applyTask1State(TaskState.STOPPED, p);
                    break;
                case STOPPED:
                    timerReflectOnText = 0;
                    calculateObstacleTimerView.setText("00:00");
                    applyTask1State(TaskState.IDLE, p);
                    break;
            }
        });

        startFastestButton.setOnClickListener(v -> {
            Palette p = (getActivity() instanceof MainActivity)
                ? ((MainActivity) getActivity()).currentPalette()
                : com.example.sc2079.ui.ThemePaletteKt.getNIGHT();
            switch (task2State) {
                case IDLE:
                    resolveGridMap().sendArenaDataBluetooth();
                    startFastestRound = true;
                    timerHandler.removeCallbacks(timerRunnable);
                    timerReflectOnText = 0;
                    timerHandler.postDelayed(timerRunnable, 1000);
                    applyTask2State(TaskState.RUNNING, p);
                    break;
                case RUNNING:
                    startFastestRound = false;
                    timerHandler.removeCallbacks(timerRunnable);
                    resolveGridMap().updateFINStatus(false);
                    applyTask2State(TaskState.STOPPED, p);
                    break;
                case STOPPED:
                    timerReflectOnText = 0;
                    fastestTimeTimerView.setText("00:00");
                    applyTask2State(TaskState.IDLE, p);
                    break;
            }
        });


        startStichButton.setOnClickListener(new View.OnClickListener()
        {
            @Override
            public void onClick(View view){
            if (!startSendStich) {
                resolveGridMap().sendStichSignalBluetooth();
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

        rootView = addStartTaskView;
        if (getActivity() instanceof MainActivity) {
            applyTheme(((MainActivity) getActivity()).currentPalette());
        }

        return addStartTaskView;
    }

    @Override
    public void onDestroyView() {
        super.onDestroyView();
        timerHandler.removeCallbacks(timerRunnable);
        calculateObstacleTimerView = null;
        fastestTimeTimerView = null;
    }

    private void applyTask1State(TaskState state, Palette p) {
        task1State = state;
        Context ctx = rootView.getContext();
        switch (state) {
            case IDLE:
                startExplorationButton.setText("TASK 1 START");
                startExplorationButton.setBackground(ThemePaletteKt.box(ctx, p.getAccentDim(), p.getAccentBorder(), 8f, 1f));
                startExplorationButton.setTextColor(p.getAccent());
                break;
            case RUNNING:
                startExplorationButton.setText("STOP");
                startExplorationButton.setBackground(ThemePaletteKt.box(ctx, p.getRedDim(), p.getRedBorder(), 8f, 1f));
                startExplorationButton.setTextColor(p.getRed());
                break;
            case STOPPED:
                startExplorationButton.setText("RESET");
                startExplorationButton.setBackground(ThemePaletteKt.box(ctx, p.getBorderStrong(), p.getBorderStrong(), 8f, 1f));
                startExplorationButton.setTextColor(p.getTextMuted());
                break;
        }
    }

    private void applyTask2State(TaskState state, Palette p) {
        task2State = state;
        Context ctx = rootView.getContext();
        switch (state) {
            case IDLE:
                startFastestButton.setText("TASK 2 START");
                startFastestButton.setBackground(ThemePaletteKt.box(ctx, p.getAccentDim(), p.getAccentBorder(), 8f, 1f));
                startFastestButton.setTextColor(p.getAccent());
                break;
            case RUNNING:
                startFastestButton.setText("STOP");
                startFastestButton.setBackground(ThemePaletteKt.box(ctx, p.getRedDim(), p.getRedBorder(), 8f, 1f));
                startFastestButton.setTextColor(p.getRed());
                break;
            case STOPPED:
                startFastestButton.setText("RESET");
                startFastestButton.setBackground(ThemePaletteKt.box(ctx, p.getBorderStrong(), p.getBorderStrong(), 8f, 1f));
                startFastestButton.setTextColor(p.getTextMuted());
                break;
        }
    }

    private Drawable f1CardDrawable(Context ctx, Palette p) {
        float radius = android.util.TypedValue.applyDimension(
            android.util.TypedValue.COMPLEX_UNIT_DIP, 8f, ctx.getResources().getDisplayMetrics());
        float barWidth = android.util.TypedValue.applyDimension(
            android.util.TypedValue.COMPLEX_UNIT_DIP, 3f, ctx.getResources().getDisplayMetrics());
        int accent = Color.parseColor("#E8002D");

        return new Drawable() {
            private final Paint bgPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
            private final Paint barPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
            private final Path clipPath = new Path();
            private final RectF rect = new RectF();

            {
                bgPaint.setStyle(Paint.Style.FILL);
                bgPaint.setColor(p.getPanel());
                barPaint.setStyle(Paint.Style.FILL);
                barPaint.setColor(accent);
            }

            @Override
            public void draw(Canvas canvas) {
                Rect bounds = getBounds();
                rect.set(bounds);
                clipPath.reset();
                clipPath.addRoundRect(rect, radius, radius, Path.Direction.CW);

                int save = canvas.save();
                canvas.clipPath(clipPath);
                canvas.drawRoundRect(rect, radius, radius, bgPaint);
                canvas.drawRect(bounds.left, bounds.top, bounds.left + barWidth, bounds.bottom, barPaint);
                canvas.restoreToCount(save);
            }

            @Override
            public void setAlpha(int alpha) {
                bgPaint.setAlpha(alpha);
                barPaint.setAlpha(alpha);
                invalidateSelf();
            }

            @Override
            public void setColorFilter(@Nullable ColorFilter colorFilter) {
                bgPaint.setColorFilter(colorFilter);
                barPaint.setColorFilter(colorFilter);
                invalidateSelf();
            }

            @Override
            public int getOpacity() {
                return PixelFormat.TRANSLUCENT;
            }
        };
    }

    @Override
    public void applyTheme(Palette p) {
        if (rootView == null) return;
        Context ctx = rootView.getContext();

        // Card backgrounds
        android.view.View card1 = rootView.findViewById(R.id.card_task1);
        android.view.View card2 = rootView.findViewById(R.id.card_task2);
        android.view.View card3 = rootView.findViewById(R.id.card_stitch);
        boolean isF1 = (p.getAccent() == Color.parseColor("#E8002D"));
        if (card1 != null) {
            if (isF1) {
                card1.setBackground(f1CardDrawable(ctx, p));
            } else {
                card1.setBackground(ThemePaletteKt.box(ctx, p.getPanel(), p.getBorderStrong(), 12f, 1f));
            }
        }
        if (card2 != null) {
            if (isF1) {
                card2.setBackground(f1CardDrawable(ctx, p));
            } else {
                card2.setBackground(ThemePaletteKt.box(ctx, p.getPanel(), p.getBorderStrong(), 12f, 1f));
            }
        }
        if (card3 != null) {
            if (isF1) {
                card3.setBackground(f1CardDrawable(ctx, p));
            } else {
                card3.setBackground(ThemePaletteKt.box(ctx, p.getPanel(), p.getBorderStrong(), 12f, 1f));
            }
        }

        // Section label TextViews
        android.widget.TextView lbl1 = rootView.findViewWithTag("lbl_task1");
        android.widget.TextView lbl2 = rootView.findViewWithTag("lbl_task2");
        android.widget.TextView lbl3 = rootView.findViewWithTag("lbl_stitch");

        // Timer display boxes
        if (calculateObstacleTimerView != null) {
            calculateObstacleTimerView.setBackground(ThemePaletteKt.box(ctx, p.getBg(), p.getBorderStrong(), 8f, 1f));
            calculateObstacleTimerView.setTextColor(p.getText());
        }
        if (fastestTimeTimerView != null) {
            fastestTimeTimerView.setBackground(ThemePaletteKt.box(ctx, p.getBg(), p.getBorderStrong(), 8f, 1f));
            fastestTimeTimerView.setTextColor(p.getText());
        }

        if (startExplorationButton != null) applyTask1State(task1State, p);
        if (startFastestButton != null) applyTask2State(task2State, p);
        // Stitch START/STOP toggle — pink colour
        if (startStichButton != null) {
            startStichButton.setBackground(ThemePaletteKt.box(ctx, p.getPinkDim(), p.getPinkBorder(), 8f, 1f));
            startStichButton.setTextColor(p.getPink());
        }
        // View Saved Images button — green colour
        if (viewSavedImagesButton != null) {
            viewSavedImagesButton.setBackground(ThemePaletteKt.box(ctx, p.getGreenDim(), p.getGreenBorder(), 8f, 1f));
            viewSavedImagesButton.setTextColor(p.getGreen());
        }

        // Section label TextViews — find by id after we add them in the XML change below
        android.widget.TextView lblTask1 = rootView.findViewById(R.id.lbl_task1);
        android.widget.TextView lblTask2 = rootView.findViewById(R.id.lbl_task2);
        android.widget.TextView lblStitch = rootView.findViewById(R.id.lbl_stitch);
        if (lblTask1 != null) lblTask1.setTextColor(p.getTextMuted());
        if (lblTask2 != null) lblTask2.setTextColor(p.getTextMuted());
        if (lblStitch != null) lblStitch.setTextColor(p.getTextMuted());

        // ScrollView / root background
        rootView.setBackgroundColor(p.getBg());
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
