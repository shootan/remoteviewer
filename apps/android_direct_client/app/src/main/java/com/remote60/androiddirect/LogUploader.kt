package com.remote60.androiddirect

import android.content.Context
import android.os.Build
import android.provider.Settings
import android.util.Log
import java.io.OutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.util.ArrayDeque
import java.util.concurrent.TimeUnit

/**
 * Ships this phone's diagnostics log to the directory service.
 *
 * The log file on the device is written either way; this is a second sink, because getting that
 * file off a phone is the step that does not happen when someone is standing in an office trying
 * to reproduce a connection failure. Lines are queued and flushed by one background thread, so a
 * server that is down or slow costs the UI nothing.
 */
object LogUploader {
    private const val TAG = "remote60_android_direct"
    private const val QUEUE_CAP_BYTES = 512 * 1024
    private const val BATCH_MAX_BYTES = 128 * 1024
    private val FLUSH_INTERVAL_MS = TimeUnit.SECONDS.toMillis(3)
    private val CONNECT_TIMEOUT_MS = TimeUnit.SECONDS.toMillis(8).toInt()
    private val READ_TIMEOUT_MS = TimeUnit.SECONDS.toMillis(10).toInt()

    private val lock = Any()
    private val queue = ArrayDeque<String>()
    private var queuedBytes = 0
    private var dropped = 0L
    private var url = ""
    private var token = ""
    private var device = ""
    private var worker: Thread? = null

    /** Starts (or re-points) the uploader. Safe to call on every sign-in. */
    fun configure(context: Context, directoryUrl: String, sessionToken: String) {
        if (directoryUrl.isEmpty() || sessionToken.isEmpty()) return
        synchronized(lock) {
            url = directoryUrl
            token = sessionToken
            if (device.isEmpty()) device = deviceName(context)
            if (worker == null) {
                worker = Thread({ loop() }, "remote60-log-upload").apply {
                    isDaemon = true
                    start()
                }
            }
        }
    }

    /** Queues one line. Never blocks; drops the oldest when the queue is full. */
    fun enqueue(line: String) {
        if (line.isEmpty()) return
        synchronized(lock) {
            if (url.isEmpty() || token.isEmpty()) return
            // The end of an overflowing log is the part worth keeping, so the front goes.
            while (queuedBytes + line.length + 1 > QUEUE_CAP_BYTES && queue.isNotEmpty()) {
                queuedBytes -= queue.removeFirst().length + 1
                dropped++
            }
            queue.addLast(line)
            queuedBytes += line.length + 1
        }
    }

    private fun deviceName(context: Context): String {
        val id = try {
            Settings.Secure.getString(context.contentResolver, Settings.Secure.ANDROID_ID).orEmpty()
        } catch (t: Throwable) {
            ""
        }
        val model = (Build.MODEL ?: "android").replace(Regex("[^A-Za-z0-9._-]"), "_")
        return if (id.length >= 6) "$model-${id.take(6)}" else model
    }

    private fun loop() {
        while (true) {
            try {
                Thread.sleep(FLUSH_INTERVAL_MS)
            } catch (t: InterruptedException) {
                return
            }
            val batch = StringBuilder()
            val endpoint: String
            val auth: String
            val name: String
            synchronized(lock) {
                endpoint = url
                auth = token
                name = device
                while (queue.isNotEmpty() && batch.length < BATCH_MAX_BYTES) {
                    val line = queue.removeFirst()
                    queuedBytes -= line.length + 1
                    batch.append(line).append('\n')
                }
                if (dropped > 0 && batch.isNotEmpty()) {
                    batch.append("[log-upload] dropped=").append(dropped).append('\n')
                    dropped = 0
                }
            }
            if (batch.isEmpty() || endpoint.isEmpty()) continue
            send(endpoint, auth, name, batch.toString())
        }
    }

    private fun send(directoryUrl: String, sessionToken: String, deviceName: String, body: String) {
        var connection: HttpURLConnection? = null
        try {
            val base = if (directoryUrl.startsWith("http")) directoryUrl else "http://$directoryUrl"
            connection = (URL(base.trimEnd('/') + "/api/logs").openConnection() as HttpURLConnection).apply {
                requestMethod = "POST"
                connectTimeout = CONNECT_TIMEOUT_MS
                readTimeout = READ_TIMEOUT_MS
                doOutput = true
                setRequestProperty("Content-Type", "text/plain")
                setRequestProperty("Authorization", "Bearer $sessionToken")
                setRequestProperty("x-log-device", deviceName)
                setRequestProperty("x-log-stream", "apk")
            }
            val bytes = body.toByteArray(Charsets.UTF_8)
            connection.setFixedLengthStreamingMode(bytes.size)
            val out: OutputStream = connection.outputStream
            out.write(bytes)
            out.flush()
            val status = connection.responseCode
            if (status !in 200..299) Log.w(TAG, "log upload refused: $status")
        } catch (t: Throwable) {
            // The device still has the file; a failed upload is not worth a user-visible error.
            Log.w(TAG, "log upload failed: ${t.message}")
        } finally {
            connection?.disconnect()
        }
    }
}
