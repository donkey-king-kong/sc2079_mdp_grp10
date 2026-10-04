package com.example.sc2079

import androidx.core.view.WindowCompat
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
import android.util.TypedValue
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
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import androidx.fragment.app.FragmentPagerAdapter
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import androidx.viewpager.widget.ViewPager
import com.example.sc2079.databinding.ActivityMainBinding
import com.example.sc2079.service.BluetoothService
import com.example.sc2079.ui.bluetooth.BluetoothFragment
import com.google.android.material.tabs.TabLayout
import com.google.gson.Gson
import com.google.gson.reflect.TypeToken
import android.util.Base64
import com.example.sc2079.ui.coordinates.PlaceObstacleDialogFragment
import com.example.sc2079.ui.coordinates.SharedViewModel
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import com.example.sc2079.ui.DAY
import com.example.sc2079.ui.NIGHT
import com.example.sc2079.ui.Palette
import com.example.sc2079.ui.ThemeAware
import com.example.sc2079.ui.box
import java.io.IOException

class MainActivity : AppCompatActivity() {
    enum class ChatLogType {
        INCOMING,
        OUTGOING,
        SYSTEM
    }

    data class ChatLogEntry @JvmOverloads constructor(
        val type: ChatLogType,
        val message: String,
        val timestamp: Long = System.currentTimeMillis(),
        val imageUri: android.net.Uri? = null
    )

    private val base64Data = StringBuilder();
    private var iterationHowMany: Int = -1;
    private var bluetoothService: BluetoothService? = null
    private var isBound = false
    private lateinit var binding: ActivityMainBinding
    private lateinit var btnBluetooth: ImageButton
    private lateinit var bluetoothStatus: ImageView
    private var activateJoyStickBool = false
    private var isDayMode = false

    private val sharedViewModel: SharedViewModel by viewModels()

    private var isConnected = false
    internal lateinit var gridMapObj: GridMapClass
    private var messageListener: MessageListener? = null

    fun currentGridMapOrNull(): GridMapClass? =
        if (::gridMapObj.isInitialized) gridMapObj else null

    interface MessageListener {
        fun onNewMessage(entry: ChatLogEntry)
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

    fun currentPalette(): Palette = if (isDayMode) DAY else NIGHT

    fun getMessageLog(): ArrayList<ChatLogEntry> {
        return sharedViewModel.messageLog
    }

    fun setMessageListener(listener: MessageListener?) {
        this.messageListener = listener
    }

    fun logIncoming(message: String) {
        val entry = ChatLogEntry(ChatLogType.INCOMING, message)
        sharedViewModel.messageLog.add(entry)
        messageListener?.onNewMessage(entry)
    }

    fun logOutgoing(message: String) {
        val entry = ChatLogEntry(ChatLogType.OUTGOING, message)
        sharedViewModel.messageLog.add(entry)
        messageListener?.onNewMessage(entry)
    }

    fun logSystem(message: String) {
        val entry = ChatLogEntry(ChatLogType.SYSTEM, message)
        sharedViewModel.messageLog.add(entry)
        messageListener?.onNewMessage(entry)
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

            if(iterationHowMany == -1){
                logIncoming(text)
            }

            if(text.contains("stitch-image")) {
                val status = gridMapObj.receiveStichImageMessageBluetooth(text);
                when (status) {
                    "-1" -> {
                        base64Data.clear()
                        iterationHowMany = -1
                        logSystem("Invalid stitched image message received")
                    }
                    "2" -> {
                        logSystem("Starting to Stitch")
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
                        logSystem("Ending Stitch, displaying image")
                        Log.d("Image Message", "Final length: ${stitchedImageBase64.length}")
                        try {
                            val savedUri = saveStitchedImageToGallery(stitchedImageBase64)
                            val entry = ChatLogEntry(
                                type = ChatLogType.SYSTEM,
                                message = "Image Received - Tap to view",
                                imageUri = savedUri
                            )
                            sharedViewModel.messageLog.add(entry)
                            messageListener?.onNewMessage(entry)
                            logSystem("Saved stitched image to Gallery")
                            Toast.makeText(
                                this@MainActivity,
                                "Stitched image saved to Gallery",
                                Toast.LENGTH_SHORT
                            ).show()
                            Log.d("Image Message", "Saved stitched image to $savedUri")
                        } catch (e: Exception) {
                            logSystem("Failed to save stitched image to Gallery")
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
                        logSystem("Running data compilation iteration $iterationHowMany")
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
                        logSystem("Bullseye Detected")
                    }
                    -2 ->{
                        //Toast.makeText(context, "Unknown Error Occurred", Toast.LENGTH_SHORT).show();
                        logSystem("Unknown Error Occurred at image-rec")
                    }
                    -1 ->{
                        //Toast.makeText(context, "No Image ID Detected", Toast.LENGTH_SHORT).show();
                        logSystem("No Image ID Detected")
                    }
                    0 ->{
                        //Toast.makeText(context, "Failed to verify Obstacle", Toast.LENGTH_SHORT).show();
                        logSystem("Failed to verify Obstacle")
                    }
                    1->{
                        //Toast.makeText(context, "Successfully Verified Obstacle", Toast.LENGTH_SHORT).show();
                        logSystem("Successfully Verified Obstacle")
                    }
                    2->{
                        //Toast.makeText(context, "Capturing Obstacle Image", Toast.LENGTH_SHORT).show();
                        logSystem("Capturing Obstacle Image")
                    }

                }
            }

            if(text.contains("location")) {
                val status = gridMapObj.receiveLocationMessageBluetooth(text);
                when (status) {
                    -2 -> {
                        //Toast.makeText(context, "Unknown Error Occurred", Toast.LENGTH_SHORT).show();
                        logSystem("Unknown Error Occurred at location")
                    }
                    0 -> {
                        //Toast.makeText(context, "Failed to verify Location", Toast.LENGTH_SHORT).show();
                        logSystem("Failed to verify Location")
                    }

                    1 -> {
                        //Toast.makeText(context, "Successfully Verified Location", Toast.LENGTH_SHORT).show();
                        logSystem("Successfully Verified Location")
                    }
                }
            }


            if(text.contains("health")){
                val status = gridMapObj.receiveHealthMessageBluetooth(text);
                when (status) {
                    -2 -> {
                        //Toast.makeText(context, "Unknown Error Occurred", Toast.LENGTH_SHORT).show();
                        logSystem("Unknown Error Occurred at health")
                    }
                    0 ->{
                        //Toast.makeText(context, "Image Rec API is down", Toast.LENGTH_SHORT).show();
                        logSystem("Image Rec API is down")
                    }
                    1 ->{
                        //Toast.makeText(context, "Algo API is down", Toast.LENGTH_SHORT).show();
                        logSystem("Algo API is down")
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

        var overlayReady = false
        val splash = installSplashScreen()
        splash.setKeepOnScreenCondition { !overlayReady }

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        // Apply system bar insets as padding on the root container (targetSdk 36 enforces edge-to-edge)
        val container = binding.root
        ViewCompat.setOnApplyWindowInsetsListener(container) { v, insets ->
            val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
            v.setPadding(bars.left, bars.top, bars.right, bars.bottom)
            insets
        }

        if (savedInstanceState == null) {
            val overlay = layoutInflater.inflate(R.layout.activity_splash_overlay, null)
            val decorView = window.decorView as android.view.ViewGroup
            decorView.addView(overlay, android.view.ViewGroup.LayoutParams(
                android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                android.view.ViewGroup.LayoutParams.MATCH_PARENT
            ))
            overlayReady = true
            overlay.postDelayed({
                overlay.animate().alpha(0f).setDuration(300).withEndAction {
                    decorView.removeView(overlay)
                }.start()
            }, 1500)
        } else {
            overlayReady = true
        }

        btnBluetooth = findViewById(R.id.btnBluetooth)
        bluetoothStatus = findViewById(R.id.bluetoothStatus)

        updateBluetoothStatus()

        btnBluetooth.setOnClickListener {
            checkBluetoothPermissionsAndState()
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

        val headerRow = findViewById<android.widget.LinearLayout>(R.id.headerRow)
        val bottomRow = findViewById<android.widget.LinearLayout>(R.id.bottomRow)
        val coordCard = findViewById<android.widget.LinearLayout>(R.id.coordCard)
        val statusCard = findViewById<android.widget.LinearLayout>(R.id.statusCard)
        val btnBluetooth = findViewById<android.widget.ImageButton>(R.id.btnBluetooth)
        val coordText = findViewById<android.widget.TextView>(R.id.give_vehicle_coord_now)
        val dirText = findViewById<android.widget.TextView>(R.id.give_vehicle_direction_now)
        val statusText = findViewById<android.widget.TextView>(R.id.give_vehicle_status_now)

        // Bottom bar buttons
        val btnGridMinus = findViewById<androidx.appcompat.widget.AppCompatButton>(R.id.btn_grid_minus)
        val btnGridPlus = findViewById<androidx.appcompat.widget.AppCompatButton>(R.id.btn_grid_plus)
        val txtGridSize = findViewById<android.widget.TextView>(R.id.txt_grid_size)
        val txtGridSizeLabel = findViewById<android.widget.TextView>(R.id.txt_grid_size_label)
        val btnReset = findViewById<androidx.appcompat.widget.AppCompatButton>(R.id.reset_map_button)
        val saveGridMapButton = findViewById<androidx.appcompat.widget.AppCompatButton>(R.id.save_map_button)
        val loadGridMapButton = findViewById<androidx.appcompat.widget.AppCompatButton>(R.id.load_map_button)
        val tabs = findViewById<TabLayout>(R.id.tabs)

        // Load persisted theme preference (default: day mode)
        val uiPrefs = getSharedPreferences("ui_prefs", MODE_PRIVATE)
        isDayMode = uiPrefs.getBoolean("day_mode", true)

        fun applyTheme(day: Boolean) {
            val p = if (day) DAY else NIGHT
            WindowCompat.getInsetsController(window, window.decorView).isAppearanceLightStatusBars = day
            btnThemeToggle.text = if (day) "🌙" else "☀"
            // Container backgrounds
            rootContainer.setBackgroundColor(p.panel)
            headerRow.setBackgroundColor(p.panel)
            bottomRow.setBackgroundColor(p.panel)
            rightPanel.setBackgroundColor(p.panel)
            subNavContainer.setBackgroundColor(p.panel)
            gridArea.setBackgroundColor(p.bg)
            // coordCard as rounded box
            coordCard.background = box(this, p.surface2, p.borderStrong, 6f)
            coordCard.backgroundTintList = null
            coordText.setTextColor(p.text)
            dirText.setTextColor(p.text)
            statusText.setTextColor(p.text)
            // Theme toggle — MaterialButton: use tint APIs, NOT setBackground
            val dp2px: (Float) -> Int = { dp -> TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, dp, resources.displayMetrics).toInt() }
            btnThemeToggle.backgroundTintList = android.content.res.ColorStateList.valueOf(p.surface2)
            btnThemeToggle.strokeColor = android.content.res.ColorStateList.valueOf(p.borderStrong)
            btnThemeToggle.strokeWidth = dp2px(2f)
            btnThemeToggle.cornerRadius = dp2px(8f)
            btnThemeToggle.setTextColor(p.text)
            // Bluetooth button
            btnBluetooth.background = box(this, p.surface2, p.borderStrong, 8f)
            btnBluetooth.backgroundTintList = null
            // Grid size controls
            btnGridMinus.background = box(this, p.surface2, p.borderStrong, 8f)
            btnGridMinus.backgroundTintList = null
            btnGridMinus.setTextColor(p.text)
            btnGridPlus.background = box(this, p.surface2, p.borderStrong, 8f)
            btnGridPlus.backgroundTintList = null
            btnGridPlus.setTextColor(p.text)
            txtGridSize.setTextColor(p.text)
            txtGridSizeLabel.setTextColor(p.textMuted)
            // Map action buttons
            btnReset.background = box(this, p.surface2, p.borderStrong, 8f)
            btnReset.backgroundTintList = null
            btnReset.setTextColor(p.text)
            saveGridMapButton.background = box(this, p.surface2, p.borderStrong, 8f)
            saveGridMapButton.backgroundTintList = null
            saveGridMapButton.setTextColor(p.text)
            loadGridMapButton.background = box(this, p.surface2, p.borderStrong, 8f)
            loadGridMapButton.backgroundTintList = null
            loadGridMapButton.setTextColor(p.text)
            // Tabs
            tabs.setBackgroundColor(p.panel)
            tabs.setSelectedTabIndicatorColor(p.accent)
            tabs.setTabTextColors(p.textMuted, p.accent)
            val tabIconTint = android.content.res.ColorStateList(
                arrayOf(
                    intArrayOf(android.R.attr.state_selected),
                    intArrayOf()
                ),
                intArrayOf(p.accent, p.textMuted)
            )
            tabs.setTabIconTint(tabIconTint)
            // Axis numbers
            updateAxisTextColor(!day)
            // Dispatch to ThemeAware fragments
            supportFragmentManager.fragments.forEach { frag ->
                if (frag is ThemeAware) frag.applyTheme(p)
            }
        }

        btnThemeToggle.setOnClickListener {
            isDayMode = !isDayMode
            uiPrefs.edit().putBoolean("day_mode", isDayMode).apply()
            applyTheme(isDayMode)
        }

        // Initializes gridmap
        val gridMapView = findViewById<LinearLayout>(R.id.gridMapView)
        gridMapObj = GridMapClass(this)
        gridMapObj.setGridColumns(20)
        gridMapObj.setGridRows(20)
        gridMapView.addView(gridMapObj)

        val gridMapSnapshot = sharedViewModel.gridMapSnapshot
        if (gridMapSnapshot != null) {
            if (sharedViewModel.gridRows != null && sharedViewModel.gridCols != null) {
                gridMapObj.setGridRows(sharedViewModel.gridRows!!)
                gridMapObj.setGridColumns(sharedViewModel.gridCols!!)
            }
            val type = object : TypeToken<ArrayList<ArrayList<ObstacleData>>>() {}.type
            val loadedData: ArrayList<ArrayList<ObstacleData>> = Gson().fromJson(gridMapSnapshot, type)
            gridMapObj.addGridMapSaved(loadedData)
        }

        gridMapObj.setOnGridChangedListener {
            snapshotGridToViewModel()
        }

        sharedViewModel.newCoordinate.observe(this) { coordinate ->
            gridMapObj.addNewObstacleToGrid(coordinate.first.toInt(), coordinate.second.toInt())
        }

        sharedViewModel.newObstacleRequest.observe(this) { request ->
            gridMapObj.addNewObstacleToGridWithDirection(request.x, request.y, request.direction)
            Toast.makeText(this, "Obstacle added at (${request.x}, ${request.y})", Toast.LENGTH_SHORT).show()
        }

        setupGraphAxes(this, !isDayMode)

        // Grid size +/- buttons (min=5, max=20, square grid so cols==rows)
        fun updateGridSizeLabel() {
            txtGridSize.text = gridMapObj.getGridColumns().toString()
        }
        btnGridMinus.setOnClickListener {
            val current = gridMapObj.getGridColumns()
            if (current > 5) applyGridSize(current - 1, current - 1)
            updateGridSizeLabel()
        }
        btnGridPlus.setOnClickListener {
            val current = gridMapObj.getGridColumns()
            if (current < 20) applyGridSize(current + 1, current + 1)
            updateGridSizeLabel()
        }

        saveGridMapButton.setOnClickListener {
            saveGridMapData(gridMapObj.returnGridMap())
        }

        loadGridMapButton.setOnClickListener {
            loadGridMapData()
        }

        btnReset.setOnClickListener {
            gridMapObj.clearGridMap()
        }

        // Initalize navigation tabz
        customNavigatorBar.addFragment(AddObstacle(gridMapObj), "Place")
        customNavigatorBar.addFragment(commsToRobot(gridMapObj), "Chat")
        customNavigatorBar.addFragment(startTask(gridMapObj), "Panels")

        // Initializes Navigation Bar
        val subNavigationBar = findViewById<ViewPager?>(R.id.sub_navigation_bar)
        subNavigationBar?.setAdapter(customNavigatorBar)
        subNavigationBar?.setOffscreenPageLimit(2)
        tabs.setupWithViewPager(subNavigationBar)

        tabs.getTabAt(0)?.setIcon(R.drawable.plus_for_enter)
        tabs.getTabAt(1)?.setIcon(R.drawable.send_message)
        tabs.getTabAt(2)?.setIcon(R.drawable.ic_dashboard_black_24dp)

        // Apply persisted theme on startup
        applyTheme(isDayMode)

        // Bind to BluetoothService here so rotation (onStop/onStart) does not unbind it
        Intent(this, BluetoothService::class.java).also { intent ->
            bindService(intent, connection, Context.BIND_AUTO_CREATE)
        }
    }

    override fun onStart() {
        super.onStart()
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
        sharedViewModel.messageLog.clear()
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

    override fun onDestroy() {
        super.onDestroy()
        if (isBound) {
            unbindService(connection)
            isBound = false
        }
    }

    fun setupGraphAxes(context: Context, isDark: Boolean) {
        val yAxis = findViewById<LinearLayout>(R.id.y_axis_numbers)
        val xAxis = findViewById<LinearLayout>(R.id.x_axis_numbers)
        val axisColor = if (isDark) NIGHT.textMuted else DAY.textMuted
        val rows = gridMapObj.getGridRows()
        val cols = gridMapObj.getGridColumns()
        val axisTextSizeSp = if (resources.configuration.orientation == android.content.res.Configuration.ORIENTATION_LANDSCAPE) 8f else 10f

        for (i in (rows - 1) downTo 0) {
            val textView = TextView(context)
            textView.text = i.toString()
            textView.setTextColor(axisColor)
            textView.textSize = axisTextSizeSp
            textView.gravity = Gravity.CENTER
            textView.includeFontPadding = false
            textView.maxLines = 1
            textView.setHorizontallyScrolling(true)
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
            textView.textSize = axisTextSizeSp
            textView.gravity = Gravity.CENTER
            textView.includeFontPadding = false
            textView.maxLines = 1
            textView.setHorizontallyScrolling(true)
            textView.layoutParams = LinearLayout.LayoutParams(
                0,
                LinearLayout.LayoutParams.MATCH_PARENT, 1f
            )
            xAxis.addView(textView)
        }
    }

    private fun updateAxisTextColor(isDark: Boolean) {
        val axisColor = if (isDark) NIGHT.textMuted else DAY.textMuted
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
        // kept for reference; no longer called from UI
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

    private fun snapshotGridToViewModel() {
        val gson = Gson()
        val json = gson.toJson(gridMapObj.returnGridMap())
        sharedViewModel.gridMapSnapshot = json
        sharedViewModel.gridRows = gridMapObj.getGridRows()
        sharedViewModel.gridCols = gridMapObj.getGridColumns()
    }

    private fun saveGridMapData(gridMapData : ArrayList<ArrayList<ObstacleData>>) {
        val sharedPreferences = getSharedPreferences("grid_map_prefs", MODE_PRIVATE)
        val editor = sharedPreferences.edit()

        val gson = Gson()
        val json = gson.toJson(gridMapData) // convert to JSON string

        editor.putString("gridMapData", json)
        editor.apply()
        Toast.makeText(this, "Map was successfully saved!", Toast.LENGTH_SHORT).show()
    }

    private fun loadGridMapData() {
        val sharedPreferences = getSharedPreferences("grid_map_prefs", MODE_PRIVATE)
        val gson = Gson()
        val json = sharedPreferences.getString("gridMapData", null)

        if (json != null) {
            val type = object : TypeToken<ArrayList<ArrayList<ObstacleData>>>() {}.type
            val loadedData: ArrayList<ArrayList<ObstacleData>> = gson.fromJson(json, type)
            gridMapObj.clearGridMap()
            gridMapObj.addGridMapSaved(loadedData)
            gridMapObj.sendArenaDataBluetooth()
        }else{
            Toast.makeText(this, "No Map was saved!", Toast.LENGTH_SHORT).show()

        }
    }


}
