package com.remote60.androiddirect

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** keepalive r1 -> keepalive-multi r1: which kept PC a tap reuses, waits for, or ends (SessionPark). */
class SessionParkTest {

    private val origin = "https://dir.example"
    private val keptA = SessionPark.Kept(
        handle = 11, hostId = "h-1", hostName = "office", accountId = "alice", directoryOrigin = origin, manual = false,
        parkedAtMs = 1_000, parkEpisode = 1,
    )
    private fun native(
        handle: Long = 11,
        phase: String = "parked",
        parkPhase: SessionPark.NativePhase = SessionPark.NativePhase.PARKED,
        alive: Boolean = true,
    ) = SessionPark.NativeEntry(handle, keptA.key, phase, parkPhase, alive, generation = 7, remainingMs = 299_000, parkEpisode = 1)

    private val me = SessionPark.Identity("alice", origin, sessionPresent = true)

    private fun tap(
        kept: SessionPark.Kept? = keptA,
        state: SessionPark.NativeEntry? = native(),
        account: String = "alice",
        dir: String = origin,
        anyConfirming: Boolean = false,
    ) = SessionPark.onHostTap(kept, state, account, dir, anyConfirming)

    @Test
    fun aKeptPcParkedAndHealthy_isReusedByItsOwnHandle() {
        assertEquals(SessionPark.Tap.Reuse(11), tap())
    }

    @Test
    fun aPcNotKept_isAnOrdinaryConnect_andTheOthersAreNotTouched() {
        // multi r1: tapping B while A is kept is B's own connect -- 0.2.30 ended A here ("other_host").
        assertEquals(SessionPark.Tap.Connect, tap(kept = null, state = null))
        // the native entry is gone (ended on its own): a new connection, not a reuse
        assertEquals(SessionPark.Tap.Connect, tap(state = null))
    }

    @Test
    fun aKeptConnectionThatCannotBeUsed_endsThatPcOnly() {
        // The same name on another account, or the same account on another server, is not this PC.
        assertEquals(SessionPark.Tap.Discard(11, "identity_changed"), tap(account = "bob"))
        assertEquals(SessionPark.Tap.Discard(11, "identity_changed"), tap(dir = "https://other.example"))
        assertEquals(SessionPark.Tap.Discard(11, "peer_lost"), tap(state = native(alive = false)))
        assertEquals(SessionPark.Tap.Discard(11, "park_failed"), tap(state = native(parkPhase = SessionPark.NativePhase.FAILED)))
        assertEquals(SessionPark.Tap.Discard(11, "park_expired"), tap(state = native(parkPhase = SessionPark.NativePhase.EXPIRED)))
        assertEquals(SessionPark.Tap.Discard(11, "park_none"), tap(state = native(parkPhase = SessionPark.NativePhase.NONE)))
    }

    @Test
    fun anyParkStillConfirming_holdsTheTap_whicheverPcItIsFor() {
        // one PC on screen at a time, and only after the previous one's pause is fenced (multi §3)
        assertEquals(SessionPark.Tap.Wait, tap(anyConfirming = true))
        assertEquals(SessionPark.Tap.Wait, tap(kept = null, state = null, anyConfirming = true))
        assertEquals(SessionPark.Tap.Wait, tap(state = native(parkPhase = SessionPark.NativePhase.FENCING)))
    }

    @Test
    fun theListShowsEachPcsOwnState() {
        assertEquals(SessionPark.ListState.KEPT, SessionPark.listState(native(), onScreen = false))
        assertEquals(SessionPark.ListState.CONFIRMING,
            SessionPark.listState(native(parkPhase = SessionPark.NativePhase.PAUSING), onScreen = false))
        assertEquals(SessionPark.ListState.ON_SCREEN, SessionPark.listState(native(phase = "active"), onScreen = false))
        assertEquals(SessionPark.ListState.CONNECTING, SessionPark.listState(native(phase = "connecting"), onScreen = false))
        assertEquals(SessionPark.ListState.NONE, SessionPark.listState(native(phase = "closing"), onScreen = false))
        assertEquals(SessionPark.ListState.NONE, SessionPark.listState(native(alive = false), onScreen = false))
        assertEquals(SessionPark.ListState.NONE, SessionPark.listState(null, onScreen = false))
        // an evicted PC is not shown as kept: its next tap is a new connection, never a "reuse"
        assertEquals("유지 한도 초과", SessionPark.endedReasonText("lru"))
        assertEquals("시간 만료", SessionPark.endedReasonText("ttl"))
    }

    @Test
    fun theKeyIsDirectoryAccountAndHostId_notANameOrAnAddress() {
        assertEquals("https://dir.example|alice|h-1", keptA.key)
        assertEquals(keptA.key, SessionPark.entryKey(origin, "alice", "h-1"))
        // a renamed PC, or a new connection (another handle) to it, is still the same key
        assertEquals(keptA.key, keptA.copy(hostName = "renamed", handle = 99).key)
    }

    @Test
    fun thePolicyIsAValueObject() {
        assertEquals(3, SessionPark.Policy().maxHosts)
        // keepalive r2: the product policy has no park limit, said as Unlimited (not a 0)
        assertEquals(SessionPark.ParkLimit.Unlimited, SessionPark.Policy().parkLimit)
        assertTrue(SessionPark.Policy().parkLimit.unlimited)
        assertEquals(45_000L, SessionPark.ParkLimit.Finite(45_000L).ttlMs)
        assertFalse(SessionPark.ParkLimit.Finite(45_000L).unlimited)
        assertTrue(runCatching { SessionPark.ParkLimit.Finite(0L) }.isFailure)
        assertFalse(SessionPark.hasDeadline(-1L))
        assertTrue(SessionPark.hasDeadline(0L))
        assertEquals(2, SessionPark.Policy(maxHosts = 2).maxHosts)
    }

    @Test
    fun theRemainingTimeIsShownPerPc() {
        assertEquals("0:00", SessionPark.formatRemaining(0))
        assertEquals("5:00", SessionPark.formatRemaining(300_000))
        assertEquals("4:59", SessionPark.formatRemaining(299_000))
        assertEquals("5분", SessionPark.formatRemainingCoarse(300_000))
        assertEquals("5분", SessionPark.formatRemainingCoarse(240_001))
        assertEquals("4분", SessionPark.formatRemainingCoarse(240_000))
        assertEquals("1분", SessionPark.formatRemainingCoarse(60_000))
        assertEquals("1분 미만", SessionPark.formatRemainingCoarse(59_000))
        assertEquals("1분 미만", SessionPark.formatRemainingCoarse(0))
    }

    @Test
    fun onlyADirectorySessionThatIsConnectedMayPark() {
        assertTrue(SessionPark.canPark("h-1", false, "alice", "tok", true))
        assertFalse(SessionPark.canPark("", false, "alice", "tok", true))          // manual / no host id
        assertFalse(SessionPark.canPark("h-1", true, "alice", "tok", true))        // typed an address
        assertFalse(SessionPark.canPark("h-1", false, "", "tok", true))            // no account
        assertFalse(SessionPark.canPark("h-1", false, "alice", "", true))          // signed out
        assertFalse(SessionPark.canPark("h-1", false, "alice", "tok", false))      // not connected
    }

    @Test
    fun nativePhaseCodes_matchClientParkPhase() {
        assertEquals(SessionPark.NativePhase.PARKED, SessionPark.NativePhase.of(4))
        assertEquals(SessionPark.NativePhase.NONE, SessionPark.NativePhase.of(0))
        // An unknown code is never read as parked.
        assertEquals(SessionPark.NativePhase.FAILED, SessionPark.NativePhase.of(99))
    }

    @Test
    fun aRefusedOrReplacedSignInEndsAKeptPc_atOnce() {
        // r2 K2: not only when the PC is tapped again -- the next tick after the screen knows.
        assertEquals("identity_changed", SessionPark.endReason(keptA, SessionPark.Identity("alice", origin, sessionPresent = false)))
        assertEquals("identity_changed", SessionPark.endReason(keptA, SessionPark.Identity("bob", origin, sessionPresent = true)))
        assertEquals("identity_changed",
            SessionPark.endReason(keptA, SessionPark.Identity("alice", "https://other.example", sessionPresent = true)))
        assertNull(SessionPark.endReason(keptA, me))
    }

    @Test
    fun anUnreadableBaselineHoldsPushesUntilARealChange() {
        assertTrue(SessionPark.clipboardPushAllowed(false, "focus_gained"))
        assertFalse(SessionPark.clipboardPushAllowed(true, "focus_gained"))
        assertFalse(SessionPark.clipboardPushAllowed(true, "session_started"))
        assertFalse(SessionPark.clipboardPushAllowed(true, "toggle_on"))
        assertTrue(SessionPark.clipboardPushAllowed(true, "clip_changed"))
    }

    /** The process's numbering, as the native gates keep it: one counter, a current op per PC's gate. */
    private class ProcessGates {
        var n = 0L
        val current = HashMap<Long, Long>()
        fun next(handle: Long): Long { ++n; current[handle] = n; return n }
        fun isCurrent(handle: Long, op: Long) = op != 0L && current[handle] == op
    }

    @Test
    fun aConnectToBDoesNotMakeAsOperationStale() {
        // multi r1 §2: op numbers stay process-unique, the cancel is per PC
        val gates = ProcessGates()
        val ops = SessionPark.EntryOps(gates::next)
        val a = ops.begin(1)
        val b = ops.begin(2)
        assertTrue(a != b)
        assertTrue("A's operation is still current after B's began", ops.isCurrent(1, a) && gates.isCurrent(1, a))
        assertTrue(ops.isCurrent(2, b))
        ops.invalidate(2)  // B's disconnect
        assertFalse(ops.isCurrent(2, b) || gates.isCurrent(2, b))
        assertTrue("A untouched by B's cancel", ops.isCurrent(1, a))
        // negative control -- 0.2.30: one owner for the whole app; B's begin made A's operation stale
        var single = 0L
        val a0 = ++single
        ++single  // B's begin
        assertTrue("NEGATIVE (0.2.30 single owner): A's op is stale once B began", single != a0)
    }

    @Test
    fun aStaleConnectIsStaleForGood() {
        val gates = ProcessGates()
        val ops = SessionPark.EntryOps(gates::next)
        val a = ops.begin(1)
        ops.invalidate(1)  // its PC disconnected
        assertFalse(ops.isCurrent(1, a) || gates.isCurrent(1, a))
        val again = ops.begin(1)
        assertFalse(ops.isCurrent(1, a))
        assertTrue(ops.isCurrent(1, again))
        ops.drop(1)
        assertFalse(ops.isCurrent(1, again))
    }

    @Test
    fun aHeldTapRunsOnlyIfNothingHappenedSince() {
        // r3 R2-b / multi r1: owned by the lifecycle epoch (sign-out, network change, refused session)
        var epoch = 1L
        val intents = SessionPark.ConnectIntents { epoch }
        intents.onStart()
        val f = intents.scheduleFallback("acc", "https://d", "h1")
        assertTrue(intents.fallbackRuns(f, "acc", "https://d", hostListed = true))
        assertFalse("another account", intents.fallbackRuns(f, "other", "https://d", hostListed = true))
        assertFalse("another directory", intents.fallbackRuns(f, "acc", "https://e", hostListed = true))
        assertFalse("the PC left the list", intents.fallbackRuns(f, "acc", "https://d", hostListed = false))
        epoch = 2  // signed out (or the network changed) meanwhile
        assertFalse("a new lifecycle epoch", intents.fallbackRuns(f, "acc", "https://d", hostListed = true))
    }

    @Test
    fun aStopEndsAHeldTapEvenIfTheAppComesBack() {
        // r4 S2-b
        val intents = SessionPark.ConnectIntents { 1L }
        intents.onStart()
        val f = intents.scheduleFallback("acc", "https://d", "h1")
        intents.onStop()   // the user left
        intents.onStart()  // and came back before it ran
        assertFalse("THE TAP THE USER LEFT IS NOT CARRIED OUT", intents.fallbackRuns(f, "acc", "https://d", hostListed = true))
        // negative control -- r3's check at run time: same epoch, same account and directory, in front again
        val r3runs = f.epoch == 1L && f.account == "acc" && f.origin == "https://d"
        assertTrue("NEGATIVE (r3): it would have run", r3runs)
        val g = intents.scheduleFallback("acc", "https://d", "h1")
        assertTrue(intents.fallbackRuns(g, "acc", "https://d", hostListed = true))
    }
}
