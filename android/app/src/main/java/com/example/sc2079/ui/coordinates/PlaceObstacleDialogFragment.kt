package com.example.sc2079.ui.coordinates

import android.os.Bundle
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import androidx.cardview.widget.CardView
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.activityViewModels
import com.example.sc2079.MainActivity
import com.example.sc2079.ObstacleData
import com.example.sc2079.R
import com.example.sc2079.ui.NIGHT
import com.example.sc2079.ui.Palette
import com.example.sc2079.ui.box

class PlaceObstacleDialogFragment : DialogFragment() {

    companion object {
        private const val ARG_IS_VEHICLE_MODE = "isVehicleMode"

        @JvmStatic
        @JvmOverloads
        fun newInstance(isVehicleMode: Boolean = false): PlaceObstacleDialogFragment {
            return PlaceObstacleDialogFragment().apply {
                arguments = Bundle().apply {
                    putBoolean(ARG_IS_VEHICLE_MODE, isVehicleMode)
                }
            }
        }
    }

    private val sharedViewModel: SharedViewModel by activityViewModels()

    private var isVehicleMode = false
    private var selectedX = 0
    private var selectedY = 0
    private var selectedDirection: ObstacleData.Direction = ObstacleData.Direction.NORTH
    private var hasSelectedX = false
    private var hasSelectedY = false
    private var hasSelectedDirection = false

    private lateinit var gridX: LinearLayout
    private lateinit var gridY: LinearLayout
    private lateinit var tvXLabel: TextView
    private lateinit var tvYLabel: TextView
    private lateinit var tvCoordSummary: TextView
    private lateinit var tvPlaceTitle: TextView
    private lateinit var tvDirectionLabel: TextView
    private lateinit var btnGo: Button

    private val xButtons = mutableListOf<Button>()
    private val yButtons = mutableListOf<Button>()

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        val root = inflater.inflate(R.layout.dialog_place_obstacle, container, false)
        dialog?.window?.setBackgroundDrawableResource(android.R.color.transparent)

        val palette = (activity as? MainActivity)?.currentPalette() ?: NIGHT
        isVehicleMode = arguments?.getBoolean(ARG_IS_VEHICLE_MODE) ?: false
        val placementLabel = if (isVehicleMode) "Vehicle" else "Obstacle"

        gridX = root.findViewById(R.id.grid_x)
        gridY = root.findViewById(R.id.grid_y)
        tvXLabel = root.findViewById(R.id.tv_x_label)
        tvYLabel = root.findViewById(R.id.tv_y_label)
        tvCoordSummary = root.findViewById(R.id.tv_coord_summary)
        tvPlaceTitle = root.findViewById(R.id.tv_place_title)
        tvDirectionLabel = root.findViewById(R.id.tv_direction_label)
        btnGo = root.findViewById(R.id.btn_go)

        val card = root as? CardView
        card?.setCardBackgroundColor(palette.surface2)
        tvPlaceTitle.text = "Place $placementLabel"
        tvPlaceTitle.setTextColor(palette.text)
        tvXLabel.text = "X"
        tvXLabel.setTextColor(palette.textMuted)
        tvYLabel.text = "Y"
        tvYLabel.setTextColor(palette.textMuted)
        tvCoordSummary.text = "-"
        tvCoordSummary.setTextColor(palette.textMuted)
        tvCoordSummary.background = box(requireContext(), palette.surface, palette.borderStrong, 24f, 1f)
        tvDirectionLabel.setTextColor(palette.textMuted)
        (card?.getChildAt(0) as? ViewGroup)?.getChildAt(2)?.setBackgroundColor(palette.borderStrong)

        setupCoordinateGrids(palette)

        root.findViewById<Button>(R.id.btn_cancel_coord).apply {
            background = box(requireContext(), palette.redDim, palette.redBorder, 8f, 1f)
            backgroundTintList = null
            setTextColor(palette.red)
            setOnClickListener { dismiss() }
        }

        root.findViewById<Button>(R.id.btn_north).setOnClickListener {
            selectedDirection = ObstacleData.Direction.NORTH
            hasSelectedDirection = true
            updateDirectionButtons(root, palette)
            updatePlaceButtonState(palette)
        }
        root.findViewById<Button>(R.id.btn_south).setOnClickListener {
            selectedDirection = ObstacleData.Direction.SOUTH
            hasSelectedDirection = true
            updateDirectionButtons(root, palette)
            updatePlaceButtonState(palette)
        }
        root.findViewById<Button>(R.id.btn_east).setOnClickListener {
            selectedDirection = ObstacleData.Direction.EAST
            hasSelectedDirection = true
            updateDirectionButtons(root, palette)
            updatePlaceButtonState(palette)
        }
        root.findViewById<Button>(R.id.btn_west).setOnClickListener {
            selectedDirection = ObstacleData.Direction.WEST
            hasSelectedDirection = true
            updateDirectionButtons(root, palette)
            updatePlaceButtonState(palette)
        }

        btnGo.setOnClickListener {
            val request = ObstacleAddition(selectedX, selectedY, selectedDirection)
            if (isVehicleMode) {
                sharedViewModel.newVehicleRequest.postValue(request)
            } else {
                sharedViewModel.newObstacleRequest.postValue(request)
            }
            dismiss()
        }

        updateDirectionButtons(root, palette)
        updatePlaceButtonState(palette)

        return root
    }

    private fun setupCoordinateGrids(palette: Palette) {
        buildNumberGrid(gridX, true, palette)
        buildNumberGrid(gridY, false, palette)
    }

    private fun buildNumberGrid(container: LinearLayout, isX: Boolean, palette: Palette) {
        container.removeAllViews()
        val buttons = if (isX) xButtons else yButtons
        buttons.clear()

        // 4 rows of 5 buttons = 0..19
        for (row in 0..3) {
            val rowLayout = LinearLayout(requireContext())
            rowLayout.orientation = LinearLayout.HORIZONTAL
            rowLayout.layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            )

            for (col in 0..4) {
                val value = row * 5 + col
                val btn = Button(requireContext())
                btn.text = value.toString()
                btn.background = box(requireContext(), palette.surface, palette.borderStrong, 6f, 1f)
                btn.backgroundTintList = null
                btn.setTextColor(palette.textMuted)
                btn.setPadding(0, 0, 0, 0)
                btn.minWidth = 0
                btn.minHeight = 0
                btn.textSize = 11f
                btn.stateListAnimator = null

                val params = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
                params.setMargins(2, 2, 2, 2)
                btn.layoutParams = params

                btn.setOnClickListener {
                    if (isX) {
                        selectedX = value
                        hasSelectedX = true
                    } else {
                        selectedY = value
                        hasSelectedY = true
                    }
                    updateSelectionStyles(palette)
                    updateLabels(palette)
                    updatePlaceButtonState(palette)
                }

                buttons.add(btn)
                rowLayout.addView(btn)
            }
            container.addView(rowLayout)
        }
    }

    private fun updateSelectionStyles(palette: Palette) {
        xButtons.forEachIndexed { index, button ->
            if (hasSelectedX && index == selectedX) {
                button.background = box(requireContext(), palette.accent, palette.accent, 4f, 1f)
                button.backgroundTintList = null
                button.setTextColor(palette.surface2)
            } else {
                button.background = box(requireContext(), palette.surface, palette.borderStrong, 4f, 1f)
                button.backgroundTintList = null
                button.setTextColor(palette.textMuted)
            }
        }
        yButtons.forEachIndexed { index, button ->
            if (hasSelectedY && index == selectedY) {
                button.background = box(requireContext(), palette.accent, palette.accent, 4f, 1f)
                button.backgroundTintList = null
                button.setTextColor(palette.surface2)
            } else {
                button.background = box(requireContext(), palette.surface, palette.borderStrong, 4f, 1f)
                button.backgroundTintList = null
                button.setTextColor(palette.textMuted)
            }
        }
    }

    private fun updateLabels(palette: Palette) {
        tvCoordSummary.text = "$selectedX,$selectedY"
        tvCoordSummary.setTextColor(palette.text)
    }

    private fun updateDirectionButtons(root: View, palette: Palette) {
        val n = root.findViewById<Button>(R.id.btn_north)
        val s = root.findViewById<Button>(R.id.btn_south)
        val e = root.findViewById<Button>(R.id.btn_east)
        val w = root.findViewById<Button>(R.id.btn_west)

        updateDirectionButton(n, ObstacleData.Direction.NORTH, palette)
        updateDirectionButton(s, ObstacleData.Direction.SOUTH, palette)
        updateDirectionButton(e, ObstacleData.Direction.EAST, palette)
        updateDirectionButton(w, ObstacleData.Direction.WEST, palette)
    }

    private fun updateDirectionButton(button: Button, direction: ObstacleData.Direction, palette: Palette) {
        if (hasSelectedDirection && selectedDirection == direction) {
            button.background = box(requireContext(), palette.accent, palette.accent, 8f, 0f)
            button.backgroundTintList = null
            button.setTextColor(palette.surface2)
        } else {
            button.background = box(requireContext(), palette.surface, palette.borderStrong, 8f, 1f)
            button.backgroundTintList = null
            button.setTextColor(palette.textMuted)
        }
    }

    private fun updatePlaceButtonState(palette: Palette) {
        val canPlace = hasSelectedDirection && hasSelectedX && hasSelectedY
        btnGo.isEnabled = canPlace
        if (canPlace) {
            btnGo.background = box(requireContext(), palette.accent, palette.accent, 8f, 0f)
            btnGo.backgroundTintList = null
            btnGo.setTextColor(palette.surface2)
        } else {
            btnGo.background = box(requireContext(), palette.surface, palette.borderStrong, 8f, 1f)
            btnGo.backgroundTintList = null
            btnGo.setTextColor(palette.textMuted)
        }
    }

    override fun onStart() {
        super.onStart()
        dialog?.window?.setBackgroundDrawableResource(android.R.color.transparent)
        val maxWidthPx = (500 * resources.displayMetrics.density).toInt()
        val desiredWidthPx = (resources.displayMetrics.widthPixels * 0.9).toInt()
        dialog?.window?.setLayout(
            minOf(desiredWidthPx, maxWidthPx),
            ViewGroup.LayoutParams.WRAP_CONTENT
        )
        Log.d("PlaceDialog", "Dialog width px: ${(resources.displayMetrics.widthPixels * 0.9).toInt()}")
        Log.d("PlaceDialog", "Screen density: ${resources.displayMetrics.density}")
    }
}
