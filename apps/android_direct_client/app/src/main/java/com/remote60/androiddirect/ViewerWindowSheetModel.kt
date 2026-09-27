package com.remote60.androiddirect

/**
 * What the in-viewer window sheet says about its list (apk-ui r1, Codex 2).
 *
 * Kept out of MainActivity so the order of the checks -- which one wins when several are true --
 * is a unit test and not a reading of the render loop. A switch in progress outranks everything:
 * the list is still shown but not tappable, so a second request cannot race the first.
 */
enum class WindowSheetState { SWITCHING, LOCKED, LOADING, ERROR, EMPTY, READY }

object ViewerWindowSheetModel {
    /** A list request that has not been answered in this long is reported as failed. */
    const val LIST_TIMEOUT_MS = 5000L

    fun resolve(
        switching: Boolean,
        selectionLocked: Boolean,
        awaitingList: Boolean,
        waitedMs: Long,
        itemCount: Int,
    ): WindowSheetState = when {
        switching -> WindowSheetState.SWITCHING
        selectionLocked -> WindowSheetState.LOCKED
        awaitingList && waitedMs >= LIST_TIMEOUT_MS -> WindowSheetState.ERROR
        awaitingList && itemCount == 0 -> WindowSheetState.LOADING
        itemCount == 0 -> WindowSheetState.EMPTY
        else -> WindowSheetState.READY
    }

    /**
     * Rows accept a tap unless a switch is already in flight or the host has pinned the target.
     * A slow or failed list does not block them: the desktop row is always there to fall back on.
     */
    fun rowsEnabled(state: WindowSheetState): Boolean =
        state != WindowSheetState.SWITCHING && state != WindowSheetState.LOCKED

    /**
     * The id that carries the check. It moves only once a switch has finished -- while one is in
     * flight the mark stays on what is actually on screen, not on what was asked for.
     */
    fun checkedId(switching: Boolean, hostSelectedId: Long, lastConfirmedId: Long): Long =
        if (switching) lastConfirmedId else hostSelectedId
}
