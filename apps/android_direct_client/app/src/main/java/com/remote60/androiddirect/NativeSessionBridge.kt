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
    external fun nativeDirectoryConnect(hostIp: String, hostPort: Int, punchBudgetMs: Int): Boolean

    external fun nativeDirectoryLastError(): String
    external fun nativeSetSurface(surface: Surface?)
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

    /** Total UDP video bytes received this session. */
    external fun nativeGetSessionBytesReceived(): Long
    external fun nativeResetVideoStream()
}
