package com.remote60.androiddirect

/**
 * keepalive r1 (Codex .claude/codex_keepalive_r1.md): the PC list while one connection is parked.
 *
 * "PC 목록" from a directory-brokered session parks it instead of hanging up: the native session
 * releases whatever the PC still holds from this phone, pauses the picture and proves the pause with a
 * Ping fence (client_park_state.hpp), and stops taking input, clipboard and previews. Tapping the same
 * PC again within five minutes goes straight back to its target list on the same connection -- no
 * directory, observe, punch or Hello. Everything else ends the park with a real disconnect.
 *
 * The rules are here, pure, so they can be exercised without a device; MainActivity only applies them.
 *  - One parked connection at most, for at most [PARK_LIMIT_MS] from the moment the list opened. A hard
 *    deadline on the monotonic clock: refreshing the list, a Ping or a status poll never extends it.
 *  - The key is the account, the directory it was issued by, the host id and the native connection
 *    generation -- not a display name or an address. A reconnect is a new generation, so a park never
 *    adopts a connection it did not make.
 *  - Reuse only once the native side says Parked. While the park is still confirming, a tap waits (it
 *    is the same tap twice, not a second connection). A park that failed, expired, lost its peer or
 *    belongs to someone else is discarded and the tap goes the ordinary way -- once, as the user asked.
 */
object SessionPark {
    const val PARK_LIMIT_MS = 5L * 60L * 1000L

    /** Native confirmation budget plus the poll that observes it (the native side gives up at 2 s). */
    const val CONFIRM_BUDGET_MS = 2_500L

    /** ClientParkPhase, as nativeParkPhase() reports it. */
    enum class NativePhase(val code: Int) {
        NONE(0), RELEASING(1), PAUSING(2), FENCING(3), PARKED(4), FAILED(5), EXPIRED(6);

        val confirming: Boolean get() = this == RELEASING || this == PAUSING || this == FENCING

        companion object {
            fun of(code: Int): NativePhase = values().firstOrNull { it.code == code } ?: FAILED
        }
    }

    data class Parked(
        val hostId: String,
        val hostName: String,
        val accountId: String,
        val directoryOrigin: String,
        val connectionGeneration: Long,
        /** SystemClock.elapsedRealtime() when the list opened. */
        val startedAtMs: Long,
        /** Which park this is, for callbacks that outlive it. */
        val episode: Long,
    ) {
        val deadlineMs: Long get() = startedAtMs + PARK_LIMIT_MS
        fun remainingMs(nowMs: Long): Long = (deadlineMs - nowMs).coerceAtLeast(0L)
    }

    /** What the native session says about itself right now. */
    data class NativeState(
        val connected: Boolean,
        val connectionGeneration: Long,
        val phase: NativePhase,
    )

    sealed class Tap {
        /** Back to this connection's targets now. */
        object Reuse : Tap()
        /** The park is still confirming: the tap does nothing yet. */
        object Wait : Tap()
        /** End the park (a real disconnect), then connect the ordinary way for this tap. */
        data class Discard(val reason: String) : Tap()
    }

    /** Whether a connection may be parked at all: directory-brokered, connected, signed in. */
    fun canPark(
        directoryHostId: String,
        manualConnect: Boolean,
        accountId: String,
        sessionToken: String,
        nativeConnected: Boolean,
    ): Boolean =
        directoryHostId.isNotEmpty() && !manualConnect && accountId.isNotEmpty() &&
            sessionToken.isNotEmpty() && nativeConnected

    fun onHostTap(
        parked: Parked,
        tappedHostId: String,
        accountId: String,
        directoryOrigin: String,
        nowMs: Long,
        native: NativeState,
    ): Tap {
        if (accountId != parked.accountId || directoryOrigin != parked.directoryOrigin) {
            return Tap.Discard("identity_changed")
        }
        if (tappedHostId != parked.hostId) return Tap.Discard("other_host")
        if (nowMs >= parked.deadlineMs) return Tap.Discard("expired")
        if (!native.connected) return Tap.Discard("peer_lost")
        if (native.connectionGeneration != parked.connectionGeneration) return Tap.Discard("generation_changed")
        return when {
            native.phase == NativePhase.PARKED -> Tap.Reuse
            native.phase.confirming && nowMs - parked.startedAtMs < CONFIRM_BUDGET_MS -> Tap.Wait
            native.phase.confirming -> Tap.Discard("confirm_timeout")
            else -> Tap.Discard("park_" + native.phase.name.lowercase())
        }
    }

    /** On every status tick while parked: null = keep it, otherwise why it ends (a disconnect follows). */
    fun endReason(parked: Parked, nowMs: Long, native: NativeState): String? {
        if (!native.connected) return "peer_lost"
        if (native.connectionGeneration != parked.connectionGeneration) return "generation_changed"
        if (nowMs >= parked.deadlineMs) return "expired"
        return when {
            native.phase == NativePhase.PARKED -> null
            native.phase.confirming && nowMs - parked.startedAtMs < CONFIRM_BUDGET_MS -> null
            native.phase.confirming -> "confirm_timeout"
            native.phase == NativePhase.FAILED -> "confirm_failed"
            native.phase == NativePhase.EXPIRED -> "native_expired"
            else -> "park_lost"
        }
    }

    /** "4:59" for the list banner. */
    fun formatRemaining(ms: Long): String {
        val totalSec = (ms + 999L) / 1000L
        return "%d:%02d".format(totalSec / 60L, totalSec % 60L)
    }
}
