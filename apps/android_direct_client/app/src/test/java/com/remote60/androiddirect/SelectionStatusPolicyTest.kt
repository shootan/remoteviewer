package com.remote60.androiddirect

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * The transition MainActivity takes on a pending selection, fed the status tokens the native
 * session actually produces (native_video_client_session.cpp / native_video_client_shared_core.cpp).
 *
 * The other half -- that a late answer to an earlier pick leaves the token as the current pick's
 * "requested" one -- is checked against a real host by host_monitor_fallback_e2e_test (phase 8).
 */
class SelectionStatusPolicyTest {
    private fun v(status: String) = SelectionStatusPolicy.verdict(status)

    @Test
    fun aPickInFlightChangesNothing() {
        assertEquals(SelectionStatusPolicy.Verdict.NONE, v("monitor_select_requested"))
        assertEquals(SelectionStatusPolicy.Verdict.NONE, v("window_select_requested"))
        assertEquals(SelectionStatusPolicy.Verdict.NONE, v("desktop_select_requested"))
        assertEquals(SelectionStatusPolicy.Verdict.NONE, v("window_list_received count=8"))
    }

    @Test
    fun itsOwnAnswerAcksOrFailsIt() {
        assertEquals(SelectionStatusPolicy.Verdict.ACKED, v("window_selected: GNLINKTEST2"))
        assertEquals(SelectionStatusPolicy.Verdict.ACKED, v("window_selected: desktop"))
        assertEquals(SelectionStatusPolicy.Verdict.FAILED, v("window_select_failed: monitor_gone"))
        assertEquals(SelectionStatusPolicy.Verdict.FAILED, v("window_select_failed: capture_restart_failed"))
        assertEquals(SelectionStatusPolicy.Verdict.FAILED, v("window_select_failed: monitor_list_changed"))
    }

    @Test
    fun refusalsBeforeSendingAreNotAFailureOfAPendingPick() {
        // Returned synchronously to the caller, which never starts the switch for them.
        assertEquals(SelectionStatusPolicy.Verdict.NONE, v("monitor_select_unsupported: host_update_required"))
        assertEquals(SelectionStatusPolicy.Verdict.NONE, v("monitor_select_failed: monitor_list_changed"))
    }
}
