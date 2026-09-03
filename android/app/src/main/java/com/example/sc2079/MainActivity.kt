package com.example.sc2079

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.IBinder
import android.os.Looper
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
import com.example.sc2079.ui.coordinates.AddCoordinateFragment
import com.example.sc2079.ui.coordinates.PlaceObstacleDialogFragment
import com.example.sc2079.ui.coordinates.SharedViewModel

class MainActivity : AppCompatActivity() {
    private val base64Data = StringBuilder();
    private var iterationHowMany: Int = -1;
    private var bluetoothService: BluetoothService? = null
    private var isBound = false
    private lateinit var binding: ActivityMainBinding
    private lateinit var btnBluetooth: ImageButton
    private lateinit var btnAddCoordinate: ImageButton
    private lateinit var bluetoothStatus: ImageView
    private var activateJoyStickBool = false;

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

            // AMD bundles grid + robot location as two newline-separated JSONs in one BT packet.
            // Split and re-dispatch each part so the individual message handlers each fire correctly.
            val parts = text.split("\n").map { it.trim() }.filter { it.isNotEmpty() }
            if (parts.size > 1) {
                for (part in parts) {
                    val subIntent = Intent(BluetoothService.ACTION_MESSAGE).apply {
                        putExtra(BluetoothService.EXTRA_TEXT, part)
                    }
                    onReceive(context, subIntent)
                }
                return
            }

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
                        //Toast.makeText(context, "Unknown Error Occurred", Toast.LENGTH_SHORT).show();
                        messageLog.add("Unknown Error Occurred at stitch-image \n");
                    }
                    "2" -> {
                        messageLog.add("Starting to Stitch \n");
                        iterationHowMany = 0;
                        base64Data.clear()
                    }
                    "3" -> {
                        messageLog.add("Ending Stitch, displaying image \n")
                        messageLog.add("Robot: [Image Received - Tap to view]\n")
                        val base64Data = base64Data.toString()
                        Log.d("Image Message", "Final length: ${base64Data.length}")
                        //val imageBytes = Base64.decode(base64Data.toString(), Base64.DEFAULT)
                        ImageDisplayFragment.newInstance(base64Data).show(supportFragmentManager, "ImageDisplayFragment")
                        iterationHowMany = -1;
                    }
                    else -> {
                        base64Data.append(status)  // add chunk
                        Log.d("Image Chunk", status)
                        iterationHowMany += 1
                        messageLog.add("Running data compilation iteration $iterationHowMany \n")
                        Log.d("Image Chunk", "Added chunk length=${status.length}, total=${base64Data.length}")

                    }
                }
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
            } else if (state == "disconnected" || state == "error") {
                isConnected = false
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

        val btnManual: com.google.android.material.button.MaterialButton = findViewById(R.id.manual_update_button)
        val btnAuto: com.google.android.material.button.MaterialButton = findViewById(R.id.auto_update_button)

        btnManual.setOnClickListener {
            bluetoothService?.write("sendArena".toByteArray())
            Toast.makeText(this, "Requested arena update", Toast.LENGTH_SHORT).show()
        }

        var autoActive = false
        val autoCyan = android.content.res.ColorStateList.valueOf(android.graphics.Color.parseColor("#26B5CB"))
        val autoGreen = android.content.res.ColorStateList.valueOf(android.graphics.Color.parseColor("#4CCB67"))

        fun updateAutoVisual() {
            if (autoActive) {
                btnAuto.setTextColor(android.graphics.Color.parseColor("#4CCB67"))
                btnAuto.strokeColor = autoGreen
                btnAuto.strokeWidth = resources.getDimensionPixelSize(com.google.android.material.R.dimen.mtrl_btn_stroke_size).coerceAtLeast(2)
            } else {
                btnAuto.setTextColor(android.graphics.Color.parseColor("#26B5CB"))
                btnAuto.strokeColor = android.content.res.ColorStateList.valueOf(android.graphics.Color.TRANSPARENT)
                btnAuto.strokeWidth = 0
            }
        }

        btnAuto.setOnClickListener {
            autoActive = !autoActive
            updateAutoVisual()
            if (autoActive) {
                autoHandler.post(autoRunnable)
            } else {
                autoHandler.removeCallbacks(autoRunnable)
            }
        }

        val customNavigatorBar: customNavigator = customNavigator(
            supportFragmentManager,
            FragmentPagerAdapter.BEHAVIOR_RESUME_ONLY_CURRENT_FRAGMENT
        )

        // Initializes gridmap
        val gridMapView = findViewById<LinearLayout>(R.id.gridMapView)
        gridMapObj = GridMapClass(this)
        gridMapObj.setGridColumns(20)
        gridMapObj.setGridRows(20)
        gridMapView.addView(gridMapObj)
        setupGraphAxes(this)

        // D-pad buttons
        val dpadUp = findViewById<android.widget.Button>(R.id.dpad_up)
        val dpadDown = findViewById<android.widget.Button>(R.id.dpad_down)
        val dpadLeft = findViewById<android.widget.Button>(R.id.dpad_left)
        val dpadRight = findViewById<android.widget.Button>(R.id.dpad_right)

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

        val reverseLeftButton = findViewById<ImageButton>(R.id.reverse_left_button)
        val reverseRightButton = findViewById<ImageButton>(R.id.reverse_right_button)

        reverseLeftButton.setOnClickListener({
            Log.d("JoystickButtons", "Reverse Left clicked")
            gridMapObj.reverseLeftVehicle(true);
        })

        reverseRightButton.setOnClickListener({
            Log.d("JoystickButtons", "Reverse Right clicked")
            gridMapObj.reverseRightVehicle(true);
        })

        val saveGridMapButton: MaterialButton = findViewById(R.id.save_map_button)

        saveGridMapButton.setOnClickListener {
            saveGridMapData(gridMapObj.returnGridMap())
        }

        val loadGridMapButton: MaterialButton = findViewById(R.id.load_map_button)
        loadGridMapButton.setOnClickListener {
            loadGridMapData()
        }

        // Initalize navigation tabz
        customNavigatorBar.addFragment(AddObstacle(gridMapObj), "")
        customNavigatorBar.addFragment(commsToRobot(gridMapObj), "")
        customNavigatorBar.addFragment(startTask(gridMapObj), "")


        // Initializes Navigation Bar
        val subNavigationBar = findViewById<ViewPager?>(R.id.sub_navigation_bar)
        subNavigationBar?.setAdapter(customNavigatorBar)
        subNavigationBar?.setOffscreenPageLimit(2)
        val tabs = findViewById<TabLayout>(R.id.tabs)
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

    fun setupGraphAxes(context: Context) {
        val yAxis = findViewById<LinearLayout>(R.id.y_axis_numbers)
        val xAxis = findViewById<LinearLayout>(R.id.x_axis_numbers)

        // Y-axis: 19 (top) to 0 (bottom)
        for (i in 19 downTo 0) {
            val textView = TextView(context)
            textView.text = i.toString()
            textView.setTextColor(Color.WHITE)
            textView.gravity = Gravity.CENTER
            textView.layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                0, 1f
            )
            yAxis.addView(textView)
        }

        // X-axis: 0 to 19
        for (i in 0..19) {
            val textView = TextView(context)
            textView.text = i.toString()
            textView.setTextColor(Color.WHITE)
            textView.gravity = Gravity.CENTER
            textView.layoutParams = LinearLayout.LayoutParams(
                0,
                LinearLayout.LayoutParams.MATCH_PARENT, 1f
            )
            xAxis.addView(textView)
        }
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
        }else{
            Toast.makeText(this, "No Map was saved!", Toast.LENGTH_SHORT).show()

        }
    }


}