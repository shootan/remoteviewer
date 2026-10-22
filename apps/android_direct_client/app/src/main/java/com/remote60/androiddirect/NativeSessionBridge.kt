package com.remote60.androiddirect

import android.view.Surface

object NativeSessionBridge {
    init {
        System.loadLibrary("remote60_android_direct")
    }

    // --- keepalive-multi r1: the kept PCs (native session table, session_table.hpp) ----------------
    //
    // Every PC connection is an entry with a process-unique [handle]. Calls about one connection take its handle;
    // calls that act on the picture / input / clipboard / targets take the [binding] the app got from
    // nativeActivate when that PC was put on screen -- captured when the action was taken, so an action of B can
    // never reach A after a switch (a stale binding does nothing). The decoder and Surface stay single.

    /** [kind (0 reuse, 1 new, 2 refused), handle, victim]: a tap on [key] (origin|account|hostId) in lifecycle [epoch]. */
    external fun nativeReserve(key: String, manual: Boolean, epoch: Long): LongArray
    /**
     * r2 M1: the new connection, made as generation [gen], is up -- [status, closed...]: 1 committed (the handles its
     * reservation evicted follow, closed now), 0 pending (its Hello is not answered yet: ask again), -1 failed (it
     * ended first: given back, nothing evicted). The eviction happens only for an established connection.
     */
    external fun nativeCommit(handle: Long, gen: Long): LongArray
    /** The connect failed or went stale: that entry only. */
    external fun nativeFail(handle: Long, reason: String)
    /** On screen: its binding (0 = refused -- another PC on screen / still confirming, or its resume failed: ended). */
    external fun nativeActivate(handle: Long): Long
    /** One PC ends; the others stay. */
    external fun nativeEnd(handle: Long, reason: String): Boolean
    /** Everything ends (sign-out, account / directory, network, disconnect all): the new lifecycle epoch. */
    external fun nativeEndAll(reason: String): Long
    external fun nativeEpoch(): Long
    external fun nativeSetKeepalivePolicy(maxHosts: Int, parkTtlMs: Long)
    /** {"epoch","maxHosts","parkTtlMs","entries":[...],"ended":[...],"closing","binding"} -- no token, no address. */
    external fun nativeEntriesJson(): String

    /** r4 S1: the manual connect of entry [handle], adopted through its gate as operation [op]; the generation (0 = none). */
    external fun nativeConnect(handle: Long, host: String, videoPort: Int, controlPort: Int, op: Long): Long

    /** r4 S2-a: a new connect operation for entry [handle] (numbers process-unique, the cancel that entry's own). */
    external fun nativeConnectOpNext(handle: Long): Long

    /** r4 S1: closes that entry's session only if [op] adopted generation [gen] and it is still the one running. */
    external fun nativeCloseIfOwn(handle: Long, op: Long, gen: Long): Boolean

    // keepalive r1 (SessionPark.kt): park the PC on screen while the PC list shows; nativeActivate resumes it.
    /** Its binding goes (no frame of it reaches the decoder), then inhibit / release / pause / fence; false = not parkable. */
    external fun nativeBeginPark(handle: Long): Boolean
    /** Drops a park without resuming (an end follows). */
    external fun nativeEndPark(handle: Long)
    /** SessionPark.NativePhase codes. */
    external fun nativeParkPhase(handle: Long): Int
    external fun nativeParkReleasedCount(handle: Long): Int
    external fun nativeParkDroppedActionCount(handle: Long): Int
    /** r2 K5: the host stops sending when paused (else "PC 목록" is not offered: 연결 해제 only). */
    external fun nativeHostHonorsStreamPause(handle: Long): Boolean
    /** Bumps on every session Connect: the identity of the authenticated connection. */
    external fun nativeConnectionGeneration(handle: Long): Long
    /** "handle= gen= connects= hellos= observes= punches=" for the log. */
    external fun nativeSessionCounters(handle: Long): String
    /** The phone's clipboard when a parked session resumes: counted as already sent. */
    external fun nativeSeedClipboardBaseline(binding: Long, text: String)

    /**
     * Asks the directory what public address this device's media socket presents, returning
     * "ip:port" or "" on failure. The socket is kept open for [nativeDirectoryConnect], because
     * NAT maps each socket separately and a different one would be punched to the wrong port.
     */
    external fun nativeDirectoryObserve(
        handle: Long,
        directoryHost: String,
        directoryUdpPort: Int,
        observeToken: String,
        op: Long,
    ): String

    /**
     * Punches every offered address at once and starts the session on whichever answers.
     *
     * Each candidate is "ip:port|kind". They are tried together rather than in turn because the
     * client cannot tell which its own network permits -- a blocked address and an offline host
     * look the same from here -- and going one at a time would multiply the wait by however many
     * blocked ones happen to be listed first.
     */
    external fun nativeDirectoryConnectAny(
        handle: Long,
        candidates: Array<String>,
        punchBudgetMs: Int,
        punchToken: String,
        op: Long,
    ): Long  // r3 R2-a: the generation this call's connection has; 0 = none (failed or stale)

    /** Which candidate answered, as "ip:port|kind", for the diagnostics log. */
    external fun nativeDirectoryChosenCandidate(handle: Long): String

    external fun nativeDirectoryLastError(handle: Long): String
    external fun nativeSetSurface(surface: Surface?)

    /** Called once per frame the view actually latched, which is the only count that
     *  corresponds to what a person sees. Everything else counts frames we handed over. */
    external fun nativeNotifyFrameDisplayed()
    external fun nativeGetStatus(handle: Long): String
    external fun nativeGetLastError(handle: Long): String
    external fun nativeGetVideoDebugStatus(): String
    external fun nativeGetVideoSizePacked(): Long
    external fun nativePrepareVideoSwitch(binding: Long, selectionGeneration: Long): Boolean
    external fun nativeAbortVideoSwitch(binding: Long)
    external fun nativeGetReadySelectionGeneration(binding: Long): Long
    external fun nativeGetLastOutputPresentationUs(): Long
    /** sh-filter r1: monotonic ns of the handoff of the frame with this buffer timestamp (0 = unknown). */
    external fun nativeHandoffNsForTimestamp(timestampNs: Long): Long
    /** sh-filter r2: [handoffNs, streamGeneration, codecEpoch, visibleWidth, visibleHeight] of the frame
     *  released with this timestamp, or null (not among the recent ones). Any thread. The single decoder
     *  sink (keepalive-multi: one active decoder) -- no binding. */
    external fun nativeHandoffInfo(timestampNs: Long): LongArray?
    /** sh-filter r2: codec instances started so far (a frame from a later codec has a larger epoch). */
    external fun nativeCodecEpoch(): Long
    external fun nativeRequestWindowList(binding: Long): Boolean
    external fun nativeSelectWindow(binding: Long, windowId: Long): Boolean
    external fun nativeSelectDesktopMode(binding: Long): Boolean
    external fun nativeSelectMonitor(binding: Long, monitorId: Int): Boolean
    // The pick together with the monitorListRevision of the list it was made from.
    external fun nativeSelectMonitorAt(binding: Long, monitorId: Int, listRevision: Long): Boolean
    external fun nativeIsHostScreenLocked(binding: Long): Boolean
    /** The host's Pong capture flags (remote reboot / prelogin bits; RemoteRebootPolicy), of this entry's host. */
    external fun nativeHostCaptureFlags(binding: Long): Int
    /** One confirmed remote reboot on this entry's host: the request id, or 0 when nothing was sent. */
    external fun nativeRequestReboot(binding: Long): Int
    /** [requestId, outcome, win32] of this entry's reboot request once, or null. outcome 100 = no answer. */
    external fun nativeTakeRebootOutcome(binding: Long): LongArray?
    external fun nativeRequestRuntimeConfig(binding: Long, bitrateBps: Int, fps: Int): Boolean
    external fun nativeRequestDesktopCaptureBackend(binding: Long, backend: Int): Boolean
    external fun nativeRequestStreamActive(binding: Long, active: Boolean): Boolean
    external fun nativeQueueInputEvent(
        binding: Long,
        kind: Int,
        x: Int,
        y: Int,
        wheelDelta: Int,
        keyCode: Int,
        buttons: Int,
    ): Boolean
    external fun nativeQueueInputText(binding: Long, text: String): Boolean

    // --- clipboard text sync (K1) --------------------------------------------------
    //
    // The app drives both directions, because Android only lets a foreground app read or write
    // the clipboard. Native never touches it; it only carries the text over the control channel.

    /**
     * Offers the phone's clipboard text to the host. Returns false when it was not worth sending
     * -- not connected, sync off, a host without the capability, or the sync core judging it an
     * echo / duplicate / empty / oversize -- so false means "nothing happened", not "failed".
     */
    external fun nativeQueueClipboardText(binding: Long, text: String): Boolean

    /** Clipboard text that arrived from the host, or null when none is waiting. Drains on read. */
    external fun nativeTakeIncomingClipboardText(binding: Long): String?

    /**
     * True once per session: the session wants this phone's current clipboard pushed, so that the
     * machine just connected from is the source rather than whatever the PC held from an earlier
     * session. Consumed as it is read.
     */
    external fun nativeTakeClipboardPushRequest(binding: Long): Boolean

    /** The user's switch: every kept connection and each new one. */
    external fun nativeSetClipboardSyncEnabled(enabled: Boolean)
    external fun nativeIsClipboardSyncEnabled(): Boolean

    /** Whether the host on screen advertised clipboard support. */
    external fun nativeHostSupportsClipboard(binding: Long): Boolean

    external fun nativeGetWindowPanelJson(binding: Long): String

    /**
     * Preview pixels for a target card (windowId 0 = desktop).
     * Layout: [width:int32 LE][height:int32 LE][RGBA bytes]; null when not fetched yet.
     */
    external fun nativeGetWindowThumbnail(binding: Long, windowId: Long): ByteArray?

    // --- input macro ---------------------------------------------------------------
    // Recording taps the send path, so anything that produces input is captured the same way:
    // a direct touch, the on-screen mouse, a gesture.

    external fun nativeMacroStartRecording()
    external fun nativeMacroStopRecording()

    /**
     * @param repeatCount 0 repeats until stopped.
     * @param gapMinMs lower bound of the random wait between repeats.
     * @param gapMaxMs upper bound of that wait.
     */
    external fun nativeMacroStartPlayback(
        binding: Long,
        timingJitterMs: Int,
        positionJitterPx: Int,
        repeatCount: Int,
        gapMinMs: Int,
        gapMaxMs: Int,
    ): Boolean

    external fun nativeMacroStopPlayback()

    /** Sends whatever is due; call from a ticker. Returns how many events went out. */
    external fun nativeMacroPump(binding: Long): Int

    /** 0 idle, 1 recording, 2 playing. */
    external fun nativeMacroState(): Int
    external fun nativeMacroStepCount(): Int
    external fun nativeMacroCompletedRepeats(): Int
    external fun nativeMacroClear()

    /** The recorded actions as readable lines, newline separated. */
    external fun nativeMacroStepLines(): String

    /** Pauses/resumes whichever of recording or playback is active; the gap leaves no trace. */
    external fun nativeMacroSetPaused(paused: Boolean)
    external fun nativeMacroIsPaused(): Boolean

    /** Editing is only possible while idle; both return false otherwise. */
    external fun nativeMacroRemoveStep(index: Int): Boolean
    external fun nativeMacroUpdateStep(index: Int, x: Int, y: Int, delayMs: Int): Boolean

    /** "kind x y wheel key buttons delay" for one step, or "" out of range. */
    external fun nativeMacroStepFields(index: Int): String

    /** The whole macro as text for a save file, and back. */
    external fun nativeMacroSerialize(): String
    external fun nativeMacroLoadSerialized(text: String): Boolean

    /** Total UDP video bytes received this session. */
    external fun nativeGetSessionBytesReceived(binding: Long): Long
    external fun nativeResetVideoStream(binding: Long)
}
