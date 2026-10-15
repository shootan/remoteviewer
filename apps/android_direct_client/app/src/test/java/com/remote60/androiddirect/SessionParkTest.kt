package com.remote60.androiddirect

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** keepalive r1: when a parked connection is reused, waited for, or ended (SessionPark). */
class SessionParkTest {

    private val start = 1_000_000L
    private val parked = SessionPark.Parked(
        hostId = "h-1", hostName = "office", accountId = "alice", directoryOrigin = "https://dir.example",
        connectionGeneration = 7, startedAtMs = start, episode = 1,
    )
    private fun native(
        connected: Boolean = true,
        gen: Long = 7,
        phase: SessionPark.NativePhase = SessionPark.NativePhase.PARKED,
    ) = SessionPark.NativeState(connected, gen, phase)

    private val me = SessionPark.Identity("alice", "https://dir.example", sessionPresent = true)

    private fun tap(
        host: String = "h-1",
        account: String = "alice",
        origin: String = "https://dir.example",
        now: Long = start + 10_000,
        state: SessionPark.NativeState = native(),
    ) = SessionPark.onHostTap(parked, host, account, origin, now, state)

    @Test
    fun theSamePcParkedAndHealthy_isReused() {
        assertEquals(SessionPark.Tap.Reuse, tap())
        assertEquals(SessionPark.Tap.Reuse, tap(now = parked.deadlineMs - 1))
    }

    @Test
    fun anythingElse_endsThePark() {
        assertEquals(SessionPark.Tap.Discard("other_host"), tap(host = "h-2"))
        // The same name on another account, or the same account on another server, is not this PC.
        assertEquals(SessionPark.Tap.Discard("identity_changed"), tap(account = "bob"))
        assertEquals(SessionPark.Tap.Discard("identity_changed"), tap(origin = "https://other.example"))
        assertEquals(SessionPark.Tap.Discard("expired"), tap(now = parked.deadlineMs))
        assertEquals(SessionPark.Tap.Discard("peer_lost"), tap(state = native(connected = false)))
        // A reconnect since is another connection, even to the same PC: never adopted.
        assertEquals(SessionPark.Tap.Discard("generation_changed"), tap(state = native(gen = 8)))
        assertEquals(SessionPark.Tap.Discard("park_failed"), tap(state = native(phase = SessionPark.NativePhase.FAILED)))
        assertEquals(SessionPark.Tap.Discard("park_expired"), tap(state = native(phase = SessionPark.NativePhase.EXPIRED)))
        assertEquals(SessionPark.Tap.Discard("park_none"), tap(state = native(phase = SessionPark.NativePhase.NONE)))
    }

    @Test
    fun aTapWhileConfirming_waits_thenGivesUp() {
        // The same tap twice is not a second connection: it waits while the native side confirms.
        val fencing = native(phase = SessionPark.NativePhase.FENCING)
        assertEquals(SessionPark.Tap.Wait, tap(now = start + 500, state = fencing))
        assertEquals(SessionPark.Tap.Discard("confirm_timeout"),
            tap(now = start + SessionPark.CONFIRM_BUDGET_MS, state = fencing))
    }

    @Test
    fun theDeadlineIsHard_fromWhenTheListOpened() {
        assertEquals(start + 5 * 60 * 1000, parked.deadlineMs)
        // Ticks keep it while healthy and end it at the deadline; nothing on the way extends it.
        for (t in listOf(start, start + 1_000, start + 299_999)) assertNull(SessionPark.endReason(parked, t, native(), me))
        assertEquals("expired", SessionPark.endReason(parked, start + 300_000, native(), me))
        assertEquals("0:00", SessionPark.formatRemaining(parked.remainingMs(start + 400_000)))
        assertEquals("5:00", SessionPark.formatRemaining(parked.remainingMs(start)))
        assertEquals("4:59", SessionPark.formatRemaining(parked.remainingMs(start + 1_000)))
    }

    @Test
    fun aParkEndsByItselfWhenTheConnectionDoes() {
        assertEquals("peer_lost", SessionPark.endReason(parked, start + 1, native(connected = false), me))
        assertEquals("generation_changed", SessionPark.endReason(parked, start + 1, native(gen = 9), me))
        assertEquals("confirm_failed",
            SessionPark.endReason(parked, start + 1, native(phase = SessionPark.NativePhase.FAILED), me))
        assertEquals("native_expired",
            SessionPark.endReason(parked, start + 1, native(phase = SessionPark.NativePhase.EXPIRED), me))
        assertNull(SessionPark.endReason(parked, start + 100, native(phase = SessionPark.NativePhase.RELEASING), me))
        assertEquals("confirm_timeout", SessionPark.endReason(parked, start + SessionPark.CONFIRM_BUDGET_MS,
            native(phase = SessionPark.NativePhase.PAUSING), me))
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
    fun aRefusedOrReplacedSignInEndsThePark_atOnce() {
        // r2 K2: not only when the PC is tapped again -- the next tick after the screen knows.
        assertEquals("identity_changed", SessionPark.endReason(parked, start + 1, native(),
            SessionPark.Identity("alice", "https://dir.example", sessionPresent = false)))
        assertEquals("identity_changed", SessionPark.endReason(parked, start + 1, native(),
            SessionPark.Identity("bob", "https://dir.example", sessionPresent = true)))
        assertEquals("identity_changed", SessionPark.endReason(parked, start + 1, native(),
            SessionPark.Identity("alice", "https://other.example", sessionPresent = true)))
        assertNull(SessionPark.endReason(parked, start + 1, native(), me))
    }

    @Test
    fun aHeldTapFallsBackOnlyWhenTheParkFailedByItself() {
        // r2 K4
        for (r in listOf("confirm_failed", "confirm_timeout", "peer_lost", "park_lost", "generation_changed",
            "native_expired", "resume_refused")) assertTrue(r, SessionPark.pendingTapFallsBack(r))
        for (r in listOf("other_host", "disconnect_tap", "sign_out", "app_stopped", "exit", "identity_changed",
            "session_refused", "expired", "network_changed", "network_lost", "host_gone")) {
            assertFalse(r, SessionPark.pendingTapFallsBack(r))
        }
    }

    @Test
    fun anUnreadableBaselineHoldsPushesUntilARealChange() {
        assertTrue(SessionPark.clipboardPushAllowed(false, "focus_gained"))
        assertFalse(SessionPark.clipboardPushAllowed(true, "focus_gained"))
        assertFalse(SessionPark.clipboardPushAllowed(true, "session_started"))
        assertFalse(SessionPark.clipboardPushAllowed(true, "toggle_on"))
        assertTrue(SessionPark.clipboardPushAllowed(true, "clip_changed"))
    }

    @Test
    fun aStaleConnectOwnsNothingNewer() {
        // r2 K2
        val owner = SessionPark.ConnectOwner()
        val a = owner.begin()
        assertTrue(owner.isCurrent(a))
        owner.invalidate()  // sign-out
        assertFalse(owner.isCurrent(a))
        val b = owner.begin()  // signed in again, connected again
        assertFalse(owner.isCurrent(a))
        assertTrue(owner.isCurrent(b))
        // a's native connect had made generation 3; b's made 4: a's late callback closes nothing
        assertFalse(owner.staleCloses(madeGeneration = 3, nativeGenerationNow = 4))
        // a's own connection still the one running: it closes that one
        assertTrue(owner.staleCloses(madeGeneration = 3, nativeGenerationNow = 3))
        // never got as far as a native connect: nothing to close
        assertFalse(owner.staleCloses(madeGeneration = 0, nativeGenerationNow = 0))
    }
}
