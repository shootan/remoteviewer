package com.remote60.androiddirect

object NativeSessionBridge {
    init {
        System.loadLibrary("remote60_android_direct")
    }

    external fun nativeConnect(host: String, videoPort: Int, controlPort: Int): Boolean
    external fun nativeDisconnect()
    external fun nativeGetStatus(): String
    external fun nativeGetLastError(): String
}
