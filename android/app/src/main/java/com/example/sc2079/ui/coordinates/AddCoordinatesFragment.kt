package com.example.sc2079.ui.coordinates;

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
import android.widget.EditText
import android.widget.ImageButton
import android.widget.TextView
import android.widget.Toast
import androidx.annotation.RequiresPermission
import androidx.core.content.ContextCompat
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.activityViewModels
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.example.sc2079.MainActivity
import com.example.sc2079.R
import com.example.sc2079.service.BluetoothService
import com.google.android.material.button.MaterialButton
import com.google.android.material.progressindicator.CircularProgressIndicator

class AddCoordinateFragment : DialogFragment() {

    private val sharedViewModel: SharedViewModel by activityViewModels()

    private lateinit var etX: EditText
    private lateinit var etY: EditText
    private lateinit var btnAdd: ImageButton
    private lateinit var btnCancel: ImageButton

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        val root = inflater.inflate(R.layout.add_coordinates, container, false)

        etX = root.findViewById(R.id.addXCoords)
        etY = root.findViewById(R.id.addYCoords)
        btnAdd = root.findViewById(R.id.addObstacleButton)
        btnCancel = root.findViewById(R.id.cancelButton)

        btnCancel.setOnClickListener {
            dismiss()
        }

        btnAdd.setOnClickListener {
            val x = etX.text.toString().toFloatOrNull()
            val y = etY.text.toString().toFloatOrNull()

            if (x == null || y == null) {
                Toast.makeText(requireContext(), "Invalid input", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }

            sharedViewModel.newCoordinate.postValue(Pair(x, y))
            Toast.makeText(requireContext(), "Added: ($x, $y)", Toast.LENGTH_SHORT).show()

            dismiss()
        }

        return root
    }

    override fun onStart() {
        super.onStart()
        dialog?.window?.setLayout(
            (resources.displayMetrics.widthPixels * 0.85).toInt(),
            ViewGroup.LayoutParams.WRAP_CONTENT
        )
    }
}