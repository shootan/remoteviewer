package com.remote60.androiddirect

/**
 * ua2-apk r1 (Codex codex_rel_0163_r1.md §1): what the settings screen says about the desktop capture backend.
 *
 * The choice belongs to THIS connection to this PC only -- the host keeps it for the session and starts every new
 * one at its own default -- so nothing here is stored, and a new connection starts with no choice of its own. What
 * is shown is the HOST's answer (ControlDesktopBackendStatus, 90): the backend that actually runs, apart from the
 * one asked for (a window target is always WGC, a quarantined DXGI runs WGC, the logon screen is DXGI only). Before
 * an answer, or from a host that does not answer, no button is lit as if the change had happened.
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
        val requested: Int,
        val effective: Int,
        val active: Int,
        val flags: Int,
        val changeCount: Long,
        val hostAnswers: Boolean,
    ) {
        companion object {
            /** [valid, seq, requested, effective, active, flags, changeCount, hostAnswers]; null/short = none. */
            fun of(raw: LongArray?): HostStatus? {
                if (raw == null || raw.size < 8) return null
                return HostStatus(raw[0] != 0L, raw[2].toInt(), raw[3].toInt(), raw[4].toInt(), raw[5].toInt(),
                    raw[6], raw[7] != 0L)
            }
        }
    }

    enum class State { NOT_CONNECTED, NO_ANSWER_HOST, CHECKING, APPLYING, APPLIED }

    data class View(
        val state: State,
        /** The button lit: what the host says runs now; null = none (nothing confirmed). */
        val lit: Int?,
        /** The buttons take a tap (only while connected). */
        val enabled: Boolean,
        /** What the line under the buttons says (MainActivity turns it into text). */
        val asked: Int?,
        val active: Int,
        val reasons: List<Reason>,
    )

    enum class Reason { WINDOW_TARGET, DXGI_QUARANTINED, BEFORE_LOGIN, NOT_YET_APPLIED }

    /**
     * @param connected a session is up (not parked away, not connecting)
     * @param asked what this connection asked for (null: nothing -- the host default)
     * @param status the host's last answer on this connection (null: none yet)
     */
    fun present(connected: Boolean, asked: Int?, status: HostStatus?): View {
        if (!connected) return View(State.NOT_CONNECTED, null, false, null, 0, emptyList())
        if (status == null || !status.hostAnswers) {
            // An older host: it takes a request but says nothing back, and keeps it as long as it keeps it --
            // the session scope is not guaranteed there. Nothing is lit as applied.
            return View(State.NO_ANSWER_HOST, null, true, asked, 0, emptyList())
        }
        if (!status.valid) return View(State.CHECKING, null, true, asked, 0, emptyList())
        val reasons = buildList {
            if (status.flags and FLAG_WINDOW_TARGET != 0) add(Reason.WINDOW_TARGET)
            if (status.flags and FLAG_DXGI_QUARANTINED != 0) add(Reason.DXGI_QUARANTINED)
            if (status.flags and FLAG_BEFORE_LOGIN != 0) add(Reason.BEFORE_LOGIN)
            if (status.flags and FLAG_NOT_YET_APPLIED != 0) add(Reason.NOT_YET_APPLIED)
        }
        // A request this connection made that the host's answer does not carry yet: still on its way.
        val applying = asked != null && status.requested != asked
        return View(if (applying) State.APPLYING else State.APPLIED, status.active.takeIf { it in DXGI..GDI },
            true, asked, status.active, reasons)
    }

    fun label(code: Int): String = when (code) {
        DXGI -> "DXGI"
        WGC -> "WGC"
        GDI -> "GDI"
        else -> "?"
    }
}
