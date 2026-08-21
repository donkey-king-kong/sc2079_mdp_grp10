package com.example.sc2079.ui.bluetooth

import android.Manifest
import android.app.Activity
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.ServiceConnection
import android.graphics.PorterDuff
import android.graphics.drawable.Animatable
import android.os.Bundle
import android.os.IBinder
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.ImageButton
import android.widget.TextView
import android.widget.Toast
import androidx.annotation.RequiresPermission
import androidx.core.content.ContextCompat
import androidx.fragment.app.DialogFragment
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.example.sc2079.MainActivity
import com.example.sc2079.R
import com.example.sc2079.service.BluetoothService
import com.google.android.material.button.MaterialButton
import com.google.android.material.progressindicator.CircularProgressIndicator

class BluetoothFragment : DialogFragment() {
    private var bluetoothService: BluetoothService? = null
    private var isBound = false
    private lateinit var statusText: TextView
    private lateinit var recyclerView: RecyclerView
    private lateinit var scanButton: MaterialButton
    private lateinit var cancelButton: ImageButton
    private lateinit var scanProgress: CircularProgressIndicator
    private lateinit var deviceAdapter: DeviceAdapter


/*
    private val connection = object : ServiceConnection {
        @RequiresPermission(Manifest.permission.BLUETOOTH_CONNECT)
        override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
            val binder = service as BluetoothService.LocalBinder
            bluetoothService = binder.getService()
            isBound = true


            if (bluetoothService?.isBluetoothEnabled() == true) {
                // populate list of paired (already known) devices
                val devices = bluetoothService?.getPairedDevices()
                if (devices != null) {
                    // Update the RecyclerView with the list of paired devices
                    Log.d("BluetoothFragment", "Paired devices found")
                    deviceAdapter.updateDevices(devices.toList())
                }
            }
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            isBound = false
            bluetoothService = null
        }
    }*/

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        bluetoothService = (activity as? MainActivity)?.getBluetoothService()
    }

    private val deviceReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent?.action == BluetoothService.ACTION_DEVICE_FOUND) {
                val device: BluetoothDevice? =
                    intent.getParcelableExtra(BluetoothService.EXTRA_DEVICE)
                device?.let {
                    deviceAdapter.addDevice(it)
                }
            }
        }
    }

    @androidx.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_SCAN)
    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        val root = inflater.inflate(R.layout.fragment_bluetooth, container, false)
        //statusText = root.findViewById(R.id.statusText)
        recyclerView = root.findViewById<RecyclerView>(R.id.recyclerViewDevices)
        scanButton = root.findViewById(R.id.btnScan)
        scanProgress = root.findViewById(R.id.progressScan)
        cancelButton = root.findViewById<ImageButton>(R.id.btnCancel)

        deviceAdapter = DeviceAdapter().apply {
            onConnectClick = { device ->
                // Ensure CONNECT permission, then connect
                val hasConnect = androidx.core.content.ContextCompat.checkSelfPermission(
                    requireContext(), Manifest.permission.BLUETOOTH_CONNECT
                ) == android.content.pm.PackageManager.PERMISSION_GRANTED

                if (!hasConnect) {
                    requestPermissions(arrayOf(Manifest.permission.BLUETOOTH_CONNECT), 2001)
                } else {
                    bluetoothService?.connectTo(device)
                }
            }
        }
        recyclerView.adapter = deviceAdapter
        recyclerView.layoutManager = LinearLayoutManager(requireContext())
        recyclerView.adapter = deviceAdapter

        // Set up cancel button to dismiss the Dialog Fragment
        cancelButton.setOnClickListener {
            dismiss()
        }

        scanButton.setOnClickListener {
            // Check if the BluetoothService is connected and if a scan is in progress
            if (bluetoothService?.isDiscovering() == true) {
                // A scan is in progress, so cancel it
                bluetoothService?.cancelDiscovery()
            } else {
                // 1. Clear the list and re-populate with paired devices
                deviceAdapter.clearDevices()
                val devices = bluetoothService?.getPairedDevices()
                if (devices != null) {
                    deviceAdapter.updateDevices(devices.toList())
                }

                // 2. Start the discovery scan to find new devices
                bluetoothService?.startDiscovery()
            }
        }

        return root
    }

    @RequiresPermission(Manifest.permission.BLUETOOTH_CONNECT)
    override fun onStart() {
        super.onStart()
        // Bind to the service
        /*
        Intent(requireContext(), BluetoothService::class.java).also { intent ->
            requireActivity().bindService(intent, connection, Context.BIND_AUTO_CREATE)
        }*/

        val isAppConnected = (activity as? MainActivity)?.getIsConnected() ?: false

        // 1. Get the currently connected device
        val connectedDevice = bluetoothService?.getConnectedDevice()

        // 2. Get all paired devices and create a new list with updated status
        val pairedDevices = bluetoothService?.getPairedDevices()
        val updatedDeviceList = pairedDevices?.map { device ->
            // Create a new DeviceItem for each device
            DeviceItem(
                device = device,
                // Check if this device is the one that is currently connected
                isConnected = isAppConnected && (device == connectedDevice)
            )
        }

        // 3. Sort the list, placing the connected device at the top
        val sortedList = updatedDeviceList?.sortedByDescending { it.isConnected }

        // 4. Update the adapter with the sorted list
        if (sortedList != null) {
            deviceAdapter.submitList(sortedList) {
                // Scroll to the top if a device is already connected
                if (isAppConnected) {
                    recyclerView.scrollToPosition(0)
                }
            }
        }

        requireContext().registerReceiver(
            discoveryStateReceiver,
            IntentFilter().apply {
                addAction(BluetoothAdapter.ACTION_DISCOVERY_STARTED)
                addAction(BluetoothAdapter.ACTION_DISCOVERY_FINISHED)
            }
        )

        // Resize the dialog
        dialog?.window?.setLayout(
            (resources.displayMetrics.widthPixels * 0.9).toInt(),  // 90% width
            (resources.displayMetrics.heightPixels * 0.8).toInt()  // 60% height
        )

        val lbm = LocalBroadcastManager.getInstance(requireContext())
        lbm.registerReceiver(deviceReceiver,
            IntentFilter(BluetoothService.ACTION_DEVICE_FOUND)
        )
        lbm.registerReceiver(connStateReceiver,
            IntentFilter(BluetoothService.ACTION_CONN_STATE)
        )
        lbm.registerReceiver(messageReceiver,
            IntentFilter(BluetoothService.ACTION_MESSAGE)
        )
    }

    override fun onStop() {
        super.onStop()
        val lbm = androidx.localbroadcastmanager.content.LocalBroadcastManager.getInstance(requireContext())
        lbm.unregisterReceiver(deviceReceiver)
        lbm.unregisterReceiver(connStateReceiver)
        lbm.unregisterReceiver(messageReceiver)
        runCatching { requireContext().unregisterReceiver(discoveryStateReceiver) }
        /*
        if (isBound) {
            requireActivity().unbindService(connection)
            isBound = false
        }*/
    }

    // Function to update the UI based on the status
    fun updateStatus(status: String) {
        statusText.text = status

        val color = when (status) {
            "Connected" -> ContextCompat.getColor(requireContext(), R.color.status_connected)
            "Connecting" -> ContextCompat.getColor(requireContext(), R.color.status_connecting)
            else -> ContextCompat.getColor(requireContext(), R.color.status_disconnected)
        }
        statusText.background.setTint(color)
    }

    private fun updateListState(device: BluetoothDevice?,state: String) {
        val currentList = deviceAdapter.currentList.toMutableList()

        if (state == "disconnected" || state == "error") {
            // If the state is disconnected, clear the connected/connecting state from ALL devices
            val newList = currentList.map { it.copy(isConnecting = false, isConnected = false) }
            deviceAdapter.submitList(newList)
            return
        }

        // Find the device in the current list
        val deviceIndex = currentList.indexOfFirst { it.device.address == device?.address }
        if (deviceIndex == -1) {
            // If the device is not found, we can't update its state, so we just return.
            return
        }

        // Update the state of the found device
        val updatedDeviceItem = when (state) {
            "connecting" -> currentList[deviceIndex].copy(isConnecting = true, isConnected = false)
            "connected" -> currentList[deviceIndex].copy(isConnecting = false, isConnected = true)
            else -> currentList[deviceIndex].copy(isConnecting = false, isConnected = false)
        }
        currentList[deviceIndex] = updatedDeviceItem

        // Sort the list
        val sortedList = currentList.sortedByDescending { it.isConnected }

        // Submit the updated and sorted list to the adapter
        deviceAdapter.submitList(sortedList){
            if (state == "connected") {
                recyclerView.scrollToPosition(0)
            }
        }
    }

    private val connStateReceiver = object : android.content.BroadcastReceiver() {
        override fun onReceive(c: android.content.Context?, i: android.content.Intent?) {
            val state = i?.getStringExtra(com.example.sc2079.service.BluetoothService.EXTRA_CONN_STATE)
            val dev: BluetoothDevice? = if (android.os.Build.VERSION.SDK_INT >= 33)
                // Probably would not go into this branch
                i?.getParcelableExtra(com.example.sc2079.service.BluetoothService.EXTRA_CONN_DEVICE, BluetoothDevice::class.java)
            else
                @Suppress("DEPRECATION") i?.getParcelableExtra(com.example.sc2079.service.BluetoothService.EXTRA_CONN_DEVICE)

            val err = i?.getStringExtra(com.example.sc2079.service.BluetoothService.EXTRA_ERROR)
            Log.d("BluetoothFragment", "conn state=$state dev=${dev?.address} err=$err")

            if (state != null) {
                updateListState(dev, state)
            }

            // Show system message
            if (state == "connected") {
                val deviceName = try { dev?.name } catch (_: SecurityException) { "Unknown Device" }

                Toast.makeText(context, "Connected to $deviceName", Toast.LENGTH_SHORT).show()
            }
        }
    }

    private val messageReceiver = object : android.content.BroadcastReceiver() {
        override fun onReceive(c: android.content.Context?, i: android.content.Intent?) {
            val text = i?.getStringExtra(com.example.sc2079.service.BluetoothService.EXTRA_TEXT)
            val bytes = i?.getByteArrayExtra(com.example.sc2079.service.BluetoothService.EXTRA_BYTES)
            Log.d("BluetoothFragment", "RX: ${text ?: bytes?.joinToString()}")
            // TODO: append to a log view if you have one
        }
    }

    private fun setScanning(scanning: Boolean) {
        scanProgress.visibility = if (scanning) View.VISIBLE else View.GONE
        if (scanning) {
            scanButton.icon = null
            scanButton.text = null
        } else {
            scanButton.setIconResource(R.drawable.ic_bluetooth_searching_24dp)
            scanButton.text = "Scan"
            scanButton.iconTint = ContextCompat.getColorStateList(requireContext(), android.R.color.white)
        }
    }

    private val discoveryStateReceiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context?, i: Intent?) {
            when (i?.action) {
                BluetoothAdapter.ACTION_DISCOVERY_STARTED -> setScanning(true)
                BluetoothAdapter.ACTION_DISCOVERY_FINISHED -> setScanning(false)
            }
        }
    }

}