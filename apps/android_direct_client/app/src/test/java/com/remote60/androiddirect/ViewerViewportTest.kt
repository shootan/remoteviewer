package com.remote60.androiddirect

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** viewer-sharpness r1 B: one placement for drawing and input -- 전체보기 fit, 크게보기 zoom/pan, the round trip. */
class ViewerViewportTest {
    // The phone of the report: a 2562x1275 view, a 1920x1080 remote screen (height-limited fit).
    private val vw = 2562f
    private val vh = 1275f
    private val cw = 1920
    private val ch = 1080

    @Test
    fun fit_isTheAspectCorrectMinimum_centred_andHeightLimitedHere() {
        val v = ViewerViewport()
        val p = v.place(vw, vh, cw, ch)!!
        assertEquals(vh / ch, ViewerViewport.fitScale(vw, vh, cw, ch), 1e-6f)
        assertEquals(1275f, p.height, 0.01f)
        assertEquals(1920f * 1275f / 1080f, p.width, 0.01f)       // ~2266.7: no wider than the height allows
        assertEquals((vw - p.width) / 2f, p.left, 0.01f)           // centred, letterbox both sides
        assertEquals(0f, p.top, 0.01f)
        assertEquals(p.width / p.height, cw.toFloat() / ch, 1e-4f)  // never stretched
    }

    @Test
    fun fit_widthLimited_onATablet() {
        val v = ViewerViewport()
        val p = v.place(2000f, 1500f, cw, ch)!!
        assertEquals(2000f, p.width, 0.01f)
        assertEquals(1125f, p.height, 0.01f)
        assertEquals(187.5f, p.top, 0.01f)
    }

    @Test
    fun everyCornerAndEdgePixel_roundTrips_inBothModes() {
        for (large in listOf(false, true)) {
            val v = ViewerViewport()
            if (large) v.enterLarge(vw, vh, cw, ch)
            for ((rx, ry) in listOf(0 to 0, cw - 1 to 0, 0 to ch - 1, cw - 1 to ch - 1, cw / 2 to ch / 2, 1 to 1)) {
                if (large) {
                    // bring the pixel on screen first: centre the view on it as far as the clamp allows
                    v.reset(); v.enterLarge(vw, vh, cw, ch)
                    v.panBy(10_000f * if (rx < cw / 2) 1 else -1, 10_000f * if (ry < ch / 2) 1 else -1, vw, vh, cw, ch)
                }
                val (x, y) = v.toView(rx, ry, vw, vh, cw, ch)!!
                assertTrue("large=$large ($rx,$ry) on screen at ($x,$y)", x in 0f..vw && y in 0f..vh)
                assertEquals("large=$large ($rx,$ry)", Pair(rx, ry), v.toRemote(x, y, vw, vh, cw, ch, clamp = false))
            }
        }
    }

    @Test
    fun letterbox_sendsNothing_unlessClamped() {
        val v = ViewerViewport()
        val p = v.place(vw, vh, cw, ch)!!
        assertNull(v.toRemote(p.left - 1f, vh / 2, vw, vh, cw, ch, clamp = false))
        assertNull(v.toRemote(p.left + p.width + 1f, vh / 2, vw, vh, cw, ch, clamp = false))
        assertEquals(Pair(0, ch / 2), v.toRemote(p.left - 50f, vh / 2, vw, vh, cw, ch, clamp = true)?.let { Pair(it.first, ch / 2) })
    }

    @Test
    fun large_fillsTheView_isUniform_andCoversEveryAxisItOverflows() {
        val v = ViewerViewport()
        v.enterLarge(vw, vh, cw, ch)
        assertTrue(v.isLarge)
        val p = v.place(vw, vh, cw, ch)!!
        assertTrue("fills the width", p.width >= vw - 0.01f)
        assertEquals(p.width / p.height, cw.toFloat() / ch, 1e-4f)  // uniform scale
        assertTrue(p.left <= 0f && p.left + p.width >= vw)        // no band beside an edge
        assertTrue(p.top <= 0f && p.top + p.height >= vh)
        // panning past an edge stops at it
        v.panBy(100_000f, 100_000f, vw, vh, cw, ch)
        val q = v.place(vw, vh, cw, ch)!!
        assertEquals(0f, q.left, 0.01f)
        assertEquals(0f, q.top, 0.01f)
        assertEquals(Pair(0, 0), v.toRemote(0f, 0f, vw, vh, cw, ch, clamp = false))
    }

    @Test
    fun zoomBy_keepsThePixelUnderTheFingers_andBelowOneIsFullView() {
        val v = ViewerViewport()
        v.enterLarge(vw, vh, cw, ch)
        val focus = Pair(900f, 600f)
        val before = v.toRemote(focus.first, focus.second, vw, vh, cw, ch, clamp = false)!!
        v.zoomBy(1.6f, focus.first, focus.second, vw, vh, cw, ch)
        val after = v.toRemote(focus.first, focus.second, vw, vh, cw, ch, clamp = false)!!
        assertTrue("$before vs $after", kotlin.math.abs(before.first - after.first) <= 1 && kotlin.math.abs(before.second - after.second) <= 1)
        v.zoomBy(0.01f, focus.first, focus.second, vw, vh, cw, ch)
        assertFalse(v.isLarge)
        assertEquals(v.place(vw, vh, cw, ch), ViewerViewport().place(vw, vh, cw, ch))
    }

    @Test
    fun reset_isOneStepBackToTheFit() {
        val v = ViewerViewport()
        v.enterLarge(vw, vh, cw, ch)
        v.panBy(-300f, 120f, vw, vh, cw, ch)
        v.reset()
        assertEquals(ViewerViewport().place(vw, vh, cw, ch), v.place(vw, vh, cw, ch))
    }

    @Test
    fun fullView_ignoresPanAndZoom() {
        val v = ViewerViewport()
        val p0 = v.place(vw, vh, cw, ch)
        v.panBy(500f, 500f, vw, vh, cw, ch)
        v.zoomBy(2f, 10f, 10f, vw, vh, cw, ch)
        assertEquals(p0, v.place(vw, vh, cw, ch))
    }

    @Test
    fun sameShape_largeStillEnlarges() {
        val v = ViewerViewport()
        v.enterLarge(1920f, 1080f, cw, ch)
        assertEquals(ViewerViewport.MIN_LARGE_ZOOM, v.zoom, 1e-4f)
        assertNotNull(v.place(1920f, 1080f, cw, ch))
    }

    @Test
    fun invalidSizes_placeNothing() {
        val v = ViewerViewport()
        assertNull(v.place(0f, 100f, cw, ch))
        assertNull(v.place(100f, 100f, 0, ch))
        assertNull(v.toRemote(10f, 10f, 100f, 100f, cw, 0, clamp = true))
    }
}
