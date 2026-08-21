package com.example.sc2079;

import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.IBinder;
import android.text.method.ScrollingMovementMethod;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.TextView;

import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.localbroadcastmanager.content.LocalBroadcastManager;

import com.example.sc2079.service.BluetoothService;

import java.nio.charset.StandardCharsets;

public class commsToRobot extends Fragment implements MainActivity.MessageListener {
    View addCommsView;

    //private BluetoothService btService;
    //private boolean bound = false;

    private EditText input;
    private ImageButton sendBtn;
    private TextView chatView;
    private GridMapClass gridMap;

    // Moved bluetooth logic to MainActivity, no need to bound bluetooth to this fragment

    public commsToRobot(GridMapClass gridMap) {
        this.gridMap = gridMap;
    }
    /*
    private final ServiceConnection conn = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder service) {
            BluetoothService.LocalBinder binder = (BluetoothService.LocalBinder) service;
            btService = binder.getService();
            bound = true;
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            bound = false;
            btService = null;
        }
    };*/

    /*
    @Override
    public void onStart() {
        super.onStart();
        Intent intent = new Intent(getContext(), BluetoothService.class);
        requireContext().bindService(intent, conn, Context.BIND_AUTO_CREATE);
    }

    @Override
    public void onStop() {
        super.onStop();
        if (bound) {
            requireContext().unbindService(conn);
            bound = false;
        }
    }*/

    // Moved broadcast receiver to MainActivity
    /*
    private final BroadcastReceiver msgReceiver = new BroadcastReceiver() {
        @Override public void onReceive(Context ctx, Intent intent) {
            if (!BluetoothService.ACTION_MESSAGE.equals(intent.getAction())) return;

            byte[] bytes = intent.getByteArrayExtra(BluetoothService.EXTRA_BYTES);
            String text   = intent.getStringExtra(BluetoothService.EXTRA_TEXT);

            // Fallback: if decoding failed, show hex so you SEE that bytes arrived
            if (text == null && bytes != null) {
                StringBuilder sb = new StringBuilder();
                for (byte b : bytes) sb.append(String.format("%02X ", b));
                text = "[bin] " + sb.toString().trim();
            }
            if (text == null) text = "(empty packet)";

            // Append on UI
            final String line = "Robot: " + text + "\n";
            if (isAdded()) {
                requireActivity().runOnUiThread(() -> {
                    chatView.append(line);
                    //chatScroll.post(() -> chatScroll.fullScroll(View.FOCUS_DOWN));
                });
            }
            if(text.contains("image-rec")){
                gridMap.receiveVerifiedObstacleBluetooth(text);
            }
        }
    };*/

    @Override
    public void onResume() {
        super.onResume();
        //lbm.registerReceiver(msgReceiver, new IntentFilter(BluetoothService.ACTION_MESSAGE));
        super.onResume();
        MainActivity activity = (MainActivity) requireActivity();
        if (activity != null) {
            // First, get the entire message history and display it
            chatView.setText(""); // Clear existing messages
            for (String message : activity.getMessageLog()) {
                chatView.append(message);
            }
            // Second, register this Fragment as the listener for new messages
            activity.setMessageListener(this);
        }
    }

    @Override
    public void onPause() {
        super.onPause();
        //LocalBroadcastManager.getInstance(requireContext()).unregisterReceiver(msgReceiver);
        super.onPause();
        MainActivity activity = (MainActivity) requireActivity();
        if (activity != null) {
            // Unregister the listener to prevent memory leaks and unnecessary updates
            activity.setMessageListener(null);
        }
    }

    @Override
    public void onNewMessage(String message) {
        if (isAdded()) { // Check if the fragment is currently attached to the activity
            // Use runOnUiThread to ensure UI updates are on the main thread
            requireActivity().runOnUiThread(() -> {
                chatView.append(message);
                // Optional: Auto-scroll to the bottom of the chat view
                // chatScroll.post(() -> chatScroll.fullScroll(View.FOCUS_DOWN));
            });
        }
    }

    public void onLogCleared() {
        if (isAdded()) {
            requireActivity().runOnUiThread(() -> {
                chatView.setText("");
            });
        }
    }

    @Nullable
    @Override
    public View onCreateView(LayoutInflater inflater, @Nullable ViewGroup container, Bundle savedInstanceState) {
        Log.d("onCreateView Function in AddObstacle", "Entering onCreateView");
        addCommsView = inflater.inflate(R.layout.comms_to_robot, container, false);

        input = addCommsView.findViewById(R.id.typeBoxEditText);
        sendBtn = addCommsView.findViewById(R.id.messageButton);
        chatView = addCommsView.findViewById(R.id.messageBlock);
        chatView.setMovementMethod(new ScrollingMovementMethod());

        /*
        sendBtn.setOnClickListener(v -> {
            if (bound && btService != null) {
                String msg = input.getText().toString();
                if (!msg.isEmpty()) {
                    btService.write(msg.getBytes(StandardCharsets.UTF_8));
                    // Optionally append to chat UI here
                    input.setText("");
                }
            }
        });

        sendBtn.setOnClickListener(v -> {
            if (bound && btService != null) {
                String msg = input.getText().toString();
                if (!msg.isEmpty()) {
                    btService.write(msg.getBytes(StandardCharsets.UTF_8));
                    chatView.append("Me: " + msg + "\n");
                    //chatScroll.post(() -> chatScroll.fullScroll(View.FOCUS_DOWN));
                    input.setText("");
                }
            }
        });*/
            sendBtn.setOnClickListener(v -> {
                MainActivity activity = (MainActivity) requireActivity();
                if (activity != null) {
                    BluetoothService btService = activity.getBluetoothService();
                    if (btService != null) {
                        String msg = input.getText().toString();
                        if (!msg.isEmpty()) {
                            btService.write(msg.getBytes(StandardCharsets.UTF_8));
                            chatView.append("Me: " + msg + "\n");
                            //chatScroll.post(() -> chatScroll.fullScroll(View.FOCUS_DOWN));
                            input.setText("");
                        }
                    }
                }
            });

        return addCommsView;
    }

}

