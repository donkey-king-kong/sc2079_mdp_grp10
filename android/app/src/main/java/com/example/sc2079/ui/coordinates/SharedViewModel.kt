package com.example.sc2079.ui.coordinates

import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.ViewModel

class SharedViewModel : ViewModel() {
    val newCoordinate = MutableLiveData<Pair<Float, Float>>()
}