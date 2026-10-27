package com.remote60.androiddirect

/**
 * keepalive-r2 ② e -- kept model A (Codex codex_keepalive_r2_kept_model.md): a PC the phone keeps is a standby LEASE
 * (StandbyRunner, native), not a parked control session. The lease has its own UDP socket, an entry at the host and
 * its Pings -- no control link, no video, no decoder, no input. A tap on a ready PC activates THAT lease: a new session
 * entry connected with the lease's activation (nativeConnectStandby) -- no directory, observe, punch or ordinary Hello.
 * "PC 목록" ends that session; the lease goes on by Ping on the same socket.
 *
 * The rules, pure (the screen and the native runner apply them):
 *  - [shown]: what the list says of a PC -- only a confirmed entry on a live lease is READY ("준비됨"); idle, waiting
 *    for a capability, unanswered or handed off never reads as ready.
 *  - [onTap]: ready -> activate; a lease still handed off (its session is ending) -> the tap waits; anything else -> the
 *    ordinary connect (a lease still preparing is let go first: one thing per PC).
 *  - [onListReturn]: from an activation -> back to standby; an ordinary directory session of a supported directory ->
 *    the FIRST transition (a new lease: its directory requests are real and logged as such); raw IP, an older host, a
 *    pre-login worker, an unknown support -> "상시 유지 미지원" and the session ends.
 *  - [Book]: the leases of this sign-in, each with an incarnation -- a late capability (a directory answer that comes
 *    back after a Release, a sign-out or a new lease under the same key) is dropped by it, never provided to another.
 *  - At most [RecentHosts.MAX] PCs (active and standby together): the leases are the recent PCs; one that drops out of
 *    the recent list (only a user's successful use reorders it) is released. A failed fourth evicts nothing.
 */
object StandbyKeep {
    /** StandbyEntryClient::State, as nativeStandbyStatus reports it. */
    enum class LeaseState(val code: Int) {
        IDLE(0), ADMITTING(1), PARKED(2), ACTIVATING(3), ACTIVE(4), UNSUPPORTED(5), WAITING(6);

        companion object {
            fun of(code: Long): LeaseState = values().firstOrNull { it.code.toLong() == code } ?: IDLE
        }
    }

    /**
     * nativeStandbyStatus: [exists, state, lost, handedOff, needsCapability, tailActive, strays, punches]. r2 D1 (Codex
     * codex_ka_e_src_r1.md): [tailActive] = datagrams from the target that are no standby traffic, HelloAck or Punch
     * still reach the lease (observed -- not a proof the host's grant ended, not a count of video), [strays] = how many
     * so far. r3 (codex_ka_e_src_r2.md D1): [punches] = the host's Punches, counted apart (r2 counted them as strays).
     */
    data class Lease(
        val exists: Boolean,
        val state: LeaseState,
        val lost: Long,
        val handedOff: Boolean,
        val needsCapability: Boolean,
        val tailActive: Boolean = false,
        val strays: Long = 0L,
        val punches: Long = 0L,
    ) {
        companion object {
            val NONE = Lease(false, LeaseState.IDLE, 0L, false, false)
            fun of(v: LongArray?): Lease {
                if (v == null || v.size < 5 || v[0] == 0L) return NONE
                return Lease(true, LeaseState.of(v[1]), v[2], v[3] != 0L, v[4] != 0L,
                    tailActive = v.size > 5 && v[5] != 0L, strays = if (v.size > 6) v[6] else 0L,
                    punches = if (v.size > 7) v[7] else 0L)
            }
        }
    }

    /**
     * r2 D1: what the list may say -- three facts kept apart: (1) the local hand-back of the socket (RETURNING while it
     * is not done), (2) the entry alive on its lease (READY / SETTLING), (3) the host's old grant and picture cleaned up
     * -- which the existing wire does not confirm: READY never claims it, and SETTLING says the old session's datagrams
     * are still arriving (observed). A tap on either is the user's explicit activation (a new generation: rollover).
     */
    enum class Shown { PREPARING, READY, SETTLING, ACTIVATING, ACTIVE, RETURNING, UNSUPPORTED, RETRY }

    /** What the list shows for a PC with this lease. [onScreen] = its activated session is the one on screen. null = no lease. */
    fun shown(lease: Lease, onScreen: Boolean): Shown? = when {
        !lease.exists -> null
        lease.state == LeaseState.UNSUPPORTED -> Shown.UNSUPPORTED
        lease.handedOff && onScreen -> if (lease.state == LeaseState.ACTIVE) Shown.ACTIVE else Shown.ACTIVATING
        lease.handedOff -> Shown.RETURNING  // its session is ending (or never came up): not ready until the lease reads again
        lease.state == LeaseState.PARKED && lease.tailActive -> Shown.SETTLING
        lease.state == LeaseState.PARKED -> Shown.READY
        lease.state == LeaseState.WAITING -> Shown.RETRY
        else -> Shown.PREPARING
    }

    sealed class Tap {
        /** The lease is ready: a new session by its activation. */
        object Activate : Tap()
        /** Its last session is still handing the socket back: the tap is held until it settles. */
        object Wait : Tap()
        /** The ordinary connect; [releaseFirst] = a lease that is not ready is let go first. */
        data class Cold(val releaseFirst: Boolean) : Tap()
    }

    fun onTap(lease: Lease?): Tap = when {
        lease == null || !lease.exists -> Tap.Cold(false)
        lease.handedOff -> Tap.Wait
        lease.state == LeaseState.PARKED -> Tap.Activate
        else -> Tap.Cold(true)
    }

    enum class ListReturn { BACK_TO_STANDBY, FIRST_TRANSITION, UNSUPPORTED_END }

    /**
     * "PC 목록" with a PC on screen. [fromStandby] = its session came from a lease's activation; [directory] = a
     * directory-brokered connection (not raw IP); [supported] = the directory said standby (read-only /api/hosts);
     * [prelogin] = the PC is at its logon screen (a pre-login worker); [knownUnsupported] = known for certain not to
     * stand by (the app passes false: a lease whose Hellos went unanswered may have met an older host OR a path that
     * failed for now -- Codex: an unconfirmed / passing failure is never taken for an older version, so "PC 목록" tries
     * again); [enabled] = model A is on in this build.
     */
    fun onListReturn(
        fromStandby: Boolean,
        directory: Boolean,
        supported: Boolean,
        prelogin: Boolean,
        knownUnsupported: Boolean,
        enabled: Boolean,
    ): ListReturn = when {
        !enabled || !directory || prelogin || knownUnsupported -> ListReturn.UNSUPPORTED_END
        fromStandby -> ListReturn.BACK_TO_STANDBY
        supported -> ListReturn.FIRST_TRANSITION
        else -> ListReturn.UNSUPPORTED_END
    }

    /**
     * r2 N4: active + standby together are at most [max]. [leasesByRecency] = the leases, most recent first (leases of PCs
     * no longer recent last); [otherActive] = sessions on screen / committed that are NOT a lease's (an ordinary or a raw-IP
     * connection: 0 or 1). The excess goes from the least recent lease -- never the one on screen. Called once a connection
     * is committed (a success): a fourth that fails evicts nothing.
     */
    fun overLimit(leasesByRecency: List<RecentHosts.Key>, otherActive: Int, onScreen: RecentHosts.Key?,
                  max: Int = RecentHosts.MAX): List<RecentHosts.Key> {
        val allowed = (max - otherActive).coerceAtLeast(0)
        if (leasesByRecency.size <= allowed) return emptyList()
        val out = ArrayList<RecentHosts.Key>()
        var excess = leasesByRecency.size - allowed
        for (k in leasesByRecency.asReversed()) {
            if (excess == 0) break
            if (k == onScreen) continue
            out.add(k)
            --excess
        }
        return out
    }

    /** r2 N4: whether one more lease may start now (the same count). */
    fun mayStart(leases: Int, otherActive: Int, max: Int = RecentHosts.MAX): Boolean = leases + otherActive < max

    /**
     * r2 N3 (clipboard r3 contract, kept): what a NEW session of a PC starts with. Back from standby (an activation):
     * what the phone holds now is not news to that PC -- it becomes the new binding's baseline and is NOT sent; if it
     * cannot be read, nothing is pushed until the clipboard really changes. An ordinary (cold) connect: as before.
     */
    sealed class ClipboardStart {
        data class Seed(val text: String) : ClipboardStart()
        object Hold : ClipboardStart()
        object Fresh : ClipboardStart()
    }

    fun clipboardOnStart(fromStandby: Boolean, phoneText: String?): ClipboardStart = when {
        !fromStandby -> ClipboardStart.Fresh
        phoneText.isNullOrEmpty() -> ClipboardStart.Hold
        else -> ClipboardStart.Seed(phoneText)
    }

    /** The leases that go: the ones whose PC is not among the recent ones any more -- never the one on screen. */
    fun evictions(recent: List<RecentHosts.Key>, leases: Collection<RecentHosts.Key>, onScreen: RecentHosts.Key?): List<RecentHosts.Key> =
        leases.filter { it !in recent.take(RecentHosts.MAX) && it != onScreen }

    /** How long the app's own preparation leaves a PC whose lease went unanswered (a user's PC 목록 never waits). */
    const val UNANSWERED_HOLD_MS = 10L * 60L * 1000L

    /** The native runner's key of a PC. */
    fun nativeKey(key: RecentHosts.Key): String = SessionPark.entryKey(key.directoryOrigin, key.accountId, key.hostId)

    /**
     * r3 (Codex codex_ka_e_src_r2.md R2): the native runner is one per process, an app screen (Activity) is not. Each
     * screen is an owner with its own key prefix ("o<n>|", n never reused in this process); its leases are keyed under
     * it and its cleanup releases that prefix only -- a late cleanup of an old screen never takes the new screen's leases.
     */
    fun ownerPrefix(n: Long): String = "o$n|"
    fun ownerKey(ownerPrefix: String, key: RecentHosts.Key): String = ownerPrefix + nativeKey(key)

    /**
     * The leases of this sign-in and their incarnations (one counter for all: a number is never reused). A capability
     * job carries the (key, incarnation) it was started for and provides only if that is still current.
     */
    class Book {
        private var counter = 0L
        private val current = LinkedHashMap<RecentHosts.Key, Long>()
        private val unsupported = HashMap<RecentHosts.Key, Long>()  // until when the app's own preparation holds off
        private val failures = HashMap<RecentHosts.Key, Int>()
        private val notBefore = HashMap<RecentHosts.Key, Long>()
        private var staging: Pair<RecentHosts.Key, Long>? = null

        /** A lease for [key] starts (or starts over): its new incarnation. */
        @Synchronized fun begin(key: RecentHosts.Key): Long {
            val inc = ++counter
            current[key] = inc
            return inc
        }
        /** The lease of [key] goes: every job of it is stale from now. */
        @Synchronized fun end(key: RecentHosts.Key) {
            current.remove(key)
            if (staging?.first == key) staging = null
        }
        /** Sign-out, an account / directory change, leaving the app: all go (returned so the caller releases them). */
        @Synchronized fun endAll(): List<RecentHosts.Key> {
            val all = current.keys.toList()
            current.clear()
            unsupported.clear()
            failures.clear()
            notBefore.clear()
            staging = null
            return all
        }
        @Synchronized fun has(key: RecentHosts.Key): Boolean = key in current
        @Synchronized fun incarnation(key: RecentHosts.Key): Long = current[key] ?: 0L
        @Synchronized fun isCurrent(key: RecentHosts.Key, inc: Long): Boolean = inc != 0L && current[key] == inc
        /**
         * r2 (AVD ke_r18): [block] runs only while [key]@[inc] is current, and no [end] / [endAll] can come between the
         * check and it (both take this lock). A capability job checked, the user's 끊기 ended the lease, and the job's
         * Provide still reached the native lease (its Release queued behind the job): now 끊기 is either before the
         * check (nothing provided) or after the Provide (which it then releases). [block] must be short (no network).
         */
        @Synchronized fun <T> ifCurrent(key: RecentHosts.Key, inc: Long, block: () -> T): T? =
            if (inc != 0L && current[key] == inc) block() else null
        @Synchronized fun keys(): List<RecentHosts.Key> = current.keys.toList()

        /**
         * A lease's Hellos went unanswered (an older host -- or a path that failed for now: not told apart). Only the
         * app's OWN preparation holds off for [UNANSWERED_HOLD_MS]; a user's "PC 목록" tries again.
         */
        @Synchronized fun markUnsupported(key: RecentHosts.Key, nowMs: Long) { unsupported[key] = nowMs + UNANSWERED_HOLD_MS }
        @Synchronized fun isUnsupported(key: RecentHosts.Key, nowMs: Long): Boolean = nowMs < (unsupported[key] ?: 0L)

        /**
         * Staging <= 1: one capability job at a time (observe + standby connect). true = [key]@[inc] may start one now
         * (current, not backing off, nothing else staging).
         */
        @Synchronized fun startStaging(key: RecentHosts.Key, inc: Long, nowMs: Long): Boolean {
            if (staging != null || !isCurrent(key, inc)) return false
            if (nowMs < (notBefore[key] ?: 0L)) return false
            staging = key to inc
            return true
        }
        /**
         * The job of [key]@[inc] ended: ok resets its back-off; a failure waits longer (1, 2, 4, 8, 16, 30 s + jitter).
         * r2 N5: a late end of a job whose lease is gone / replaced changes nothing of the current one.
         */
        @Synchronized fun endStaging(key: RecentHosts.Key, inc: Long, ok: Boolean, nowMs: Long, random: Double) {
            if (staging == key to inc) staging = null
            if (!isCurrent(key, inc)) return
            if (ok) {
                failures.remove(key)
                notBefore.remove(key)
                return
            }
            val f = (failures[key] ?: 0) + 1
            failures[key] = f
            notBefore[key] = nowMs + Preconnect.backoffMs(f, random)
        }
        @Synchronized fun stagingKey(): RecentHosts.Key? = staging?.first
        @Synchronized fun failuresOf(key: RecentHosts.Key): Int = failures[key] ?: 0
    }
}
