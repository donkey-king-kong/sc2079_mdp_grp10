package com.example.sc2079.service

import android.Manifest
import android.app.Service
import android.bluetooth.*
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.os.Binder
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.util.Log
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import androidx.annotation.RequiresApi
import androidx.annotation.RequiresPermission
import androidx.core.content.ContextCompat
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.io.IOException
import java.util.*


class BluetoothService : Service() {
    private val binder = LocalBinder()
    private lateinit var bluetoothManager: BluetoothManager
    private var bluetoothAdapter: BluetoothAdapter? = null
    private var bluetoothDevice: BluetoothDevice? = null
    private var bluetoothSocket: BluetoothSocket? = null
    private var lastConnectedDevice: BluetoothDevice? = null
    private var reconnectAttempts = 0
    private val MAX_RECONNECT_ATTEMPTS = 3
    // UUID for serial port profile (SPP)
    private val uuid: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")

    // Class-tied singletons, one instance per containing class
    companion object {
        const val ACTION_DEVICE_FOUND = "com.example.sc2079.ACTION_DEVICE_FOUND"
        const val EXTRA_DEVICE = "com.example.sc2079.EXTRA_DEVICE"
        const val ACTION_CONN_STATE = "com.example.sc2079.ACTION_CONN_STATE"
        const val EXTRA_CONN_STATE = "com.example.sc2079.EXTRA_CONN_STATE"   // "connecting"|"connected"|"disconnected"|"error"
        const val EXTRA_CONN_DEVICE = "com.example.sc2079.EXTRA_CONN_DEVICE"
        const val EXTRA_ERROR = "com.example.sc2079.EXTRA_ERROR"
        const val ACTION_MESSAGE = "com.example.sc2079.ACTION_MESSAGE"
        const val EXTRA_BYTES = "com.example.sc2079.EXTRA_BYTES"
        const val EXTRA_TEXT = "com.example.sc2079.EXTRA_TEXT"
        const val MAX_RECONNECT_ATTEMPTS = 3
        const val TAG = "BluetoothService"
    }

    private val serviceScope = kotlinx.coroutines.CoroutineScope(
        kotlinx.coroutines.SupervisorJob() + kotlinx.coroutines.Dispatchers.IO
    )

    @Volatile private var connectJob: kotlinx.coroutines.Job? = null
    @Volatile private var readerJob: Job? = null

    @RequiresPermission(Manifest.permission.BLUETOOTH_CONNECT)
    fun connectTo(device: BluetoothDevice) {
        // avoid concurrent connects
        connectJob?.cancel()
        readerJob?.cancel()
        connectJob = serviceScope.launch {
            Log.d(TAG, "Attempting to connect to device: ${device.name}")
            sendConnState("connecting", device)
            try {
                // discovery interferes with connect; always cancel first
                bluetoothAdapter?.cancelDiscovery()

                if (!ensureBonded(device)) {
                    sendConnState("error", device, "Pairing required or failed.")
                    sendConnState("disconnected", device)
                    lastConnectedDevice = null
                    Log.e(TAG, "Pairing failed or was not possible.")
                    return@launch
                }

                // Try insecure first (often works without pairing), fall back to secure SPP
                val sock = tryCreateInsecureSocket(device) ?: device.createRfcommSocketToServiceRecord(uuid)
                Log.d(TAG, "Created BluetoothSocket. Now attempting to connect()...")

                // close any previous connection
                synchronized(this@BluetoothService) { bluetoothSocket?.close() }
                Log.d(TAG, "Closed any previous socket.")

                sock.connect() // blocking
                Log.d(TAG, "Socket connected successfully!")

                synchronized(this@BluetoothService) {
                    bluetoothSocket = sock
                    bluetoothDevice = device
                    lastConnectedDevice = device
                    reconnectAttempts = 0
                }
                sendConnState("connected", device)
                startReader(sock)
            } catch (e: IOException) {
                Log.e(TAG, "connect failed", e)
                sendConnState("error", device, e.message ?: "IO error")
                closeQuietly()
                sendConnState("disconnected", device)
            } catch (e: SecurityException) {
                Log.e(TAG, "connect security", e)
                sendConnState("error", device, "Missing BLUETOOTH_CONNECT")
                closeQuietly()
                sendConnState("disconnected", device)
            }
        }
    }

    private fun tryCreateInsecureSocket(device: BluetoothDevice): BluetoothSocket? = try {
        device.createInsecureRfcommSocketToServiceRecord(uuid)
    } catch (_: Exception) {
        // older stacks: reflection fallback to channel 1
        try {
            val m = device.javaClass.getMethod("createRfcommSocket", Int::class.javaPrimitiveType)
            m.invoke(device, 1) as BluetoothSocket
        } catch (_: Exception) { null }
    }

    private fun startReader(sock: BluetoothSocket) {
        serviceScope.launch {
            val input = sock.inputStream
            val buf = ByteArray(1024)
            try {
                while (true) {
                    val n = input.read(buf)
                    if (n == -1) break
                    val data = buf.copyOf(n)
                    LocalBroadcastManager.getInstance(applicationContext).sendBroadcast(
                        Intent(ACTION_MESSAGE).apply {
                            putExtra(EXTRA_BYTES, data)
                            putExtra(EXTRA_TEXT, runCatching { String(data) }.getOrNull())
                        }
                    )
                }
            } catch (e: IOException) {
                Log.w(TAG, "reader ended: ${e.message}")
            } finally {
                closeQuietly()
                sendConnState("disconnected", bluetoothDevice)
                reconnect()
            }
        }
    }

    @RequiresPermission(Manifest.permission.BLUETOOTH_CONNECT)
    fun write(bytes: ByteArray): Boolean {
        val s = bluetoothSocket ?: return false
        return try {
            s.outputStream.write(bytes)
            s.outputStream.flush()
            true
        } catch (e: IOException) {
            Log.e(TAG, "write failed", e)
            false
        }
    }

    fun disconnect() {
        serviceScope.launch {
            lastConnectedDevice = null
            reconnectAttempts = 0
            closeQuietly()
            sendConnState("disconnected", bluetoothDevice)
        }
    }

    @RequiresPermission(Manifest.permission.BLUETOOTH_CONNECT)
    private fun reconnect() {
        if (lastConnectedDevice != null && reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
            reconnectAttempts++
            Log.d(TAG, "Connection lost. Attempting to reconnect (Attempt $reconnectAttempts of $MAX_RECONNECT_ATTEMPTS)")
            serviceScope.launch {
                delay(2000L) // Wait 2 seconds before retrying
                connectTo(lastConnectedDevice!!)
            }
        } else {
            Log.d(TAG, "Reconnection attempts exhausted or no device to reconnect to.")
            lastConnectedDevice = null
        }
    }

    private fun closeQuietly() {
        try { bluetoothSocket?.close() } catch (_: IOException) {}
        bluetoothSocket = null
    }

    private fun sendConnState(state: String, device: BluetoothDevice?, error: String? = null) {
        LocalBroadcastManager.getInstance(applicationContext).sendBroadcast(
            Intent(ACTION_CONN_STATE).apply {
                putExtra(EXTRA_CONN_STATE, state)
                putExtra(EXTRA_CONN_DEVICE, device)
                if (error != null) putExtra(EXTRA_ERROR, error)
            }
        )
    }

    // Implement BluetoothService as a bound service
    inner class LocalBinder : Binder() {
        fun getService(): BluetoothService = this@BluetoothService
    }

    override fun onBind(intent: Intent?): IBinder {
        return binder
    }

    override fun onCreate() {
        super.onCreate()

        // Get BluetoothAdapter
        bluetoothManager = getSystemService(BluetoothManager::class.java)
        bluetoothAdapter = bluetoothManager.getAdapter()

        if (bluetoothAdapter == null) {
            Log.e("TAG", "Device doesn't support Bluetooth")
            stopSelf() // Stop the service if not supported
        }

        val filter = IntentFilter(BluetoothDevice.ACTION_FOUND)
        registerReceiver(receiver, filter)
        Log.d("TAG", "BluetoothService created")
    }

    fun isBluetoothEnabled(): Boolean {
        return bluetoothAdapter?.isEnabled == true
    }

    @RequiresPermission(Manifest.permission.BLUETOOTH_SCAN)
    fun isDiscovering(): Boolean {
        return bluetoothAdapter?.isDiscovering == true
    }

    @RequiresPermission(Manifest.permission.BLUETOOTH_CONNECT)
    fun getPairedDevices(): Set<BluetoothDevice> =
        bluetoothAdapter?.bondedDevices ?: emptySet()

    @RequiresPermission(Manifest.permission.BLUETOOTH_CONNECT)
    fun getConnectedDevice(): BluetoothDevice? {
        return bluetoothDevice
    }

    @RequiresPermission(Manifest.permission.BLUETOOTH_SCAN)
    fun startDiscovery() {
        bluetoothAdapter?.let { adapter ->
            if (adapter.isDiscovering) {
                Log.d("TAG", "Canceling ongoing discovery")
                adapter.cancelDiscovery()
            }
            val ok = adapter.startDiscovery()
            Log.d("TAG", "startDiscovery() -> $ok")
            if (!ok) {
                // Often due to missing runtime perms or BT off
                // (or discovery throttling if called too frequently)
                Log.w("TAG", "Discovery failed to start")
            }
        } ?: Log.e("TAG", "No adapter")
    }

    @RequiresPermission(Manifest.permission.BLUETOOTH_SCAN)
    fun cancelDiscovery() {
        bluetoothAdapter?.cancelDiscovery()
    }

    private val receiver = object : android.content.BroadcastReceiver() {
        override fun onReceive(context: android.content.Context?, intent: Intent?) {
            Log.d("TAG", "Received action: ${intent?.action}")
            if (intent?.action == BluetoothDevice.ACTION_FOUND) {
                val device: BluetoothDevice? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
                } else {
                    @Suppress("DEPRECATION")
                    intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
                }
                device?.let {
                    Log.d("TAG", "Found device: ${it.name} - ${it.address}")
                    val broadcastIntent = Intent(ACTION_DEVICE_FOUND).apply {
                        putExtra(EXTRA_DEVICE, it)
                    }
                    LocalBroadcastManager.getInstance(applicationContext).sendBroadcast(broadcastIntent)
                }
            }
        }
    }

    @RequiresPermission(Manifest.permission.BLUETOOTH_CONNECT)
    private suspend fun ensureBonded(device: BluetoothDevice): Boolean {
        if (device.bondState == BluetoothDevice.BOND_BONDED) return true
        if (device.bondState == BluetoothDevice.BOND_NONE) device.createBond()

        return kotlinx.coroutines.suspendCancellableCoroutine { cont ->
            val f = IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
            val r = object : android.content.BroadcastReceiver() {
                override fun onReceive(c: Context?, i: Intent?) {
                    val d = if (android.os.Build.VERSION.SDK_INT >= 33)
                        i?.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
                    else
                        @Suppress("DEPRECATION") i?.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
                    if (d?.address != device.address) return
                    when (i?.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, -1)) {
                        BluetoothDevice.BOND_BONDED -> { applicationContext.unregisterReceiver(this); cont.resume(true) {} }
                        BluetoothDevice.BOND_NONE   -> { applicationContext.unregisterReceiver(this); cont.resume(false) {} }
                    }
                }
            }
            applicationContext.registerReceiver(r, f)
        }
    }


    override fun onDestroy() {
        super.onDestroy()
        connectJob?.cancel()
        readerJob?.cancel()
        serviceScope.cancel()
        try { unregisterReceiver(receiver) } catch (_: IllegalArgumentException) {}
        closeQuietly()
    }
}