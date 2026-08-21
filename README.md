# SC2079 MDP - Group 10

## Android App

The Android application serves as the main remote controller and monitoring interface for the MDP robot. It handles Bluetooth communication with the Raspberry Pi, visualizes the 20x20 arena map, and issues commands for tasks like exploration and image recognition.

### Getting Started

1. **Prerequisites**: Download and install [Android Studio](https://developer.android.com/studio).
2. **Open Project**: Launch Android Studio, click **Open**, and select the `android/` folder from this repository.
3. **Sync Gradle**: Allow Android Studio to download the necessary dependencies and sync the project.
4. **Testing UI**: You can run the app on the built-in Android Emulator to test the Grid Map, layouts, and navigation.
5. **Testing Bluetooth (Important)**: The Android Emulator **does not support Bluetooth**. To test the actual connection to the Raspberry Pi/robot, you must connect a physical Android device and deploy the app via USB Debugging.

### Build & Run

1. In Android Studio, wait for the indexing and Gradle sync to finish (indicated by the progress bar at the bottom).
2. Select your target device from the device dropdown menu in the top toolbar (either your connected physical device or a created Virtual Device).
3. Click the **Run** button (green play icon ▶️) or press `Control + R` (Mac).
4. Android Studio will compile the app (APK) and automatically launch it on your selected device.
