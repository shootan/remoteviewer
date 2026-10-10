package com.remote60.androiddirect

import com.remote60.androiddirect.ViewerKeyboardCycle.Action
import com.remote60.androiddirect.ViewerKeyboardCycle.Mode
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ViewerKeyboardCycleTest {
    /** (phone keyboard up, panel open), changed by an action the way MainActivity applies it. */
    private data class Shown(val phone: Boolean, val panel: Boolean) {
        val mode get() = ViewerKeyboardCycle.mode(phone, panel)
    }

    private fun apply(s: Shown, action: Action): Shown = when (action) {
        Action.SHOW_PHONE, Action.PC_KEYS_TO_PHONE -> Shown(phone = true, panel = false)
        Action.PHONE_TO_PC_KEYS -> Shown(phone = false, panel = true)
        Action.CLOSE_ALL -> Shown(phone = false, panel = false)
        Action.NONE -> s
    }

    private val closed = Shown(false, false)

    @Test
    fun theKeyboardButtonBringsUpThePhoneKeyboardFirst() {
        assertEquals(Action.SHOW_PHONE, ViewerKeyboardCycle.onKeyboardButton(closed.mode))
        assertEquals(Mode.PHONE, apply(closed, Action.SHOW_PHONE).mode)
    }

    @Test
    fun phoneToPcKeysAndBackGoesThroughTheTabsNotTheButton() {
        var s = apply(closed, ViewerKeyboardCycle.onKeyboardButton(closed.mode))
        val seen = mutableListOf(s.mode)
        s = apply(s, ViewerKeyboardCycle.onPcKeysTab(s.mode)); seen += s.mode
        s = apply(s, ViewerKeyboardCycle.onPhoneTab(s.mode)); seen += s.mode
        s = apply(s, ViewerKeyboardCycle.onPcKeysTab(s.mode)); seen += s.mode
        assertEquals(listOf(Mode.PHONE, Mode.PC_KEYS, Mode.PHONE, Mode.PC_KEYS), seen)
    }

    @Test
    fun theKeyboardButtonClosesWhicheverIsUp() {
        // 0.2.26 sent a press with the phone keyboard up on to the panel; that press is the one a
        // landscape phone keyboard covers, so the panel now goes through the tab instead.
        assertEquals(Action.CLOSE_ALL, ViewerKeyboardCycle.onKeyboardButton(Mode.PHONE))
        assertEquals(Action.CLOSE_ALL, ViewerKeyboardCycle.onKeyboardButton(Mode.PC_KEYS))
        assertEquals(Mode.CLOSED, apply(Shown(false, true), Action.CLOSE_ALL).mode)
    }

    @Test
    fun theSelectedTabDoesNothing() {
        assertEquals(Action.NONE, ViewerKeyboardCycle.onPhoneTab(Mode.PHONE))
        assertEquals(Action.NONE, ViewerKeyboardCycle.onPcKeysTab(Mode.PC_KEYS))
        assertEquals(Action.NONE, ViewerKeyboardCycle.onPhoneTab(Mode.CLOSED))
        assertEquals(Action.NONE, ViewerKeyboardCycle.onPcKeysTab(Mode.CLOSED))
    }

    @Test
    fun aPhoneKeyboardTheSystemPutAwayTakesTheTabBarWithIt() {
        // Its own down arrow or Back: the IME goes, MainActivity drops the capture focus.
        val up = apply(closed, Action.SHOW_PHONE)
        assertTrue(ViewerKeyboardCycle.barVisible(up.mode, imeBottomPx = 900, awaitingIme = false))
        val putAway = up.copy(phone = false)
        assertEquals(Mode.CLOSED, putAway.mode)
        assertFalse(ViewerKeyboardCycle.barVisible(putAway.mode, imeBottomPx = 0, awaitingIme = false))
        assertEquals(Action.SHOW_PHONE, ViewerKeyboardCycle.onKeyboardButton(putAway.mode))
    }

    @Test
    fun theImeGoingDownBecausePcKeysWasChosenKeepsThePanel() {
        val pc = apply(apply(closed, Action.SHOW_PHONE), Action.PHONE_TO_PC_KEYS)
        // The IME animates away after the switch; the panel is what is showing.
        assertEquals(Mode.PC_KEYS, pc.mode)
        assertTrue(ViewerKeyboardCycle.barVisible(pc.mode, imeBottomPx = 0, awaitingIme = false))
    }

    @Test
    fun backOnThePanelClosesTheTabBarToo() {
        val pc = Shown(phone = false, panel = true)
        val afterBack = pc.copy(panel = false)
        assertFalse(ViewerKeyboardCycle.barVisible(afterBack.mode, imeBottomPx = 0, awaitingIme = false))
    }

    @Test
    fun theTabBarIsNeverLeftOnItsOwn() {
        for (ime in listOf(0, 1, 300, 1200)) {
            for (awaiting in listOf(false, true)) {
                assertFalse("ime=$ime awaiting=$awaiting", ViewerKeyboardCycle.barVisible(Mode.CLOSED, ime, awaiting))
            }
        }
    }

    @Test
    fun withNoImeShowingTheBarStillLeadsToThePcKeys() {
        // r3 K1: a hardware keyboard (or an IME that does not show) leaves focus held and no
        // bottom inset. The button now closes, so after the short wait the bar appears at its
        // slot at the bottom and the PC keys stay one tap away.
        val up = apply(closed, Action.SHOW_PHONE)
        assertFalse("not while the IME may still come", ViewerKeyboardCycle.barVisible(up.mode, imeBottomPx = 0, awaitingIme = true))
        assertTrue("after the wait", ViewerKeyboardCycle.barVisible(up.mode, imeBottomPx = 0, awaitingIme = false))
        assertEquals(0, ViewerKeyboardCycle.barTranslationYPx(up.mode, imeBottomPx = 0, panelHeightPx = 0))
        assertEquals(Mode.PC_KEYS, apply(up, ViewerKeyboardCycle.onPcKeysTab(up.mode)).mode)
        // and it still closes: the button, or Back (the capture view drops focus).
        assertEquals(Action.CLOSE_ALL, ViewerKeyboardCycle.onKeyboardButton(up.mode))
        assertFalse(ViewerKeyboardCycle.barVisible(up.copy(phone = false).mode, imeBottomPx = 0, awaitingIme = false))
    }

    @Test
    fun anImeThatComesInTimeIsNotWaitedFor() {
        assertTrue(ViewerKeyboardCycle.barVisible(Mode.PHONE, imeBottomPx = 700, awaitingIme = true))
    }

    /** One device, its window and the IME it gets, in px. */
    private data class Screen(val name: String, val windowH: Int, val imeH: Int, val panelH: Int)

    private val screens = listOf(
        // SM-S948N class phone, 1080x2340 at 3x: a portrait IME ~ 40% of the height, landscape ~ 55%.
        Screen("phone portrait", 2340, 940, 1170),
        Screen("phone landscape", 1080, 600, 540),
        // SM-T975N tablet, 2560x1600 at 2x.
        Screen("tablet portrait", 2560, 830, 1280),
        Screen("tablet landscape", 1600, 720, 800),
    )
    private val barH = 132 // 44dp at 3x

    @Test
    fun withThePhoneKeyboardUpTheBarSitsDirectlyAboveIt() {
        for (s in screens) {
            // The panel is gone, so the bar's slot is at the window bottom, which the IME covers.
            val slotBottom = s.windowH
            val dy = ViewerKeyboardCycle.barTranslationYPx(Mode.PHONE, s.imeH, panelHeightPx = 0)
            val bottom = slotBottom + dy
            val top = bottom - barH
            val imeTop = s.windowH - s.imeH
            assertEquals("${s.name}: bar bottom on the IME top", imeTop, bottom)
            assertTrue("${s.name}: bar on screen", top >= 0)
        }
    }

    @Test
    fun withThePanelUpTheBarStaysInItsPlaceAboveThePanel() {
        for (s in screens) {
            assertEquals(s.name, 0, ViewerKeyboardCycle.barTranslationYPx(Mode.PC_KEYS, imeBottomPx = 0, panelHeightPx = s.panelH))
            // An IME still sliding away while the panel is up does not move the bar either.
            assertEquals(s.name, 0, ViewerKeyboardCycle.barTranslationYPx(Mode.PC_KEYS, s.imeH, s.panelH))
        }
    }
}
