package com.example.sc2079;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import android.widget.ToggleButton;
import java.lang.Thread;
import java.util.Locale;

import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

public class startTask extends Fragment {
    private ToggleButton startExplorationButton;
    private ToggleButton startFastestButton;
    private ToggleButton startStichButton;
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
                    if(startTraverseMap) {
                        startTraverseMap = false;
                    }else if(startFastestRound){
                        startFastestRound = false;
                    }
                    timerReflectOnText = 0;
                    gridMap.updateFINStatus(false);
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
                    gridMap.sendArenaDataBluetooth();
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

            return addStartTaskView;
    }
}
