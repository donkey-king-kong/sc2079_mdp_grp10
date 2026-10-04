package com.example.sc2079.ui;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.DashPathEffect;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.view.View;

public class KnobView extends View {

    private final Paint bgPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint ringPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint dashPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint innerPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint notchPaint = new Paint(Paint.ANTI_ALIAS_FLAG);

    private int bgColor      = 0xFF1E1E1E;
    private int ringColor    = 0x80E8002D;
    private int innerColor   = 0xFFE8002D;
    private float notchAngle = -45f;

    public KnobView(Context ctx) { super(ctx); init(); }
    public KnobView(Context ctx, AttributeSet attrs) { super(ctx, attrs); init(); }
    public KnobView(Context ctx, AttributeSet attrs, int defStyle) { super(ctx, attrs, defStyle); init(); }

    private void init() {
        bgPaint.setStyle(Paint.Style.FILL);

        ringPaint.setStyle(Paint.Style.STROKE);
        ringPaint.setStrokeWidth(3f);

        dashPaint.setStyle(Paint.Style.STROKE);
        dashPaint.setStrokeWidth(2f);

        innerPaint.setStyle(Paint.Style.FILL);

        notchPaint.setStyle(Paint.Style.STROKE);
        notchPaint.setStrokeCap(Paint.Cap.ROUND);
        notchPaint.setColor(0xFFFFFFFF);
    }

    public void setColors(int bg, int ring, int inner) {
        bgColor    = bg;
        ringColor  = ring;
        innerColor = inner;
        invalidate();
    }

    public void setNotchAngle(float degrees) {
        notchAngle = degrees;
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        float w = getWidth();
        float h = getHeight();
        float cx = w / 2f;
        float cy = h / 2f;
        float outerR = Math.min(cx, cy) - 2f;
        float innerR = outerR * 0.60f;

        // Outer dark background circle
        bgPaint.setColor(bgColor);
        canvas.drawCircle(cx, cy, outerR, bgPaint);

        // Dashed outer ring
        dashPaint.setColor(ringColor);
        float dashLen = (float) (2 * Math.PI * outerR / 24f) * 0.55f;
        float gapLen  = (float) (2 * Math.PI * outerR / 24f) * 0.45f;
        dashPaint.setPathEffect(new DashPathEffect(new float[]{dashLen, gapLen}, 0f));
        RectF oval = new RectF(cx - outerR, cy - outerR, cx + outerR, cy + outerR);
        canvas.drawOval(oval, dashPaint);

        // Solid inner ring border
        ringPaint.setColor(ringColor);
        ringPaint.setPathEffect(null);
        canvas.drawCircle(cx, cy, outerR, ringPaint);

        // Red inner filled circle
        innerPaint.setColor(innerColor);
        canvas.drawCircle(cx, cy, innerR, innerPaint);

        // White notch line
        float notchPct = 0.55f;
        double rad = Math.toRadians(notchAngle - 90f);
        float nx1 = cx + (float) Math.cos(rad) * innerR * 0.35f;
        float ny1 = cy + (float) Math.sin(rad) * innerR * 0.35f;
        float nx2 = cx + (float) Math.cos(rad) * innerR * notchPct;
        float ny2 = cy + (float) Math.sin(rad) * innerR * notchPct;
        notchPaint.setStrokeWidth(innerR * 0.18f);
        canvas.drawLine(nx1, ny1, nx2, ny2, notchPaint);
    }
}
