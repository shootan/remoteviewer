package com.remote60.androiddirect

import android.view.Surface

object NativeSessionBridge {
    init {
        System.loadLibrary("remote60_android_direct")
    }

    external fun nativeConnect(host: String, videoPort: Int, controlPort: Int): Boolean
    external fun nativeDisconnect()

    /**
     * Asks the directory what public address this device's media socket presents, returning
     * "ip:port" or "" on failure. The socket is kept open for [nativeDirectoryConnect], because
     * NAT maps each socket separately and a different one would be punched to the wrong port.
     */
    external fun nativeDirectoryObserve(
        directoryHost: String,
        directoryUdpPort: Int,
        observeToken: String,
    ): String

    /** Punches towards the host on the observed socket and starts the session on it. */
    external fun nativeDirectoryConnect(
        hostIp: String,
        hostPort: Int,
        punchBudgetMs: Int,
        punchToken: String,
    ): Boolean

    /**
     * Punches every offered address at once and starts the session on whichever answers.
     *
     * Each candidate is "ip:port|kind". They are tried together rather than in turn because the
     * client cannot tell which its own network permits -- a blocked address and an offline host
     * look the same from here -- and going one at a time would multiply the wait by however many
     * blocked ones happen to be listed first.
     */
    external fun nativeDirectoryConnectAny(
        candidates: Array<String>,
        punchBudgetMs: Int,
        punchToken: String,
    ): Boolean

    /** Which candidate answered, as "ip:port|kind", for the diagnostics log. */
    external fun nativeDirectoryChosenCandidate(): String

    external fun nativeDirectoryLastError(): String
    external fun nativeSetSurface(surface: Surface?)

    /** Called once per frame the view actually latched, which is the only count that
     *  corresponds to what a person sees. Everything else counts frames we handed over. */
    external fun nativeNotifyFrameDisplayed()
    external fun nativeGetStatus(): String
    external fun nativeGetLastError(): String
    external fun nativeGetVideoDebugStatus(): String
    external fun nativeGetVideoSizePacked(): Long
    external fun nativePrepareVideoSwitch(selectionGeneration: Long)
    external fun nativeAbortVideoSwitch()
    external fun nativeGetReadySelectionGeneration(): Long
    external fun nativeGetLastOutputPresentationUs(): Long
    external fun nativeRequestWindowList(): Boolean
    external fun nativeSelectWindow(windowId: Long): Boolean
    external fun nativeSelectDesktopMode(): Boolean
    external fun nativeRequestRuntimeConfig(bitrateBps: Int, fps: Int): Boolean
    external fun nativeRequestDesktopCaptureBackend(backend: Int): Boolean
    external fun nativeRequestStreamActive(active: Boolean): Boolean
    external fun nativeQueueInputEvent(
        kind: Int,
        x: Int,
        y: Int,
        wheelDelta: Int,
        keyCode: Int,
        buttons: Int,
    ): Boolean
    external fun nativeQueueInputText(text: String): Boolean
    external fun nativeGetWindowPanelJson(): String

    /**
     * Preview pixels for a target card (windowId 0 = desktop).
     * Layout: [width:int32 LE][height:int32 LE][RGBA bytes]; null when not fetched yet.
     */
    external fun nativeGetWindowThumbnail(windowId: Long): ByteArray?

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
        timingJitterMs: Int,
        positionJitterPx: Int,
        repeatCount: Int,
        gapMinMs: Int,
        gapMaxMs: Int,
    ): Boolean

    external fun nativeMacroStopPlayback()

    /** Sends whatever is due; call from a ticker. Returns how many events went out. */
    external fun nativeMacroPump(): Int

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
    external fun nativeGetSessionBytesReceived(): Long
    external fun nativeResetVideoStream()
}
