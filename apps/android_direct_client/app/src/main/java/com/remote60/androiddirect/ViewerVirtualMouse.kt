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
 * acts: dragging the centre pad walks the pointer at a fraction of the finger's speed, an arrow
 * shows exactly where its tip sits, and only then does a button press happen -- so the aim cannot drift
 * at the moment it matters.
 *
 * Only the arrow travels with the pointer. The button cluster stays where it was put -- a
 * cluster that chased the pointer meant the buttons were somewhere new by the time the aim was
 * right, so the clicking hand could never rest. Dragging the cluster's background moves it;
 * otherwise it holds its place until the viewer closes.
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

    /** Whether the cluster has a position yet; the first open gives it its default corner. */
    private var clusterPlaced = false

    val isOpen: Boolean get() = root.visibility == View.VISIBLE

    init {
        bindPad()
        bindWheel()
        bindClusterDrag()
        bindButton(leftButton, Button.LEFT)
        bindButton(rightButton, Button.RIGHT)
        bindButton(middleButton, Button.MIDDLE)
        closeButton.setOnClickListener { hide() }
    }

    fun show() {
        root.visibility = View.VISIBLE
        root.bringToFront()
        listener.onRequestInitialPosition()
        root.post { settleCluster() }
    }

    fun hide() {
        root.visibility = View.GONE
    }

    /**
     * Puts the arrow's tip on [viewX], [viewY]. The cluster deliberately stays where it is.
     */
    fun moveTo(viewX: Float, viewY: Float) {
        if (!isOpen) return
        root.post {
            if (marker.width <= 0) return@post
            // The arrow's tip is at the drawable's own origin, so no centring offset: the
            // point being aimed at and the point that gets clicked are the same pixel.
            marker.x = viewX
            marker.y = viewY
        }
    }

    /**
     * First open parks the cluster in the lower right, out of the picture's way; after that it
     * keeps whatever place the user dragged it to, pulled back inside if the frame shrank.
     */
    private fun settleCluster() {
        if (cluster.width <= 0 || root.width <= 0) return
        val margin = 16f * density
        val maxX = (root.width - cluster.width).toFloat().coerceAtLeast(0f)
        val maxY = (root.height - cluster.height).toFloat().coerceAtLeast(0f)
        if (!clusterPlaced) {
            cluster.x = (maxX - margin).coerceAtLeast(0f)
            cluster.y = (maxY - margin).coerceAtLeast(0f)
            clusterPlaced = true
        } else {
            cluster.x = cluster.x.coerceIn(0f, maxX)
            cluster.y = cluster.y.coerceIn(0f, maxY)
        }
    }

    /** The cluster's own background is its handle: drag it to wherever the hand rests. */
    private fun bindClusterDrag() {
        var lastX = 0f
        var lastY = 0f
        cluster.setOnTouchListener { _, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    lastX = event.rawX
                    lastY = event.rawY
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    cluster.x = (cluster.x + (event.rawX - lastX))
                        .coerceIn(0f, (root.width - cluster.width).toFloat().coerceAtLeast(0f))
                    cluster.y = (cluster.y + (event.rawY - lastY))
                        .coerceIn(0f, (root.height - cluster.height).toFloat().coerceAtLeast(0f))
                    lastX = event.rawX
                    lastY = event.rawY
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> true
                else -> false
            }
        }
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
