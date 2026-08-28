package com.example.sc2079.ui.coordinates

import android.graphics.Color
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.GridLayout
import android.widget.LinearLayout
import android.widget.TextView
import androidx.core.content.ContextCompat
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.activityViewModels
import com.example.sc2079.ObstacleData
import com.example.sc2079.R

class PlaceObstacleDialogFragment : DialogFragment() {

    private val sharedViewModel: SharedViewModel by activityViewModels()

    private var selectedX = 0
    private var selectedY = 0
    private var selectedDirection: ObstacleData.Direction = ObstacleData.Direction.NORTH

    private lateinit var layoutCoordSelection: LinearLayout
    private lateinit var layoutDirectionSelection: LinearLayout

    private lateinit var gridX: GridLayout
    private lateinit var gridY: GridLayout
    private lateinit var tvXLabel: TextView
    private lateinit var tvYLabel: TextView
    private lateinit var tvCoordSummary: TextView
    private lateinit var tvDirectionTitle: TextView

    private val xButtons = mutableListOf<Button>()
    private val yButtons = mutableListOf<Button>()

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        val root = inflater.inflate(R.layout.dialog_place_obstacle, container, false)

        layoutCoordSelection = root.findViewById(R.id.layout_coord_selection)
        layoutDirectionSelection = root.findViewById(R.id.layout_direction_selection)

        gridX = root.findViewById(R.id.grid_x)
        gridY = root.findViewById(R.id.grid_y)
        tvXLabel = root.findViewById(R.id.tv_x_label)
        tvYLabel = root.findViewById(R.id.tv_y_label)
        tvCoordSummary = root.findViewById(R.id.tv_coord_summary)
        tvDirectionTitle = root.findViewById(R.id.tv_direction_title)

        setupCoordinateGrids()

        root.findViewById<Button>(R.id.btn_cancel_coord).setOnClickListener { dismiss() }
        root.findViewById<Button>(R.id.btn_cancel_dir).setOnClickListener { dismiss() }

        root.findViewById<Button>(R.id.btn_next).setOnClickListener {
            layoutCoordSelection.visibility = View.GONE
            layoutDirectionSelection.visibility = View.VISIBLE
            tvDirectionTitle.text = "Add Obstacle at ($selectedX, $selectedY)"
        }

        root.findViewById<Button>(R.id.btn_north).setOnClickListener { selectedDirection = ObstacleData.Direction.NORTH; updateDirectionButtons(root) }
        root.findViewById<Button>(R.id.btn_south).setOnClickListener { selectedDirection = ObstacleData.Direction.SOUTH; updateDirectionButtons(root) }
        root.findViewById<Button>(R.id.btn_east).setOnClickListener { selectedDirection = ObstacleData.Direction.EAST; updateDirectionButtons(root) }
        root.findViewById<Button>(R.id.btn_west).setOnClickListener { selectedDirection = ObstacleData.Direction.WEST; updateDirectionButtons(root) }

        root.findViewById<Button>(R.id.btn_go).setOnClickListener {
            sharedViewModel.newObstacleRequest.postValue(ObstacleAddition(selectedX, selectedY, selectedDirection))
            dismiss()
        }

        updateDirectionButtons(root)

        return root
    }

    private fun setupCoordinateGrids() {
        for (i in 0..19) {
            val btnX = createGridButton(i, true)
            xButtons.add(btnX)
            gridX.addView(btnX)

            val btnY = createGridButton(i, false)
            yButtons.add(btnY)
            gridY.addView(btnY)
        }
        updateSelectionStyles()
    }

    private fun createGridButton(value: Int, isX: Boolean): Button {
        val btn = Button(requireContext(), null, com.google.android.material.R.style.Widget_Material3_Button_OutlinedButton)
        btn.text = value.toString()
        btn.setPadding(0, 0, 0, 0)
        btn.minWidth = 0
        btn.minHeight = 0
        btn.textSize = 10f
        
        val params = GridLayout.LayoutParams()
        params.width = 0
        params.height = ViewGroup.LayoutParams.WRAP_CONTENT
        params.columnSpec = GridLayout.spec(GridLayout.UNDEFINED, 1f)
        params.setMargins(2, 2, 2, 2)
        btn.layoutParams = params

        btn.setOnClickListener {
            if (isX) selectedX = value else selectedY = value
            updateSelectionStyles()
            updateLabels()
        }
        return btn
    }

    private fun updateSelectionStyles() {
        xButtons.forEachIndexed { index, button ->
            if (index == selectedX) {
                button.setBackgroundColor(ContextCompat.getColor(requireContext(), R.color.cyan))
                button.setTextColor(Color.WHITE)
            } else {
                button.setBackgroundColor(Color.WHITE)
                button.setTextColor(Color.BLACK)
            }
        }
        yButtons.forEachIndexed { index, button ->
            if (index == selectedY) {
                button.setBackgroundColor(ContextCompat.getColor(requireContext(), R.color.cyan))
                button.setTextColor(Color.WHITE)
            } else {
                button.setBackgroundColor(Color.WHITE)
                button.setTextColor(Color.BLACK)
            }
        }
    }

    private fun updateLabels() {
        tvXLabel.text = "Row (X): $selectedX"
        tvYLabel.text = "Col (Y): $selectedY"
        tvCoordSummary.text = "($selectedX, $selectedY)"
    }

    private fun updateDirectionButtons(root: View) {
        val n = root.findViewById<Button>(R.id.btn_north)
        val s = root.findViewById<Button>(R.id.btn_south)
        val e = root.findViewById<Button>(R.id.btn_east)
        val w = root.findViewById<Button>(R.id.btn_west)

        val selectedColor = ContextCompat.getColor(requireContext(), R.color.cyan)
        val defaultColor = Color.parseColor("#F2F2F2")

        n.setBackgroundColor(if (selectedDirection == ObstacleData.Direction.NORTH) selectedColor else defaultColor)
        s.setBackgroundColor(if (selectedDirection == ObstacleData.Direction.SOUTH) selectedColor else defaultColor)
        e.setBackgroundColor(if (selectedDirection == ObstacleData.Direction.EAST) selectedColor else defaultColor)
        w.setBackgroundColor(if (selectedDirection == ObstacleData.Direction.WEST) selectedColor else defaultColor)
        
        val textColor = if (selectedDirection == ObstacleData.Direction.NORTH) Color.WHITE else Color.BLACK
        n.setTextColor(if (selectedDirection == ObstacleData.Direction.NORTH) Color.WHITE else Color.BLACK)
        s.setTextColor(if (selectedDirection == ObstacleData.Direction.SOUTH) Color.WHITE else Color.BLACK)
        e.setTextColor(if (selectedDirection == ObstacleData.Direction.EAST) Color.WHITE else Color.BLACK)
        w.setTextColor(if (selectedDirection == ObstacleData.Direction.WEST) Color.WHITE else Color.BLACK)
    }

    override fun onStart() {
        super.onStart()
        dialog?.window?.setLayout(
            (resources.displayMetrics.widthPixels * 0.9).toInt(),
            ViewGroup.LayoutParams.WRAP_CONTENT
        )
    }
}
