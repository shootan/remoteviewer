package com.remote60.androiddirect

import android.content.Context

data class SavedEndpoint(
    val host: String = "",
    val videoPort: Int = 43000,
    val controlPort: Int = 43001,
    val bitrateKbps: Int = 8000,
    val fps: Int = 30,
    val desktopBackendCode: Int = 1,
)

object SessionPersistence {
    private const val PREFS_NAME = "remote60_android_direct"
    private const val KEY_HOST = "host"
    private const val KEY_VIDEO_PORT = "video_port"
    private const val KEY_CONTROL_PORT = "control_port"
    private const val KEY_BITRATE_KBPS = "bitrate_kbps"
    private const val KEY_FPS = "fps"
    private const val KEY_DESKTOP_BACKEND = "desktop_backend"
    private const val KEY_UNLOCK_PASSWORD_PREFIX = "unlock_pw_"

    fun load(context: Context): SavedEndpoint {
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        return SavedEndpoint(
            host = prefs.getString(KEY_HOST, "") ?: "",
            videoPort = prefs.getInt(KEY_VIDEO_PORT, 43000),
            controlPort = prefs.getInt(KEY_CONTROL_PORT, 43001),
            bitrateKbps = prefs.getInt(KEY_BITRATE_KBPS, 8000),
            fps = prefs.getInt(KEY_FPS, 30),
            desktopBackendCode = prefs.getInt(KEY_DESKTOP_BACKEND, 1),
        )
    }

    /**
     * The Windows sign-in password, for one-tap unlocking.
     *
     * Kept per host, because one phone reaches several PCs and typing the wrong one at a lock
     * screen costs a failed sign-in attempt. Stored in the app's private preferences, which no
     * other app can read on a device that has not been rooted -- but this is a real credential,
     * so it is only written when the user asks for it and can be removed from the same dialog.
     */
    fun loadUnlockPassword(context: Context, host: String): String? {
        if (host.isEmpty()) return null
        val value = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            .getString(KEY_UNLOCK_PASSWORD_PREFIX + host, null)
        return if (value.isNullOrEmpty()) null else value
    }

    fun saveUnlockPassword(context: Context, host: String, password: String?) {
        if (host.isEmpty()) return
        val editor = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE).edit()
        if (password.isNullOrEmpty()) editor.remove(KEY_UNLOCK_PASSWORD_PREFIX + host)
        else editor.putString(KEY_UNLOCK_PASSWORD_PREFIX + host, password)
        editor.apply()
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
