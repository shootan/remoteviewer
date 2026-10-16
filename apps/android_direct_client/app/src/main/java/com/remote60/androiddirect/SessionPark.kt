package com.remote60.androiddirect

/**
 * keepalive r1 -> keepalive-multi r1 (Codex .claude/codex_keepalive_multi_r1.md): the PCs the phone keeps.
 *
 * "PC 목록" from a directory-brokered session parks it instead of hanging up: the native session releases
 * whatever the PC still holds from this phone, pauses the picture and proves the pause with a Ping fence
 * (client_park_state.hpp), and stops taking input, clipboard and previews. Several PCs can be kept this way at
 * once -- A -> list -> B -> list -> A goes back to A's target list on A's own connection (no directory, observe,
 * punch or Hello), and B stays kept meanwhile.
 *
 * The native session table (session_table.hpp, via NativeSessionBridge) owns the connections and decides what may
 * happen; the rules the screen applies are here, pure, so they can be exercised without a device:
 *  - At most [Policy.maxHosts] PCs (the one on screen included). A new PC beyond that closes the parked PC used
 *    longest ago -- the native table picks it, atomically with the reservation.
 *  - Each PC's park has its own limit ([Policy.parkTtlMs]) from the moment it was left; nothing extends it, only
 *    showing that PC again and leaving it again starts a new one. Its own session's control loop enforces it.
 *  - The key of a kept PC is the directory it was issued by, the account and the host id -- not a name or an
 *    address. A new connection to the same PC is a new native handle.
 *  - Reuse only once the native side says Parked. While any PC's park is still confirming, a tap is held (the
 *    latest one) and carried out when it settles: one PC on screen at a time, and only after the previous one's
 *    pause is fenced.
 *  - One PC ending (its disconnect, peer lost, refused, TTL, LRU, a failed pause) leaves the others. Sign-out, an
 *    account / directory change, the network changing and leaving the app end the kept ones. A PC missing from one
 *    list refresh ends nothing.
 */
object SessionPark {
    const val PARK_LIMIT_MS = 5L * 60L * 1000L

    /** Native confirmation budget plus the poll that observes it (the native side gives up at 2 s). */
    const val CONFIRM_BUDGET_MS = 2_500L

    /**
     * The policy object (the user's answer pending, these are the proposed values): how many PCs are kept, the
     * one on screen included, and how long each one's park lasts. Handed to the native table as is.
     */
    data class Policy(val maxHosts: Int = 3, val parkTtlMs: Long = PARK_LIMIT_MS)

    /** ClientParkPhase, as the native side reports it. */
    enum class NativePhase(val code: Int) {
        NONE(0), RELEASING(1), PAUSING(2), FENCING(3), PARKED(4), FAILED(5), EXPIRED(6);

        val confirming: Boolean get() = this == RELEASING || this == PAUSING || this == FENCING

        companion object {
            fun of(code: Int): NativePhase = values().firstOrNull { it.code == code } ?: FAILED
        }
    }

    /** The table key of a PC: directory origin, account, host id. */
    fun entryKey(directoryOrigin: String, accountId: String, hostId: String): String =
        "$directoryOrigin|$accountId|$hostId"

    /** One kept PC as the native table reports it (nativeEntriesJson). */
    data class NativeEntry(
        val handle: Long,
        val key: String,
        /** "connecting", "active", "parked", "closing". */
        val phase: String,
        val parkPhase: NativePhase,
        val alive: Boolean,
        val generation: Long,
        val remainingMs: Long,
        val parkEpisode: Long,
    )

    /** What the screen knows of a PC it keeps, beside the native entry (names for the list and the log). */
    data class Kept(
        val handle: Long,
        val hostId: String,
        val hostName: String,
        val accountId: String,
        val directoryOrigin: String,
        val manual: Boolean,
        /** SystemClock.elapsedRealtime() when it was last left (0 = on screen / never parked yet). */
        val parkedAtMs: Long = 0L,
        val parkEpisode: Long = 0L,
        val confirmLogged: Boolean = false,
    ) {
        val key: String get() = entryKey(directoryOrigin, accountId, hostId)
    }

    sealed class Tap {
        /** Back to this PC's targets on its live connection. */
        data class Reuse(val handle: Long) : Tap()
        /** A park is still confirming: the tap is held (the latest one) until it settles. */
        object Wait : Tap()
        /** No kept connection for this PC: the ordinary connect. */
        object Connect : Tap()
        /** This PC's kept connection cannot be used: it is ended, then the ordinary connect -- once, for this tap. */
        data class Discard(val handle: Long, val reason: String) : Tap()
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

    /**
     * A tap on a PC in the list. [kept] / [native] are that PC's (null = not kept); [anyConfirming] = some PC's
     * park is still being confirmed (the tap waits for it, whichever PC it is for).
     */
    fun onHostTap(
        kept: Kept?,
        native: NativeEntry?,
        accountId: String,
        directoryOrigin: String,
        anyConfirming: Boolean,
    ): Tap {
        if (anyConfirming) return Tap.Wait
        if (kept == null || native == null) return Tap.Connect
        if (accountId != kept.accountId || directoryOrigin != kept.directoryOrigin) {
            return Tap.Discard(kept.handle, "identity_changed")
        }
        if (!native.alive) return Tap.Discard(kept.handle, "peer_lost")
        return when {
            native.phase == "parked" && native.parkPhase == NativePhase.PARKED -> Tap.Reuse(kept.handle)
            native.parkPhase.confirming -> Tap.Wait
            else -> Tap.Discard(kept.handle, "park_" + native.parkPhase.name.lowercase())
        }
    }

    /** Who is signed in now, as the screen knows it (r2 K2). */
    data class Identity(val accountId: String, val directoryOrigin: String, val sessionPresent: Boolean)

    /**
     * On every status tick, per kept PC: null = keep it, otherwise why it ends. The native side ends a PC whose park
     * expired, failed or lost its peer on its own (and the table reports it); what only the screen knows is who is
     * signed in -- r2 K2: a refused or replaced sign-in ends it as soon as the screen knows.
     */
    fun endReason(kept: Kept, identity: Identity): String? =
        if (!identity.sessionPresent || identity.accountId != kept.accountId ||
            identity.directoryOrigin != kept.directoryOrigin) "identity_changed" else null

    /** What the list shows for a PC. [onScreen] = the PC the viewer / targets belong to right now. */
    enum class ListState { NONE, ON_SCREEN, CONFIRMING, KEPT, CONNECTING }

    fun listState(native: NativeEntry?, onScreen: Boolean): ListState = when {
        native == null || native.phase == "closing" -> ListState.NONE
        onScreen || native.phase == "active" -> ListState.ON_SCREEN
        native.phase == "connecting" -> ListState.CONNECTING
        native.parkPhase.confirming -> ListState.CONFIRMING
        native.phase == "parked" && native.parkPhase == NativePhase.PARKED && native.alive -> ListState.KEPT
        else -> ListState.NONE
    }

    /** How an ended PC's reason is named in the list's status line (the reason the native table gave). */
    fun endedReasonText(reason: String): String = when (reason) {
        "ttl" -> "시간 만료"
        "lru" -> "유지 한도 초과"
        "peer_lost" -> "연결 끊김"
        "pause_failed", "resume_refused" -> "유지 실패"
        "pause_unsupported" -> "PC 업데이트 필요"
        "never_shown" -> "사용 안 함"
        else -> reason
    }

    /**
     * r4 S2-b: the intents that are scheduled and not yet carried out (a held tap). One per Activity, driven by its
     * lifecycle. A stop ends every one of them for good: being in front again later does not bring back what the
     * user left. keepalive-multi r1: the owner is the lifecycle epoch of the native table (a sign-out, a network
     * change or leaving the app moves it), not one connect operation -- connects are per PC now.
     */
    data class TapFallback(
        val epoch: Long, val account: String, val origin: String, val hostId: String, val lifeEpoch: Long,
    )

    class ConnectIntents(private val epoch: () -> Long) {
        private var lifeEpoch = 0L
        private var foreground = false

        @Synchronized fun onStart() { foreground = true }
        @Synchronized fun onStop() {
            foreground = false
            ++lifeEpoch
        }
        @Synchronized fun scheduleFallback(account: String, origin: String, hostId: String): TapFallback =
            TapFallback(epoch(), account, origin, hostId, lifeEpoch)

        /** At run time: nothing moved -- the epoch, the account, the directory, the app's lifetime -- and in front. */
        @Synchronized fun fallbackRuns(f: TapFallback, account: String, origin: String, hostListed: Boolean): Boolean =
            f.epoch == epoch() && f.lifeEpoch == lifeEpoch && foreground &&
                f.account == account && f.origin == origin && hostListed
    }

    /**
     * r2 K2 / r3 R2-a / r4 S2-a, per PC (keepalive-multi r1): the connect operation that is current for each kept
     * PC's handle. Numbers come from [next] -- in the app the native gate of that PC (nativeConnectOpNext(handle)),
     * which numbers operations for the whole process; cancelling B's connect never touches A's. The late cleanup of
     * a stale operation is the native compare-and-close (nativeCloseIfOwn), not a check here.
     */
    class EntryOps(private val next: (Long) -> Long) {
        private val current = HashMap<Long, Long>()

        @Synchronized fun begin(handle: Long): Long {
            val op = next(handle)
            current[handle] = op
            return op
        }
        @Synchronized fun invalidate(handle: Long) {
            if (current.containsKey(handle)) current[handle] = next(handle)
        }
        @Synchronized fun isCurrent(handle: Long, op: Long): Boolean = op != 0L && current[handle] == op
        @Synchronized fun drop(handle: Long) { current.remove(handle) }
        @Synchronized fun handles(): List<Long> = current.keys.toList()
    }

    /**
     * r2 §7 clipboard: after a resume whose baseline could not be read (no text, no access), nothing the
     * phone held during the park may be pushed by a focus / session-start read -- only a real change
     * (the clipboard listener) lifts the hold.
     */
    fun clipboardPushAllowed(resumeHold: Boolean, reason: String): Boolean = !resumeHold || reason == "clip_changed"

    /** "5분" / "4분" ... "59초": a kept PC's card, which changes once a minute until the last one. */
    fun formatRemainingCoarse(ms: Long): String =
        if (ms >= 60_000L) "${(ms + 59_999L) / 60_000L}분" else "${(ms.coerceAtLeast(0L) + 999L) / 1000L}초"

    /** "4:59" for the list. */
    fun formatRemaining(ms: Long): String {
        val totalSec = (ms + 999L) / 1000L
        return "%d:%02d".format(totalSec / 60L, totalSec % 60L)
    }
}
