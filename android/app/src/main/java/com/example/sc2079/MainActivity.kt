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
import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.GradientDrawable
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
import android.widget.EditText
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.AppCompatButton
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
import com.example.sc2079.ui.F1
import com.example.sc2079.ui.NIGHT
import com.example.sc2079.ui.Palette
import com.example.sc2079.ui.ThemeAware
import com.example.sc2079.ui.ThemeMode
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
    private var themeMode = ThemeMode.NIGHT

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

    fun currentPalette(): Palette = when (themeMode) {
        ThemeMode.DAY -> DAY
        ThemeMode.NIGHT -> NIGHT
        ThemeMode.F1 -> F1
    }

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
                        Log.d("Image Chunk", "Added chunk length=${status.length}, total=${base64Data.length}")

                    }
                }
                return
            } else if(iterationHowMany == -1){
                logIncoming(text)
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
        themeMode = uiPrefs.getString("theme_mode", null)
            ?.let { runCatching { ThemeMode.valueOf(it) }.getOrNull() }
            ?: if (uiPrefs.getBoolean("day_mode", true)) ThemeMode.DAY else ThemeMode.NIGHT
        isDayMode = themeMode == ThemeMode.DAY

        fun applyTheme(mode: ThemeMode) {
            val p = when (mode) {
                ThemeMode.DAY -> DAY
                ThemeMode.NIGHT -> NIGHT
                ThemeMode.F1 -> F1
            }
            isDayMode = mode == ThemeMode.DAY
            WindowCompat.getInsetsController(window, window.decorView).isAppearanceLightStatusBars = mode == ThemeMode.DAY
            btnThemeToggle.text = when (mode) {
                ThemeMode.DAY -> "🌙"
                ThemeMode.NIGHT -> "F1"
                ThemeMode.F1 -> "☀"
            }
            // Container backgrounds
            rootContainer.setBackgroundColor(p.panel)
            headerRow.setBackgroundColor(p.panel)
            bottomRow.setBackgroundColor(p.panel)
            rightPanel.setBackgroundColor(p.panel)
            subNavContainer.setBackgroundColor(p.panel)
            gridArea.setBackgroundColor(p.bg)
            val headerDivider = findViewById<android.view.View>(R.id.divider_header)
            if (mode == ThemeMode.F1) {
                headerDivider?.setBackgroundColor(android.graphics.Color.parseColor("#E8002D"))
                headerDivider?.layoutParams?.height = android.util.TypedValue.applyDimension(
                    android.util.TypedValue.COMPLEX_UNIT_DIP, 2f, resources.displayMetrics).toInt()
            } else {
                headerDivider?.setBackgroundColor(if (mode == ThemeMode.DAY) android.graphics.Color.parseColor("#33000000") else android.graphics.Color.parseColor("#1FFFFFFF"))
                headerDivider?.layoutParams?.height = android.util.TypedValue.applyDimension(
                    android.util.TypedValue.COMPLEX_UNIT_DIP, 1f, resources.displayMetrics).toInt()
            }
            headerDivider?.requestLayout()
            val gridColors = when (mode) {
                ThemeMode.F1    -> Triple(
                    android.graphics.Color.parseColor("#080808"),
                    android.graphics.Color.parseColor("#FFFFFF"),
                    android.graphics.Color.parseColor("#2A2A2A")
                )
                ThemeMode.DAY   -> Triple(
                    android.graphics.Color.parseColor("#B8C8B8"),
                    android.graphics.Color.parseColor("#80000000"),
                    android.graphics.Color.parseColor("#7A9A7A")
                )
                ThemeMode.NIGHT -> Triple(
                    android.graphics.Color.parseColor("#111A11"),
                    android.graphics.Color.parseColor("#3DFFFFFF"),
                    android.graphics.Color.parseColor("#1E3A1E")
                )
            }
            gridArea.setBackgroundColor(gridColors.first)
            if (::gridMapObj.isInitialized) {
                gridMapObj.setGridTheme(gridColors.first, gridColors.second, gridColors.third)
            }
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
            val btnRadius = if (mode == ThemeMode.F1) 4f else 8f
            val btnFill = if (mode == ThemeMode.F1) p.bg else p.surface2
            val btnStroke = if (mode == ThemeMode.F1) p.accentBorder else p.borderStrong
            val btnTextColor = if (mode == ThemeMode.F1) p.text else p.text
            btnGridMinus.background = box(this, btnFill, btnStroke, btnRadius)
            btnGridMinus.backgroundTintList = null
            btnGridMinus.setTextColor(btnTextColor)
            btnGridPlus.background = box(this, btnFill, btnStroke, btnRadius)
            btnGridPlus.backgroundTintList = null
            btnGridPlus.setTextColor(btnTextColor)
            txtGridSize.setTextColor(btnTextColor)
            txtGridSizeLabel.setTextColor(p.textMuted)
            // Map action buttons
            btnReset.background = box(this, btnFill, btnStroke, btnRadius)
            btnReset.backgroundTintList = null
            btnReset.setTextColor(btnTextColor)
            saveGridMapButton.background = box(this, btnFill, btnStroke, btnRadius)
            saveGridMapButton.backgroundTintList = null
            saveGridMapButton.setTextColor(btnTextColor)
            loadGridMapButton.background = box(this, btnFill, btnStroke, btnRadius)
            loadGridMapButton.backgroundTintList = null
            loadGridMapButton.setTextColor(btnTextColor)
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
            updateAxisTextColor(mode != ThemeMode.DAY)
            // Dispatch to ThemeAware fragments
            supportFragmentManager.fragments.forEach { frag ->
                if (frag is ThemeAware) frag.applyTheme(p)
            }
        }

        btnThemeToggle.setOnClickListener {
            themeMode = when (themeMode) {
                ThemeMode.DAY -> ThemeMode.NIGHT
                ThemeMode.NIGHT -> ThemeMode.F1
                ThemeMode.F1 -> ThemeMode.DAY
            }
            isDayMode = themeMode == ThemeMode.DAY
            uiPrefs.edit()
                .putString("theme_mode", themeMode.name)
                .putBoolean("day_mode", isDayMode)
                .apply()
            applyTheme(themeMode)
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
        } else if (gridMapPreferences().contains("autosave")) {
            val json = gridMapPreferences().getString("autosave", null)
            if (json != null) {
                val type = object : TypeToken<ArrayList<ArrayList<ObstacleData>>>() {}.type
                val loadedData: ArrayList<ArrayList<ObstacleData>> = Gson().fromJson(json, type)
                gridMapObj.addGridMapSaved(loadedData)
            }
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

        sharedViewModel.newVehicleRequest.observe(this) { request ->
            val result = gridMapObj.addVehicleToMap(request.x, request.y)
            if (result == 1) {
                if (request.direction != ObstacleData.Direction.NORTH) {
                    gridMapObj.changeDirectionOfObstacleFlexible(request.x, request.y, request.direction)
                }
                Toast.makeText(this, "Vehicle added at (${request.x}, ${request.y})", Toast.LENGTH_SHORT).show()
            } else {
                Toast.makeText(this, "Vehicle could not be added at (${request.x}, ${request.y})", Toast.LENGTH_SHORT).show()
            }
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
            showSaveMapSlotDialog()
        }

        loadGridMapButton.setOnClickListener {
            showLoadMapSlotDialog()
        }

        btnReset.setOnClickListener {
            gridMapObj.clearGridMap()
            gridMapPreferences().edit().remove("autosave").apply()
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
        applyTheme(themeMode)

        // Bind to BluetoothService here so rotation (onStop/onStart) does not unbind it
        Intent(this, BluetoothService::class.java).also { intent ->
            startService(intent)
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
        LocalBroadcastManager.getInstance(this).unregisterReceiver(connStateReceiver)
        LocalBroadcastManager.getInstance(this).unregisterReceiver(msgReceiver)
        if (::gridMapObj.isInitialized) {
            val json = Gson().toJson(gridMapObj.returnGridMap())
            gridMapPreferences().edit().putString("autosave", json).apply()
        }
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

    private fun showAddCoordinatesFragment(isVehicleMode: Boolean = false) {
        PlaceObstacleDialogFragment.newInstance(isVehicleMode).show(supportFragmentManager, "PlaceObstacleDialog")
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
        if (isFinishing) {
            stopService(Intent(this, BluetoothService::class.java))
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

    private fun gridMapPreferences() = getSharedPreferences("grid_map_prefs", MODE_PRIVATE)

    private fun gridMapDataKey(slotIndex: Int) = "gridMapData_$slotIndex"

    private fun gridMapNameKey(slotIndex: Int) = "gridMapName_$slotIndex"

    private fun migrateLegacyGridMapSave() {
        val sharedPreferences = gridMapPreferences()
        val legacyJson = sharedPreferences.getString("gridMapData", null)

        if (legacyJson != null && !sharedPreferences.contains(gridMapDataKey(0))) {
            sharedPreferences.edit()
                .putString(gridMapDataKey(0), legacyJson)
                .remove("gridMapData")
                .apply()
        } else if (legacyJson != null) {
            sharedPreferences.edit()
                .remove("gridMapData")
                .apply()
        }
    }

    private fun createThemedDialogTitle(
        title: String,
        textColor: Int,
        surfaceColor: Int
    ): TextView {
        return TextView(this).apply {
            text = title
            setTextColor(textColor)
            textSize = 20f
            typeface = android.graphics.Typeface.DEFAULT_BOLD
            setBackgroundColor(surfaceColor)
            setPadding(dpToPx(24), dpToPx(20), dpToPx(24), dpToPx(8))
        }
    }

    private fun showSaveMapSlotDialog() {
        val bgColor: Int
        val surfaceColor: Int
        val cardColor: Int
        val accentColor: Int
        val textColor: Int
        val textMuted: Int
        val buttonText: Int
        val borderColor: Int
        val disabledBg: Int
        val disabledText: Int
        when (themeMode) {
            ThemeMode.DAY -> {
                bgColor = Color.parseColor("#C8CDD4")
                surfaceColor = Color.parseColor("#E8EAED")
                cardColor = Color.parseColor("#DDE0E4")
                accentColor = Color.parseColor("#1A3ECF")
                textColor = Color.parseColor("#0C0E11")
                textMuted = Color.parseColor("#8C0C0E11")
                buttonText = Color.parseColor("#FFFFFF")
                borderColor = Color.parseColor("#2E000000")
                disabledBg = Color.parseColor("#DDE0E4")
                disabledText = Color.parseColor("#610C0E11")
            }
            ThemeMode.NIGHT -> {
                bgColor = Color.parseColor("#0E1117")
                surfaceColor = Color.parseColor("#151B27")
                cardColor = Color.parseColor("#1C2333")
                accentColor = Color.parseColor("#6C8EF5")
                textColor = Color.parseColor("#E8EAF0")
                textMuted = Color.parseColor("#73E8EAF0")
                buttonText = Color.parseColor("#FFFFFF")
                borderColor = Color.parseColor("#14FFFFFF")
                disabledBg = Color.parseColor("#1C2333")
                disabledText = Color.parseColor("#47E8EAF0")
            }
            ThemeMode.F1 -> {
                bgColor = Color.parseColor("#060606")
                surfaceColor = Color.parseColor("#0E0E0E")
                cardColor = Color.parseColor("#1A1A1A")
                accentColor = Color.parseColor("#E8002D")
                textColor = Color.parseColor("#FFFFFF")
                textMuted = Color.parseColor("#666666")
                buttonText = Color.parseColor("#FFFFFF")
                borderColor = Color.parseColor("#2A2A2A")
                disabledBg = Color.parseColor("#2A2A2A")
                disabledText = Color.parseColor("#555555")
            }
        }

        migrateLegacyGridMapSave()

        lateinit var dialog: AlertDialog
        dialog = AlertDialog.Builder(this)
            .setCustomTitle(createThemedDialogTitle("Save Map", textColor, surfaceColor))
            .setView(createSlotSelectionView(isLoadDialog = false, parentDialog = null) { slotIndex ->
                dialog.dismiss()
                handleSaveSlotSelected(slotIndex)
            })
            .setNegativeButton("Cancel", null)
            .create()
        dialog.window?.setBackgroundDrawable(ColorDrawable(surfaceColor))
        dialog.show()
        dialog.window?.setBackgroundDrawable(ColorDrawable(surfaceColor))
        dialog.getButton(AlertDialog.BUTTON_NEGATIVE)?.setTextColor(accentColor)
    }

    private fun showLoadMapSlotDialog() {
        val bgColor: Int
        val surfaceColor: Int
        val cardColor: Int
        val accentColor: Int
        val textColor: Int
        val textMuted: Int
        val buttonText: Int
        val borderColor: Int
        val disabledBg: Int
        val disabledText: Int
        when (themeMode) {
            ThemeMode.DAY -> {
                bgColor = Color.parseColor("#C8CDD4")
                surfaceColor = Color.parseColor("#E8EAED")
                cardColor = Color.parseColor("#DDE0E4")
                accentColor = Color.parseColor("#1A3ECF")
                textColor = Color.parseColor("#0C0E11")
                textMuted = Color.parseColor("#8C0C0E11")
                buttonText = Color.parseColor("#FFFFFF")
                borderColor = Color.parseColor("#2E000000")
                disabledBg = Color.parseColor("#DDE0E4")
                disabledText = Color.parseColor("#610C0E11")
            }
            ThemeMode.NIGHT -> {
                bgColor = Color.parseColor("#0E1117")
                surfaceColor = Color.parseColor("#151B27")
                cardColor = Color.parseColor("#1C2333")
                accentColor = Color.parseColor("#6C8EF5")
                textColor = Color.parseColor("#E8EAF0")
                textMuted = Color.parseColor("#73E8EAF0")
                buttonText = Color.parseColor("#FFFFFF")
                borderColor = Color.parseColor("#14FFFFFF")
                disabledBg = Color.parseColor("#1C2333")
                disabledText = Color.parseColor("#47E8EAF0")
            }
            ThemeMode.F1 -> {
                bgColor = Color.parseColor("#060606")
                surfaceColor = Color.parseColor("#0E0E0E")
                cardColor = Color.parseColor("#1A1A1A")
                accentColor = Color.parseColor("#E8002D")
                textColor = Color.parseColor("#FFFFFF")
                textMuted = Color.parseColor("#666666")
                buttonText = Color.parseColor("#FFFFFF")
                borderColor = Color.parseColor("#2A2A2A")
                disabledBg = Color.parseColor("#2A2A2A")
                disabledText = Color.parseColor("#555555")
            }
        }

        migrateLegacyGridMapSave()

        lateinit var dialog: AlertDialog
        dialog = AlertDialog.Builder(this)
            .setCustomTitle(createThemedDialogTitle("Load Map", textColor, surfaceColor))
            .setNegativeButton("Cancel", null)
            .create()
        dialog.setView(createSlotSelectionView(isLoadDialog = true, parentDialog = dialog) { slotIndex ->
            dialog.dismiss()
            loadGridMapData(slotIndex)
        })
        dialog.window?.setBackgroundDrawable(ColorDrawable(surfaceColor))
        dialog.show()
        dialog.window?.setBackgroundDrawable(ColorDrawable(surfaceColor))
        dialog.getButton(AlertDialog.BUTTON_NEGATIVE)?.setTextColor(accentColor)
    }

    private fun createSlotSelectionView(
        isLoadDialog: Boolean,
        parentDialog: AlertDialog? = null,
        onSlotSelected: (Int) -> Unit
    ): ScrollView {
        val bgColor: Int
        val surfaceColor: Int
        val cardColor: Int
        val accentColor: Int
        val textColor: Int
        val textMuted: Int
        val buttonText: Int
        val borderColor: Int
        val disabledBg: Int
        val disabledText: Int
        when (themeMode) {
            ThemeMode.DAY -> {
                bgColor = Color.parseColor("#C8CDD4")
                surfaceColor = Color.parseColor("#E8EAED")
                cardColor = Color.parseColor("#DDE0E4")
                accentColor = Color.parseColor("#1A3ECF")
                textColor = Color.parseColor("#0C0E11")
                textMuted = Color.parseColor("#8C0C0E11")
                buttonText = Color.parseColor("#FFFFFF")
                borderColor = Color.parseColor("#2E000000")
                disabledBg = Color.parseColor("#DDE0E4")
                disabledText = Color.parseColor("#610C0E11")
            }
            ThemeMode.NIGHT -> {
                bgColor = Color.parseColor("#0E1117")
                surfaceColor = Color.parseColor("#151B27")
                cardColor = Color.parseColor("#1C2333")
                accentColor = Color.parseColor("#6C8EF5")
                textColor = Color.parseColor("#E8EAF0")
                textMuted = Color.parseColor("#73E8EAF0")
                buttonText = Color.parseColor("#FFFFFF")
                borderColor = Color.parseColor("#14FFFFFF")
                disabledBg = Color.parseColor("#1C2333")
                disabledText = Color.parseColor("#47E8EAF0")
            }
            ThemeMode.F1 -> {
                bgColor = Color.parseColor("#060606")
                surfaceColor = Color.parseColor("#0E0E0E")
                cardColor = Color.parseColor("#1A1A1A")
                accentColor = Color.parseColor("#E8002D")
                textColor = Color.parseColor("#FFFFFF")
                textMuted = Color.parseColor("#666666")
                buttonText = Color.parseColor("#FFFFFF")
                borderColor = Color.parseColor("#2A2A2A")
                disabledBg = Color.parseColor("#2A2A2A")
                disabledText = Color.parseColor("#555555")
            }
        }

        val sharedPreferences = gridMapPreferences()
        val container = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dpToPx(8), dpToPx(8), dpToPx(8), dpToPx(8))
            setBackgroundColor(surfaceColor)
        }

        for (slotIndex in 0 until 5) {
            val hasData = sharedPreferences.contains(gridMapDataKey(slotIndex))
            val slotName = getSlotDisplayName(slotIndex)
            val isEnabled = !isLoadDialog || hasData

            val row = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                background = GradientDrawable().apply {
                    setColor(cardColor)
                    cornerRadius = dpToPx(4).toFloat()
                    setStroke(dpToPx(1), borderColor)
                }
                setPadding(dpToPx(10), dpToPx(8), dpToPx(10), dpToPx(8))
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                ).apply {
                    bottomMargin = dpToPx(4)
                }
            }

            val label = TextView(this).apply {
                text = "Slot ${slotIndex + 1}: $slotName"
                textSize = 16f
                setTextColor(if (isEnabled) textColor else textMuted)
                layoutParams = LinearLayout.LayoutParams(
                    0,
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                    1f
                )
            }

            val trashButton = ImageButton(this).apply {
                setImageResource(R.drawable.ic_trash)
                setColorFilter(
                    if (hasData) accentColor else disabledText,
                    android.graphics.PorterDuff.Mode.SRC_IN
                )
                background = null
                this.isEnabled = hasData
                alpha = if (hasData) 1f else 0.4f
                scaleType = ImageView.ScaleType.FIT_CENTER
                setPadding(dpToPx(4), dpToPx(4), dpToPx(4), dpToPx(4))
                layoutParams = LinearLayout.LayoutParams(dpToPx(36), dpToPx(36)).apply {
                    marginEnd = dpToPx(8)
                }
                setOnClickListener {
                    if (hasData) {
                        val confirmDialog = AlertDialog.Builder(this@MainActivity)
                            .setCustomTitle(createThemedDialogTitle("Delete slot?", textColor, surfaceColor))
                            .setMessage("This will permanently delete \"$slotName\". This cannot be undone.")
                            .setPositiveButton("Delete") { _, _ ->
                                deleteMapSlot(slotIndex)
                                parentDialog?.dismiss()
                            }
                            .setNegativeButton("Cancel", null)
                            .create()
                        confirmDialog.window?.setBackgroundDrawable(ColorDrawable(surfaceColor))
                        confirmDialog.show()
                        confirmDialog.window?.setBackgroundDrawable(ColorDrawable(surfaceColor))
                        confirmDialog.findViewById<TextView>(android.R.id.message)
                            ?.setTextColor(textMuted)
                        confirmDialog.getButton(AlertDialog.BUTTON_POSITIVE)
                            ?.setTextColor(Color.parseColor("#E05252"))
                        confirmDialog.getButton(AlertDialog.BUTTON_NEGATIVE)
                            ?.setTextColor(textMuted)
                    }
                }
            }

            val actionButton = AppCompatButton(this).apply {
                text = if (isLoadDialog) "Load" else "Save"
                this.isEnabled = isEnabled
                if (android.os.Build.VERSION.SDK_INT >= 21) {
                    stateListAnimator = null
                }
                background = GradientDrawable().apply {
                    setColor(if (isEnabled) accentColor else disabledBg)
                    cornerRadius = dpToPx(8).toFloat()
                }
                setTextColor(if (isEnabled) buttonText else disabledText)
                textSize = 12f
                minWidth = 0
                minHeight = 0
                setPadding(0, 0, 0, 0)
                layoutParams = LinearLayout.LayoutParams(dpToPx(80), dpToPx(36))
                setOnClickListener {
                    if (isEnabled) {
                        onSlotSelected(slotIndex)
                    }
                }
            }

            row.addView(label)
            if (isLoadDialog) {
                row.addView(trashButton)
            }
            row.addView(actionButton)
            container.addView(row)
        }

        return ScrollView(this).apply {
            setBackgroundColor(surfaceColor)
            addView(container)
        }
    }

    private fun handleSaveSlotSelected(slotIndex: Int) {
        val bgColor: Int
        val surfaceColor: Int
        val cardColor: Int
        val accentColor: Int
        val textColor: Int
        val textMuted: Int
        val buttonText: Int
        val borderColor: Int
        val disabledBg: Int
        val disabledText: Int
        when (themeMode) {
            ThemeMode.DAY -> {
                bgColor = Color.parseColor("#C8CDD4")
                surfaceColor = Color.parseColor("#E8EAED")
                cardColor = Color.parseColor("#DDE0E4")
                accentColor = Color.parseColor("#1A3ECF")
                textColor = Color.parseColor("#0C0E11")
                textMuted = Color.parseColor("#8C0C0E11")
                buttonText = Color.parseColor("#FFFFFF")
                borderColor = Color.parseColor("#2E000000")
                disabledBg = Color.parseColor("#DDE0E4")
                disabledText = Color.parseColor("#610C0E11")
            }
            ThemeMode.NIGHT -> {
                bgColor = Color.parseColor("#0E1117")
                surfaceColor = Color.parseColor("#151B27")
                cardColor = Color.parseColor("#1C2333")
                accentColor = Color.parseColor("#6C8EF5")
                textColor = Color.parseColor("#E8EAF0")
                textMuted = Color.parseColor("#73E8EAF0")
                buttonText = Color.parseColor("#FFFFFF")
                borderColor = Color.parseColor("#14FFFFFF")
                disabledBg = Color.parseColor("#1C2333")
                disabledText = Color.parseColor("#47E8EAF0")
            }
            ThemeMode.F1 -> {
                bgColor = Color.parseColor("#060606")
                surfaceColor = Color.parseColor("#0E0E0E")
                cardColor = Color.parseColor("#1A1A1A")
                accentColor = Color.parseColor("#E8002D")
                textColor = Color.parseColor("#FFFFFF")
                textMuted = Color.parseColor("#666666")
                buttonText = Color.parseColor("#FFFFFF")
                borderColor = Color.parseColor("#2A2A2A")
                disabledBg = Color.parseColor("#2A2A2A")
                disabledText = Color.parseColor("#555555")
            }
        }

        val sharedPreferences = gridMapPreferences()

        if (sharedPreferences.contains(gridMapDataKey(slotIndex))) {
            val slotName = getSlotDisplayName(slotIndex)
            val dialog = AlertDialog.Builder(this)
                .setCustomTitle(createThemedDialogTitle("Overwrite $slotName?", textColor, surfaceColor))
                .setPositiveButton("Yes") { _, _ ->
                    showSaveNameDialog(slotIndex)
                }
                .setNegativeButton("No", null)
                .create()
            dialog.window?.setBackgroundDrawable(ColorDrawable(surfaceColor))
            dialog.show()
            dialog.window?.setBackgroundDrawable(ColorDrawable(surfaceColor))
            dialog.getButton(AlertDialog.BUTTON_POSITIVE)?.setTextColor(accentColor)
            dialog.getButton(AlertDialog.BUTTON_NEGATIVE)?.setTextColor(textMuted)
        } else {
            showSaveNameDialog(slotIndex)
        }
    }

    private fun showSaveNameDialog(slotIndex: Int) {
        val bgColor: Int
        val surfaceColor: Int
        val cardColor: Int
        val accentColor: Int
        val textColor: Int
        val textMuted: Int
        val buttonText: Int
        val borderColor: Int
        val disabledBg: Int
        val disabledText: Int
        when (themeMode) {
            ThemeMode.DAY -> {
                bgColor = Color.parseColor("#C8CDD4")
                surfaceColor = Color.parseColor("#E8EAED")
                cardColor = Color.parseColor("#DDE0E4")
                accentColor = Color.parseColor("#1A3ECF")
                textColor = Color.parseColor("#0C0E11")
                textMuted = Color.parseColor("#8C0C0E11")
                buttonText = Color.parseColor("#FFFFFF")
                borderColor = Color.parseColor("#2E000000")
                disabledBg = Color.parseColor("#DDE0E4")
                disabledText = Color.parseColor("#610C0E11")
            }
            ThemeMode.NIGHT -> {
                bgColor = Color.parseColor("#0E1117")
                surfaceColor = Color.parseColor("#151B27")
                cardColor = Color.parseColor("#1C2333")
                accentColor = Color.parseColor("#6C8EF5")
                textColor = Color.parseColor("#E8EAF0")
                textMuted = Color.parseColor("#73E8EAF0")
                buttonText = Color.parseColor("#FFFFFF")
                borderColor = Color.parseColor("#14FFFFFF")
                disabledBg = Color.parseColor("#1C2333")
                disabledText = Color.parseColor("#47E8EAF0")
            }
            ThemeMode.F1 -> {
                bgColor = Color.parseColor("#060606")
                surfaceColor = Color.parseColor("#0E0E0E")
                cardColor = Color.parseColor("#1A1A1A")
                accentColor = Color.parseColor("#E8002D")
                textColor = Color.parseColor("#FFFFFF")
                textMuted = Color.parseColor("#666666")
                buttonText = Color.parseColor("#FFFFFF")
                borderColor = Color.parseColor("#2A2A2A")
                disabledBg = Color.parseColor("#2A2A2A")
                disabledText = Color.parseColor("#555555")
            }
        }

        val sharedPreferences = gridMapPreferences()
        val defaultName = sharedPreferences.getString(gridMapNameKey(slotIndex), null)
            ?: "Map ${slotIndex + 1}"
        val nameInput = EditText(this).apply {
            background = GradientDrawable().apply {
                setColor(cardColor)
                cornerRadius = dpToPx(4).toFloat()
                setStroke(dpToPx(1), borderColor)
            }
            setTextColor(textColor)
            setHintTextColor(textMuted)
            setPadding(dpToPx(8), dpToPx(8), dpToPx(8), dpToPx(8))
            setText(defaultName)
            selectAll()
        }

        val dialog = AlertDialog.Builder(this)
            .setCustomTitle(createThemedDialogTitle("Name Save Slot ${slotIndex + 1}", textColor, surfaceColor))
            .setView(nameInput)
            .setPositiveButton("Save") { _, _ ->
                val enteredName = nameInput.text.toString().trim()
                val slotName = enteredName.ifEmpty { "Map ${slotIndex + 1}" }
                saveGridMapData(slotIndex, slotName, gridMapObj.returnGridMap())
            }
            .setNegativeButton("Cancel", null)
            .create()
        dialog.window?.setBackgroundDrawable(ColorDrawable(surfaceColor))
        dialog.show()
        dialog.window?.setBackgroundDrawable(ColorDrawable(surfaceColor))
        dialog.getButton(AlertDialog.BUTTON_POSITIVE)?.setTextColor(accentColor)
        dialog.getButton(AlertDialog.BUTTON_NEGATIVE)?.setTextColor(textMuted)
    }

    private fun deleteMapSlot(slotIndex: Int) {
        gridMapPreferences().edit()
            .remove(gridMapDataKey(slotIndex))
            .remove(gridMapNameKey(slotIndex))
            .apply()

        Toast.makeText(this, "Slot ${slotIndex + 1} deleted", Toast.LENGTH_SHORT).show()
    }

    private fun saveGridMapData(
        slotIndex: Int,
        slotName: String,
        gridMapData: ArrayList<ArrayList<ObstacleData>>
    ) {
        val json = Gson().toJson(gridMapData)

        gridMapPreferences().edit()
            .putString(gridMapDataKey(slotIndex), json)
            .putString(gridMapNameKey(slotIndex), slotName)
            .apply()

        Toast.makeText(this, "Saved to $slotName", Toast.LENGTH_SHORT).show()
    }

    private fun loadGridMapData(slotIndex: Int) {
        val json = gridMapPreferences().getString(gridMapDataKey(slotIndex), null)

        if (json != null) {
            val type = object : TypeToken<ArrayList<ArrayList<ObstacleData>>>() {}.type
            val loadedData: ArrayList<ArrayList<ObstacleData>> = Gson().fromJson(json, type)
            val slotName = getSlotDisplayName(slotIndex)

            gridMapObj.clearGridMap()
            gridMapObj.addGridMapSaved(loadedData)
            gridMapObj.sendArenaDataBluetooth()

            Toast.makeText(this, "Loaded $slotName", Toast.LENGTH_SHORT).show()
        } else {
            Toast.makeText(this, "Slot ${slotIndex + 1} is empty", Toast.LENGTH_SHORT).show()
        }
    }

    private fun getSlotDisplayName(slotIndex: Int): String {
        val sharedPreferences = gridMapPreferences()
        val hasData = sharedPreferences.contains(gridMapDataKey(slotIndex))
        val savedName = sharedPreferences.getString(gridMapNameKey(slotIndex), null)

        return when {
            !hasData -> "Empty"
            !savedName.isNullOrBlank() -> savedName
            else -> "Map ${slotIndex + 1}"
        }
    }

    private fun dpToPx(dp: Int): Int =
        TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP,
            dp.toFloat(),
            resources.displayMetrics
        ).toInt()


}
