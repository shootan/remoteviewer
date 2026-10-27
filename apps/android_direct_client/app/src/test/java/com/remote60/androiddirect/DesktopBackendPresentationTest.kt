package com.remote60.androiddirect

import com.remote60.androiddirect.DesktopBackendPresentation.HostStatus
import com.remote60.androiddirect.DesktopBackendPresentation.Reason
import com.remote60.androiddirect.DesktopBackendPresentation.State
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** ua2-apk r1/r2: the settings screen's capture-backend state (Codex codex_rel_0163_r1.md §1, codex_ua2_apk_src_r1.md). */
class DesktopBackendPresentationTest {
    private val p = DesktopBackendPresentation

    /** A supported host's answer `seq` (requested/active/flags), with the native intent (asked, sent, sentSeq). */
    private fun st(seq: Long, requested: Int, active: Int, flags: Int = p.FLAG_SESSION_SCOPED,
                   asked: Long = 0, sent: Long = 0, sentSeq: Long = 0, valid: Boolean = true, answers: Boolean = true,
                   roleKnown: Boolean = true, effective: Int = requested) =
        HostStatus(valid, seq, requested, effective, active, flags, 1, answers, roleKnown, asked, sent, sentSeq)

    @Test fun notConnectedDisablesAndLightsNothing() {
        val v = p.present(false, p.WGC, st(1, p.WGC, p.WGC))
        assertEquals(State.NOT_CONNECTED, v.state)
        assertFalse(v.enabled)
        assertNull(v.lit)
    }

    // ---- N3: capability unknown / unsupported
    @Test fun beforeTheFirstPongItIsCheckingNotAnOlderHost() {
        val v = p.present(true, null, st(0, 0, 0, valid = false, answers = false, roleKnown = false))
        assertEquals(State.CHECKING, v.state)
        assertNull(v.lit)
    }

    @Test fun anOlderHostIsNeverShownAppliedNorSessionScoped() {
        val v = p.present(true, p.WGC, st(0, 0, 0, valid = false, answers = false, asked = 1, sent = 1, sentSeq = 1))
        assertEquals(State.NO_ANSWER_HOST, v.state)
        assertNull(v.lit)
        assertFalse(v.sessionScoped)
        assertEquals(p.WGC, v.asked)
    }

    @Test fun sessionScopeOnlyWhenTheHostSaysSo() {
        assertTrue(p.present(true, null, st(1, p.DXGI, p.DXGI)).sessionScoped)
        assertFalse(p.present(true, null, st(1, p.DXGI, p.DXGI, flags = 0)).sessionScoped)
    }

    @Test fun supportedButNoAnswerYetIsChecking() {
        assertEquals(State.CHECKING, p.present(true, null, st(0, 0, 0, valid = false)).state)
    }

    // ---- N1: the current request, by seq
    @Test fun prelogonRefusalIsTerminalNotApplying() {
        // asked WGC at a logon-screen host: handled (its seq), requested stays DXGI, BEFORE_LOGIN, no NotYetApplied
        val v = p.present(true, p.WGC, st(3, p.DXGI, p.DXGI, flags = p.FLAG_SESSION_SCOPED or p.FLAG_BEFORE_LOGIN,
            asked = 1, sent = 1, sentSeq = 3))
        assertEquals(State.REFUSED, v.state)
        assertEquals(p.DXGI, v.lit)
        assertEquals(listOf(Reason.BEFORE_LOGIN), v.reasons)
        // ...and stays refused at later questions (no endless "applying")
        assertEquals(State.REFUSED, p.present(true, p.WGC, st(5, p.DXGI, p.DXGI,
            flags = p.FLAG_SESSION_SCOPED or p.FLAG_BEFORE_LOGIN, asked = 1, sent = 1, sentSeq = 3)).state)
    }

    @Test fun sameValueAgainIsNotDoneUntilItsOwnAnswerSaysSo() {
        // DXGI asked again; the last answer already says DXGI but is OLDER than this request -> applying
        assertEquals(State.APPLYING, p.present(true, p.DXGI, st(4, p.DXGI, p.DXGI, asked = 2, sent = 2, sentSeq = 5)).state)
        // its own answer, NotYetApplied -> still applying, nothing lit
        val nya = p.present(true, p.DXGI, st(5, p.DXGI, p.DXGI, flags = p.FLAG_SESSION_SCOPED or p.FLAG_NOT_YET_APPLIED,
            asked = 2, sent = 2, sentSeq = 5))
        assertEquals(State.APPLYING, nya.state)
        assertNull(nya.lit)
        // handled -> applied
        assertEquals(State.APPLIED, p.present(true, p.DXGI, st(6, p.DXGI, p.DXGI, asked = 2, sent = 2, sentSeq = 5)).state)
    }

    @Test fun aBaLateAnswerKeepsTheLatestIntent() {
        // A (WGC) seq 3, B (GDI) seq 4, A (WGC) again seq 5: the late answer to B (seq 4, requested GDI) says nothing
        val late = p.present(true, p.WGC, st(4, p.GDI, p.GDI, asked = 3, sent = 3, sentSeq = 5))
        assertEquals(State.APPLYING, late.state)
        assertNull(late.lit)
        assertEquals(State.APPLIED, p.present(true, p.WGC, st(5, p.WGC, p.WGC, asked = 3, sent = 3, sentSeq = 5)).state)
    }

    @Test fun notSentYetIsApplying() {
        assertEquals(State.APPLYING, p.present(true, p.GDI, st(2, p.DXGI, p.DXGI, asked = 2, sent = 1, sentSeq = 2)).state)
    }

    // ---- N2: a window target is WGC now; the desktop request is kept for the desktop
    @Test fun windowTargetKeepsTheDesktopRequestAndSaysWgcNow() {
        // the stored evidence: requested=3 effective=3 active=1 flags=0x5 (scoped | window target)
        val v = p.present(true, p.GDI, HostStatus(true, 7, p.GDI, p.GDI, p.DXGI, 0x5, 1, true, true, 1, 1, 7))
        assertEquals(State.STORED_FOR_DESKTOP, v.state)
        assertEquals(p.GDI, v.lit)          // this connection's desktop method, as handled -- not the dormant DXGI
        assertTrue(v.windowTarget)
        assertTrue(v.reasons.contains(Reason.WINDOW_TARGET))
    }

    // r3 (codex_ua2_apk_src_r2.md N1 (b)): a policy replacement is not "DXGI applied"
    @Test fun quarantineShowsTheRealOne() {
        val v = p.present(true, p.DXGI, st(2, p.DXGI, p.WGC, flags = p.FLAG_SESSION_SCOPED or p.FLAG_DXGI_QUARANTINED,
            asked = 1, sent = 1, sentSeq = 2, effective = p.WGC))
        assertEquals(State.POLICY_REPLACED, v.state)
        assertEquals(p.WGC, v.active)
        assertEquals(p.WGC, v.effective)
        assertEquals(listOf(Reason.DXGI_QUARANTINED), v.reasons)
    }

    // r3 N1 (a): handled GDI, requested = effective = GDI, but DXGI captures (idle desktop stored it, or the restart fell
    // back) -- "received", not "applied"; and it is terminal (no endless applying), the request still lit as handled
    @Test fun handledButNotCapturingIsReceivedNotApplied() {
        val v = p.present(true, p.GDI, st(4, p.GDI, p.DXGI, asked = 2, sent = 2, sentSeq = 4))
        assertEquals(State.RECEIVED, v.state)
        assertEquals(p.GDI, v.lit)
        assertEquals(p.DXGI, v.active)
        assertEquals(State.RECEIVED, p.present(true, p.GDI, st(9, p.GDI, p.DXGI, asked = 2, sent = 2, sentSeq = 4)).state)
        // the same request once the desktop does capture with it: applied (d)
        assertEquals(State.APPLIED, p.present(true, p.GDI, st(10, p.GDI, p.GDI, asked = 2, sent = 2, sentSeq = 4)).state)
    }

    // r3 N1 (c): nothing asked on this connection; an answer with a seq but no state yet (0 fields, NotYetApplied)
    @Test fun noRequestAndNoStateYetIsCheckingNotIdle() {
        assertEquals(State.CHECKING, p.present(true, null, st(1, 0, 0, flags = p.FLAG_SESSION_SCOPED or p.FLAG_NOT_YET_APPLIED)).state)
        assertEquals(State.CHECKING, p.present(true, null, st(1, 0, 0)).state)
        assertNull(p.present(true, null, st(1, 0, 0)).lit)
        // a window target with no desktop state is not "waiting": WGC now is a fact
        assertEquals(State.IDLE, p.present(true, null, st(1, 0, 0, flags = p.FLAG_SESSION_SCOPED or p.FLAG_WINDOW_TARGET)).state)
    }

    // r3 (Codex note): an OLD answer whose requested equals the latest request (A, seq 3 < sentSeq 5) proves nothing
    @Test fun anOldAnswerWithTheSameCodeIsNotTheLatestRequestsAnswer() {
        val old = p.present(true, p.WGC, st(3, p.WGC, p.WGC, asked = 3, sent = 3, sentSeq = 5))
        assertEquals(State.APPLYING, old.state)
        assertNull(old.lit)
    }

    @Test fun newConnectionShowsTheHostsOwn() {
        val v = p.present(true, null, st(1, p.DXGI, p.DXGI))
        assertEquals(State.IDLE, v.state)
        assertEquals(p.DXGI, v.lit)
    }

    @Test fun rawSnapshotParsing() {
        val s = HostStatus.of(longArrayOf(1, 7, 3, 3, 1, 5, 2, 1, 1, 4, 4, 7))!!
        assertTrue(s.valid && s.hostAnswers && s.roleKnown)
        assertEquals(p.GDI, s.requested)
        assertEquals(7L, s.intentSentSeq)
        assertNull(HostStatus.of(longArrayOf(1, 2, 3, 4, 5, 6, 7, 8)))
        assertNull(HostStatus.of(null))
    }
}
