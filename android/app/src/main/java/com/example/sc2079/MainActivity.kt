package com.example.sc2079

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.content.ContentValues
import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.graphics.BitmapFactory
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.os.Environment
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.provider.MediaStore
import android.util.Log
import android.view.Gravity
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.fragment.app.FragmentPagerAdapter
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import androidx.viewpager.widget.ViewPager
import com.example.sc2079.databinding.ActivityMainBinding
import com.example.sc2079.service.BluetoothService
import com.example.sc2079.ui.bluetooth.BluetoothFragment
import com.google.android.material.button.MaterialButton
import com.google.android.material.tabs.TabLayout
import com.google.gson.Gson
import com.google.gson.reflect.TypeToken
import android.util.Base64
import android.widget.EditText
import android.widget.ScrollView
import android.text.InputFilter
import android.text.InputType
import com.google.android.material.bottomsheet.BottomSheetDialog
import android.widget.NumberPicker
import androidx.appcompat.app.AlertDialog
import com.example.sc2079.ui.coordinates.AddCoordinateFragment
import com.example.sc2079.ui.coordinates.PlaceObstacleDialogFragment
import com.example.sc2079.ui.coordinates.SharedViewModel
import java.io.IOException

class MainActivity : AppCompatActivity() {
    private val base64Data = StringBuilder();
    private var iterationHowMany: Int = -1;
    private var bluetoothService: BluetoothService? = null
    private var isBound = false
    private lateinit var binding: ActivityMainBinding
    private lateinit var btnBluetooth: ImageButton
    private lateinit var btnAddCoordinate: ImageButton
    private lateinit var bluetoothStatus: ImageView
    private var activateJoyStickBool = false
    private var isDayMode = false

    private val sharedViewModel: SharedViewModel by viewModels()

    private var isConnected = false
    private lateinit var gridMapObj: GridMapClass
    private val messageLog = ArrayList<String>()
    private var messageListener: MessageListener? = null

    interface MessageListener {
        fun onNewMessage(message: String)
        fun onLogCleared()
    }
    private lateinit var givevehicleDirectionNow: TextView
    private lateinit var givevehicleCoordinatesNow: TextView
    private lateinit var givevehicleStatusNow: TextView
    private val handler = Handler(Looper.getMainLooper())
    // Polls AMD every 2s when Auto mode is ON, requesting arena + robot position update
    private val autoHandler = Handler(Looper.getMainLooper())
    private val autoRunnable = object : Runnable {
        override fun run() {
            bluetoothService?.write("sendArena".toByteArray())
            autoHandler.postDelayed(this, 2000)
        }
    }
    private val updateTask = object : Runnable {
        override fun run() {
            val givevehicleDirectionNow = findViewById<TextView?>(R.id.give_vehicle_direction_now)
            val givevehicleCoordinatesNow = findViewById<TextView?>(R.id.give_vehicle_coord_now)
            val givevehicleStatusNow = findViewById<TextView?>(R.id.give_vehicle_status_now)
            // Update your TextViews from gridMapObj
            givevehicleDirectionNow.text = gridMapObj.getImmediateVehicleDirection()
            givevehicleCoordinatesNow.text = gridMapObj.getImmediateVehicleCoord()
            val getString = gridMapObj.getImmediateVehicleStatus()
            givevehicleStatusNow.text = gridMapObj.getImmediateVehicleStatus()

            // Schedule the next update after 500 ms (adjust as needed)
            handler.postDelayed(this, 500)
        }
    }
    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
            val binder = service as BluetoothService.LocalBinder
            bluetoothService = binder.getService()
            isBound = true
            gridMapObj.setBluetoothService(bluetoothService)

            // Start Bluetooth server to allow incoming connections (e.g., from Windows)
            val service = bluetoothService
            if (service != null && ContextCompat.checkSelfPermission(this@MainActivity, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED) {
                service.startServer()
            }
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            isBound = false
            bluetoothService = null
            gridMapObj.setBluetoothService(null)
        }
    }

    private val bluetoothEnableLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            if (result.resultCode == RESULT_OK) {
                // User enabled Bluetooth → show fragment
                BluetoothFragment().show(supportFragmentManager, "BluetoothFragment")
            } else {
                Toast.makeText(this, "Bluetooth is required to continue", Toast.LENGTH_SHORT).show()
            }
        }

    fun getBluetoothService(): BluetoothService? {
        // Check that the service is bound and the instance is not null
        return if (isBound) bluetoothService else null
    }

    fun getIsConnected(): Boolean {
        return isConnected
    }

    fun getMessageLog(): ArrayList<String> {
        return messageLog
    }

    fun setMessageListener(listener: MessageListener?) {
        this.messageListener = listener
    }

    private fun saveStitchedImageToGallery(base64Image: String): Uri {
        val imageBytes = Base64.decode(base64Image, Base64.DEFAULT)
        val (mimeType, extension) = detectImageType(imageBytes)
        val fileName = "sc2079_stitched_${System.currentTimeMillis()}.$extension"

        val values = ContentValues().apply {
            put(MediaStore.Images.Media.DISPLAY_NAME, fileName)
            put(MediaStore.Images.Media.MIME_TYPE, mimeType)
            put(
                MediaStore.Images.Media.RELATIVE_PATH,
                "${Environment.DIRECTORY_PICTURES}/SC2079"
            )
            put(MediaStore.Images.Media.IS_PENDING, 1)
        }

        val resolver = contentResolver
        val uri = resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values)
            ?: throw IOException("Unable to create Gallery image entry")

        try {
            resolver.openOutputStream(uri)?.use { outputStream ->
                outputStream.write(imageBytes)
            } ?: throw IOException("Unable to open Gallery image output stream")

            values.clear()
            values.put(MediaStore.Images.Media.IS_PENDING, 0)
            resolver.update(uri, values, null, null)
            return uri
        } catch (e: Exception) {
            resolver.delete(uri, null, null)
            throw e
        }
    }

    private fun detectImageType(bytes: ByteArray): Pair<String, String> {
        val isPng = bytes.size >= 8 &&
            bytes[0] == 0x89.toByte() &&
            bytes[1] == 0x50.toByte() &&
            bytes[2] == 0x4E.toByte() &&
            bytes[3] == 0x47.toByte()
        val isWebp = bytes.size >= 12 &&
            bytes[0] == 0x52.toByte() &&
            bytes[1] == 0x49.toByte() &&
            bytes[2] == 0x46.toByte() &&
            bytes[3] == 0x46.toByte() &&
            bytes[8] == 0x57.toByte() &&
            bytes[9] == 0x45.toByte() &&
            bytes[10] == 0x42.toByte() &&
            bytes[11] == 0x50.toByte()

        return when {
            bytes.size >= 3 &&
                bytes[0] == 0xFF.toByte() &&
                bytes[1] == 0xD8.toByte() &&
                bytes[2] == 0xFF.toByte() -> "image/jpeg" to "jpg"
            isPng -> "image/png" to "png"
            isWebp -> "image/webp" to "webp"
            else -> "image/jpeg" to "jpg"
        }
    }

    // This BroadcastReceiver will handle incoming data messages from the BluetoothService
    private val msgReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (BluetoothService.ACTION_MESSAGE != intent?.action) return

            val bytes = intent.getByteArrayExtra(BluetoothService.EXTRA_BYTES)
            var text = intent.getStringExtra(BluetoothService.EXTRA_TEXT)

            if (text == null && bytes != null) {
                val sb = StringBuilder()
                for (b in bytes) sb.append(String.format("%02X ", b))
                text = "[bin] " + sb.toString().trim()
            }
            if (text == null) text = "(empty packet)"

            // Checklist requirements C.9 & C.10 (Plain text protocol)
            if (text.startsWith("TARGET,")) {
                val subParts = text.split(",").map { it.trim() }
                if (subParts.size >= 3) {
                    try {
                        gridMapObj.updateObstacleTarget(subParts[1].toInt(), subParts[2])
                    } catch (e: Exception) {
                        Log.e("MainActivity", "Error parsing TARGET: $text")
                    }
                }
            }

            if (text.startsWith("ROBOT,")) {
                val subParts = text.split(",").map { it.trim() }
                if (subParts.size >= 4) {
                    try {
                        gridMapObj.updateRobotPosition(subParts[1].toInt(), subParts[2].toInt(), subParts[3])
                    } catch (e: Exception) {
                        Log.e("MainActivity", "Error parsing ROBOT: $text")
                    }
                }
            }

            val line: String
            /*
            if (text.contains("stitch-image:")) {
                // 1. Extract Base64 data (everything after "image-rec:")
                val base64Data = text.substringAfter("stitch-image:").trim()

                // 2. Launch the image display fragment (pop-up)
                if (base64Data.isNotEmpty()) {
                    // Check for isBound before showing fragment
                    ImageDisplayFragment.newInstance(base64Data)
                        .show(supportFragmentManager, "ImageDisplayFragment")
                }

                // 3. Log a simple placeholder message to the chat history
                line = "Robot: [Image Received - Tap to view]\n"

                // Still notify GridMap for obstacle verification
                //gridMapObj.receiveVerifiedObstacleBluetooth(text);
            */
            //} else {
            // Regular text message
            line = "Robot: " + text + "\n"
            //}

            if(iterationHowMany == -1){
                // Store the message in the persistent log
                messageLog.add(line)

                // Notify the registered listener (if one exists)
                messageListener?.onNewMessage(line)
            }

            if(text.contains("stitch-image")) {
                val status = gridMapObj.receiveStichImageMessageBluetooth(text);
                when (status) {
                    "-1" -> {
                        base64Data.clear()
                        iterationHowMany = -1
                        messageLog.add("Invalid stitched image message received \n");
                    }
                    "2" -> {
                        messageLog.add("Starting to Stitch \n");
                        iterationHowMany = 0;
                        base64Data.clear()
                    }
                    "3" -> {
                        if (iterationHowMany < 0 || base64Data.isEmpty()) {
                            base64Data.clear()
                            iterationHowMany = -1
                            Toast.makeText(this@MainActivity, "Incomplete stitched image received", Toast.LENGTH_SHORT).show()
                            return
                        }
                        val stitchedImageBase64 = base64Data.toString()
                        base64Data.clear()
                        iterationHowMany = -1
                        try {
                            val imageBytes = Base64.decode(stitchedImageBase64, Base64.DEFAULT)
                            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                            BitmapFactory.decodeByteArray(imageBytes, 0, imageBytes.size, bounds)
                            require(bounds.outWidth > 0 && bounds.outHeight > 0) { "Invalid image data" }
                        } catch (e: Exception) {
                            Log.e("Image Message", "Invalid stitched image", e)
                            Toast.makeText(this@MainActivity, "Invalid stitched image received", Toast.LENGTH_SHORT).show()
                            return
                        }
                        messageLog.add("Ending Stitch, displaying image \n")
                        messageLog.add("Robot: [Image Received - Tap to view]\n")
                        Log.d("Image Message", "Final length: ${stitchedImageBase64.length}")
                        try {
                            val savedUri = saveStitchedImageToGallery(stitchedImageBase64)
                            messageLog.add("Saved stitched image to Gallery \n")
                            Toast.makeText(
                                this@MainActivity,
                                "Stitched image saved to Gallery",
                                Toast.LENGTH_SHORT
                            ).show()
                            Log.d("Image Message", "Saved stitched image to $savedUri")
                        } catch (e: Exception) {
                            messageLog.add("Failed to save stitched image to Gallery \n")
                            Toast.makeText(
                                this@MainActivity,
                                "Failed to save stitched image",
                                Toast.LENGTH_SHORT
                            ).show()
                            Log.e("Image Message", "Failed to save stitched image", e)
                        }
                        //val imageBytes = Base64.decode(base64Data.toString(), Base64.DEFAULT)
                        ImageDisplayFragment.newInstance(stitchedImageBase64).show(supportFragmentManager, "ImageDisplayFragment")
                        iterationHowMany = -1;
                    }
                    else -> {
                        if (iterationHowMany < 0) return // Wait for a start marker.
                        base64Data.append(status)  // add chunk
                        iterationHowMany += 1
                        messageLog.add("Running data compilation iteration $iterationHowMany \n")
                        Log.d("Image Chunk", "Added chunk length=${status.length}, total=${base64Data.length}")

                    }
                }
                return
            }


            if(text.contains("image-rec")){
                val status = gridMapObj.receiveVerifiedObstacleBluetooth(text);
                when(status){
                    -3->{
                        //Toast.makeText(context, "Bullseye Detected!", Toast.LENGTH_SHORT).show();
                        messageLog.add("Bullseye Detected \n");
                    }
                    -2 ->{
                        //Toast.makeText(context, "Unknown Error Occurred", Toast.LENGTH_SHORT).show();
                        messageLog.add("Unknown Error Occurred at image-rec \n");
                    }
                    -1 ->{
                        //Toast.makeText(context, "No Image ID Detected", Toast.LENGTH_SHORT).show();
                        messageLog.add("No Image ID Detected \n");
                    }
                    0 ->{
                        //Toast.makeText(context, "Failed to verify Obstacle", Toast.LENGTH_SHORT).show();
                        messageLog.add("Failed to verify Obstacle \n");
                    }
                    1->{
                        //Toast.makeText(context, "Successfully Verified Obstacle", Toast.LENGTH_SHORT).show();
                        messageLog.add("Successfully Verified Obstacle \n");
                    }
                    2->{
                        //Toast.makeText(context, "Capturing Obstacle Image", Toast.LENGTH_SHORT).show();
                        messageLog.add("Capturing Obstacle Image \n");
                    }

                }
            }

            if(text.contains("location")) {
                val status = gridMapObj.receiveLocationMessageBluetooth(text);
                when (status) {
                    -2 -> {
                        //Toast.makeText(context, "Unknown Error Occurred", Toast.LENGTH_SHORT).show();
                        messageLog.add("Unknown Error Occurred at location \n");
                    }
                    0 -> {
                        //Toast.makeText(context, "Failed to verify Location", Toast.LENGTH_SHORT).show();
                        messageLog.add("Failed to verify Location \n");
                    }

                    1 -> {
                        //Toast.makeText(context, "Successfully Verified Location", Toast.LENGTH_SHORT).show();
                        messageLog.add("Successfully Verified Location \n");
                    }
                }
            }


            if(text.contains("health")){
                val status = gridMapObj.receiveHealthMessageBluetooth(text);
                when (status) {
                    -2 -> {
                        //Toast.makeText(context, "Unknown Error Occurred", Toast.LENGTH_SHORT).show();
                        messageLog.add("Unknown Error Occurred at health \n");
                    }
                    0 ->{
                        //Toast.makeText(context, "Image Rec API is down", Toast.LENGTH_SHORT).show();
                        messageLog.add("Image Rec API is down \n");
                    }
                    1 ->{
                        //Toast.makeText(context, "Algo API is down", Toast.LENGTH_SHORT).show();
                        messageLog.add("Algo API is down \n");
                    }
                }
            }


            if(text.contains("status")){
                activateJoyStickBool = false;
                gridMapObj.receiveStatusMessageBluetooth(text, activateJoyStickBool);
            }

            if(text.contains("\"grid\"")){
                gridMapObj.receiveGridHexBluetooth(text);
            }

            // if(text.contains("Failed to convert raw Android message")){
            // gridMapObj.sendAlertToSignalFailure();
            // }
        }
    }

    private val requestBluetoothPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { permissions ->
            val allGranted = permissions.entries.all { it.value }
            if (allGranted) {
                // Permissions granted, now we can ask to enable Bluetooth
                checkBluetoothEnabled()
                // Start the server now that permissions are granted
                val service = bluetoothService
                if (isBound && service != null && ContextCompat.checkSelfPermission(this@MainActivity, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED) {
                    @Suppress("MissingPermission")
                    service.startServer()
                }
            } else {
                Toast.makeText(this, "Bluetooth permissions are required", Toast.LENGTH_SHORT).show()
            }
        }

    private val connStateReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            val state = intent?.getStringExtra(BluetoothService.EXTRA_CONN_STATE)

            if (state == "connected") {
                isConnected = true
                gridMapObj.syncArenaDataBluetooth()
            } else if (state == "disconnected" || state == "error") {
                isConnected = false
                base64Data.clear()
                iterationHowMany = -1
            }

            // Update the UI with the new status
            updateBluetoothStatus()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
        //val navView: BottomNavigationView = binding.navView
        //val navController = findNavController(R.id.nav_host_fragment_activity_main)

        //navView.setupWithNavController(navController)
        btnBluetooth = findViewById(R.id.btnBluetooth)
        bluetoothStatus = findViewById(R.id.bluetoothStatus)

        btnAddCoordinate = findViewById(R.id.btnAddCoordinate)

        btnAddCoordinate.setOnClickListener {
            showAddCoordinatesFragment()
        }

        updateBluetoothStatus()

        btnBluetooth.setOnClickListener {
            checkBluetoothPermissionsAndState()
        }

        sharedViewModel.newCoordinate.observe(this) { coordinate ->
            gridMapObj.addNewObstacleToGrid(coordinate.first.toInt(), coordinate.second.toInt())
        }

        sharedViewModel.newObstacleRequest.observe(this) { request ->
            gridMapObj.addNewObstacleToGridWithDirection(request.x, request.y, request.direction)
            Toast.makeText(this, "Obstacle added at (${request.x}, ${request.y})", Toast.LENGTH_SHORT).show()
        }

        val btnLogClear: com.google.android.material.button.MaterialButton =
            findViewById(R.id.clear_logs_button)
        btnLogClear.setOnClickListener {
            clearMessageLog()
        }

        var autoActive = false

        val customNavigatorBar: customNavigator = customNavigator(
            supportFragmentManager,
            FragmentPagerAdapter.BEHAVIOR_RESUME_ONLY_CURRENT_FRAGMENT
        )

        // Day/Night theme toggle
        val btnThemeToggle = findViewById<com.google.android.material.button.MaterialButton>(R.id.btnThemeToggle)
        val rootContainer = findViewById<android.widget.LinearLayout>(R.id.container)
        val rightPanel = findViewById<android.widget.LinearLayout>(R.id.rightPanel)
        val gridArea = findViewById<androidx.constraintlayout.widget.ConstraintLayout>(R.id.constraintGridMapView)
        val subNavContainer = findViewById<android.widget.LinearLayout>(R.id.sub_navigation_container)
        val dpadCenter = findViewById<android.view.View>(R.id.dpadCenterSquare)

        val headerRow = findViewById<android.widget.LinearLayout>(R.id.headerRow)
        val bottomRow = findViewById<android.widget.LinearLayout>(R.id.bottomRow)
        val coordCard = findViewById<android.widget.LinearLayout>(R.id.coordCard)
        val statusCard = findViewById<android.widget.LinearLayout>(R.id.statusCard)
        val btnAddCoordinate = findViewById<android.widget.ImageButton>(R.id.btnAddCoordinate)
        val btnBluetooth = findViewById<android.widget.ImageButton>(R.id.btnBluetooth)
        val dpadUp = findViewById<android.widget.Button>(R.id.dpad_up)
        val dpadDown = findViewById<android.widget.Button>(R.id.dpad_down)
        val dpadLeft = findViewById<android.widget.Button>(R.id.dpad_left)
        val dpadRight = findViewById<android.widget.Button>(R.id.dpad_right)
        val reverseLeft = findViewById<android.widget.LinearLayout>(R.id.reverse_left_button)
        val reverseRight = findViewById<android.widget.LinearLayout>(R.id.reverse_right_button)
        val revLeftIcon = findViewById<android.widget.ImageView>(R.id.revLeftIcon)
        val revLeftLabel = findViewById<android.widget.TextView>(R.id.revLeftLabel)
        val revRightIcon = findViewById<android.widget.ImageView>(R.id.revRightIcon)
        val revRightLabel = findViewById<android.widget.TextView>(R.id.revRightLabel)
        val coordText = findViewById<android.widget.TextView>(R.id.give_vehicle_coord_now)
        val dirText = findViewById<android.widget.TextView>(R.id.give_vehicle_direction_now)
        val statusText = findViewById<android.widget.TextView>(R.id.give_vehicle_status_now)

        // Bottom bar buttons — declared here so applyTheme can reach them
        val btnGridSize = findViewById<MaterialButton>(R.id.btn_grid_size)
        val btnReset = findViewById<MaterialButton>(R.id.reset_map_button)
        val saveGridMapButton = findViewById<MaterialButton>(R.id.save_map_button)
        val loadGridMapButton = findViewById<MaterialButton>(R.id.load_map_button)
        val tabs = findViewById<TabLayout>(R.id.tabs)

        fun applyTheme(day: Boolean) {
            fun tint(color: String) = android.content.res.ColorStateList.valueOf(android.graphics.Color.parseColor(color))
            fun col(color: String) = android.graphics.Color.parseColor(color)
            if (day) {
                btnThemeToggle.text = "🌙"
                rootContainer.setBackgroundColor(col("#EDF1F7"))
                rightPanel.setBackgroundColor(col("#EDF1F7"))
                gridArea.setBackgroundColor(col("#EDF1F7"))
                subNavContainer.setBackgroundColor(col("#EDF1F7"))
                headerRow.backgroundTintList = tint("#D6DEF0")
                bottomRow.backgroundTintList = tint("#D6DEF0")
                coordCard.backgroundTintList = tint("#C2CEDF")
                statusCard.backgroundTintList = tint("#C2CEDF")
                btnAddCoordinate.backgroundTintList = tint("#C2CEDF")
                btnThemeToggle.backgroundTintList = tint("#C2CEDF")
                btnBluetooth.backgroundTintList = tint("#C2CEDF")
                // D-pad: darker blue bg so the navy arrow text pops
                dpadUp.backgroundTintList = tint("#7A9BBF")
                dpadDown.backgroundTintList = tint("#7A9BBF")
                dpadLeft.backgroundTintList = tint("#7A9BBF")
                dpadRight.backgroundTintList = tint("#7A9BBF")
                dpadUp.setTextColor(col("#FFFFFF"))
                dpadDown.setTextColor(col("#FFFFFF"))
                dpadLeft.setTextColor(col("#FFFFFF"))
                dpadRight.setTextColor(col("#FFFFFF"))
                reverseLeft.backgroundTintList = tint("#7A9BBF")
                reverseRight.backgroundTintList = tint("#7A9BBF")
                revLeftIcon.imageTintList = tint("#FFFFFF")
                revLeftLabel.setTextColor(col("#FFFFFF"))
                revRightIcon.imageTintList = tint("#FFFFFF")
                revRightLabel.setTextColor(col("#FFFFFF"))
                dpadCenter.backgroundTintList = tint("#C5D5E8")
                coordText.setTextColor(col("#1A2A4A"))
                dirText.setTextColor(col("#3A5A8A"))
                statusText.setTextColor(col("#1A2A4A"))
                // Bottom bar buttons: dark text on light bar
                val darkNavy = col("#1A2A4A")
                btnGridSize.setTextColor(darkNavy)
                btnReset.setTextColor(darkNavy)
                saveGridMapButton.setTextColor(darkNavy)
                loadGridMapButton.setTextColor(darkNavy)
                btnLogClear.setTextColor(darkNavy)
                // Tab bar: match panel background
                tabs.setBackgroundColor(col("#EDF1F7"))
                tabs.setSelectedTabIndicatorColor(col("#2563A8"))
                tabs.setTabIconTint(android.content.res.ColorStateList.valueOf(col("#1A2A4A")))
                updateAxisTextColor(false)
            } else {
                btnThemeToggle.text = "☀"
                rootContainer.setBackgroundColor(col("#0F1C3A"))
                rightPanel.setBackgroundColor(android.graphics.Color.TRANSPARENT)
                gridArea.setBackgroundColor(android.graphics.Color.TRANSPARENT)
                subNavContainer.setBackgroundColor(android.graphics.Color.TRANSPARENT)
                headerRow.backgroundTintList = tint("#182D4B")
                bottomRow.backgroundTintList = tint("#1E3A5F")
                coordCard.backgroundTintList = tint("#1A3A5C")
                statusCard.backgroundTintList = tint("#1A3A5C")
                btnAddCoordinate.backgroundTintList = tint("#1A3A5C")
                btnThemeToggle.backgroundTintList = tint("#1A3A5C")
                btnBluetooth.backgroundTintList = tint("#1A3A5C")
                dpadUp.backgroundTintList = tint("#1E4A65")
                dpadDown.backgroundTintList = tint("#1E4A65")
                dpadLeft.backgroundTintList = tint("#1E4A65")
                dpadRight.backgroundTintList = tint("#1E4A65")
                dpadUp.setTextColor(col("#4AD8F0"))
                dpadDown.setTextColor(col("#4AD8F0"))
                dpadLeft.setTextColor(col("#4AD8F0"))
                dpadRight.setTextColor(col("#4AD8F0"))
                reverseLeft.backgroundTintList = tint("#1E4A65")
                reverseRight.backgroundTintList = tint("#1E4A65")
                revLeftIcon.imageTintList = tint("#26B5CB")
                revLeftLabel.setTextColor(col("#4AD8F0"))
                revRightIcon.imageTintList = tint("#26B5CB")
                revRightLabel.setTextColor(col("#4AD8F0"))
                dpadCenter.backgroundTintList = tint("#2A4A6A")
                coordText.setTextColor(android.graphics.Color.WHITE)
                dirText.setTextColor(col("#7AAFCB"))
                statusText.setTextColor(android.graphics.Color.WHITE)
                // Bottom bar buttons: restore original light-blue text
                val cyanText = col("#26B5CB")
                val paleText = col("#A8C8E8")
                btnGridSize.setTextColor(paleText)
                btnReset.setTextColor(paleText)
                saveGridMapButton.setTextColor(paleText)
                loadGridMapButton.setTextColor(paleText)
                btnLogClear.setTextColor(paleText)
                // Tab bar: restore dark background
                tabs.setBackgroundColor(android.graphics.Color.TRANSPARENT)
                tabs.setSelectedTabIndicatorColor(col("#F137A5"))
                tabs.setTabIconTint(android.content.res.ColorStateList.valueOf(col("#FFFFFF")))
                updateAxisTextColor(true)
            }
        }

        btnThemeToggle.setOnClickListener {
            isDayMode = !isDayMode
            applyTheme(isDayMode)
        }

        // Initializes gridmap
        val gridMapView = findViewById<LinearLayout>(R.id.gridMapView)
        gridMapObj = GridMapClass(this)
        gridMapObj.setGridColumns(20)
        gridMapObj.setGridRows(20)
        gridMapView.addView(gridMapObj)
        setupGraphAxes(this, true)

        // D-pad buttons
        dpadUp.setOnClickListener {
            activateJoyStickBool = true
            gridMapObj.moveVehicleStraight(ObstacleData.Direction.NORTH, true)
        }
        dpadDown.setOnClickListener {
            activateJoyStickBool = true
            gridMapObj.moveVehicleStraight(ObstacleData.Direction.SOUTH, true)
        }
        dpadLeft.setOnClickListener {
            activateJoyStickBool = true
            gridMapObj.moveVehicleStraight(ObstacleData.Direction.WEST, true)
        }
        dpadRight.setOnClickListener {
            activateJoyStickBool = true
            gridMapObj.moveVehicleStraight(ObstacleData.Direction.EAST, true)
        }

        reverseLeft.setOnClickListener {
            Log.d("JoystickButtons", "Reverse Left clicked")
            gridMapObj.reverseLeftVehicle(true)
        }

        reverseRight.setOnClickListener {
            Log.d("JoystickButtons", "Reverse Right clicked")
            gridMapObj.reverseRightVehicle(true)
        }

        saveGridMapButton.setOnClickListener {
            saveGridMapData(gridMapObj.returnGridMap())
        }

        loadGridMapButton.setOnClickListener {
            loadGridMapData()
        }

        btnGridSize.setOnClickListener {
            showGridSizeDialog()
        }

        btnReset.setOnClickListener {
            gridMapObj.clearGridMap()
        }

        // Initalize navigation tabz
        customNavigatorBar.addFragment(AddObstacle(gridMapObj), "")
        customNavigatorBar.addFragment(commsToRobot(gridMapObj), "")
        customNavigatorBar.addFragment(startTask(gridMapObj), "")

        // Initializes Navigation Bar
        val subNavigationBar = findViewById<ViewPager?>(R.id.sub_navigation_bar)
        subNavigationBar?.setAdapter(customNavigatorBar)
        subNavigationBar?.setOffscreenPageLimit(2)
        tabs.setupWithViewPager(subNavigationBar)

        tabs.getTabAt(0)?.setIcon(R.drawable.plus_for_enter)
        tabs.getTabAt(1)?.setIcon(R.drawable.send_message)
        tabs.getTabAt(2)?.setIcon(R.drawable.ic_dashboard_black_24dp)
    }

    override fun onStart() {
        super.onStart()
        Intent(this, BluetoothService::class.java).also { intent ->
            bindService(intent, connection, Context.BIND_AUTO_CREATE)
        }

        LocalBroadcastManager.getInstance(this).registerReceiver(
            connStateReceiver,
            IntentFilter(BluetoothService.ACTION_CONN_STATE)
        )

        LocalBroadcastManager.getInstance(this).registerReceiver(
            msgReceiver,
            IntentFilter(BluetoothService.ACTION_MESSAGE)
        )
    }

    override fun onStop() {
        super.onStop()
        // Unbind from the service
        if (isBound) {
            unbindService(connection)
            isBound = false
        }

        // Unregister broadcast receivers
        LocalBroadcastManager.getInstance(this).unregisterReceiver(connStateReceiver)
        LocalBroadcastManager.getInstance(this).unregisterReceiver(msgReceiver)
        autoHandler.removeCallbacks(autoRunnable)
    }

    private fun checkBluetoothPermissionsAndState() {
        val bluetoothScanPermission = ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED
        val bluetoothConnectPermission = ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED

        if (bluetoothScanPermission && bluetoothConnectPermission) {
            checkBluetoothEnabled()
        } else {
            requestBluetoothPermissionLauncher.launch(
                arrayOf(
                    Manifest.permission.BLUETOOTH_SCAN,
                    Manifest.permission.BLUETOOTH_CONNECT
                )
            )
        }
    }

    private fun checkBluetoothEnabled() {
        if (isBound && bluetoothService?.isBluetoothEnabled() == true) {
            showBluetoothFragment()
        } else {
            val enableBtIntent = Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE)
            bluetoothEnableLauncher.launch(enableBtIntent)
        }
    }

    private fun showBluetoothFragment() {
        BluetoothFragment().show(supportFragmentManager, "BluetoothFragment")
    }

    private fun showAddCoordinatesFragment() {
        PlaceObstacleDialogFragment().show(supportFragmentManager, "PlaceObstacleDialog")
    }

    private fun updateBluetoothStatus() {
        if (isConnected) {
            bluetoothStatus.setImageResource(R.drawable.ic_status_connected_24dp)
            bluetoothStatus.backgroundTintList = ContextCompat.getColorStateList(this, R.color.status_connected)
        } else {
            bluetoothStatus.setImageResource(R.drawable.ic_status_disconnected_24dp)
            bluetoothStatus.backgroundTintList = ContextCompat.getColorStateList(this, R.color.status_disconnected)
        }
    }
    fun clearMessageLog() {
        messageLog.clear()
        messageListener?.onLogCleared() // Notify listener to clear the displayed text
        Toast.makeText(this, "Bluetooth message log cleared", Toast.LENGTH_SHORT).show()
    }

    override fun onResume() {
        super.onResume()
        handler.post(updateTask) // Start updating when activity is visible
    }

    override fun onPause() {
        super.onPause()
        handler.removeCallbacks(updateTask) // Stop updating when activity is hidden
    }

    fun setupGraphAxes(context: Context, isDark: Boolean) {
        val yAxis = findViewById<LinearLayout>(R.id.y_axis_numbers)
        val xAxis = findViewById<LinearLayout>(R.id.x_axis_numbers)
        val axisColor = if (isDark) Color.WHITE else Color.BLACK
        val rows = gridMapObj.getGridRows()
        val cols = gridMapObj.getGridColumns()

        for (i in (rows - 1) downTo 0) {
            val textView = TextView(context)
            textView.text = i.toString()
            textView.setTextColor(axisColor)
            textView.gravity = Gravity.CENTER
            textView.layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                0, 1f
            )
            yAxis.addView(textView)
        }

        for (i in 0 until cols) {
            val textView = TextView(context)
            textView.text = i.toString()
            textView.setTextColor(axisColor)
            textView.gravity = Gravity.CENTER
            textView.layoutParams = LinearLayout.LayoutParams(
                0,
                LinearLayout.LayoutParams.MATCH_PARENT, 1f
            )
            xAxis.addView(textView)
        }
    }

    private fun updateAxisTextColor(isDark: Boolean) {
        val axisColor = if (isDark) Color.WHITE else Color.BLACK
        val yAxis = findViewById<LinearLayout>(R.id.y_axis_numbers)
        val xAxis = findViewById<LinearLayout>(R.id.x_axis_numbers)
        for (i in 0 until yAxis.childCount) {
            (yAxis.getChildAt(i) as? TextView)?.setTextColor(axisColor)
        }
        for (i in 0 until xAxis.childCount) {
            (xAxis.getChildAt(i) as? TextView)?.setTextColor(axisColor)
        }
    }
    private fun showGridSizeDialog() {
        val dialogView = layoutInflater.inflate(R.layout.dialog_grid_size, null)
        val colPicker = dialogView.findViewById<NumberPicker>(R.id.picker_cols)
        val rowPicker = dialogView.findViewById<NumberPicker>(R.id.picker_rows)
        colPicker.minValue = 5; colPicker.maxValue = 20; colPicker.value = gridMapObj.getGridColumns()
        rowPicker.minValue = 5; rowPicker.maxValue = 20; rowPicker.value = gridMapObj.getGridRows()
        AlertDialog.Builder(this)
            .setTitle("Arena Size")
            .setView(dialogView)
            .setPositiveButton("Apply") { _, _ ->
                applyGridSize(colPicker.value, rowPicker.value)
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun applyGridSize(cols: Int, rows: Int) {
        gridMapObj.setGridColumns(cols)
        gridMapObj.setGridRows(rows)
        gridMapObj.clearGridMap()

        // Keep the grid view filling the full area with the correct col:row ratio
        // so cells are always as large as possible without overflowing.
        val gridView = findViewById<LinearLayout>(R.id.gridMapView)
        val params = gridView.layoutParams as androidx.constraintlayout.widget.ConstraintLayout.LayoutParams
        params.width = 0
        params.height = 0
        params.dimensionRatio = "$cols:$rows"
        gridView.layoutParams = params
        gridView.requestLayout()

        val yAxis = findViewById<LinearLayout>(R.id.y_axis_numbers)
        val xAxis = findViewById<LinearLayout>(R.id.x_axis_numbers)
        yAxis.removeAllViews()
        xAxis.removeAllViews()
        setupGraphAxes(this, !isDayMode)
    }

    private fun readMapPresets(): List<MapPreset>? {
        val preferences = getSharedPreferences("grid_map_prefs", MODE_PRIVATE)
        return try {
            val json = preferences.getString("mapPresets", null)
            val presets = MapPresets.decode(json, preferences.getString("gridMapData", null))
            if (json == null && presets.isNotEmpty()) writeMapPresets(presets)
            presets
        } catch (e: Exception) {
            Log.e("MapPresets", "Unable to read saved maps", e)
            Toast.makeText(this, "Unable to read saved maps", Toast.LENGTH_SHORT).show()
            null
        }
    }

    private fun writeMapPresets(presets: List<MapPreset>) {
        getSharedPreferences("grid_map_prefs", MODE_PRIVATE).edit()
            .putString("mapPresets", Gson().toJson(presets)).apply()
    }

    private fun saveGridMapData(gridMapData: ArrayList<ArrayList<ObstacleData>>) {
        val presets = readMapPresets() ?: return
        // Capture a snapshot, so editing or loading the grid cannot alter a saved preset.
        val gridJson = Gson().toJson(gridMapData)
        val columns = gridMapObj.getGridColumns()
        val rows = gridMapObj.getGridRows()
        val nameInput = EditText(this).apply {
            hint = "Map name"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_SENTENCES
            setSingleLine(true)
            filters = arrayOf(InputFilter.LengthFilter(MapPresets.MAX_NAME_LENGTH))
        }
        val container = LinearLayout(this).apply {
            setPadding(mapPickerDp(24), mapPickerDp(8), mapPickerDp(24), 0)
            addView(nameInput, LinearLayout.LayoutParams(-1, -2))
        }
        val dialog = AlertDialog.Builder(this)
            .setTitle("Save map as")
            .setMessage("${presets.size}/${MapPresets.LIMIT} presets saved")
            .setView(container)
            .setPositiveButton("Save", null)
            .setNegativeButton("Cancel", null)
            .create()
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val name = nameInput.text.toString().trim()
                if (name.isEmpty()) {
                    nameInput.error = "Enter a map name"
                    return@setOnClickListener
                }
                val current = readMapPresets() ?: return@setOnClickListener
                val existing = current.firstOrNull { it.name.equals(name, ignoreCase = true) }
                if (existing == null && current.size >= MapPresets.LIMIT) {
                    nameInput.error = "All 10 slots are full. Use an existing name to replace a map, or delete one from Load."
                    return@setOnClickListener
                }
                val save = {
                    val latest = readMapPresets()
                    if (latest != null) {
                        val updated = MapPresets.save(latest, MapPreset(name, columns, rows, gridJson))
                        writeMapPresets(updated)
                        Toast.makeText(this, "Saved as \"$name\"", Toast.LENGTH_SHORT).show()
                        dialog.dismiss()
                    }
                }
                if (existing != null) {
                    AlertDialog.Builder(this)
                        .setTitle("Replace \"${existing.name}\"?")
                        .setMessage("Replace this preset with the current map?")
                        .setPositiveButton("Replace") { _, _ -> save() }
                        .setNegativeButton("Cancel", null)
                        .show()
                } else save()
            }
        }
        dialog.show()
    }

    private fun loadGridMapData() {
        val presets = readMapPresets() ?: return
        val sheet = BottomSheetDialog(this)
        val textColor = if (isDayMode) Color.parseColor("#102A43") else Color.WHITE
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(mapPickerDp(20), mapPickerDp(16), mapPickerDp(20), mapPickerDp(20))
            setBackgroundColor(if (isDayMode) Color.WHITE else Color.parseColor("#102A43"))
        }
        content.addView(TextView(this).apply {
            text = "Load map (${presets.size}/${MapPresets.LIMIT})"
            textSize = 20f
            setTextColor(textColor)
            setPadding(0, 0, 0, mapPickerDp(12))
        })
        if (presets.isEmpty()) {
            content.addView(TextView(this).apply {
                text = "No saved maps yet. Use Save to create a preset."
                setTextColor(textColor)
                setPadding(0, mapPickerDp(16), 0, mapPickerDp(16))
            })
        } else {
            val list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
            presets.forEach { preset ->
                val row = LinearLayout(this).apply {
                    orientation = LinearLayout.HORIZONTAL
                    gravity = Gravity.CENTER_VERTICAL
                }
                val loadButton = MaterialButton(this).apply {
                    text = "${preset.name}  ·  ${preset.columns} × ${preset.rows}"
                    isAllCaps = false
                    maxLines = 2
                    contentDescription = "Load ${preset.name}"
                    setOnClickListener {
                        try {
                            val data = MapPresets.restoreGrid(preset)
                            applyGridSize(preset.columns, preset.rows)
                            gridMapObj.addGridMapSaved(data)
                            gridMapObj.invalidate()
                            Toast.makeText(this@MainActivity, "Loaded \"${preset.name}\"", Toast.LENGTH_SHORT).show()
                            sheet.dismiss()
                        } catch (e: Exception) {
                            Log.e("MapPresets", "Unable to load ${preset.name}", e)
                            Toast.makeText(this@MainActivity, "Unable to load this map", Toast.LENGTH_SHORT).show()
                        }
                    }
                }
                row.addView(loadButton, LinearLayout.LayoutParams(0, -2, 1f))
                row.addView(MaterialButton(this, null, com.google.android.material.R.attr.borderlessButtonStyle).apply {
                    text = "Delete"
                    isAllCaps = false
                    setTextColor(if (isDayMode) Color.parseColor("#B71C1C") else Color.parseColor("#FF8A80"))
                    contentDescription = "Delete ${preset.name}"
                    setOnClickListener {
                        AlertDialog.Builder(this@MainActivity)
                            .setTitle("Delete \"${preset.name}\"?")
                            .setMessage("This removes the saved preset. Your current map stays on screen.")
                            .setPositiveButton("Delete") { _, _ ->
                                val current = readMapPresets()
                                if (current != null) {
                                    writeMapPresets(current.filterNot { it.name == preset.name })
                                    sheet.dismiss()
                                    loadGridMapData()
                                }
                            }
                            .setNegativeButton("Cancel", null)
                            .show()
                    }
                }, LinearLayout.LayoutParams(-2, -2))
                list.addView(row)
            }
            val scroll = ScrollView(this).apply { addView(list); isFillViewport = true }
            val listHeight = minOf(mapPickerDp(presets.size * 64), (resources.displayMetrics.heightPixels * 0.55).toInt())
            content.addView(scroll, LinearLayout.LayoutParams(-1, listHeight))
        }
        content.addView(MaterialButton(this).apply {
            text = "Close"
            setOnClickListener { sheet.dismiss() }
        }, LinearLayout.LayoutParams(-1, -2))
        sheet.setContentView(content)
        sheet.show()
    }

    private fun mapPickerDp(value: Int): Int = (value * resources.displayMetrics.density).toInt()

}


internal data class MapPreset(val name: String, val columns: Int, val rows: Int, val gridJson: String)

/** Named snapshots stored in the existing grid_map_prefs preferences. */
internal object MapPresets {
    const val LIMIT = 10
    const val MAX_NAME_LENGTH = 40

    fun decode(json: String?, legacyGridJson: String?): List<MapPreset> {
        val presets: List<MapPreset> = if (json != null) {
            Gson().fromJson(json, object : TypeToken<List<MapPreset>>() {}.type)
                ?: throw IllegalArgumentException("Invalid presets")
        } else if (legacyGridJson != null) {
            listOf(MapPreset("Saved Map", 20, 20, legacyGridJson))
        } else emptyList()
        require(presets.size <= LIMIT)
        require(presets.all { it.name.isNotBlank() && it.name.length <= MAX_NAME_LENGTH &&
            it.columns in 5..20 && it.rows in 5..20 && it.gridJson.isNotBlank() })
        require(presets.map { it.name.lowercase(java.util.Locale.ROOT) }.distinct().size == presets.size)
        return presets
    }

    fun restoreGrid(preset: MapPreset): ArrayList<ArrayList<ObstacleData>> {
        val type = object : TypeToken<ArrayList<ArrayList<ObstacleData>>>() {}.type
        val saved: ArrayList<ArrayList<ObstacleData>> = Gson().fromJson(preset.gridJson, type)
            ?: throw IllegalArgumentException("Saved map is empty")
        require(saved.all { row -> row.size == 20 && row.all {
            it.direction != null && it.obstacleType != null
        } }) { "Saved map contains invalid cells" }
        // Older GridMapClass constructors initialized twice. The second set of
        // 20 rows was never displayed or edited; preserve the first 20 rows.
        val legacyDoubleGrid = saved.size == 40 && saved.drop(20).all { row ->
            row.all { !it.occupied && it.obstacleType == ObstacleData.OBSTACLETYPE.EMPTY }
        }
        require(saved.size == 20 || legacyDoubleGrid) { "Saved map has an unsupported layout" }
        return ArrayList(saved.take(20))
    }

    fun save(presets: List<MapPreset>, preset: MapPreset): List<MapPreset> {
        val normalized = preset.copy(name = preset.name.trim())
        require(normalized.name.isNotEmpty() && normalized.name.length <= MAX_NAME_LENGTH)
        val index = presets.indexOfFirst { it.name.equals(normalized.name, ignoreCase = true) }
        require(index >= 0 || presets.size < LIMIT) { "All 10 preset slots are full" }
        return presets.toMutableList().apply {
            if (index >= 0) this[index] = normalized else add(normalized)
        }
    }
}
