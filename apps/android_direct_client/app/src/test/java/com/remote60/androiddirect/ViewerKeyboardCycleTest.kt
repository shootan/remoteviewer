package com.remote60.androiddirect

import com.remote60.androiddirect.ViewerKeyboardCycle.Action
import org.junit.Assert.assertEquals
import org.junit.Test

class ViewerKeyboardCycleTest {
    /** Applies an action to (phone up, panel open) the way MainActivity does. */
    private fun apply(action: Action): Pair<Boolean, Boolean> = when (action) {
        Action.SHOW_PHONE -> true to false
        Action.PHONE_TO_PANEL -> false to true
        Action.CLOSE_PANEL -> false to false
    }

    @Test
    fun firstPressBringsUpThePhoneKeyboard() {
        assertEquals(Action.SHOW_PHONE, ViewerKeyboardCycle.onPress(phoneKeyboardUp = false, panelOpen = false))
    }

    @Test
    fun theOrderIsPhoneThenPanelThenClosed() {
        var state = false to false
        val seen = mutableListOf<Pair<Boolean, Boolean>>()
        repeat(3) {
            state = apply(ViewerKeyboardCycle.onPress(state.first, state.second))
            seen += state
        }
        assertEquals(listOf(true to false, false to true, false to false), seen)
    }

    @Test
    fun afterTheSystemPutsThePhoneKeyboardAwayTheNextPressBringsItBack() {
        // Phone up, then hidden by its own down arrow: nothing is showing, so it is press one again.
        var state = apply(ViewerKeyboardCycle.onPress(phoneKeyboardUp = false, panelOpen = false))
        assertEquals(true to false, state)
        state = false to false
        assertEquals(Action.SHOW_PHONE, ViewerKeyboardCycle.onPress(state.first, state.second))
    }

    @Test
    fun phoneKeyboardUpWinsOverAnOpenPanel() {
        // The panel's 휴대폰 자판 tab hides the panel first, but if both were ever up the phone
        // keyboard is what the user is looking at, so the press moves on to the panel.
        assertEquals(Action.PHONE_TO_PANEL, ViewerKeyboardCycle.onPress(phoneKeyboardUp = true, panelOpen = true))
    }
}
