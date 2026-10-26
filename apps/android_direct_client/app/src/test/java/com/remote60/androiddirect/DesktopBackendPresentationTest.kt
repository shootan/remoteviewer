package com.remote60.androiddirect

import com.remote60.androiddirect.DesktopBackendPresentation.HostStatus
import com.remote60.androiddirect.DesktopBackendPresentation.Reason
import com.remote60.androiddirect.DesktopBackendPresentation.State
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** ua2-apk r1: the settings screen's capture-backend line (Codex codex_rel_0163_r1.md §1). */
class DesktopBackendPresentationTest {
    private val p = DesktopBackendPresentation

    private fun st(requested: Int, active: Int, flags: Int = 0x1, valid: Boolean = true, answers: Boolean = true) =
        HostStatus(valid, requested, active, active, flags, 1, answers)

    @Test fun notConnectedDisablesAndLightsNothing() {
        val v = p.present(false, p.WGC, st(p.WGC, p.WGC))
        assertEquals(State.NOT_CONNECTED, v.state)
        assertFalse(v.enabled)
        assertNull(v.lit)
    }

    @Test fun anOlderHostIsNeverShownAsApplied() {
        val v = p.present(true, p.WGC, st(p.WGC, p.WGC, answers = false))
        assertEquals(State.NO_ANSWER_HOST, v.state)
        assertNull(v.lit)
        assertTrue(v.enabled)
        assertEquals(p.WGC, v.asked)
    }

    @Test fun beforeTheAnswerNothingIsLit() {
        val v = p.present(true, p.WGC, st(0, 0, valid = false))
        assertEquals(State.CHECKING, v.state)
        assertNull(v.lit)
    }

    @Test fun theHostsActiveBackendIsLitNotTheRequest() {
        // asked DXGI, the host quarantined DXGI: WGC runs and is the one lit, with the reason
        val v = p.present(true, p.DXGI, st(p.DXGI, p.WGC, flags = 0x1 or p.FLAG_DXGI_QUARANTINED))
        assertEquals(State.APPLIED, v.state)
        assertEquals(p.WGC, v.lit)
        assertEquals(listOf(Reason.DXGI_QUARANTINED), v.reasons)
    }

    @Test fun aRequestNotInTheAnswerYetIsApplying() {
        val v = p.present(true, p.WGC, st(p.DXGI, p.DXGI))
        assertEquals(State.APPLYING, v.state)
        assertEquals(p.DXGI, v.lit)  // what runs now, not what was asked
    }

    @Test fun aNewConnectionWithNoChoiceShowsTheHostDefault() {
        val v = p.present(true, null, st(p.DXGI, p.DXGI))
        assertEquals(State.APPLIED, v.state)
        assertEquals(p.DXGI, v.lit)
        assertNull(v.asked)
    }

    @Test fun windowTargetAndLogonScreenReasons() {
        assertEquals(listOf(Reason.WINDOW_TARGET), p.present(true, p.DXGI, st(p.DXGI, p.WGC, flags = p.FLAG_WINDOW_TARGET)).reasons)
        assertEquals(listOf(Reason.BEFORE_LOGIN, Reason.NOT_YET_APPLIED),
            p.present(true, p.WGC, st(p.WGC, p.DXGI, flags = p.FLAG_BEFORE_LOGIN or p.FLAG_NOT_YET_APPLIED)).reasons)
    }

    @Test fun rawSnapshotParsing() {
        val s = HostStatus.of(longArrayOf(1, 7, 2, 2, 2, 1, 3, 1))!!
        assertTrue(s.valid && s.hostAnswers)
        assertEquals(p.WGC, s.active)
        assertNull(HostStatus.of(longArrayOf(1, 2)))
        assertNull(HostStatus.of(null))
    }
}
