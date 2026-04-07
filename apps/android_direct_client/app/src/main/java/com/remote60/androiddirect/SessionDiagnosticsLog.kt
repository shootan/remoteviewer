package com.remote60.androiddirect

import android.content.Context
import android.util.Log
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class SessionDiagnosticsLog(context: Context) {
    companion object {
        private const val LOG_TAG = "remote60_android_direct"
        private const val MAX_LOG_BYTES = 512 * 1024L
    }

    private val timestampFormat = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US)
    private val lock = Any()
    private val logDir = context.getExternalFilesDir(null) ?: context.filesDir
    private val logFile = File(logDir, "android_direct_client_session.log")
    private val rotatedFile = File(logDir, "android_direct_client_session.log.1")

    fun filePath(): String = logFile.absolutePath

    fun log(tag: String, message: String) {
        val line = timestampFormat.format(Date()) + " [" + tag + "] " + message
        Log.i(LOG_TAG, line)
        synchronized(lock) {
            rotateIfNeeded()
            logFile.parentFile?.mkdirs()
            logFile.appendText(line + "\n")
        }
    }

    private fun rotateIfNeeded() {
        if (!logFile.exists()) return
        if (logFile.length() < MAX_LOG_BYTES) return
        rotatedFile.delete()
        logFile.copyTo(rotatedFile, overwrite = true)
        logFile.writeText("")
    }
}
