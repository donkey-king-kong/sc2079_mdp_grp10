package com.example.sc2079.ui.coordinates

import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.ViewModel
import com.example.sc2079.MainActivity
import com.example.sc2079.ObstacleData

data class ObstacleAddition(val x: Int, val y: Int, val direction: ObstacleData.Direction)

class SharedViewModel : ViewModel() {
    val newCoordinate = MutableLiveData<Pair<Float, Float>>()
    val newObstacleRequest = MutableLiveData<ObstacleAddition>()
    val messageLog = ArrayList<MainActivity.ChatLogEntry>()
    var gridMapSnapshot: String? = null
    var gridRows: Int? = null
    var gridCols: Int? = null
}
