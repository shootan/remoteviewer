package com.remote60.androiddirect

import org.junit.Assert.assertEquals
import org.junit.Test

class HostKeyChordTest {
    private fun down(vk: Int) = HostKeyChord.Step(true, vk)
    private fun up(vk: Int) = HostKeyChord.Step(false, vk)

    @Test
    fun altTabIsTheFourEventsThePcViewerSends() {
        // viewer_startup.cpp: send_host_key_chord(ctx, VK_LMENU, VK_TAB, "switch-window")
        assertEquals(
            listOf(down(0xA4), down(0x09), up(0x09), up(0xA4)),
            HostKeyChord.SWITCH_WINDOW,
        )
    }

    @Test
    fun winDIsTheFourEventsThePcViewerSends() {
        // viewer_startup.cpp: send_host_key_chord(ctx, VK_LWIN, 'D', "show-desktop")
        assertEquals(
            listOf(down(0x5B), down(0x44), up(0x44), up(0x5B)),
            HostKeyChord.SHOW_DESKTOP,
        )
    }

    @Test
    fun modifiersGoDownFirstAndComeUpLastInReverse() {
        // Ctrl+Shift+Esc from the key panel goes through the same function.
        assertEquals(
            listOf(down(0x11), down(0x10), down(0x1B), up(0x1B), up(0x10), up(0x11)),
            HostKeyChord.steps(listOf(0x11, 0x10), 0x1B),
        )
    }

    @Test
    fun aPlainKeyIsDownThenUp() {
        assertEquals(listOf(down(0x74), up(0x74)), HostKeyChord.steps(emptyList(), 0x74))
    }
}
