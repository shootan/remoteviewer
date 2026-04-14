package com.remote60.androiddirect

import android.view.Surface

object NativeSessionBridge {
    init {
        System.loadLibrary("remote60_android_direct")
    }

    external fun nativeConnect(host: String, videoPort: Int, controlPort: Int): Boolean
    external fun nativeDisconnect()
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
    external fun nativeResetVideoStream()
}
