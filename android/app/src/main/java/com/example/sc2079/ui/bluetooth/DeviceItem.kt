package com.example.sc2079.ui.bluetooth

import android.bluetooth.BluetoothDevice

data class DeviceItem(
    val device: BluetoothDevice,
    val isConnecting: Boolean = false,
    val isConnected: Boolean = false
)
