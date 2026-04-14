package com.remote60.androiddirect

import android.content.Context

data class SavedEndpoint(
    val host: String = "",
    val videoPort: Int = 43000,
    val controlPort: Int = 43001,
    val bitrateKbps: Int = 8000,
    val fps: Int = 30,
    val desktopBackendCode: Int = 2,
)

object SessionPersistence {
    private const val PREFS_NAME = "remote60_android_direct"
    private const val KEY_HOST = "host"
    private const val KEY_VIDEO_PORT = "video_port"
    private const val KEY_CONTROL_PORT = "control_port"
    private const val KEY_BITRATE_KBPS = "bitrate_kbps"
    private const val KEY_FPS = "fps"
    private const val KEY_DESKTOP_BACKEND = "desktop_backend"

    fun load(context: Context): SavedEndpoint {
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        return SavedEndpoint(
            host = prefs.getString(KEY_HOST, "") ?: "",
            videoPort = prefs.getInt(KEY_VIDEO_PORT, 43000),
            controlPort = prefs.getInt(KEY_CONTROL_PORT, 43001),
            bitrateKbps = prefs.getInt(KEY_BITRATE_KBPS, 8000),
            fps = prefs.getInt(KEY_FPS, 30),
            desktopBackendCode = prefs.getInt(KEY_DESKTOP_BACKEND, 2),
        )
    }

    fun save(
        context: Context,
        host: String,
        videoPort: Int,
        controlPort: Int,
        bitrateKbps: Int,
        fps: Int,
        desktopBackendCode: Int,
    ) {
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            .edit()
            .putString(KEY_HOST, host)
            .putInt(KEY_VIDEO_PORT, videoPort)
            .putInt(KEY_CONTROL_PORT, controlPort)
            .putInt(KEY_BITRATE_KBPS, bitrateKbps)
            .putInt(KEY_FPS, fps)
            .putInt(KEY_DESKTOP_BACKEND, desktopBackendCode)
            .apply()
    }
}
