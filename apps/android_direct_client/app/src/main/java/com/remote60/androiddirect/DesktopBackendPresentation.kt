package com.remote60.androiddirect

/**
 * ua2-apk (Codex codex_rel_0163_r1.md §1; r2: codex_ua2_apk_src_r1.md N1-N3): what the settings screen says about the
 * desktop capture backend of THIS connection.
 *
 * Three different things are kept apart:
 *  - the host's CAPABILITY: unknown until its first pong (CHECKING), supported (it answers ControlDesktopBackendStatus),
 *    or unsupported (an older host: a change still goes out on the old wire, but nothing is confirmed, nothing is
 *    lit and no session scope is promised);
 *  - THE CURRENT REQUEST: matched to the host's answers by the wire seq it went out with (native intent) -- an answer
 *    older than it says nothing about it, NotYetApplied means still pending, and a handled answer is terminal: applied,
 *    kept for the desktop (a window is being captured: WGC now, this method when the desktop is shown), or refused
 *    with the host's reason (the logon screen is DXGI only). Never inferred from a code merely being equal;
 *  - WHAT CAPTURES NOW: a window target is WGC; the desktop's backend.active is shown only for a desktop target.
 * The lit button is this connection's desktop method as the host HANDLED it (status.requested), and only then.
 *
 * Pure (no Android types): MainActivity feeds it the native snapshot; DesktopBackendPresentationTest drives it.
 */
object DesktopBackendPresentation {
    const val DXGI = 1
    const val WGC = 2
    const val GDI = 3

    // ControlDesktopBackendStatus flags (poc_protocol.hpp kDesktopBackendStatusFlag*)
    const val FLAG_SESSION_SCOPED = 0x1
    const val FLAG_DXGI_QUARANTINED = 0x2
    const val FLAG_WINDOW_TARGET = 0x4
    const val FLAG_BEFORE_LOGIN = 0x8
    const val FLAG_NOT_YET_APPLIED = 0x10

    /** The native snapshot (NativeSessionBridge.nativeGetDesktopBackendStatus). */
    data class HostStatus(
        val valid: Boolean,
        val seq: Long,
        val requested: Int,
        val effective: Int,
        val active: Int,
        val flags: Int,
        val changeCount: Long,
        val hostAnswers: Boolean,
        /** The host's first pong has come (its capability is known: answers or not). */
        val roleKnown: Boolean,
        /** Native intent: the latest request on this connection, the one sent last, and that send's wire seq. */
        val intentAsked: Long,
        val intentSent: Long,
        val intentSentSeq: Long,
    ) {
        companion object {
            /** [valid, seq, requested, effective, active, flags, changeCount, hostAnswers, roleKnown, intentAsked,
             *  intentSent, intentSentSeq]; null/short = none. */
            fun of(raw: LongArray?): HostStatus? {
                if (raw == null || raw.size < 12) return null
                return HostStatus(raw[0] != 0L, raw[1], raw[2].toInt(), raw[3].toInt(), raw[4].toInt(), raw[5].toInt(),
                    raw[6], raw[7] != 0L, raw[8] != 0L, raw[9], raw[10], raw[11])
            }
        }
    }

    enum class State {
        NOT_CONNECTED,
        /** The host's capability or its first answer is not known yet. */
        CHECKING,
        /** An older host: requests go out, nothing is confirmed, no session scope promised. */
        NO_ANSWER_HOST,
        /** Supported, no request of this connection: the host's own. */
        IDLE,
        /** The current request is not answered yet (not sent, an older answer, or NotYetApplied). */
        APPLYING,
        APPLIED,
        /** Handled, but a window is being captured (WGC): the method applies when the desktop is shown. */
        STORED_FOR_DESKTOP,
        /** Handled and not taken (the host's reason). Terminal: another pick is possible. */
        REFUSED,
    }

    enum class Reason { WINDOW_TARGET, DXGI_QUARANTINED, BEFORE_LOGIN }

    data class View(
        val state: State,
        /** The lit button: this connection's desktop method as the host handled it; null = none confirmed. */
        val lit: Int?,
        val enabled: Boolean,
        val asked: Int?,
        /** What the host's answer says (0 when there is none). */
        val requested: Int,
        val active: Int,
        val windowTarget: Boolean,
        /** The host says the choice ends with this session (supported hosts only). */
        val sessionScoped: Boolean,
        val reasons: List<Reason>,
    )

    /**
     * @param connected a session is up (not parked away, not connecting)
     * @param asked what this connection asked for last (null: nothing -- the host's own)
     * @param status the native snapshot (null: none)
     */
    fun present(connected: Boolean, asked: Int?, status: HostStatus?): View {
        fun view(state: State, lit: Int? = null, s: HostStatus? = null, reasons: List<Reason> = emptyList()) = View(
            state, lit, connected, asked, s?.requested ?: 0, s?.active ?: 0,
            s != null && s.flags and FLAG_WINDOW_TARGET != 0,
            s != null && s.hostAnswers && s.flags and FLAG_SESSION_SCOPED != 0, reasons)
        if (!connected) return View(State.NOT_CONNECTED, null, false, null, 0, 0, false, false, emptyList())
        if (status == null || !status.roleKnown) return view(State.CHECKING)
        if (!status.hostAnswers) return view(State.NO_ANSWER_HOST)
        if (!status.valid) return view(State.CHECKING)
        val reasons = buildList {
            if (status.flags and FLAG_WINDOW_TARGET != 0) add(Reason.WINDOW_TARGET)
            if (status.flags and FLAG_DXGI_QUARANTINED != 0) add(Reason.DXGI_QUARANTINED)
            if (status.flags and FLAG_BEFORE_LOGIN != 0) add(Reason.BEFORE_LOGIN)
        }
        val handledLit = status.requested.takeIf { it in DXGI..GDI }
        if (asked == null) return view(State.IDLE, handledLit, status, reasons)
        // the current request: sent? answered at or after its seq? handled?
        val pending = status.intentAsked == 0L || status.intentSent < status.intentAsked || status.intentSentSeq == 0L ||
            status.seq < status.intentSentSeq || status.flags and FLAG_NOT_YET_APPLIED != 0
        if (pending) return view(State.APPLYING, null, status, reasons)
        if (status.requested != asked) return view(State.REFUSED, handledLit, status, reasons)
        if (status.flags and FLAG_WINDOW_TARGET != 0) return view(State.STORED_FOR_DESKTOP, handledLit, status, reasons)
        return view(State.APPLIED, handledLit, status, reasons)
    }

    fun label(code: Int): String = when (code) {
        DXGI -> "DXGI"
        WGC -> "WGC"
        GDI -> "GDI"
        else -> "?"
    }
}
