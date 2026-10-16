package com.remote60.androiddirect

import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * viewer-sharpness r1 B: where the remote picture sits in the video view -- one model for drawing it
 * (TextureView.setTransform) and for turning a touch back into a remote pixel, so the two can never disagree.
 *
 * - 전체보기 (zoom 1): the aspect-correct fit, `min(viewW / contentW, viewH / contentH)`, centred. The whole remote
 *   screen is on the phone; the rest of the view is letterbox, where no touch reaches the PC.
 * - 크게보기 (zoom > 1): the same picture scaled further about a centre the user moves. Never stretched one way
 *   only; along an axis where it is larger than the view it always covers the view (no empty band beside an edge),
 *   so every remote pixel can be brought on screen by moving.
 *
 * Content is the visible remote picture (the decoder's crop applied: AndroidVideoDecoderSink.VideoSizePacked), in
 * pixels. Remote coordinates use the existing convention: 0..content-1 spread over the displayed width.
 */
class ViewerViewport {
    /** Where the picture is drawn, in view pixels. May extend past the view in 크게보기. */
    data class Placement(val left: Float, val top: Float, val width: Float, val height: Float)

    /** 1 = 전체보기; above 1, how much larger than the fit. */
    var zoom = 1f
        private set

    // The content point (in content pixels) at the view's centre while zoomed.
    private var centerX = Float.NaN
    private var centerY = Float.NaN

    val isLarge: Boolean get() = zoom > 1f + EPS

    /** Back to 전체보기 (one step, from anywhere). */
    fun reset() {
        zoom = 1f
        centerX = Float.NaN
        centerY = Float.NaN
    }

    /**
     * 크게보기: fill the view (the larger of the two fit ratios, so no letterbox remains), at least [MIN_LARGE_ZOOM]
     * when the shapes already match. Centred on the picture.
     */
    fun enterLarge(viewW: Float, viewH: Float, contentW: Int, contentH: Int) {
        if (!valid(viewW, viewH, contentW, contentH)) return
        val fit = fitScale(viewW, viewH, contentW, contentH)
        val cover = max(viewW / contentW, viewH / contentH)
        zoom = max(cover / fit, MIN_LARGE_ZOOM).coerceAtMost(MAX_ZOOM)
        centerX = contentW / 2f
        centerY = contentH / 2f
    }

    fun place(viewW: Float, viewH: Float, contentW: Int, contentH: Int): Placement? {
        if (!valid(viewW, viewH, contentW, contentH)) return null
        val scale = fitScale(viewW, viewH, contentW, contentH) * zoom
        val w = contentW * scale
        val h = contentH * scale
        if (centerX.isNaN() || centerY.isNaN()) {
            centerX = contentW / 2f
            centerY = contentH / 2f
        }
        return Placement(axisStart(viewW, w, centerX * scale), axisStart(viewH, h, centerY * scale), w, h)
    }

    /**
     * A view point as a remote pixel. Outside the drawn picture (letterbox) it is null unless [clamp] -- a touch there
     * must not reach the PC. The same placement the picture is drawn with.
     */
    fun toRemote(x: Float, y: Float, viewW: Float, viewH: Float, contentW: Int, contentH: Int, clamp: Boolean): Pair<Int, Int>? {
        val p = place(viewW, viewH, contentW, contentH) ?: return null
        // The visible part of the picture: the placement cut to the view.
        val minX = max(p.left, 0f)
        val maxX = min(p.left + p.width, viewW)
        val minY = max(p.top, 0f)
        val maxY = min(p.top + p.height, viewH)
        val px = if (clamp) x.coerceIn(minX, maxX) else if (x < minX || x > maxX) return null else x
        val py = if (clamp) y.coerceIn(minY, maxY) else if (y < minY || y > maxY) return null else y
        val rx = ((px - p.left) / p.width).coerceIn(0f, 1f)
        val ry = ((py - p.top) / p.height).coerceIn(0f, 1f)
        return Pair(
            (rx * (contentW - 1)).roundToInt().coerceIn(0, contentW - 1),
            (ry * (contentH - 1)).roundToInt().coerceIn(0, contentH - 1),
        )
    }

    /** A remote pixel as a view point (the inverse of [toRemote]; the virtual mouse pointer is drawn with it). */
    fun toView(remoteX: Int, remoteY: Int, viewW: Float, viewH: Float, contentW: Int, contentH: Int): Pair<Float, Float>? {
        val p = place(viewW, viewH, contentW, contentH) ?: return null
        if (contentW <= 1 || contentH <= 1) return null
        return Pair(p.left + remoteX.toFloat() / (contentW - 1) * p.width, p.top + remoteY.toFloat() / (contentH - 1) * p.height)
    }

    /** 크게보기 only: move the picture with the finger by (dx, dy) view pixels. */
    fun panBy(dx: Float, dy: Float, viewW: Float, viewH: Float, contentW: Int, contentH: Int) {
        if (!isLarge || !valid(viewW, viewH, contentW, contentH)) return
        val scale = fitScale(viewW, viewH, contentW, contentH) * zoom
        place(viewW, viewH, contentW, contentH)
        centerX = clampCenter(centerX - dx / scale, viewW, contentW * scale, scale)
        centerY = clampCenter(centerY - dy / scale, viewH, contentH * scale, scale)
    }

    /** 크게보기 only: scale by [factor] about the view point (fx, fy), which keeps the remote pixel under it. Below 1 is 전체보기. */
    fun zoomBy(factor: Float, fx: Float, fy: Float, viewW: Float, viewH: Float, contentW: Int, contentH: Int) {
        if (!isLarge || !valid(viewW, viewH, contentW, contentH) || factor <= 0f) return
        val fit = fitScale(viewW, viewH, contentW, contentH)
        val before = place(viewW, viewH, contentW, contentH) ?: return
        // The content point under the focus, kept there.
        val ux = (fx - before.left) / (fit * zoom)
        val uy = (fy - before.top) / (fit * zoom)
        val next = (zoom * factor).coerceIn(1f, MAX_ZOOM)
        if (next <= 1f + EPS) {
            reset()
            return
        }
        zoom = next
        val scale = fit * zoom
        centerX = clampCenter(ux + (viewW / 2f - fx) / scale, viewW, contentW * scale, scale)
        centerY = clampCenter(uy + (viewH / 2f - fy) / scale, viewH, contentH * scale, scale)
    }

    private fun axisStart(view: Float, size: Float, centerScaled: Float): Float =
        if (size <= view + EPS) (view - size) / 2f
        else (view / 2f - centerScaled).coerceIn(view - size, 0f)

    private fun clampCenter(c: Float, view: Float, size: Float, scale: Float): Float {
        if (size <= view + EPS) return size / 2f / scale
        val half = view / 2f / scale
        return c.coerceIn(half, size / scale - half)
    }

    companion object {
        const val MAX_ZOOM = 4f
        const val MIN_LARGE_ZOOM = 1.5f
        private const val EPS = 1e-3f

        fun fitScale(viewW: Float, viewH: Float, contentW: Int, contentH: Int): Float =
            min(viewW / contentW, viewH / contentH)

        private fun valid(viewW: Float, viewH: Float, contentW: Int, contentH: Int) =
            viewW > 0f && viewH > 0f && contentW > 0 && contentH > 0
    }
}
