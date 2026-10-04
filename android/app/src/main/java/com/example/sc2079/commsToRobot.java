package com.example.sc2079;

import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.IBinder;
import android.graphics.Color;
import android.text.method.ScrollingMovementMethod;
import android.text.SpannableStringBuilder;
import android.text.Spannable;
import android.text.style.ForegroundColorSpan;
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
    private Button clearLogButton;
    private TextView chatView;
    private GridMapClass gridMap;

    // Moved bluetooth logic to MainActivity, no need to bound bluetooth to this fragment

    public commsToRobot() {}

    public commsToRobot(GridMapClass gridMap) {
        this.gridMap = gridMap;
    }

    @Override
    public void onAttach(android.content.Context context) {
        super.onAttach(context);
        if (gridMap == null && context instanceof MainActivity) {
            gridMap = ((MainActivity) context).currentGridMapOrNull();
        }
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
            for (MainActivity.ChatLogEntry entry : activity.getMessageLog()) {
                appendEntry(entry);
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
    public void onNewMessage(MainActivity.ChatLogEntry entry) {
        if (isAdded()) {
            requireActivity().runOnUiThread(() -> appendEntry(entry));
        }
    }

    private void appendEntry(MainActivity.ChatLogEntry entry) {
        String timeStr = new java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.getDefault())
                .format(new java.util.Date(entry.getTimestamp()));

        SpannableStringBuilder sb = new SpannableStringBuilder();

        // Timestamp prefix
        String tsStr = "[" + timeStr + "] ";
        sb.append(tsStr);
        sb.setSpan(
                new ForegroundColorSpan(Color.parseColor("#888888")),
                0, tsStr.length(),
                Spannable.SPAN_EXCLUSIVE_EXCLUSIVE
        );

        // Message body
        String body;
        int bodyColor;

        switch (entry.getType()) {
            case OUTGOING:
                body = "Me: " + entry.getMessage() + "\n";
                bodyColor = Color.parseColor("#378ADD");
                break;
            case SYSTEM:
                body = "• " + entry.getMessage() + "\n";
                bodyColor = Color.parseColor("#BA7517");
                break;
            default: // INCOMING
                body = "Robot: " + entry.getMessage() + "\n";
                bodyColor = Color.parseColor("#3B6D11");
                break;
        }

        int start = sb.length();
        sb.append(body);
        sb.setSpan(
                new ForegroundColorSpan(bodyColor),
                start, sb.length(),
                Spannable.SPAN_EXCLUSIVE_EXCLUSIVE
        );

        chatView.append(sb);

        // Auto-scroll
        final android.text.Layout layout = chatView.getLayout();
        if (layout != null) {
            int scrollDelta = layout.getLineBottom(chatView.getLineCount() - 1)
                    - chatView.getScrollY() - chatView.getHeight();
            if (scrollDelta > 0) chatView.scrollBy(0, scrollDelta);
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
        clearLogButton = addCommsView.findViewById(R.id.clearLogButton);
        chatView = addCommsView.findViewById(R.id.messageBlock);
        chatView.setMovementMethod(new ScrollingMovementMethod());

        clearLogButton.setOnClickListener(v -> {
            MainActivity activity = (MainActivity) requireActivity();
            if (activity != null) {
                activity.clearMessageLog();
            }
        });

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
                            activity.logOutgoing(msg);
                            input.setText("");
                        }
                    }
                }
            });

        return addCommsView;
    }

}

