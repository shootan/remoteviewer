package com.remote60.androiddirect

import android.view.MotionEvent
import android.view.View
import android.widget.FrameLayout
import kotlin.math.abs

/**
 * On-screen mouse for work a fingertip cannot do.
 *
 * Touching the picture directly puts the pointer wherever the finger landed, which is fine for
 * a button and hopeless for a 16-pixel close box or a scrollbar. Here steering and clicking are
 * separated: the centre pad moves the pointer at a fraction of the finger's speed, and the click
 * comes from a button afterwards, so the aim cannot drift at the moment it matters.
 *
 * The class owns no protocol knowledge; it reports intent through [Listener] and the activity
 * turns that into input events.
 */
class ViewerVirtualMouse(
    private val root: FrameLayout,
    private val listener: Listener,
) {
    interface Listener {
        /** Move the pointer by this many *remote* pixels, relative to where it is now. */
        fun onMoveBy(dx: Int, dy: Int)

        fun onButtonDown(button: Button)
        fun onButtonUp(button: Button)

        /** Positive scrolls up, in wheel notches. */
        fun onWheel(notches: Int)
    }

    enum class Button { LEFT, RIGHT, MIDDLE }

    private val pad: View = root.findViewById(R.id.virtualMousePad)
    private val leftButton: View = root.findViewById(R.id.virtualMouseLeft)
    private val rightButton: View = root.findViewById(R.id.virtualMouseRight)
    private val middleButton: View = root.findViewById(R.id.virtualMouseMiddle)
    private val roll: View = root.findViewById(R.id.virtualMouseRoll)
    private val closeButton: View = root.findViewById(R.id.virtualMouseClose)
    private val handle: View = root.findViewById(R.id.virtualMouseHandle)

    private val density = root.resources.displayMetrics.density

    /**
     * Finger travel is scaled down: the point of the pad is fine control, and 1:1 would make it
     * no better than touching the picture. Fast drags still cover distance because the scale
     * applies to every sample, not to the total.
     */
    private var moveScale = 0.55f

    /** False until the widget has been given a sensible position on screen. */
    private var placed = false

    /** Pixels of drag on the wheel strip that count as one notch. */
    private val wheelStepPx = 26f * density

    val isOpen: Boolean get() = root.visibility == View.VISIBLE

    init {
        bindPad()
        bindWheel()
        bindButton(leftButton, Button.LEFT)
        bindButton(rightButton, Button.RIGHT)
        bindButton(middleButton, Button.MIDDLE)
        closeButton.setOnClickListener { hide() }
        bindHandle()
    }

    fun show() {
        root.visibility = View.VISIBLE
        root.bringToFront()
        if (!placed) placeBottomLeft(attempt = 0)
    }

    fun hide() {
        root.visibility = View.GONE
    }

    fun toggle() {
        if (isOpen) hide() else show()
    }

    /** Forgets where the widget was put, so the next show() places it afresh. */
    fun resetPosition() {
        placed = false
    }

    /**
     * Bottom-left, clear of the control rail on the right.
     *
     * Retried because the widget has no height until it has been laid out, and positioning
     * against a height of zero put it below the bottom edge with only a sliver visible.
     */
    private fun placeBottomLeft(attempt: Int) {
        root.post {
            val parent = root.parent as? View
            if (parent == null || root.height <= 0 || parent.height <= 0) {
                if (attempt < 5) placeBottomLeft(attempt + 1)
                return@post
            }
            val margin = 12f * density
            root.x = margin
            root.y = (parent.height - root.height - margin).coerceAtLeast(0f)
            placed = true
        }
    }

    private fun bindPad() {
        var lastX = 0f
        var lastY = 0f
        // Sub-pixel remainder, so slow drags still move rather than rounding away to nothing.
        var carryX = 0f
        var carryY = 0f
        pad.setOnTouchListener { _, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    lastX = event.rawX
                    lastY = event.rawY
                    carryX = 0f
                    carryY = 0f
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    carryX += (event.rawX - lastX) * moveScale
                    carryY += (event.rawY - lastY) * moveScale
                    lastX = event.rawX
                    lastY = event.rawY
                    val dx = carryX.toInt()
                    val dy = carryY.toInt()
                    if (dx != 0 || dy != 0) {
                        carryX -= dx
                        carryY -= dy
                        listener.onMoveBy(dx, dy)
                    }
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> true
                else -> false
            }
        }
    }

    private fun bindWheel() {
        var lastY = 0f
        var carry = 0f
        roll.setOnTouchListener { _, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    lastY = event.rawY
                    carry = 0f
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    carry += lastY - event.rawY
                    lastY = event.rawY
                    val notches = (abs(carry) / wheelStepPx).toInt()
                    if (notches > 0) {
                        val direction = if (carry > 0f) 1 else -1
                        carry -= direction * notches * wheelStepPx
                        listener.onWheel(direction * notches)
                    }
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> true
                else -> false
            }
        }
    }

    /**
     * Down and up follow the finger rather than firing on release, so press-and-hold works:
     * dragging a window or selecting text needs the button held while the pointer moves.
     */
    private fun bindButton(view: View, button: Button) {
        view.setOnTouchListener { v, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    v.isPressed = true
                    listener.onButtonDown(button)
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    v.isPressed = false
                    listener.onButtonUp(button)
                    true
                }
                else -> false
            }
        }
    }

    private fun bindHandle() {
        var downRawX = 0f
        var downRawY = 0f
        var originX = 0f
        var originY = 0f
        handle.setOnTouchListener { _, event ->
            val parent = root.parent as? View ?: return@setOnTouchListener false
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    downRawX = event.rawX
                    downRawY = event.rawY
                    originX = root.x
                    originY = root.y
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    // Kept inside the parent: dragged off screen it would be unrecoverable.
                    root.x = (originX + event.rawX - downRawX)
                        .coerceIn(0f, (parent.width - root.width).toFloat().coerceAtLeast(0f))
                    root.y = (originY + event.rawY - downRawY)
                        .coerceIn(0f, (parent.height - root.height).toFloat().coerceAtLeast(0f))
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> true
                else -> false
            }
        }
    }
}
