package com.remote60.androiddirect

/**
 * The viewer's two keyboards and the [휴대폰 자판 | PC 키] tabs that switch between them
 * (apk-keyboard-tabs r1).
 *
 * The rail's keyboard button brings up the phone's own keyboard first (most typing is text). It
 * used to take a second press of the same button to get to the PC key panel, but in landscape the
 * phone keyboard covers the rail, so that press could not be made and the panel was unreachable.
 * The way across is now a tab bar that sits directly above whichever keyboard is up: above the
 * phone keyboard (lifted by the IME inset, which this window does not resize for) or above the
 * panel (in the normal layout flow).
 *
 * Decided from what is actually showing, not from a remembered step, so a phone keyboard the
 * system put away (its own down arrow, Back) closes everything, tab bar included.
 */
object ViewerKeyboardCycle {
    enum class Mode { CLOSED, PHONE, PC_KEYS }

    enum class Action { SHOW_PHONE, CLOSE_ALL, PHONE_TO_PC_KEYS, PC_KEYS_TO_PHONE, NONE }

    fun mode(phoneKeyboardUp: Boolean, panelOpen: Boolean): Mode = when {
        panelOpen -> Mode.PC_KEYS
        phoneKeyboardUp -> Mode.PHONE
        else -> Mode.CLOSED
    }

    /** The rail's keyboard button: opens the phone keyboard, or closes whichever is up. */
    fun onKeyboardButton(mode: Mode): Action =
        if (mode == Mode.CLOSED) Action.SHOW_PHONE else Action.CLOSE_ALL

    fun onPhoneTab(mode: Mode): Action =
        if (mode == Mode.PC_KEYS) Action.PC_KEYS_TO_PHONE else Action.NONE

    fun onPcKeysTab(mode: Mode): Action =
        if (mode == Mode.PHONE) Action.PHONE_TO_PC_KEYS else Action.NONE

    /**
     * Shown only while a keyboard is up, so the bar never stays behind on its own. With the phone
     * keyboard it waits for the IME to have a height: before that its place is not known.
     */
    fun barVisible(mode: Mode, imeBottomPx: Int): Boolean = when (mode) {
        Mode.CLOSED -> false
        Mode.PHONE -> imeBottomPx > 0
        Mode.PC_KEYS -> true
    }

    /**
     * The bar's layout slot is at the bottom of the viewer, directly above the panel (the window
     * bottom when the panel is gone). The phone keyboard draws over the window from the bottom, so
     * the bar is lifted by however much of the IME reaches above that slot's bottom.
     */
    fun barTranslationYPx(mode: Mode, imeBottomPx: Int, panelHeightPx: Int): Int =
        if (mode == Mode.PHONE) -(imeBottomPx - panelHeightPx).coerceAtLeast(0) else 0
}
