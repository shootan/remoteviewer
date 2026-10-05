package com.remote60.androiddirect

/**
 * What one press of the viewer rail's keyboard button does (apk-keyboard-phone-first r1).
 *
 * The order is phone keyboard -> PC key panel -> closed. It used to open the PC panel first, with
 * the phone keyboard one tab away; most typing is text, so the phone's own keyboard comes first.
 *
 * Decided from what is actually showing, not from a remembered step, so a phone keyboard the
 * system put away (its own down arrow, Back) makes the next press bring it up again.
 */
object ViewerKeyboardCycle {
    enum class Action { SHOW_PHONE, PHONE_TO_PANEL, CLOSE_PANEL }

    fun onPress(phoneKeyboardUp: Boolean, panelOpen: Boolean): Action = when {
        phoneKeyboardUp -> Action.PHONE_TO_PANEL
        panelOpen -> Action.CLOSE_PANEL
        else -> Action.SHOW_PHONE
    }
}
