package com.remote60.androiddirect

/**
 * A modifier+key chord as the key events the host must receive, in order (apk-ui r3).
 *
 * The same contract as the PC viewer's host_key_chord (viewer_key_chord.hpp): modifiers go down
 * first and come up LAST, in reverse. The host applies these as real keystrokes -- releasing Alt
 * early turns Alt+Tab into a bare Tab, and never releasing it leaves Alt stuck down on someone
 * else's machine. Pure, so the order is a unit test.
 */
object HostKeyChord {
    const val VK_TAB = 0x09
    const val VK_D = 0x44
    const val VK_LWIN = 0x5B
    const val VK_LMENU = 0xA4

    data class Step(val down: Boolean, val vk: Int)

    fun steps(modifiers: List<Int>, key: Int): List<Step> =
        modifiers.map { Step(true, it) } +
            Step(true, key) +
            Step(false, key) +
            modifiers.asReversed().map { Step(false, it) }

    /** The rail's 창 전환: Alt+Tab, with the VKs the PC viewer's toolbar sends. */
    val SWITCH_WINDOW: List<Step> = steps(listOf(VK_LMENU), VK_TAB)

    /** The rail's 바탕화면: Win+D. */
    val SHOW_DESKTOP: List<Step> = steps(listOf(VK_LWIN), VK_D)
}
