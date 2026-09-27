package com.remote60.androiddirect

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ViewerWindowSheetModelTest {
    private fun state(
        switching: Boolean = false,
        locked: Boolean = false,
        awaiting: Boolean = false,
        waitedMs: Long = 0L,
        items: Int = 3,
    ) = ViewerWindowSheetModel.resolve(switching, locked, awaiting, waitedMs, items)

    @Test
    fun aSwitchInFlightOutranksEverything() {
        assertEquals(WindowSheetState.SWITCHING, state(switching = true, locked = true, awaiting = true, waitedMs = 99_999L, items = 0))
    }

    @Test
    fun aPinnedTargetIsReportedBeforeTheListState() {
        assertEquals(WindowSheetState.LOCKED, state(locked = true, awaiting = true, items = 0))
    }

    @Test
    fun anUnansweredRequestFailsAtTheTimeoutAndNotBefore() {
        val limit = ViewerWindowSheetModel.LIST_TIMEOUT_MS
        assertEquals(WindowSheetState.LOADING, state(awaiting = true, waitedMs = limit - 1, items = 0))
        assertEquals(WindowSheetState.ERROR, state(awaiting = true, waitedMs = limit, items = 0))
        // A refresh over a list already shown fails the same way instead of hiding the old rows.
        assertEquals(WindowSheetState.ERROR, state(awaiting = true, waitedMs = limit, items = 2))
        assertEquals(WindowSheetState.READY, state(awaiting = true, waitedMs = limit - 1, items = 2))
    }

    @Test
    fun anAnsweredEmptyListIsEmptyNotLoading() {
        assertEquals(WindowSheetState.EMPTY, state(awaiting = false, items = 0))
        assertEquals(WindowSheetState.READY, state(awaiting = false, items = 1))
    }

    @Test
    fun rowsAreBlockedOnlyWhileSwitchingOrPinned() {
        assertFalse(ViewerWindowSheetModel.rowsEnabled(WindowSheetState.SWITCHING))
        assertFalse(ViewerWindowSheetModel.rowsEnabled(WindowSheetState.LOCKED))
        for (s in listOf(WindowSheetState.READY, WindowSheetState.LOADING, WindowSheetState.ERROR, WindowSheetState.EMPTY)) {
            assertTrue(s.name, ViewerWindowSheetModel.rowsEnabled(s))
        }
    }

    @Test
    fun theCheckStaysOnWhatIsShownUntilTheSwitchFinishes() {
        // The host may already report the new id while the first frame has not arrived.
        assertEquals(7L, ViewerWindowSheetModel.checkedId(switching = true, hostSelectedId = 9L, lastConfirmedId = 7L))
        assertEquals(9L, ViewerWindowSheetModel.checkedId(switching = false, hostSelectedId = 9L, lastConfirmedId = 7L))
    }
}

class ViewerKeyRoutingTest {
    @Test
    fun aHardwareKeyReachesThePcOnlyWithNoSheetOpen() {
        assertEquals(KeyRoute.HOST, ViewerKeyRouting.route(inViewer = true, sheetOpen = false, fromVirtualKeyboard = false, isBack = false))
        assertEquals(KeyRoute.LOCAL, ViewerKeyRouting.route(inViewer = true, sheetOpen = true, fromVirtualKeyboard = false, isBack = false))
    }

    @Test
    fun backVirtualKeysAndOtherScreensStayOnThePhone() {
        assertEquals(KeyRoute.LOCAL, ViewerKeyRouting.route(inViewer = true, sheetOpen = false, fromVirtualKeyboard = false, isBack = true))
        assertEquals(KeyRoute.LOCAL, ViewerKeyRouting.route(inViewer = true, sheetOpen = false, fromVirtualKeyboard = true, isBack = false))
        assertEquals(KeyRoute.LOCAL, ViewerKeyRouting.route(inViewer = false, sheetOpen = false, fromVirtualKeyboard = false, isBack = false))
    }
}
