package com.remote60.androiddirect

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** keepalive r2 (Codex codex_keepalive_r2.md sections 1 and 6): the client policy, without a device. */
class KeepaliveR2PolicyTest {
    private val o = "https://dir.example"
    private fun k(host: String, account: String = "alice", origin: String = o) = RecentHosts.Key(origin, account, host)

    // ---------------------------------------------------------------- the recent three

    @Test
    fun aSuccessfulUseMovesToTheFrontAndOnlyThreeAreKept() {
        val b = RecentHosts.Book()
        for (h in listOf("A", "B", "C")) b.recordUse(k(h))
        assertEquals(listOf("C", "B", "A"), b.recent(o, "alice"))
        b.recordUse(k("A"))  // A used again: to the front, nothing drops
        assertEquals(listOf("A", "C", "B"), b.recent(o, "alice"))
        b.recordUse(k("D"))  // a fourth PC used: the least recent (B) drops off
        assertEquals(listOf("D", "A", "C"), b.recent(o, "alice"))
    }

    @Test
    fun theBookIsPerAccountAndPerDirectory() {
        val b = RecentHosts.Book()
        b.recordUse(k("A"))
        b.recordUse(k("B", account = "bob"))
        b.recordUse(k("C", origin = "https://other.example"))
        assertEquals(listOf("A"), b.recent(o, "alice"))
        assertEquals(listOf("B"), b.recent(o, "bob"))
        assertEquals(listOf("C"), b.recent("https://other.example", "alice"))
    }

    @Test
    fun nothingWithoutAFullKeyIsRecorded() {
        val b = RecentHosts.Book()
        assertFalse(b.recordUse(RecentHosts.Key(o, "", "A")))
        assertFalse(b.recordUse(RecentHosts.Key("", "alice", "A")))
        assertFalse(b.recordUse(RecentHosts.Key(o, "alice", "")))
        assertTrue(b.recent(o, "alice").isEmpty())
    }

    @Test
    fun itSurvivesARestartAndABrokenDocumentIsAnEmptyBook() {
        val b = RecentHosts.Book()
        for (h in listOf("A", "B", "C")) b.recordUse(k(h))
        b.recordUse(k("X", account = "bob"))
        val again = RecentHosts.Book.fromJson(b.toJson())
        assertEquals(listOf("C", "B", "A"), again.recent(o, "alice"))
        assertEquals(listOf("X"), again.recent(o, "bob"))
        assertTrue(RecentHosts.Book.fromJson("{not json").recent(o, "alice").isEmpty())
        assertTrue(RecentHosts.Book.fromJson("{\"v\":2,\"lists\":[]}").recent(o, "alice").isEmpty())
        // a document with more than three (an older or tampered write) is cut to three
        val many = "{\"v\":1,\"lists\":[{\"origin\":\"$o\",\"account\":\"alice\",\"hosts\":[\"1\",\"2\",\"3\",\"4\"]}]}"
        assertEquals(listOf("1", "2", "3"), RecentHosts.Book.fromJson(many).recent(o, "alice"))
    }

    @Test
    fun theStoredDocumentHoldsNoTokenAddressOrName() {
        val b = RecentHosts.Book()
        b.recordUse(k("host-123"))
        val doc = b.toJson()
        assertFalse(doc.contains("token"))
        assertFalse(doc.contains("ip"))
        assertFalse(doc.contains("name"))
        assertTrue(doc.contains("host-123"))
    }

    // ---------------------------------------------------------------- the user's disconnect

    @Test
    fun aDisconnectedPcIsNotPreparedAgainUntilTheUserConnectsItOrSignsOut() {
        val s = RecentHosts.Suppression()
        val p = Preconnect.Planner { 0.0 }
        val cands = listOf(Preconnect.Candidate(k("A"), true), Preconnect.Candidate(k("B"), true))
        s.suppress(k("A"))
        assertEquals(k("B"), p.next(0, cands, emptySet(), s, manualPending = false))  // A skipped, B prepared
        p.onResult(k("B"), true, 0)
        assertNull(p.next(1, cands, setOf(k("B")), s, false))  // B kept now, A suppressed: nothing
        s.release(k("A"))  // the user connects A again
        assertEquals(k("A"), p.next(2, cands, setOf(k("B")), s, false))
        s.suppress(k("A"))
        s.clear()  // sign-out
        assertEquals(0, s.size())
    }

    // ---------------------------------------------------------------- one at a time, manual first, back-off

    @Test
    fun oneAutomaticConnectAtATimeAndAManualTapComesFirst() {
        val s = RecentHosts.Suppression()
        val p = Preconnect.Planner { 0.0 }
        val cands = listOf(Preconnect.Candidate(k("A"), true), Preconnect.Candidate(k("B"), true))
        assertEquals(k("A"), p.next(0, cands, emptySet(), s, false))
        assertNull("a second one while A is in flight", p.next(0, cands, emptySet(), s, false))
        assertEquals("a manual tap: the automatic one in flight gives way", k("A"), p.onManualTap())
        assertNull("nothing automatic while the manual tap is handled", p.next(0, cands, emptySet(), s, true))
    }

    @Test
    fun theBackOffIs1_2_4_8_16Then30sWithJitter() {
        assertEquals(1_000L, Preconnect.backoffMs(1, 0.0))
        assertEquals(2_000L, Preconnect.backoffMs(2, 0.0))
        assertEquals(4_000L, Preconnect.backoffMs(3, 0.0))
        assertEquals(8_000L, Preconnect.backoffMs(4, 0.0))
        assertEquals(16_000L, Preconnect.backoffMs(5, 0.0))
        assertEquals(30_000L, Preconnect.backoffMs(6, 0.0))
        assertEquals(30_000L, Preconnect.backoffMs(20, 0.0))  // capped
        assertTrue(Preconnect.backoffMs(6, 0.999999999) in 35_000L..36_000L)  // +20 % at most
        assertTrue(Preconnect.backoffMs(1, 0.5) in 1_000L..1_200L)
    }

    @Test
    fun aFailingPcWaitsItsBackOffAndDoesNotHoldUpTheOthers() {
        val s = RecentHosts.Suppression()
        val p = Preconnect.Planner { 0.0 }
        val cands = listOf(Preconnect.Candidate(k("A"), true), Preconnect.Candidate(k("B"), true))
        assertEquals(k("A"), p.next(0, cands, emptySet(), s, false))
        p.onResult(k("A"), false, 0)
        assertEquals(1, p.failuresOf(k("A")))
        assertEquals("A waits 1 s; B is next", k("B"), p.next(10, cands, emptySet(), s, false))
        p.onResult(k("B"), true, 10)
        assertNull("A still inside its back-off", p.next(500, cands, setOf(k("B")), s, false))
        assertEquals(k("A"), p.next(1_000, cands, setOf(k("B")), s, false))
        p.onResult(k("A"), false, 1_000)
        assertEquals(1_000L + 2_000L, p.notBeforeOf(k("A")))  // second failure: 2 s
        p.onResult(k("A"), true, 3_000)
        assertEquals(0, p.failuresOf(k("A")))  // a success resets it
    }

    @Test
    fun offlineKeptAndBeyondThreeAreSkipped() {
        val s = RecentHosts.Suppression()
        val p = Preconnect.Planner { 0.0 }
        val cands = listOf(
            Preconnect.Candidate(k("A"), false),  // offline: skipped, nothing else changes
            Preconnect.Candidate(k("B"), true),
            Preconnect.Candidate(k("C"), true),
            Preconnect.Candidate(k("D"), true),  // a fourth: never prepared
        )
        assertEquals(k("C"), p.next(0, cands, setOf(k("B")), s, false))
        p.onResult(k("C"), true, 0)
        assertNull(p.next(1, cands, setOf(k("B"), k("C")), s, false))
    }

    @Test
    fun automaticPreparationIsOffInAnOrdinaryBuild() {
        // codex_ka_e_switch_decision.md: off until the independent check; only -Pgnlink.testStandbyAutoPrepare=true
        // (a debug test build, refused for a release) turns it on
        assertFalse(Preconnect.AUTO_PREPARE)
    }
}
