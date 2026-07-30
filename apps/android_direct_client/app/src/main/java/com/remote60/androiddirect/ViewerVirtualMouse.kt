package com.remote60.androiddirect

import android.view.MotionEvent
import android.view.View
import android.widget.FrameLayout
import kotlin.math.abs

/**
 * On-screen mouse for work a fingertip cannot do.
 *
 * Touching the picture puts the pointer wherever the finger landed, which is fine for a button
 * and hopeless for a scrollbar or a 16-pixel close box. Here aiming and clicking are separate
 * acts: dragging the centre pad walks the pointer at a fraction of the finger's speed, a ring
 * marks exactly where it is, and only then does a button press happen -- so the aim cannot drift
 * at the moment it matters.
 *
 * The marker and the button cluster both travel with the pointer. That is the whole point: a
 * cluster parked in a corner would tell you nothing about what you are about to click. They are
 * positioned independently so the cluster can be pushed back inside the frame while the pointer
 * carries on into a corner.
 *
 * The class owns no protocol knowledge. It reports intent through [Listener]; the activity keeps
 * the pointer's real position and turns intent into input events.
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

        /** Asked when the widget opens: where the pointer should start, in view pixels. */
        fun onRequestInitialPosition()
    }

    enum class Button { LEFT, RIGHT, MIDDLE }

    private val marker: View = root.findViewById(R.id.virtualMousePointer)
    private val cluster: View = root.findViewById(R.id.virtualMouseCluster)
    private val pad: View = root.findViewById(R.id.virtualMousePad)
    private val leftButton: View = root.findViewById(R.id.virtualMouseLeft)
    private val rightButton: View = root.findViewById(R.id.virtualMouseRight)
    private val middleButton: View = root.findViewById(R.id.virtualMouseMiddle)
    private val roll: View = root.findViewById(R.id.virtualMouseRoll)
    private val closeButton: View = root.findViewById(R.id.virtualMouseClose)

    private val density = root.resources.displayMetrics.density

    /**
     * Finger travel is scaled down: the pad exists for fine control, and 1:1 would make it no
     * better than touching the picture. Long drags still cover ground because the scale applies
     * to each sample rather than to the total.
     */
    private val moveScale = 0.5f

    /** Drag on the wheel strip that counts as one notch. */
    private val wheelStepPx = 26f * density

    /** How far the cluster sits from the pointer, so it never covers the target. */
    private val clusterGapPx = 26f * density

    val isOpen: Boolean get() = root.visibility == View.VISIBLE

    init {
        bindPad()
        bindWheel()
        bindButton(leftButton, Button.LEFT)
        bindButton(rightButton, Button.RIGHT)
        bindButton(middleButton, Button.MIDDLE)
        closeButton.setOnClickListener { hide() }
    }

    fun show() {
        root.visibility = View.VISIBLE
        root.bringToFront()
        listener.onRequestInitialPosition()
    }

    fun hide() {
        root.visibility = View.GONE
    }

    /**
     * Puts the marker on [viewX], [viewY] and parks the cluster beside it.
     *
     * Called every time the pointer moves, so the ring is always over the pixel that a button
     * press would hit.
     */
    fun moveTo(viewX: Float, viewY: Float) {
        if (!isOpen) return
        root.post {
            if (marker.width <= 0 || cluster.width <= 0) return@post
            marker.x = viewX - marker.width / 2f
            marker.y = viewY - marker.height / 2f
            placeCluster(viewX, viewY)
        }
    }

    /**
     * Beside the pointer, on whichever side has room.
     *
     * Down-right by default; it flips when the pointer nears an edge, which is what keeps the
     * buttons usable when aiming at the taskbar or the window close box in a corner.
     */
    private fun placeCluster(viewX: Float, viewY: Float) {
        val parent = root
        val halfMarker = marker.width / 2f
        val roomRight = parent.width - (viewX + halfMarker + clusterGapPx)
        val roomBelow = parent.height - (viewY + halfMarker + clusterGapPx)

        val x = if (roomRight >= cluster.width) {
            viewX + halfMarker + clusterGapPx
        } else {
            viewX - halfMarker - clusterGapPx - cluster.width
        }
        val y = if (roomBelow >= cluster.height) {
            viewY + halfMarker + clusterGapPx
        } else {
            viewY - halfMarker - clusterGapPx - cluster.height
        }
        // Clamped as a last resort: on a short screen neither side fits, and a cluster half off
        // the display cannot be pressed at all.
        cluster.x = x.coerceIn(0f, (parent.width - cluster.width).toFloat().coerceAtLeast(0f))
        cluster.y = y.coerceIn(0f, (parent.height - cluster.height).toFloat().coerceAtLeast(0f))
    }

    private fun bindPad() {
        var lastX = 0f
        var lastY = 0f
        // Sub-pixel remainder, so a slow drag still moves instead of rounding away to nothing.
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
}
