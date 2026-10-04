package com.example.sc2079;

import android.content.Context;
import android.os.Bundle;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.LinearLayout;

import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.example.sc2079.service.BluetoothService;
import com.example.sc2079.ui.Palette;
import com.example.sc2079.ui.ThemeAware;
import com.example.sc2079.ui.ThemePaletteKt;

import java.nio.charset.StandardCharsets;

public class commsToRobot extends Fragment implements MainActivity.MessageListener, ThemeAware {
    View addCommsView;

    //private BluetoothService btService;
    //private boolean bound = false;

    private EditText input;
    private ImageButton sendBtn;
    private Button clearLogButton;
    private RecyclerView messageRecycler;
    private ChatAdapter chatAdapter;
    private View rootView;
    private android.widget.TextView headerText;
    private LinearLayout inputRow;
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
        MainActivity activity = (MainActivity) requireActivity();
        if (activity != null) {
            // First, get the entire message history and display it
            chatAdapter.clearEntries();
            chatAdapter.setEntries(activity.getMessageLog());
            scrollToBottom();
            // Second, register this Fragment as the listener for new messages
            activity.setMessageListener(this);
        }
    }

    @Override
    public void onPause() {
        super.onPause();
        //LocalBroadcastManager.getInstance(requireContext()).unregisterReceiver(msgReceiver);
        MainActivity activity = (MainActivity) requireActivity();
        if (activity != null) {
            // Unregister the listener to prevent memory leaks and unnecessary updates
            activity.setMessageListener(null);
        }
    }

    @Override
    public void onNewMessage(MainActivity.ChatLogEntry entry) {
        if (isAdded()) {
            requireActivity().runOnUiThread(() -> {
                chatAdapter.addEntry(entry);
                scrollToBottom();
            });
        }
    }

    public void onLogCleared() {
        if (isAdded()) {
            requireActivity().runOnUiThread(() -> {
                chatAdapter.clearEntries();
            });
        }
    }

    private void scrollToBottom() {
        int lastPosition = chatAdapter.getItemCount() - 1;
        if (lastPosition >= 0) {
            messageRecycler.scrollToPosition(lastPosition);
        }
    }

    @Nullable
    @Override
    public View onCreateView(LayoutInflater inflater, @Nullable ViewGroup container, Bundle savedInstanceState) {
        Log.d("onCreateView Function in AddObstacle", "Entering onCreateView");
        addCommsView = inflater.inflate(R.layout.comms_to_robot, container, false);
        rootView = addCommsView;
        headerText = addCommsView.findViewById(R.id.messageLogHeader);
        inputRow = addCommsView.findViewById(R.id.inputRow);

        input = addCommsView.findViewById(R.id.typeBoxEditText);
        sendBtn = addCommsView.findViewById(R.id.messageButton);
        clearLogButton = addCommsView.findViewById(R.id.clearLogButton);
        messageRecycler = addCommsView.findViewById(R.id.messageRecycler);
        chatAdapter = new ChatAdapter();
        LinearLayoutManager layoutManager = new LinearLayoutManager(requireContext());
        layoutManager.setStackFromEnd(true);
        messageRecycler.setLayoutManager(layoutManager);
        messageRecycler.setAdapter(chatAdapter);

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

        if (getActivity() instanceof MainActivity) {
            applyTheme(((MainActivity) getActivity()).currentPalette());
        }

        return addCommsView;
    }

    @Override
    public void applyTheme(Palette p) {
        if (rootView == null) return;
        Context ctx = rootView.getContext();

        rootView.setBackgroundColor(p.getPanel());
        if (headerText != null)
            headerText.setTextColor(p.getTextMuted());
        if (messageRecycler != null)
            messageRecycler.setBackground(
                ThemePaletteKt.box(ctx, p.getBg(), p.getBorderStrong(), 8f, 1f));
        if (inputRow != null)
            inputRow.setBackground(
                ThemePaletteKt.box(ctx, p.getBg(), p.getBorderStrong(), 8f, 1f));
        if (input != null) {
            input.setTextColor(p.getText());
            input.setHintTextColor(p.getTextDim());
        }
        if (clearLogButton != null) {
            boolean isF1 = (p.getAccent() == android.graphics.Color.parseColor("#E8002D"));
            clearLogButton.setTextColor(isF1 ? android.graphics.Color.WHITE : p.getRed());
            clearLogButton.setBackground(
                ThemePaletteKt.box(ctx, p.getRedDim(), p.getRedBorder(), 8f, 1f));
        }
        if (sendBtn != null) {
            sendBtn.setBackground(
                ThemePaletteKt.box(ctx, p.getAccentDim(), p.getAccentBorder(), 8f, 1f));
        }
    }

}
