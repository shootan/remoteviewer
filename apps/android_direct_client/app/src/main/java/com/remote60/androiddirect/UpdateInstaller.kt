package com.remote60.androiddirect

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageInstaller
import android.net.Uri
import android.os.Build
import android.provider.Settings
import java.io.File
import java.security.MessageDigest

/**
 * Installing an update on Android: the part that needs a device.
 *
 * The rules live in UpdateDecision.kt and are tested on a JVM. What is here is the machinery --
 * asking for the permission, opening a PackageInstaller session, streaming the APK into it, and
 * reading back what the system said. None of it can be unit tested, which is exactly why as
 * little as possible of the thinking is in it.
 *
 * PackageInstaller rather than an ACTION_VIEW intent on a file. The session takes a stream, so no
 * FileProvider is needed and no content URI is granted to another process; the APK never leaves
 * this app's private storage. It also reports back a status code, which is how a dismissed dialog
 * can be told apart from a real failure -- with a file intent, both look like nothing happening.
 *
 * Design: docs/업데이트_기능_설계.md 4.2, docs/업데이트_배선_계획.md W7.
 */
object UpdateInstaller {

    const val ACTION_INSTALL_STATUS = "com.remote60.androiddirect.INSTALL_STATUS"

    /**
     * Whether this app may ask the system to install a package.
     *
     * Below API 26 the permission is granted at install time; from 26 the user grants it per app
     * in settings and can take it away again, so it is asked every time rather than remembered.
     */
    fun canInstallPackages(context: Context): Boolean =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.packageManager.canRequestPackageInstalls()
        } else {
            true
        }

    /**
     * The settings screen where the user grants it.
     *
     * Returned rather than started here so the caller -- an Activity that can receive the result
     * -- decides when to show it. Sending someone to a settings page is interrupting them, and
     * that decision belongs where the rest of the interruption budget is spent.
     */
    fun manageUnknownSourcesIntent(context: Context): Intent =
        Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
               Uri.parse("package:${context.packageName}"))

    /** SHA-256 of a file as lowercase hex, or empty when it cannot be read. */
    fun sha256Hex(file: File): String = try {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { input ->
            val buffer = ByteArray(64 * 1024)
            while (true) {
                val read = input.read(buffer)
                if (read <= 0) break
                digest.update(buffer, 0, read)
            }
        }
        digest.digest().joinToString("") { "%02x".format(it) }
    } catch (_: Exception) {
        ""
    }

    /**
     * Where a downloaded APK is kept: this app's own cache, and nowhere else.
     *
     * Private storage on purpose. An APK in shared storage is writable by anything with the
     * permission to write there, and the window between verifying it and installing it is exactly
     * when that matters.
     */
    fun downloadTarget(context: Context): File {
        val dir = File(context.cacheDir, "update")
        dir.mkdirs()
        // One fixed name, and deliberately NOT the artifact's name from the manifest. That name
        // has been checked, but it is still a string from a document on its way to becoming a
        // path, and not using it at all removes the question entirely.
        return File(dir, "pending.apk")
    }

    /**
     * Hands a verified APK to the system installer.
     *
     * Returns false only when the session could not be opened or written -- the user's answer to
     * the dialog arrives later, as a broadcast, because it is their decision and not this
     * function's return value.
     *
     * The caller must have verified size and hash first. This does not re-check: doing it in two
     * places invites the two from drifting, and UpdateDecision.verifyDownload is where it lives.
     */
    fun install(context: Context, apk: File): Boolean {
        if (!apk.isFile) return false
        val installer = context.packageManager.packageInstaller
        var sessionId = -1
        return try {
            val params = PackageInstaller.SessionParams(
                PackageInstaller.SessionParams.MODE_FULL_INSTALL)
            params.setAppPackageName(context.packageName)
            sessionId = installer.createSession(params)
            installer.openSession(sessionId).use { session ->
                session.openWrite("payload", 0, apk.length()).use { out ->
                    apk.inputStream().use { input -> input.copyTo(out) }
                    session.fsync(out)
                }
                val intent = Intent(ACTION_INSTALL_STATUS).setPackage(context.packageName)
                // FLAG_MUTABLE: the system fills in the status extras. Immutable would leave the
                // callback arriving with nothing in it.
                val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE
                } else {
                    PendingIntent.FLAG_UPDATE_CURRENT
                }
                val pending = PendingIntent.getBroadcast(context, sessionId, intent, flags)
                session.commit(pending.intentSender)
            }
            true
        } catch (_: Exception) {
            if (sessionId >= 0) {
                try { installer.abandonSession(sessionId) } catch (_: Exception) { }
            }
            false
        }
    }

    /**
     * Receives what the system decided.
     *
     * STATUS_PENDING_USER_ACTION is the system asking for the confirmation dialog to be shown; it
     * is not an outcome, and treating it as one would report a failure before the user had been
     * asked anything.
     */
    class StatusReceiver(private val onOutcome: (UpdateDecision.InstallOutcome, String) -> Unit,
                         private val onUserActionRequired: (Intent) -> Unit) : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS,
                                            PackageInstaller.STATUS_FAILURE)
            val message = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE) ?: ""
            if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
                @Suppress("DEPRECATION")
                val confirm = intent.getParcelableExtra<Intent>(Intent.EXTRA_INTENT)
                if (confirm != null) onUserActionRequired(confirm)
                return
            }
            onOutcome(UpdateDecision.installOutcomeFromStatus(status), message)
        }

        fun filter(): IntentFilter = IntentFilter(ACTION_INSTALL_STATUS)
    }
}
