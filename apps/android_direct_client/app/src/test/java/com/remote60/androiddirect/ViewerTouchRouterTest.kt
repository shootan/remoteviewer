package com.remote60.androiddirect

import com.remote60.androiddirect.ViewerTouchRouter.Companion.ACTION_CANCEL
import com.remote60.androiddirect.ViewerTouchRouter.Companion.ACTION_DOWN
import com.remote60.androiddirect.ViewerTouchRouter.Companion.ACTION_MOVE
import com.remote60.androiddirect.ViewerTouchRouter.Companion.ACTION_POINTER_DOWN
import com.remote60.androiddirect.ViewerTouchRouter.Companion.ACTION_POINTER_UP
import com.remote60.androiddirect.ViewerTouchRouter.Companion.ACTION_UP
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * viewer-sharpness r2 R1: the stream ownership MainActivity.handleViewerTouch routes by (ViewerTouchRouter), fed the
 * MotionEvent sequences of the Codex counterexamples. What reaches the PC is modelled the way handleViewerTouch sends
 * it: a finger landing on the picture (ACTION_DOWN / ACTION_POINTER_DOWN) is a mouse-down when nothing is held yet; a
 * two-finger MOVE under a tablet lock is Ctrl+wheel (handlePinchZoom runs before the held-pointer check); an UP of a
 * pressed stream is a mouse-up; an interrupted PC stream gets exactly one release.
 * Each case also runs bf0e3a2's rule (the mode read on every event) -- the negative control, which must leak.
 */
class ViewerTouchRouterTest {
    /** The viewer as the touches see it. */
    private class Viewer(val newRule: Boolean) {
        val router = ViewerTouchRouter()
        var large = false
        var panMode = false
        var tabletLocked = false
        var pressed = false        // a PC mouse button is down (activeTouchPointerId)
        val sent = mutableListOf<String>()

        fun local() = panMode && large

        fun event(action: Int, pointers: Int = 1) {
            val owner = if (newRule) router.route(action, local()) else if (local()) ViewerTouchRouter.Owner.LOCAL else ViewerTouchRouter.Owner.PC
            when (owner) {
                ViewerTouchRouter.Owner.LOCAL, ViewerTouchRouter.Owner.SWALLOW, ViewerTouchRouter.Owner.NONE -> return
                ViewerTouchRouter.Owner.PC -> Unit
            }
            when (action) {
                ACTION_DOWN, ACTION_POINTER_DOWN -> if (!pressed) { pressed = true; sent += "mouse-down" }
                ACTION_MOVE -> if (tabletLocked && pointers >= 2) sent += "ctrl+wheel"
                ACTION_UP, ACTION_POINTER_UP -> if (pressed) { pressed = false; sent += "mouse-up" }
                ACTION_CANCEL -> if (pressed) { pressed = false; sent += "mouse-up" }
            }
        }

        /** A mode / size / scene change (MainActivity.interruptViewerTouch). */
        fun interrupt() {
            if (newRule) {
                if (router.interrupt() && pressed) { pressed = false; sent += "mouse-up" }
            } else if (pressed) {
                pressed = false; sent += "mouse-up"   // bf0e3a2: cancelActiveViewerTouch, the stream itself not held
            }
        }

        /** onScale below the fit: exitLargeView -> resetViewerViewport (pan off, 전체보기). */
        fun pinchBelowFit() {
            interrupt()
            large = false
            panMode = false
        }
    }

    private fun fitCrossing(newRule: Boolean, tabletLocked: Boolean): List<String> {
        val v = Viewer(newRule)
        v.large = true; v.panMode = true; v.tabletLocked = tabletLocked
        v.event(ACTION_DOWN)
        v.event(ACTION_POINTER_DOWN, 2)
        v.event(ACTION_MOVE, 2)
        v.pinchBelowFit()          // the pinch went below the fit while both fingers are down
        v.event(ACTION_MOVE, 2)    // still pinching (tablet lock: Ctrl+wheel under the old rule)
        v.event(ACTION_POINTER_UP, 2)
        v.event(ACTION_POINTER_DOWN, 2)  // the lifted finger comes back: no new ACTION_DOWN
        v.event(ACTION_MOVE, 2)
        v.event(ACTION_POINTER_UP, 2)
        v.event(ACTION_UP)
        return v.sent
    }

    @Test
    fun aPinchBelowTheFit_staysLocalToTheEnd_nothingReachesThePc() {
        assertEquals(emptyList<String>(), fitCrossing(newRule = true, tabletLocked = false))
        assertEquals(emptyList<String>(), fitCrossing(newRule = true, tabletLocked = true))
    }

    @Test
    fun negativeControl_bf0e3a2_leaksAClickAndCtrlWheel() {
        assertTrue("old rule: the returning finger clicks", "mouse-down" in fitCrossing(newRule = false, tabletLocked = false))
        assertTrue("old rule: tablet-lock MOVE sends Ctrl+wheel", "ctrl+wheel" in fitCrossing(newRule = false, tabletLocked = true))
    }

    @Test
    fun theNextIndependentDown_isOrdinaryPcInputAgain() {
        val v = Viewer(newRule = true)
        v.large = true; v.panMode = true
        v.event(ACTION_DOWN); v.event(ACTION_POINTER_DOWN, 2); v.pinchBelowFit(); v.event(ACTION_POINTER_UP, 2); v.event(ACTION_UP)
        v.event(ACTION_DOWN); v.event(ACTION_UP)
        assertEquals(listOf("mouse-down", "mouse-up"), v.sent)
    }

    @Test
    fun aPcPressCutByAModeChange_releasesOnce_thenTheRestIsSwallowed() {
        for (change in listOf("size", "chip-by-another-finger", "controls", "large")) {
            val v = Viewer(newRule = true)
            v.event(ACTION_DOWN)               // PC mouse-down held
            v.interrupt()                      // the change while it is held
            v.interrupt()                      // a second change in the same stream: no second release
            v.event(ACTION_MOVE)
            v.event(ACTION_POINTER_DOWN, 2)    // another finger: not a new press
            v.event(ACTION_POINTER_UP, 2)
            v.event(ACTION_UP)
            assertEquals(change, listOf("mouse-down", "mouse-up"), v.sent)
            assertFalse(change, v.pressed)
            v.event(ACTION_DOWN); v.event(ACTION_UP)  // the next stream is ordinary
            assertEquals(change, listOf("mouse-down", "mouse-up", "mouse-down", "mouse-up"), v.sent)
        }
    }

    @Test
    fun negativeControl_bf0e3a2_pressesAgainWithTheHeldStream() {
        val v = Viewer(newRule = false)
        v.event(ACTION_DOWN)
        v.interrupt()
        v.event(ACTION_POINTER_DOWN, 2)  // the stream continues on the PC path: a second press
        assertEquals(listOf("mouse-down", "mouse-up", "mouse-down"), v.sent)
    }

    @Test
    fun aLocalStream_isNotInterruptedByAChip_orASizeChange() {
        val r = ViewerTouchRouter()
        assertEquals(ViewerTouchRouter.Owner.LOCAL, r.route(ACTION_DOWN, localNow = true))
        assertFalse(r.interrupt())
        assertEquals(ViewerTouchRouter.Owner.LOCAL, r.route(ACTION_MOVE, localNow = false))
        assertEquals(ViewerTouchRouter.Owner.LOCAL, r.route(ACTION_POINTER_DOWN, localNow = false))
        assertEquals(ViewerTouchRouter.Owner.LOCAL, r.route(ACTION_CANCEL, localNow = false))
        assertEquals(ViewerTouchRouter.Owner.NONE, r.owner)
        assertEquals(ViewerTouchRouter.Owner.PC, r.route(ACTION_DOWN, localNow = false))
    }

    @Test
    fun aStreamWithoutItsDown_reachesNobody() {
        val r = ViewerTouchRouter()
        assertEquals(ViewerTouchRouter.Owner.SWALLOW, r.route(ACTION_MOVE, localNow = false))
        assertEquals(ViewerTouchRouter.Owner.SWALLOW, r.route(ACTION_UP, localNow = false))
        assertEquals(ViewerTouchRouter.Owner.PC, r.route(ACTION_DOWN, localNow = false))
    }
}
