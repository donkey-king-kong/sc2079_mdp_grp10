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
import android.widget.NumberPicker
import androidx.appcompat.app.AlertDialog
import com.example.sc2079.ui.coordinates.AddCoordinateFragment
import com.example.sc2079.ui.coordinates.PlaceObstacleDialogFragment
import com.example.sc2079.ui.coordinates.SharedViewModel

class MainActivity : AppCompatActivity() {
    private val base64Data = StringBuilder()
    private var iterationHowMany: Int = -1
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

    private val handler = Handler(Looper.getMainLooper())
    private val autoHandler = Handler(Looper.getMainLooper())
    private val autoRunnable = object : Runnable {
        override fun run() {
            bluetoothService?.write("sendArena".toByteArray())
            autoHandler.postDelayed(this, 2000)
        }
    }
    private val updateTask = object : Runnable {
        override fun run() {
            findViewById<TextView?>(R.id.give_vehicle_direction_now)?.text = gridMapObj.getImmediateVehicleDirection()
            findViewById<TextView?>(R.id.give_vehicle_coord_now)?.text = gridMapObj.getImmediateVehicleCoord()
            findViewById<TextView?>(R.id.give_vehicle_status_now)?.text = gridMapObj.getImmediateVehicleStatus()
            handler.postDelayed(this, 500)
        }
    }

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
            val binder = service as BluetoothService.LocalBinder
            bluetoothService = binder.getService()
            isBound = true
            gridMapObj.setBluetoothService(bluetoothService)
            val svc = bluetoothService
            if (svc != null && ContextCompat.checkSelfPermission(this@MainActivity, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED) {
                svc.startServer()
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
                BluetoothFragment().show(supportFragmentManager, "BluetoothFragment")
            } else {
                Toast.makeText(this, "Bluetooth is required to continue", Toast.LENGTH_SHORT).show()
            }
        }

    fun getBluetoothService(): BluetoothService? = if (isBound) bluetoothService else null
    fun getIsConnected(): Boolean = isConnected
    fun getMessageLog(): ArrayList<String> = messageLog
    fun setMessageListener(listener: MessageListener?) { this.messageListener = listener }

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

            if (text.startsWith("TARGET,")) {
                val subParts = text.split(",").map { it.trim() }
                if (subParts.size >= 3) {
                    try { gridMapObj.updateObstacleTarget(subParts[1].toInt(), subParts[2]) }
                    catch (e: Exception) { Log.e("MainActivity", "Error parsing TARGET: $text") }
                }
            }

            if (text.startsWith("ROBOT,")) {
                val subParts = text.split(",").map { it.trim() }
                if (subParts.size >= 4) {
                    try { gridMapObj.updateRobotPosition(subParts[1].toInt(), subParts[2].toInt(), subParts[3]) }
                    catch (e: Exception) { Log.e("MainActivity", "Error parsing ROBOT: $text") }
                }
            }

            val line = "Robot: $text\n"

            if (iterationHowMany == -1) {
                messageLog.add(line)
                messageListener?.onNewMessage(line)
            }

            if (text.contains("stitch-image")) {
                val status = gridMapObj.receiveStichImageMessageBluetooth(text)
                when (status) {
                    "-1" -> messageLog.add("Unknown Error Occurred at stitch-image \n")
                    "2"  -> { messageLog.add("Starting to Stitch \n"); iterationHowMany = 0; base64Data.clear() }
                    "3"  -> {
                        messageLog.add("Ending Stitch, displaying image \n")
                        messageLog.add("Robot: [Image Received - Tap to view]\n")
                        val data = base64Data.toString()
                        Log.d("Image Message", "Final length: ${data.length}")
                        ImageDisplayFragment.newInstance(data).show(supportFragmentManager, "ImageDisplayFragment")
                        iterationHowMany = -1
                    }
                    else -> {
                        base64Data.append(status)
                        iterationHowMany += 1
                        messageLog.add("Running data compilation iteration $iterationHowMany \n")
                        Log.d("Image Chunk", "Added chunk length=${status.length}, total=${base64Data.length}")
                    }
                }
            }

            if (text.contains("image-rec")) {
                when (gridMapObj.receiveVerifiedObstacleBluetooth(text)) {
                    -3 -> messageLog.add("Bullseye Detected \n")
                    -2 -> messageLog.add("Unknown Error Occurred at image-rec \n")
                    -1 -> messageLog.add("No Image ID Detected \n")
                    0  -> messageLog.add("Failed to verify Obstacle \n")
                    1  -> messageLog.add("Successfully Verified Obstacle \n")
                    2  -> messageLog.add("Capturing Obstacle Image \n")
                }
            }

            if (text.contains("location")) {
                when (gridMapObj.receiveLocationMessageBluetooth(text)) {
                    -2 -> messageLog.add("Unknown Error Occurred at location \n")
                    0  -> messageLog.add("Failed to verify Location \n")
                    1  -> messageLog.add("Successfully Verified Location \n")
                }
            }

            if (text.contains("health")) {
                when (gridMapObj.receiveHealthMessageBluetooth(text)) {
                    -2 -> messageLog.add("Unknown Error Occurred at health \n")
                    0  -> messageLog.add("Image Rec API is down \n")
                    1  -> messageLog.add("Algo API is down \n")
                }
            }

            if (text.contains("status")) {
                activateJoyStickBool = false
                gridMapObj.receiveStatusMessageBluetooth(text, activateJoyStickBool)
            }

            if (text.contains("\"grid\"")) {
                gridMapObj.receiveGridHexBluetooth(text)
            }
        }
    }

    private val requestBluetoothPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { permissions ->
            if (permissions.entries.all { it.value }) {
                checkBluetoothEnabled()
                val svc = bluetoothService
                if (isBound && svc != null && ContextCompat.checkSelfPermission(this@MainActivity, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED) {
                    @Suppress("MissingPermission")
                    svc.startServer()
                }
            } else {
                Toast.makeText(this, "Bluetooth permissions are required", Toast.LENGTH_SHORT).show()
            }
        }

    private val connStateReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            val state = intent?.getStringExtra(BluetoothService.EXTRA_CONN_STATE)
            isConnected = state == "connected"
            updateBluetoothStatus()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        btnBluetooth = findViewById(R.id.btnBluetooth)
        bluetoothStatus = findViewById(R.id.bluetoothStatus)
        btnAddCoordinate = findViewById(R.id.btnAddCoordinate)

        btnAddCoordinate.setOnClickListener { showAddCoordinatesFragment() }
        btnBluetooth.setOnClickListener { checkBluetoothPermissionsAndState() }

        updateBluetoothStatus()

        sharedViewModel.newCoordinate.observe(this) { coordinate ->
            gridMapObj.addNewObstacleToGrid(coordinate.first.toInt(), coordinate.second.toInt())
        }
        sharedViewModel.newObstacleRequest.observe(this) { request ->
            gridMapObj.addNewObstacleToGridWithDirection(request.x, request.y, request.direction)
            Toast.makeText(this, "Obstacle added at (${request.x}, ${request.y})", Toast.LENGTH_SHORT).show()
        }

        // Hidden clear logs button kept for logic wiring
        findViewById<MaterialButton>(R.id.clear_logs_button).setOnClickListener { clearMessageLog() }

        var autoActive = false

        // Theme toggle — flips between design-system dark and light token sets
        val btnThemeToggle = findViewById<MaterialButton>(R.id.btnThemeToggle)
        val rootContainer = findViewById<LinearLayout>(R.id.container)
        val headerRow = findViewById<LinearLayout>(R.id.headerRow)
        val bottomRow = findViewById<LinearLayout>(R.id.bottomRow)
        val rightPanel = findViewById<LinearLayout>(R.id.rightPanel)
        val tabs = findViewById<TabLayout>(R.id.tabs)

        fun applyTheme(day: Boolean) {
            fun tint(color: String) = android.content.res.ColorStateList.valueOf(Color.parseColor(color))
            fun col(color: String) = Color.parseColor(color)
            if (day) {
                btnThemeToggle.text = "🌙"
                rootContainer.setBackgroundColor(col("#C8CDD4"))
                headerRow.setBackgroundColor(col("#E8EAED"))
                bottomRow.setBackgroundColor(col("#E8EAED"))
                rightPanel.setBackgroundColor(col("#E8EAED"))
                tabs.setBackgroundColor(col("#C8CDD4"))
                tabs.setSelectedTabIndicatorColor(col("#1A3ECF"))
                tabs.setTabTextColors(col("#606163"), col("#1A3ECF"))
                updateAxisTextColor(false)
            } else {
                btnThemeToggle.text = "☀"
                rootContainer.setBackgroundColor(col("#0E1117"))
                headerRow.setBackgroundColor(col("#151B27"))
                bottomRow.setBackgroundColor(col("#151B27"))
                rightPanel.setBackgroundColor(col("#151B27"))
                tabs.setBackgroundColor(col("#0E1117"))
                tabs.setSelectedTabIndicatorColor(col("#6C8EF5"))
                tabs.setTabTextColors(col("#47E8EAF0"), col("#6C8EF5"))
                updateAxisTextColor(true)
            }
        }

        btnThemeToggle.setOnClickListener {
            isDayMode = !isDayMode
            applyTheme(isDayMode)
        }

        // Initialise grid
        val gridMapView = findViewById<LinearLayout>(R.id.gridMapView)
        gridMapObj = GridMapClass(this)
        gridMapObj.setGridColumns(20)
        gridMapObj.setGridRows(20)
        gridMapView.addView(gridMapObj)
        setupGraphAxes(this, true)
        updateGridSizeLabel()

        // D-pad
        val dpadUp    = findViewById<android.widget.Button>(R.id.dpad_up)
        val dpadDown  = findViewById<android.widget.Button>(R.id.dpad_down)
        val dpadLeft  = findViewById<android.widget.Button>(R.id.dpad_left)
        val dpadRight = findViewById<android.widget.Button>(R.id.dpad_right)
        val reverseLeft  = findViewById<LinearLayout>(R.id.reverse_left_button)
        val reverseRight = findViewById<LinearLayout>(R.id.reverse_right_button)

        dpadUp.setOnClickListener    { activateJoyStickBool = true; gridMapObj.moveVehicleStraight(ObstacleData.Direction.NORTH, true) }
        dpadDown.setOnClickListener  { activateJoyStickBool = true; gridMapObj.moveVehicleStraight(ObstacleData.Direction.SOUTH, true) }
        dpadLeft.setOnClickListener  { activateJoyStickBool = true; gridMapObj.moveVehicleStraight(ObstacleData.Direction.WEST, true) }
        dpadRight.setOnClickListener { activateJoyStickBool = true; gridMapObj.moveVehicleStraight(ObstacleData.Direction.EAST, true) }
        reverseLeft.setOnClickListener  { Log.d("JoystickButtons", "Reverse Left clicked");  gridMapObj.reverseLeftVehicle(true) }
        reverseRight.setOnClickListener { Log.d("JoystickButtons", "Reverse Right clicked"); gridMapObj.reverseRightVehicle(true) }

        // Bottom bar
        val btnGridSize       = findViewById<MaterialButton>(R.id.btn_grid_size)
        val btnReset          = findViewById<MaterialButton>(R.id.reset_map_button)
        val saveGridMapButton = findViewById<MaterialButton>(R.id.save_map_button)
        val loadGridMapButton = findViewById<MaterialButton>(R.id.load_map_button)

        btnGridSize.setOnClickListener { showGridSizeDialog() }
        btnReset.setOnClickListener    { gridMapObj.clearGridMap() }
        saveGridMapButton.setOnClickListener { saveGridMapData(gridMapObj.returnGridMap()) }
        loadGridMapButton.setOnClickListener { loadGridMapData() }

        // Tabs
        val customNavigatorBar = customNavigator(
            supportFragmentManager,
            FragmentPagerAdapter.BEHAVIOR_RESUME_ONLY_CURRENT_FRAGMENT
        )
        customNavigatorBar.addFragment(AddObstacle(gridMapObj), "")
        customNavigatorBar.addFragment(commsToRobot(gridMapObj), "")
        customNavigatorBar.addFragment(startTask(gridMapObj), "")

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
        Intent(this, BluetoothService::class.java).also { bindService(it, connection, Context.BIND_AUTO_CREATE) }
        LocalBroadcastManager.getInstance(this).registerReceiver(connStateReceiver, IntentFilter(BluetoothService.ACTION_CONN_STATE))
        LocalBroadcastManager.getInstance(this).registerReceiver(msgReceiver, IntentFilter(BluetoothService.ACTION_MESSAGE))
    }

    override fun onStop() {
        super.onStop()
        if (isBound) { unbindService(connection); isBound = false }
        LocalBroadcastManager.getInstance(this).unregisterReceiver(connStateReceiver)
        LocalBroadcastManager.getInstance(this).unregisterReceiver(msgReceiver)
        autoHandler.removeCallbacks(autoRunnable)
    }

    override fun onResume() { super.onResume(); handler.post(updateTask) }
    override fun onPause()  { super.onPause();  handler.removeCallbacks(updateTask) }

    private fun checkBluetoothPermissionsAndState() {
        val scan    = ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_SCAN)    == PackageManager.PERMISSION_GRANTED
        val connect = ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
        if (scan && connect) checkBluetoothEnabled()
        else requestBluetoothPermissionLauncher.launch(arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT))
    }

    private fun checkBluetoothEnabled() {
        if (isBound && bluetoothService?.isBluetoothEnabled() == true) showBluetoothFragment()
        else bluetoothEnableLauncher.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
    }

    private fun showBluetoothFragment() = BluetoothFragment().show(supportFragmentManager, "BluetoothFragment")
    private fun showAddCoordinatesFragment() = PlaceObstacleDialogFragment().show(supportFragmentManager, "PlaceObstacleDialog")

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
        messageListener?.onLogCleared()
        Toast.makeText(this, "Bluetooth message log cleared", Toast.LENGTH_SHORT).show()
    }

    fun setupGraphAxes(context: Context, isDark: Boolean) {
        val yAxis = findViewById<LinearLayout>(R.id.y_axis_numbers)
        val xAxis = findViewById<LinearLayout>(R.id.x_axis_numbers)
        val axisColor = if (isDark) Color.WHITE else Color.BLACK
        val rows = gridMapObj.getGridRows()
        val cols = gridMapObj.getGridColumns()

        for (i in (rows - 1) downTo 0) {
            val tv = TextView(context)
            tv.text = i.toString()
            tv.setTextColor(axisColor)
            tv.textSize = 8f
            tv.gravity = Gravity.CENTER
            tv.layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f)
            yAxis.addView(tv)
        }
        for (i in 0 until cols) {
            val tv = TextView(context)
            tv.text = i.toString()
            tv.setTextColor(axisColor)
            tv.textSize = 8f
            tv.gravity = Gravity.CENTER
            tv.layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f)
            xAxis.addView(tv)
        }
    }

    private fun updateAxisTextColor(isDark: Boolean) {
        val axisColor = if (isDark) Color.WHITE else Color.BLACK
        val yAxis = findViewById<LinearLayout>(R.id.y_axis_numbers)
        val xAxis = findViewById<LinearLayout>(R.id.x_axis_numbers)
        for (i in 0 until yAxis.childCount) (yAxis.getChildAt(i) as? TextView)?.setTextColor(axisColor)
        for (i in 0 until xAxis.childCount) (xAxis.getChildAt(i) as? TextView)?.setTextColor(axisColor)
    }

    private fun updateGridSizeLabel() {
        findViewById<TextView?>(R.id.grid_size_label)?.text = gridMapObj.getGridColumns().toString()
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
            .setPositiveButton("Apply") { _, _ -> applyGridSize(colPicker.value, rowPicker.value) }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun applyGridSize(cols: Int, rows: Int) {
        gridMapObj.setGridColumns(cols)
        gridMapObj.setGridRows(rows)
        gridMapObj.clearGridMap()

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
        updateGridSizeLabel()
    }

    private fun saveGridMapData(gridMapData: ArrayList<ArrayList<ObstacleData>>) {
        val sharedPreferences = getSharedPreferences("grid_map_prefs", MODE_PRIVATE)
        val gson = Gson()
        sharedPreferences.edit().putString("gridMapData", gson.toJson(gridMapData)).apply()
        Toast.makeText(this, "Map was successfully saved!", Toast.LENGTH_SHORT).show()
    }

    private fun loadGridMapData() {
        val sharedPreferences = getSharedPreferences("grid_map_prefs", MODE_PRIVATE)
        val json = sharedPreferences.getString("gridMapData", null)
        if (json != null) {
            val type = object : TypeToken<ArrayList<ArrayList<ObstacleData>>>() {}.type
            val loadedData: ArrayList<ArrayList<ObstacleData>> = Gson().fromJson(json, type)
            gridMapObj.clearGridMap()
            gridMapObj.addGridMapSaved(loadedData)
            gridMapObj.sendArenaDataBluetooth()
        } else {
            Toast.makeText(this, "No Map was saved!", Toast.LENGTH_SHORT).show()
        }
    }
}
