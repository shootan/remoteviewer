package com.remote60.androiddirect

import android.content.Context
import android.os.Handler
import android.os.Looper
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import javax.net.ssl.HttpsURLConnection

/**
 * The Android update path, end to end: ask, verify, and hand over.
 *
 * The pieces existed before this file and none of them was called. UpdateManifest parsed
 * documents nobody fetched, and UpdateDecision judged releases nobody asked about. This is what
 * connects them, and it is deliberately thin -- every rule it applies lives somewhere that a JVM
 * test can reach.
 *
 * Nothing here blocks the caller. A start-up check runs on a phone that is often somewhere with
 * no route to the server, and an app that will not show its sign-in screen until an update server
 * answers is an app that cannot be used on a train. The check runs on its own thread and the
 * answer comes back on the main one.
 *
 * The other rule is that only "there is a newer version" reaches the user. A start-up check that
 * could not reach the server is the ordinary case, and a dialog for it teaches people to dismiss
 * dialogs -- including the next one, which matters. Everything else goes to the log.
 *
 * Design: docs/업데이트_기능_설계.md 3.5, 4.1-4.2, docs/업데이트_배선_계획.md W7.
 */
object UpdateFlow {

    /** What a check found. `artifact` is present only when there is something to install. */
    data class Outcome(
        val verdict: UpdateDecision.Verdict,
        val artifact: UpdateManifest.Artifact? = null,
        val version: String = "",
        val detail: String = "",
    )

    /** Caps a manifest body. A manifest is a few hundred bytes; anything else is not one. */
    private const val MAX_MANIFEST_BYTES = 64 * 1024

    /**
     * Fetches and judges, off the calling thread. `onResult` arrives on the main looper.
     *
     * `trustedPublicKeyHex` empty means this build cannot check at all, which is reported rather
     * than treated as a failure -- nothing is wrong, the feature is simply not switched on.
     */
    fun checkAsync(
        manifestUrl: String,
        trustedPublicKeyHex: String,
        installedVersionCode: Long,
        onResult: (Outcome) -> Unit,
    ) {
        val main = Handler(Looper.getMainLooper())
        Thread {
            val outcome = check(manifestUrl, trustedPublicKeyHex, installedVersionCode)
            main.post { onResult(outcome) }
        }.apply {
            isDaemon = true  // never keeps the process alive on its own
            name = "gnlink-update-check"
            start()
        }
    }

    /** The same, synchronously. Called by checkAsync; separated so it can be reasoned about. */
    fun check(
        manifestUrl: String,
        trustedPublicKeyHex: String,
        installedVersionCode: Long,
    ): Outcome {
        if (manifestUrl.isEmpty() || trustedPublicKeyHex.isEmpty()) {
            return Outcome(UpdateDecision.Verdict.NotForUs, null, "",
                           "this build has no update endpoint or trusted key")
        }
        // https only, and refused here rather than three layers down. The manifest is the thing
        // that decides what gets installed; fetching it over cleartext would make every check
        // below it theatre.
        if (!manifestUrl.startsWith("https://")) {
            return Outcome(UpdateDecision.Verdict.NotForUs, null, "", "the update url is not https")
        }

        val body = fetchText(manifestUrl) ?: return Outcome(
            UpdateDecision.Verdict.NotForUs, null, "", "could not reach the update server")

        // One response carries both. The signature used to be fetched from "$manifestUrl.sig",
        // which the server has never served: its route is /api/update/manifest?platform=android
        // and appending ".sig" lands on a query value, not a file. Nobody noticed because the url
        // came from a build constant that was empty, so this line never ran. It runs now that the
        // url is derived from the directory, so it has to match what the server actually
        // publishes -- the same shape the Windows client reads.
        val document = jsonStringField(body, "manifest") ?: return Outcome(
            UpdateDecision.Verdict.NotForUs, null, "", "the update server sent no manifest")
        val signature = jsonStringField(body, "signature")?.trim() ?: return Outcome(
            UpdateDecision.Verdict.NotForUs, null, "", "the update server sent no signature")

        return evaluateDocument(document, signature, trustedPublicKeyHex, installedVersionCode)
    }

    /**
     * One string field out of the server's small response.
     *
     * By hand rather than with JSONObject, for the same reason the Windows client parses it by
     * hand: the document's exact bytes decide whether its signature verifies, so the extraction
     * has to be something that can be read and checked rather than trusted. Only the escapes a
     * manifest actually contains are decoded.
     */
    internal fun jsonStringField(body: String, key: String): String? {
        val needle = "\"" + key + "\":\""
        val at = body.indexOf(needle)
        if (at < 0) return null
        val out = StringBuilder()
        var i = at + needle.length
        while (i < body.length && body[i] != '"') {
            if (body[i] == '\\' && i + 1 < body.length) {
                when (body[i + 1]) {
                    'n' -> out.append('\n')
                    'r' -> out.append('\r')
                    't' -> out.append('\t')
                    else -> out.append(body[i + 1])
                }
                i += 2
                continue
            }
            out.append(body[i])
            i++
        }
        // An unterminated string is a truncated response, not a field.
        if (i >= body.length) return null
        return out.toString()
    }

    /**
     * The gates between a fetched document and a decision to install.
     *
     * Separated from the fetching so all of it can be driven without a network, because what
     * matters here is that there are THREE checks and that each one alone is enough to refuse:
     *
     *   1. the signature, which says the document is ours;
     *   2. `versionCode`, which says the package may replace this one;
     *   3. and later, at download time, the sha256, which says the bytes are the ones signed for.
     *
     * They are not independent conveniences. The signature makes the versionCode and the hash
     * trustworthy; the hash makes the signature mean something about the file rather than about a
     * document; the versionCode is what Android will enforce whatever the other two say. Remove
     * any one and the remaining two stop being worth having -- a verified manifest pointing at
     * unverified bytes, or verified bytes nobody vouched for, or a package the system refuses
     * after the user approved it.
     */
    fun evaluateDocument(
        document: String,
        signatureHex: String,
        trustedPublicKeyHex: String,
        installedVersionCode: Long,
    ): Outcome {
        val result = UpdateManifest.load(document, signatureHex, trustedPublicKeyHex, "android")
        if (result.status != UpdateManifest.Status.Ok || result.fields == null) {
            return Outcome(UpdateDecision.Verdict.NotForUs, null, "",
                           "manifest ${result.status}: ${result.detail}")
        }
        val fields = result.fields
        val evaluation = UpdateDecision.evaluate(fields, installedVersionCode)
        return Outcome(evaluation.verdict, evaluation.artifact, fields.version, evaluation.detail)
    }

    /**
     * Downloads the artifact and checks it against what the manifest promised.
     *
     * Returns the file only when size and hash both match. A file that fails either is deleted
     * before returning: a rejected download left on disk is one that something later might
     * mistake for a good one.
     */
    fun download(context: Context, artifact: UpdateManifest.Artifact): File? {
        if (!artifact.url.startsWith("https://")) return null
        val target = UpdateInstaller.downloadTarget(context)
        target.delete()
        try {
            openHttps(artifact.url)?.use { connection ->
                if (connection.responseCode != HttpURLConnection.HTTP_OK) return null
                connection.inputStream.use { input ->
                    target.outputStream().use { out ->
                        var total = 0L
                        val buffer = ByteArray(64 * 1024)
                        while (true) {
                            val read = input.read(buffer)
                            if (read <= 0) break
                            total += read
                            // Stopped while it arrives rather than after it has been written. The
                            // manifest's own size is the cap, so a body larger than what was
                            // signed for never lands in full.
                            if (total > artifact.size) return@download null.also { target.delete() }
                            out.write(buffer, 0, read)
                        }
                    }
                }
            } ?: return null
        } catch (_: Exception) {
            target.delete()
            return null
        }

        val verdict = UpdateDecision.verifyDownload(
            target.length(), UpdateInstaller.sha256Hex(target), artifact)
        if (verdict != UpdateDecision.DownloadVerdict.Ok) {
            target.delete()
            return null
        }
        return target
    }

    private fun openHttps(url: String): HttpsURLConnection? {
        val connection = URL(url).openConnection()
        if (connection !is HttpsURLConnection) return null
        connection.connectTimeout = 15_000
        connection.readTimeout = 30_000
        // Redirects are followed by the platform, but never from https to http: that downgrade is
        // refused by HttpsURLConnection itself, which is the behaviour we want and the reason no
        // custom redirect handling is written here.
        connection.instanceFollowRedirects = true
        return connection
    }

    private fun fetchText(url: String): String? = try {
        openHttps(url)?.use { connection ->
            if (connection.responseCode != HttpURLConnection.HTTP_OK) {
                null
            } else {
                connection.inputStream.use { input ->
                    val bytes = input.readBytes(MAX_MANIFEST_BYTES + 1)
                    if (bytes.size > MAX_MANIFEST_BYTES) null else String(bytes, Charsets.UTF_8)
                }
            }
        }
    } catch (_: Exception) {
        null
    }

    private inline fun <T> HttpsURLConnection.use(block: (HttpsURLConnection) -> T): T =
        try { block(this) } finally { disconnect() }

    private fun java.io.InputStream.readBytes(limit: Int): ByteArray {
        val out = java.io.ByteArrayOutputStream()
        val buffer = ByteArray(8 * 1024)
        while (out.size() <= limit) {
            val read = read(buffer)
            if (read <= 0) break
            out.write(buffer, 0, read)
        }
        return out.toByteArray()
    }
}
