package com.example.sc2079.ui

import android.content.Context
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.util.TypedValue

data class Palette(
    val bg: Int,
    val panel: Int,
    val surface: Int,
    val surface2: Int,
    val text: Int,
    val textMuted: Int,
    val textDim: Int,
    val border: Int,
    val borderStrong: Int,
    val accent: Int,
    val accentDim: Int,
    val accentBorder: Int,
    val pink: Int,
    val pinkDim: Int,
    val pinkBorder: Int,
    val red: Int,
    val redDim: Int,
    val redBorder: Int,
    val green: Int,
    val greenDim: Int,
    val greenBorder: Int
)

val DAY = Palette(
    bg           = Color.parseColor("#C8CDD4"),
    panel        = Color.parseColor("#E8EAED"),
    surface      = Color.parseColor("#DDE0E4"),
    surface2     = Color.parseColor("#FFFFFF"),
    text         = Color.parseColor("#0C0E11"),
    textMuted    = Color.parseColor("#8C0C0E11"),
    textDim      = Color.parseColor("#610C0E11"),
    border       = Color.parseColor("#2E000000"),
    borderStrong = Color.parseColor("#47000000"),
    accent       = Color.parseColor("#1A3ECF"),
    accentDim    = Color.parseColor("#1A1A3ECF"),
    accentBorder = Color.parseColor("#731A3ECF"),
    pink         = Color.parseColor("#9E0F44"),
    pinkDim      = Color.parseColor("#1A9E0F44"),
    pinkBorder   = Color.parseColor("#669E0F44"),
    red          = Color.parseColor("#9E1515"),
    redDim       = Color.parseColor("#1A9E1515"),
    redBorder    = Color.parseColor("#669E1515"),
    green        = Color.parseColor("#155E32"),
    greenDim     = Color.parseColor("#1A155E32"),
    greenBorder  = Color.parseColor("#66155E32")
)

val NIGHT = Palette(
    bg           = Color.parseColor("#0E1117"),
    panel        = Color.parseColor("#151B27"),
    surface      = Color.parseColor("#1C2333"),
    surface2     = Color.parseColor("#232B3D"),
    text         = Color.parseColor("#E8EAF0"),
    textMuted    = Color.parseColor("#8CE8EAF0"),
    textDim      = Color.parseColor("#61E8EAF0"),
    border       = Color.parseColor("#1FFFFFFF"),
    borderStrong = Color.parseColor("#3DFFFFFF"),
    accent       = Color.parseColor("#6C8EF5"),
    accentDim    = Color.parseColor("#2E6C8EF5"),
    accentBorder = Color.parseColor("#806C8EF5"),
    pink         = Color.parseColor("#D45A8E"),
    pinkDim      = Color.parseColor("#33D45A8E"),
    pinkBorder   = Color.parseColor("#80D45A8E"),
    red          = Color.parseColor("#E05252"),
    redDim       = Color.parseColor("#26E05252"),
    redBorder    = Color.parseColor("#80E05252"),
    green        = Color.parseColor("#3DBA6E"),
    greenDim     = Color.parseColor("#263DBA6E"),
    greenBorder  = Color.parseColor("#803DBA6E")
)

fun box(ctx: Context, fill: Int, stroke: Int, radiusDp: Float = 8f, strokeDp: Float = 2f): GradientDrawable {
    val r = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, radiusDp, ctx.resources.displayMetrics)
    val s = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, strokeDp, ctx.resources.displayMetrics).toInt()
    return GradientDrawable().apply {
        shape = GradientDrawable.RECTANGLE
        cornerRadius = r
        setColor(fill)
        setStroke(s, stroke)
    }
}

interface ThemeAware {
    fun applyTheme(p: Palette)
}
