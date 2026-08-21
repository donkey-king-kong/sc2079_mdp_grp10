package com.example.sc2079.ui.bluetooth

import android.Manifest
import android.bluetooth.BluetoothDevice
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.TextView
import androidx.annotation.RequiresPermission
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.RecyclerView
import androidx.recyclerview.widget.ListAdapter
import com.example.sc2079.R
import com.google.android.material.progressindicator.CircularProgressIndicator

class DeviceAdapter : ListAdapter<DeviceItem, DeviceAdapter.DeviceViewHolder>(DeviceDiffCallback()) {

    var onConnectClick: ((BluetoothDevice) -> Unit)? = null
    // A view holder that holds the view for a single device item.
    class DeviceViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        // Find the TextViews from the custom app layout.
        private val deviceName: TextView = itemView.findViewById(R.id.deviceName)
        private val btnConnect: Button = itemView.findViewById(R.id.btnConnect)
        private val connectProgress: CircularProgressIndicator = itemView.findViewById(R.id.progressConnect)
        private val connectStatus: View = itemView.findViewById(R.id.connectedStatus)
        // The deviceAddress TextView is not being used for now, as requested.
        // private val deviceAddress: TextView = itemView.findViewById(R.id.deviceAddress)

        @RequiresPermission(Manifest.permission.BLUETOOTH_CONNECT)
        fun bind(deviceItem: DeviceItem, onConnect: ((BluetoothDevice) -> Unit)?) {
            val device = deviceItem.device
            val name = try { device.name } catch (_: SecurityException) { null }
            deviceName.text = name ?: "Unknown Device"
            if(deviceItem.isConnecting){
                // Show spinner, hide text
                connectProgress.visibility = View.VISIBLE
                btnConnect.text = null
            }
            else if(deviceItem.isConnected){
                // Show connected status, hide spinner
                connectStatus.visibility = View.VISIBLE
                connectProgress.visibility = View.GONE
                btnConnect.text = "Disconnect"
            }
            else{
                connectStatus.visibility = View.GONE
                connectProgress.visibility = View.GONE
                btnConnect.text = "Connect"
            }
            connectProgress.visibility = if (deviceItem.isConnecting) View.VISIBLE else View.GONE
            btnConnect.setOnClickListener { onConnect?.invoke(device) }
        }
    }

    // This method is now corrected to inflate YOUR custom layout XML.
    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): DeviceViewHolder {
        val view = LayoutInflater.from(parent.context)
            .inflate(R.layout.list_item_device, parent, false)
        return DeviceViewHolder(view)
    }

    // This method is called to bind the data to a view holder.
    @RequiresPermission(Manifest.permission.BLUETOOTH_CONNECT)
    override fun onBindViewHolder(holder: DeviceViewHolder, position: Int) {
        holder.bind(getItem(position), onConnectClick)
    }

    fun updateDevices(newDevices: List<BluetoothDevice>) {
        // Map BluetoothDevice list to our new DeviceItem list
        val newDeviceItems = newDevices.map { DeviceItem(it) }
        submitList(newDeviceItems)
    }

    fun addDevice(device: BluetoothDevice) {
        val currentList = currentList.toMutableList()
        // Only add the device if it's not already in the list to prevent duplicates.
        if (!currentList.any { it.device.address == device.address }) {
            currentList.add(DeviceItem(device))
            submitList(currentList)
        }
    }

    fun clearDevices() {
        submitList(null)
    }
}

// DiffUtil is used to calculate the difference between two lists and
// efficiently update the RecyclerView.
class DeviceDiffCallback : DiffUtil.ItemCallback<DeviceItem>() {
    // Check if the items are the same based on a unique identifier (the address).
    override fun areItemsTheSame(oldItem: DeviceItem, newItem: DeviceItem): Boolean {
        return oldItem.device.address == newItem.device.address
    }

    // Check if the contents of the items are the same.
    @RequiresPermission(Manifest.permission.BLUETOOTH_CONNECT)
    override fun areContentsTheSame(oldItem: DeviceItem, newItem: DeviceItem): Boolean {
        return oldItem == newItem
    }
}

