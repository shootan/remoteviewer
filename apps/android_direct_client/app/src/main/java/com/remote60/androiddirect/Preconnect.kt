package com.remote60.androiddirect

/**
 * keepalive r2: which recent PC to prepare next, and when -- the client half of "keep the three most recent PCs
 * connected" (Codex codex_keepalive_r2.md sections 1 and 6). Pure: the caller supplies the clock and the random.
 *
 *  - One automatic connect at a time; a manual tap always comes first (an automatic one in flight gives way to it).
 *  - Each PC has its own back-off after a failure: 1, 2, 4, 8, 16, then every 30 s, each with up to +20 % jitter so
 *    several phones do not retry in step. A success resets it.
 *  - Never a PC that is already kept (or connecting), never one the user disconnected ([RecentHosts.Suppression]),
 *    never more than the recent list holds. An offline PC is skipped; the other two are not affected by it.
 *
 * [AUTO_PREPARE] (keepalive-r2 ② e, Codex codex_ka_e_switch_decision.md): whether the app STARTS a standby for a
 * recent PC on its own -- at start / return, a list refresh, an online event, the planner's retry. OFF in every
 * shipped build (BuildConfig.STANDBY_AUTO_PREPARE; only a debug test build turns it on) until the independent check.
 * It is not a transport switch: a user's own "PC 목록" from a supported PC goes to standby (kept model A), and a
 * lease that exists keeps its Pings either way. OFF never prepares a recent PC behind the user's back.
 */
object Preconnect {
    @JvmField val AUTO_PREPARE: Boolean = BuildConfig.STANDBY_AUTO_PREPARE

    val BACKOFF_STEPS_MS = longArrayOf(1_000L, 2_000L, 4_000L, 8_000L, 16_000L, 30_000L)
    const val JITTER = 0.2

    /** The wait after [failures] consecutive failures (>= 1), with jitter from [random] in [0, 1). */
    fun backoffMs(failures: Int, random: Double): Long {
        if (failures <= 0) return 0L
        val base = BACKOFF_STEPS_MS[minOf(failures, BACKOFF_STEPS_MS.size) - 1]
        return base + (base * JITTER * random.coerceIn(0.0, 1.0)).toLong()
    }

    data class Candidate(val key: RecentHosts.Key, val online: Boolean)

    class Planner(private val random: () -> Double) {
        private class Backoff(var failures: Int = 0, var notBeforeMs: Long = 0L)
        private val backoff = HashMap<RecentHosts.Key, Backoff>()
        var inFlight: RecentHosts.Key? = null
            private set

        /**
         * The next PC to prepare now, or null. [candidates] in recency order (the recent list); [kept] = PCs this phone
         * already holds or is connecting to; [manualPending] = a user's tap is being handled.
         */
        fun next(
            nowMs: Long,
            candidates: List<Candidate>,
            kept: Set<RecentHosts.Key>,
            suppression: RecentHosts.Suppression,
            manualPending: Boolean,
        ): RecentHosts.Key? {
            if (manualPending || inFlight != null) return null
            for (c in candidates.take(RecentHosts.MAX)) {
                if (!c.online || c.key in kept || suppression.isSuppressed(c.key)) continue
                val b = backoff[c.key]
                if (b != null && nowMs < b.notBeforeMs) continue
                inFlight = c.key
                return c.key
            }
            return null
        }

        /** The automatic connect to [key] ended: up (reset its back-off) or not (wait longer before the next try). */
        fun onResult(key: RecentHosts.Key, success: Boolean, nowMs: Long) {
            if (inFlight == key) inFlight = null
            if (success) {
                backoff.remove(key)
                return
            }
            val b = backoff.getOrPut(key) { Backoff() }
            b.failures += 1
            b.notBeforeMs = nowMs + backoffMs(b.failures, random())
        }

        /** A manual tap: the automatic connect in flight (if any) gives way -- returned so the caller cancels it. */
        fun onManualTap(): RecentHosts.Key? {
            val giving = inFlight
            inFlight = null
            return giving
        }

        /** Sign-out / account change: nothing carries over. */
        fun reset() {
            backoff.clear()
            inFlight = null
        }

        fun failuresOf(key: RecentHosts.Key): Int = backoff[key]?.failures ?: 0
        fun notBeforeOf(key: RecentHosts.Key): Long = backoff[key]?.notBeforeMs ?: 0L
    }
}
