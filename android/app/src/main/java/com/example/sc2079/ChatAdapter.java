package com.example.sc2079;

import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Date;
import java.util.List;
import java.util.Locale;

public class ChatAdapter extends RecyclerView.Adapter<RecyclerView.ViewHolder> {
    private static final int VIEW_TYPE_INCOMING = 0;
    private static final int VIEW_TYPE_OUTGOING = 1;
    private static final int VIEW_TYPE_SYSTEM = 2;

    private final List<MainActivity.ChatLogEntry> entries = new ArrayList<>();
    private final SimpleDateFormat timeFormat = new SimpleDateFormat("HH:mm", Locale.getDefault());

    @Override
    public int getItemViewType(int position) {
        MainActivity.ChatLogType type = entries.get(position).getType();
        if (type == MainActivity.ChatLogType.OUTGOING) {
            return VIEW_TYPE_OUTGOING;
        }
        if (type == MainActivity.ChatLogType.SYSTEM) {
            return VIEW_TYPE_SYSTEM;
        }
        return VIEW_TYPE_INCOMING;
    }

    @NonNull
    @Override
    public RecyclerView.ViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        LayoutInflater inflater = LayoutInflater.from(parent.getContext());
        if (viewType == VIEW_TYPE_OUTGOING) {
            View view = inflater.inflate(R.layout.item_chat_outgoing, parent, false);
            return new MessageViewHolder(view, true);
        }
        if (viewType == VIEW_TYPE_SYSTEM) {
            View view = inflater.inflate(R.layout.item_chat_system, parent, false);
            return new SystemViewHolder(view);
        }
        View view = inflater.inflate(R.layout.item_chat_incoming, parent, false);
        return new MessageViewHolder(view, false);
    }

    @Override
    public void onBindViewHolder(@NonNull RecyclerView.ViewHolder holder, int position) {
        MainActivity.ChatLogEntry entry = entries.get(position);
        if (holder instanceof MessageViewHolder) {
            ((MessageViewHolder) holder).bind(entry, timeFormat.format(new Date(entry.getTimestamp())));
        } else if (holder instanceof SystemViewHolder) {
            ((SystemViewHolder) holder).bind(entry);
        }
    }

    @Override
    public int getItemCount() {
        return entries.size();
    }

    public void setEntries(Collection<MainActivity.ChatLogEntry> newEntries) {
        entries.clear();
        entries.addAll(newEntries);
        notifyDataSetChanged();
    }

    public void addEntry(MainActivity.ChatLogEntry entry) {
        entries.add(entry);
        notifyItemInserted(entries.size() - 1);
    }

    public void clearEntries() {
        int oldSize = entries.size();
        entries.clear();
        if (oldSize > 0) {
            notifyItemRangeRemoved(0, oldSize);
        }
    }

    private static class MessageViewHolder extends RecyclerView.ViewHolder {
        private final TextView messageText;
        private final TextView timestampText;

        MessageViewHolder(@NonNull View itemView, boolean outgoing) {
            super(itemView);
            messageText = itemView.findViewById(R.id.messageText);
            timestampText = itemView.findViewById(R.id.timestampText);
            messageText.setBackground(outgoing ? outgoingBubble(itemView) : incomingBubble(itemView));
        }

        void bind(MainActivity.ChatLogEntry entry, String timestamp) {
            messageText.setText(entry.getMessage());
            timestampText.setText(timestamp);
        }
    }

    private static class SystemViewHolder extends RecyclerView.ViewHolder {
        private final TextView systemText;

        SystemViewHolder(@NonNull View itemView) {
            super(itemView);
            systemText = itemView.findViewById(R.id.systemText);
            systemText.setBackground(systemPill(itemView));
        }

        void bind(MainActivity.ChatLogEntry entry) {
            systemText.setText("• " + entry.getMessage());
            android.net.Uri uri = entry.getImageUri();
            if (uri != null) {
                systemText.setOnClickListener(v -> {
                    try {
                        android.content.Intent intent = new android.content.Intent(android.content.Intent.ACTION_VIEW);
                        intent.setDataAndType(uri, "image/*");
                        intent.addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION);
                        v.getContext().startActivity(intent);
                    } catch (android.content.ActivityNotFoundException e) {
                        android.util.Log.w("ChatAdapter", "No app to open image");
                    }
                });
            } else {
                systemText.setOnClickListener(null);
            }
        }
    }

    private static GradientDrawable outgoingBubble(View view) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setShape(GradientDrawable.RECTANGLE);
        drawable.setColor(Color.parseColor("#5C6BC0"));
        drawable.setCornerRadii(new float[]{
                dp(view, 18), dp(view, 18),
                dp(view, 18), dp(view, 18),
                dp(view, 4), dp(view, 4),
                dp(view, 18), dp(view, 18)
        });
        return drawable;
    }

    private static GradientDrawable incomingBubble(View view) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setShape(GradientDrawable.RECTANGLE);
        drawable.setColor(Color.parseColor("#1C2333"));
        drawable.setStroke((int) dp(view, 1), Color.parseColor("#24FFFFFF"));
        drawable.setCornerRadii(new float[]{
                dp(view, 18), dp(view, 18),
                dp(view, 18), dp(view, 18),
                dp(view, 18), dp(view, 18),
                dp(view, 4), dp(view, 4)
        });
        return drawable;
    }

    private static GradientDrawable systemPill(View view) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setShape(GradientDrawable.RECTANGLE);
        drawable.setColor(Color.parseColor("#1C2333"));
        drawable.setStroke((int) dp(view, 1), Color.parseColor("#3A2A10"));
        drawable.setCornerRadius(dp(view, 18));
        return drawable;
    }

    private static float dp(View view, float value) {
        return value * view.getResources().getDisplayMetrics().density;
    }
}
