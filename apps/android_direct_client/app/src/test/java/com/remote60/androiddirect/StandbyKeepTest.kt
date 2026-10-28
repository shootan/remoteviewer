package com.remote60.androiddirect

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** keepalive-r2 ② e, kept model A (Codex codex_keepalive_r2_kept_model.md, codex_ka_e_switch_decision.md): the rules, pure. */
class StandbyKeepTest {
    private val o = "https://dir.example"
    private fun k(host: String, account: String = "alice") = RecentHosts.Key(o, account, host)
    private fun lease(state: StandbyKeep.LeaseState, handedOff: Boolean = false, needs: Boolean = false) =
        StandbyKeep.Lease(true, state, 0L, handedOff, needs)

    // ---------------------------------------------------------------- what the list says

    @Test
    fun onlyAConfirmedEntryOnALiveLeaseIsReady() {
        assertEquals(StandbyKeep.Shown.READY, StandbyKeep.shown(lease(StandbyKeep.LeaseState.PARKED), false))
        assertNull(StandbyKeep.shown(StandbyKeep.Lease.NONE, false))
        // idle, waiting for a capability, admitting, backing off: never "ready"
        assertEquals(StandbyKeep.Shown.PREPARING, StandbyKeep.shown(lease(StandbyKeep.LeaseState.IDLE, needs = true), false))
        assertEquals(StandbyKeep.Shown.PREPARING, StandbyKeep.shown(lease(StandbyKeep.LeaseState.ADMITTING), false))
        assertEquals(StandbyKeep.Shown.RETRY, StandbyKeep.shown(lease(StandbyKeep.LeaseState.WAITING), false))
        assertEquals(StandbyKeep.Shown.UNSUPPORTED, StandbyKeep.shown(lease(StandbyKeep.LeaseState.UNSUPPORTED), false))
    }

    @Test
    fun aHandedOffLeaseIsNeverReady() {
        // its session is on screen: active / activating; not on screen: the session is ending -- returning, not ready
        assertEquals(StandbyKeep.Shown.ACTIVE, StandbyKeep.shown(lease(StandbyKeep.LeaseState.ACTIVE, handedOff = true), true))
        assertEquals(StandbyKeep.Shown.ACTIVATING,
            StandbyKeep.shown(lease(StandbyKeep.LeaseState.ACTIVATING, handedOff = true), true))
        assertEquals(StandbyKeep.Shown.RETURNING, StandbyKeep.shown(lease(StandbyKeep.LeaseState.ACTIVE, handedOff = true), false))
        // even a parked entry is not ready while its socket is still handed off
        assertEquals(StandbyKeep.Shown.RETURNING, StandbyKeep.shown(lease(StandbyKeep.LeaseState.PARKED, handedOff = true), false))
    }

    @Test
    fun theNativeStatusArrayIsRead() {
        val l = StandbyKeep.Lease.of(longArrayOf(1, 2, 3, 0, 1))
        assertTrue(l.exists)
        assertEquals(StandbyKeep.LeaseState.PARKED, l.state)
        assertEquals(3L, l.lost)
        assertFalse(l.handedOff)
        assertTrue(l.needsCapability)
        assertFalse(StandbyKeep.Lease.of(longArrayOf(0, 2, 0, 0, 0)).exists)
        assertFalse(StandbyKeep.Lease.of(null).exists)
        assertFalse(StandbyKeep.Lease.of(longArrayOf(1, 2)).exists)
    }

    // ---------------------------------------------------------------- a tap

    @Test
    fun aTapActivatesOnlyAReadyLease() {
        assertEquals(StandbyKeep.Tap.Activate, StandbyKeep.onTap(lease(StandbyKeep.LeaseState.PARKED)))
        assertEquals(StandbyKeep.Tap.Cold(false), StandbyKeep.onTap(null))
        assertEquals(StandbyKeep.Tap.Cold(false), StandbyKeep.onTap(StandbyKeep.Lease.NONE))
        // a lease still preparing / backing off / unsupported: the ordinary connect, the lease let go first
        assertEquals(StandbyKeep.Tap.Cold(true), StandbyKeep.onTap(lease(StandbyKeep.LeaseState.ADMITTING)))
        assertEquals(StandbyKeep.Tap.Cold(true), StandbyKeep.onTap(lease(StandbyKeep.LeaseState.WAITING)))
        assertEquals(StandbyKeep.Tap.Cold(true), StandbyKeep.onTap(lease(StandbyKeep.LeaseState.UNSUPPORTED)))
    }

    @Test
    fun aTapWhileTheLastSessionStillHandsTheSocketBackWaits() {
        // not a cold connect (a fresh directory request is not the steady reuse model A counts)
        assertEquals(StandbyKeep.Tap.Wait, StandbyKeep.onTap(lease(StandbyKeep.LeaseState.ACTIVE, handedOff = true)))
        assertEquals(StandbyKeep.Tap.Wait, StandbyKeep.onTap(lease(StandbyKeep.LeaseState.PARKED, handedOff = true)))
    }

    // ---------------------------------------------------------------- "PC 목록"

    @Test
    fun theListReturnFollowsModelA() {
        fun r(fromStandby: Boolean = false, directory: Boolean = true, supported: Boolean = true, prelogin: Boolean = false,
              known: Boolean = false, enabled: Boolean = true) =
            StandbyKeep.onListReturn(fromStandby, directory, supported, prelogin, known, enabled)
        assertEquals(StandbyKeep.ListReturn.BACK_TO_STANDBY, r(fromStandby = true))
        assertEquals(StandbyKeep.ListReturn.FIRST_TRANSITION, r())
        // raw IP, a directory without standby, a pre-login worker, an older host already found: unsupported -> ends
        assertEquals(StandbyKeep.ListReturn.UNSUPPORTED_END, r(directory = false))
        assertEquals(StandbyKeep.ListReturn.UNSUPPORTED_END, r(supported = false))
        assertEquals(StandbyKeep.ListReturn.UNSUPPORTED_END, r(prelogin = true))
        assertEquals(StandbyKeep.ListReturn.UNSUPPORTED_END, r(fromStandby = true, prelogin = true))
        assertEquals(StandbyKeep.ListReturn.UNSUPPORTED_END, r(known = true))
        assertEquals(StandbyKeep.ListReturn.UNSUPPORTED_END, r(fromStandby = true, enabled = false))
    }

    // ---------------------------------------------------------------- the three, the fences

    @Test
    fun aLeaseGoesWhenItsPcLeavesTheRecentThreeButNeverTheOneOnScreen() {
        val recent = listOf(k("D"), k("A"), k("C"))
        assertEquals(listOf(k("B")), StandbyKeep.evictions(recent, listOf(k("A"), k("B"), k("C")), null))
        assertEquals(emptyList<RecentHosts.Key>(), StandbyKeep.evictions(recent, listOf(k("A"), k("B")), k("B")))
        // a failed fourth does not reorder the recent list (RecentHosts.recordUse is a success only): nothing goes
        assertEquals(emptyList<RecentHosts.Key>(), StandbyKeep.evictions(listOf(k("A"), k("B"), k("C")),
            listOf(k("A"), k("B"), k("C")), null))
    }

    @Test
    fun aLateCapabilityOfAReleasedOrRecreatedLeaseIsNotCurrent() {
        val b = StandbyKeep.Book()
        val i1 = b.begin(k("A"))
        assertTrue(b.isCurrent(k("A"), i1))
        b.end(k("A"))
        assertFalse(b.isCurrent(k("A"), i1))  // released: its capability goes nowhere
        val i2 = b.begin(k("A"))               // a new lease under the same key
        assertTrue(i2 != i1)
        assertFalse(b.isCurrent(k("A"), i1))  // the old one's late Provide does not reach the new lease
        assertTrue(b.isCurrent(k("A"), i2))
        assertFalse(b.isCurrent(k("A"), 0L))
    }

    @Test
    fun aReleaseCannotFallBetweenTheCheckAndTheProvide() {
        // r2 (AVD ke_r18): the check and the Provide hold the book's lock; an end() from another thread waits for both
        val b = StandbyKeep.Book()
        val i = b.begin(k("A"))
        val order = java.util.Collections.synchronizedList(mutableListOf<String>())
        val inBlock = java.util.concurrent.CountDownLatch(1)
        val ender = Thread {
            inBlock.await()
            b.end(k("A"))
            order.add("end")
        }
        ender.start()
        val r = b.ifCurrent(k("A"), i) {
            inBlock.countDown()
            Thread.sleep(200)  // the ender is blocked on the lock all this time
            order.add("provide")
            true
        }
        ender.join(5000)
        assertEquals(true, r)
        assertEquals(listOf("provide", "end"), order.toList())
        assertNull(b.ifCurrent(k("A"), i) { true })  // after the end: nothing runs
        assertNull(b.ifCurrent(k("A"), 0L) { true })
    }

    @Test
    fun signOutEndsEveryLeaseAndEveryFence() {
        val b = StandbyKeep.Book()
        val a = b.begin(k("A"))
        b.begin(k("B"))
        b.markUnsupported(k("C"), 1_000L)
        assertEquals(setOf(k("A"), k("B")), b.endAll().toSet())
        assertFalse(b.isCurrent(k("A"), a))
        assertTrue(b.keys().isEmpty())
        assertFalse(b.isUnsupported(k("C"), 1_000L))
        // numbers are not reused after a sign-out: an old job of the previous sign-in cannot match a new lease
        assertTrue(b.begin(k("A")) > a)
    }

    @Test
    fun oneCapabilityJobAtATimeWithItsOwnBackoff() {
        val b = StandbyKeep.Book()
        val a = b.begin(k("A"))
        val c = b.begin(k("C"))
        assertTrue(b.startStaging(k("A"), a, 1_000L))
        assertFalse(b.startStaging(k("C"), c, 1_000L))        // staging <= 1
        b.endStaging(k("A"), a, ok = false, nowMs = 1_000L, random = 0.0)
        assertEquals(1, b.failuresOf(k("A")))
        assertFalse(b.startStaging(k("A"), a, 1_500L))        // backing off (1 s)
        assertTrue(b.startStaging(k("C"), c, 1_500L))         // another PC is not held by A's failure
        b.endStaging(k("C"), c, ok = true, nowMs = 1_600L, random = 0.0)
        assertTrue(b.startStaging(k("A"), a, 2_000L))
        b.endStaging(k("A"), a, ok = true, nowMs = 2_100L, random = 0.0)
        assertEquals(0, b.failuresOf(k("A")))
        assertFalse(b.startStaging(k("A"), a + 100, 3_000L))  // not the current incarnation
        b.end(k("A"))
        assertFalse(b.startStaging(k("A"), a, 3_000L))        // released
    }

    @Test
    fun anUnansweredLeaseHoldsOffOnlyTheAppsOwnPreparationForAWhile() {
        // Codex: an unanswered lease may be an older host or a path that failed for now -- not taken for an older version
        val b = StandbyKeep.Book()
        b.markUnsupported(k("A"), 1_000L)
        assertTrue(b.isUnsupported(k("A"), 1_000L + StandbyKeep.UNANSWERED_HOLD_MS - 1))
        assertFalse(b.isUnsupported(k("A"), 1_000L + StandbyKeep.UNANSWERED_HOLD_MS))
        assertFalse(b.isUnsupported(k("B"), 1_000L))
    }

    @Test
    fun releasingTheStagingLeaseFreesTheSlot() {
        val b = StandbyKeep.Book()
        val a = b.begin(k("A"))
        val c = b.begin(k("C"))
        assertTrue(b.startStaging(k("A"), a, 0L))
        b.end(k("A"))
        assertNull(b.stagingKey())
        assertTrue(b.startStaging(k("C"), c, 0L))
    }

    // ---------------------------------------------------------------- r2 (Codex codex_ka_e_src_r1.md)

    @Test
    fun theOldSessionsTailIsSettlingNotReady() {
        // D1: the entry alive on its lease, but the old session's datagrams still arriving -- not "ready"
        val settling = StandbyKeep.Lease(true, StandbyKeep.LeaseState.PARKED, 0L, false, false, tailActive = true, strays = 40)
        assertEquals(StandbyKeep.Shown.SETTLING, StandbyKeep.shown(settling, false))
        // a tap on it is the user's explicit activation (rollover allowed)
        assertEquals(StandbyKeep.Tap.Activate, StandbyKeep.onTap(settling))
        val l = StandbyKeep.Lease.of(longArrayOf(1, 2, 0, 0, 0, 1, 40))
        assertTrue(l.tailActive)
        assertEquals(40L, l.strays)
        // an older bridge's 5-element status: no tail
        assertFalse(StandbyKeep.Lease.of(longArrayOf(1, 2, 0, 0, 0)).tailActive)
    }

    @Test
    fun aReturnsFallbackKeepsItsClipboardBaseline() {
        // r3 R3: the activation failed and the same tap connected the ordinary way (relay / private): still a return --
        // the phone's text is this binding's baseline (not sent), unreadable -> held; a separate cold connect is fresh
        assertTrue(StandbyKeep.isReturn(preparedReturn = true, pathKind = "relay"))
        assertTrue(StandbyKeep.isReturn(preparedReturn = false, pathKind = "standby"))
        assertFalse(StandbyKeep.isReturn(preparedReturn = false, pathKind = "relay"))
        val fallback = StandbyKeep.isReturn(true, "private")
        assertEquals(StandbyKeep.ClipboardStart.Seed("copied on B"), StandbyKeep.clipboardOnStart(fallback, "copied on B"))
        assertEquals(StandbyKeep.ClipboardStart.Hold, StandbyKeep.clipboardOnStart(fallback, null))
        assertEquals(StandbyKeep.ClipboardStart.Fresh,
            StandbyKeep.clipboardOnStart(StandbyKeep.isReturn(false, "private"), "copied on B"))
    }

    @Test
    fun punchesAreReadApartFromStrays() {
        // r3 D1: the 8th element is the host's Punches; a 7-element status reads none
        val l = StandbyKeep.Lease.of(longArrayOf(1, 2, 0, 0, 0, 0, 3, 25))
        assertEquals(3L, l.strays)
        assertEquals(25L, l.punches)
        assertFalse(l.tailActive)
        assertEquals(0L, StandbyKeep.Lease.of(longArrayOf(1, 2, 0, 0, 0, 1, 40)).punches)
    }

    @Test
    fun eachOwnerHasItsOwnKeyPrefix() {
        // r3 R2: owner 1's prefix never matches owner 10's keys
        val a = StandbyKeep.ownerKey(StandbyKeep.ownerPrefix(1), k("A"))
        val b = StandbyKeep.ownerKey(StandbyKeep.ownerPrefix(10), k("A"))
        assertTrue(a.startsWith(StandbyKeep.ownerPrefix(1)))
        assertFalse(b.startsWith(StandbyKeep.ownerPrefix(1)))
        assertTrue(a != b)
        assertEquals(StandbyKeep.nativeKey(k("A")), a.removePrefix(StandbyKeep.ownerPrefix(1)))
    }

    @Test
    fun activePlusStandbyAreAtMostThree() {
        // N4: three leases, then an ordinary / raw-IP session is committed: the least recent lease goes
        val leases = listOf(k("A"), k("B"), k("C"))
        assertEquals(listOf(k("C")), StandbyKeep.overLimit(leases, otherActive = 1, onScreen = null))
        assertEquals(emptyList<RecentHosts.Key>(), StandbyKeep.overLimit(leases, otherActive = 0, onScreen = null))
        // never the one on screen
        assertEquals(listOf(k("B")), StandbyKeep.overLimit(leases, otherActive = 1, onScreen = k("C")))
        assertFalse(StandbyKeep.mayStart(leases = 2, otherActive = 1))
        assertTrue(StandbyKeep.mayStart(leases = 2, otherActive = 0))
    }

    @Test
    fun backFromStandbyTheClipboardIsABaselineNotNews() {
        // N3: the phone's text becomes the new binding's baseline (not sent); unreadable -> held until a real change
        assertEquals(StandbyKeep.ClipboardStart.Seed("copied on B"), StandbyKeep.clipboardOnStart(true, "copied on B"))
        assertEquals(StandbyKeep.ClipboardStart.Hold, StandbyKeep.clipboardOnStart(true, null))
        assertEquals(StandbyKeep.ClipboardStart.Hold, StandbyKeep.clipboardOnStart(true, ""))
        // a cold connect: as before
        assertEquals(StandbyKeep.ClipboardStart.Fresh, StandbyKeep.clipboardOnStart(false, "anything"))
        // and nothing held lets a session_started push through, only a real change (SessionPark, unchanged)
        assertFalse(SessionPark.clipboardPushAllowed(true, "session_started"))
        assertTrue(SessionPark.clipboardPushAllowed(true, "clip_changed"))
    }

    @Test
    fun aLateCapabilityJobChangesNothingOfTheNewLease() {
        // N5: an old job's end after endAll / a new incarnation leaves the new one's back-off and staging alone
        val b = StandbyKeep.Book()
        val old = b.begin(k("A"))
        assertTrue(b.startStaging(k("A"), old, 0L))
        b.endAll()
        val fresh = b.begin(k("A"))
        assertTrue(b.startStaging(k("A"), fresh, 0L))
        b.endStaging(k("A"), old, ok = false, nowMs = 100L, random = 0.0)  // the late failure of the old job
        assertEquals(0, b.failuresOf(k("A")))
        assertEquals(k("A"), b.stagingKey())                              // the new one is still staging
        b.endStaging(k("A"), fresh, ok = false, nowMs = 200L, random = 0.0)
        assertEquals(1, b.failuresOf(k("A")))
    }

    // ---------------------------------------------------------------- the directory's side

    @Test
    fun standbySupportIsReadFromTheHostList() {
        val withIt = JSONObject().put("hosts", JSONArray())
            .put("standby", JSONObject().put("supported", true).put("relay", true))
        assertEquals(DirectoryClient.StandbySupport(true, true), DirectoryClient.StandbySupport.of(withIt))
        // an older directory: no object -> unsupported
        assertEquals(DirectoryClient.StandbySupport.NONE, DirectoryClient.StandbySupport.of(JSONObject().put("hosts", JSONArray())))
    }

    private fun connectAnswer(standbyRelay: String?, vararg candidates: Triple<String, Int, String>): JSONObject {
        val arr = JSONArray()
        for (c in candidates) arr.put(JSONObject().put("ip", c.first).put("port", c.second).put("kind", c.third))
        val o = JSONObject().put("hostPublicIp", "203.0.113.5").put("hostPublicUdpPort", 40000)
            .put("punchToken", "0123456789abcdef0123456789abcdef").put("candidates", arr)
        if (standbyRelay != null) o.put("standbyRelay", standbyRelay)
        return o
    }

    @Test
    fun aReservedRelayIsWhereTheLeaseAims() {
        val t = DirectoryClient.parseConnect(connectAnswer("reserved",
            Triple("192.168.0.10", 29180, "private"), Triple("203.0.113.5", 40000, "public"), Triple("198.51.100.7", 41000, "relay")))
        assertEquals("reserved", t.standbyRelay)
        assertEquals("198.51.100.7" to 41000, DirectoryClient.standbyAim(t))
    }

    @Test
    fun withoutARelayTheLeaseAimsAtTheHostLanFirst() {
        val t = DirectoryClient.parseConnect(connectAnswer("not-eligible",
            Triple("203.0.113.5", 40000, "public"), Triple("192.168.0.10", 29180, "private"), Triple("198.51.100.7", 41000, "relay")))
        assertEquals("192.168.0.10" to 29180, DirectoryClient.standbyAim(t))
        val pub = DirectoryClient.parseConnect(connectAnswer(null, Triple("203.0.113.5", 40000, "public")))
        assertEquals("", pub.standbyRelay)
        assertEquals("203.0.113.5" to 40000, DirectoryClient.standbyAim(pub))
        // only a relay offered, none reserved for this connect: nowhere (a relay is never aimed at unreserved)
        val relayOnly = DirectoryClient.parseConnect(connectAnswer("relay-disabled", Triple("198.51.100.7", 41000, "relay")))
        assertNull(DirectoryClient.standbyAim(relayOnly))
    }

    // N3 (codex_avd_guard_src_r1): the activation's failure that comes after its generation -- the same tap's ordinary
    // connect, once, only for an unknown entry
    @Test
    fun anActivationThatFailsAfterItsGenerationFallsBackOnlyForAnUnknownEntry() {
        val unknown = "standby activation: " + StandbyKeep.ACTIVATION_UNKNOWN_ENTRY
        assertTrue(StandbyKeep.fallbackAfterEstablishFailure("standby", establishTimedOut = false, lastError = unknown))
        // not an activation: an ordinary / raw / relay connect (and so the fallback itself) never falls back again
        assertFalse(StandbyKeep.fallbackAfterEstablishFailure("relay", false, unknown))
        assertFalse(StandbyKeep.fallbackAfterEstablishFailure("private", false, unknown))
        assertFalse(StandbyKeep.fallbackAfterEstablishFailure("direct", false, unknown))
        // the establish limit ran out: not this failure
        assertFalse(StandbyKeep.fallbackAfterEstablishFailure("standby", true, unknown))
        // superseded (a stale generation), cancelled, another device holding the PC, nothing said: no fallback
        assertFalse(StandbyKeep.fallbackAfterEstablishFailure("standby", false,
            "standby activation: activation refused (superseded)"))
        assertFalse(StandbyKeep.fallbackAfterEstablishFailure("standby", false, "standby activation cancelled"))
        assertFalse(StandbyKeep.fallbackAfterEstablishFailure("standby", false, "standby activation: host busy"))
        assertFalse(StandbyKeep.fallbackAfterEstablishFailure("standby", false, ""))
        assertFalse(StandbyKeep.fallbackAfterEstablishFailure("standby", false, "not_established"))
    }
}
