package com.example.sc2079;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.Bundle;
import android.util.Base64;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.DialogFragment;

public class ImageDisplayFragment extends DialogFragment {

    private static final String ARG_BASE64_IMAGE = "base64_image";

    /**
     * Factory method to create a new instance of this dialog fragment.
     * @param base64Data The Base64 encoded image string.
     * @return A new instance of ImageDisplayFragment.
     */
    public static ImageDisplayFragment newInstance(String base64Data) {
        ImageDisplayFragment fragment = new ImageDisplayFragment();
        Bundle args = new Bundle();
        args.putString(ARG_BASE64_IMAGE, base64Data);
        fragment.setArguments(args);
        return fragment;
    }

    @Override
    public void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Make the dialog full-screen for a better image viewing experience
        setStyle(DialogFragment.STYLE_NORMAL, android.R.style.Theme_DeviceDefault_Light_NoActionBar_Fullscreen);
    }

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container, @Nullable Bundle savedInstanceState) {
        // Inflate a simple layout containing just an ImageView and a close button/instruction
        View view = inflater.inflate(R.layout.fragment_image_display, container, false);

        ImageView imageView = view.findViewById(R.id.full_screen_image_view);
        TextView closeText = view.findViewById(R.id.close_text);

        // Handle closing the dialog (e.g., tapping the image or a close button)
        closeText.setOnClickListener(v -> dismiss());

        if (getArguments() != null) {
            String base64Data = getArguments().getString(ARG_BASE64_IMAGE);

            if (base64Data != null && !base64Data.isEmpty()) {
                try {
                    // 1. Decode Base64 to byte array
                    byte[] decodedBytes = Base64.decode(base64Data, Base64.DEFAULT);

                    // 2. Decode byte array to Bitmap
                    Bitmap bitmap = BitmapFactory.decodeByteArray(decodedBytes, 0, decodedBytes.length);

                    // 3. Display the Bitmap
                    imageView.setImageBitmap(bitmap);

                } catch (Exception e) {
                    Log.e("ImageDisplayFragment", "Failed to decode Base64 image: " + e.getMessage());
                    // Display error message in the center of the screen
                    closeText.setText("Error displaying image: " + e.getMessage());
                }
            }
        }

        return view;
    }
}
