//package com.example.sc2079.ui.arena
//
//import android.content.Context
//import android.graphics.Canvas
//import android.graphics.Color
//import android.graphics.Paint
//import android.util.AttributeSet
//import android.view.MotionEvent
//import android.view.View
//import androidx.lifecycle.LifecycleOwner
//import androidx.lifecycle.Observer
//import androidx.lifecycle.get
//import kotlin.math.floor
//
//// Assuming you have these data classes for your ViewModel
//data class Coord(var x: Int, var y: Int)
//data class GridCar(var coord: Coord, var direction: String)
//data class GridObstacle(var coord: Coord, var value: String, var number: String?)
//
//class ArenaGridView @JvmOverloads constructor(
//    context: Context,
//    attrs: AttributeSet? = null,
//    defStyleAttr: Int = 0
//) : View(context, attrs, defStyleAttr) {
//
//    private val gridPaint = Paint().apply {
//        color = Color.BLACK
//        style = Paint.Style.STROKE
//        strokeWidth = 2f
//    }
//    private val robotPaint = Paint().apply {
//        color = Color.RED
//    }
//    private val obstaclePaint = Paint().apply {
//        color = Color.BLACK
//    }
//    private val obstacleTextPaint = Paint().apply {
//        color = Color.WHITE
//        textSize = 40f
//        textAlign = Paint.Align.CENTER
//    }
//
//    private var viewModel: MainViewModel? = null
//    private var isDragging = false
//    private var draggedObstacle: GridObstacle? = null
//
//    companion object {
//        const val GRID_SIZE = 20
//    }
//
//    fun setViewModel(owner: LifecycleOwner, viewModel: MainViewModel) {
//        this.viewModel = viewModel
//        // Observe LiveData from the ViewModel to trigger redraws
//        viewModel.car.observe(owner) {
//            invalidate() // Request a redraw
//        }
//        viewModel.obstaclesList.observe(owner, Observer {
//            invalidate() // Request a redraw
//        })
//    }
//
//    override fun onDraw(canvas: Canvas) {
//        super.onDraw(canvas)
//
//        val cellWidth = width.toFloat() / GRID_SIZE
//        val cellHeight = height.toFloat() / GRID_SIZE
//
//        // Draw grid lines
//        for (i in 0..GRID_SIZE) {
//            canvas.drawLine(i * cellWidth, 0f, i * cellWidth, height.toFloat(), gridPaint)
//            canvas.drawLine(0f, i * cellHeight, width.toFloat(), i * cellHeight, gridPaint)
//        }
//
//        // Draw robot
//        viewModel?.car?.value?.let { car ->
//            val left = car.coord.x * cellWidth
//            val top = (GRID_SIZE - 1 - car.coord.y) * cellHeight
//            canvas.drawRect(left, top, left + cellWidth, top + cellHeight, robotPaint)
//        }
//
//        // Draw obstacles
//        viewModel?.obstaclesList?.value?.let { obstacles ->
//            for (obstacle in obstacles) {
//                if (obstacle == draggedObstacle) continue // Don't draw the dragged one here
//                drawObstacle(canvas, obstacle, cellWidth, cellHeight)
//            }
//        }
//        // Draw the dragged obstacle on top, following the finger
//        draggedObstacle?.let {
//            drawObstacle(canvas, it, cellWidth, cellHeight, isDragging = true)
//        }
//    }
//
//    private fun drawObstacle(canvas: Canvas, obstacle: GridObstacle, cellWidth: Float, cellHeight: Float, isDragging: Boolean = false) {
//        val left = obstacle.coord.x * cellWidth
//        val top = (GRID_SIZE - 1 - obstacle.coord.y) * cellHeight
//        val alpha = if (isDragging) 150 else 255
//        obstaclePaint.alpha = alpha
//        obstacleTextPaint.alpha = alpha
//
//        canvas.drawRect(left, top, left + cellWidth, top + cellHeight, obstaclePaint)
//        val textY = top + (cellHeight / 2) - ((obstacleTextPaint.descent() + obstacleTextPaint.ascent()) / 2)
//        canvas.drawText(obstacle.value, left + (cellWidth / 2), textY, obstacleTextPaint)
//    }
//
//
//    // --- Drag and Drop Functionality ---
//    override fun onTouchEvent(event: MotionEvent): Boolean {
//        val x = event.x
//        val y = event.y
//        val cellWidth = width.toFloat() / GRID_SIZE
//        val cellHeight = height.toFloat() / GRID_SIZE
//        val gridX = floor(x / cellWidth).toInt()
//        val gridY = GRID_SIZE - 1 - floor(y / cellHeight).toInt()
//
//        when (event.action) {
//            MotionEvent.ACTION_DOWN -> {
//                // Check if a tap is on an obstacle
//                val clickedObstacle = viewModel?.obstaclesList?.value?.find {
//                    it.coord.x == gridX && it.coord.y == gridY
//                }
//                if (clickedObstacle != null) {
//                    isDragging = true
//                    draggedObstacle = clickedObstacle
//                    return true // Consume the event
//                }
//            }
//            MotionEvent.ACTION_MOVE -> {
//                if (isDragging && draggedObstacle != null) {
//                    // Update the dragged obstacle's position
//                    draggedObstacle?.coord = Coord(gridX, gridY)
//                    invalidate() // Redraw the view
//                    return true
//                }
//            }
//            MotionEvent.ACTION_UP -> {
//                if (isDragging && draggedObstacle != null) {
//                    // Update the ViewModel with the new position
//                    val updatedList = viewModel?.obstaclesList?.value?.toMutableList()
//                    val index = updatedList?.indexOfFirst { it.number == draggedObstacle?.number }
//                    if (index != null && index != -1) {
//                        updatedList[index] = draggedObstacle!!
//                        viewModel?.obstaclesList?.value = updatedList
//                    }
//                    // Reset drag state
//                    isDragging = false
//                    draggedObstacle = null
//                    invalidate()
//                    return true
//                }
//            }
//        }
//        return super.onTouchEvent(event)
//    }
//}