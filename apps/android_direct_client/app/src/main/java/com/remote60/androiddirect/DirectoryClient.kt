package com.remote60.androiddirect

import android.content.Context
import org.json.JSONObject
import java.io.BufferedReader
import java.net.HttpURLConnection
import java.net.URL
import java.security.SecureRandom
import java.util.concurrent.TimeUnit

/**
 * Talks to the rendezvous server that lets this phone find a PC without knowing its address.
 *
 * Only the HTTP half lives here. The UDP address probe and the punch happen in native code,
 * because they must use the very socket the video will arrive on.
 */
object DirectoryClient {

    data class Host(
        val hostId: String,
        val hostName: String,
        val online: Boolean,
        val lastSeen: Long,
    )

    data class ConnectTarget(
        val ip: String,
        val port: Int,
    )

    class DirectoryException(message: String) : Exception(message)

    private const val PREFS = "remote60_directory"
    private const val KEY_URL = "url"
    private const val KEY_ACCOUNT = "accountId"
    private const val KEY_SESSION = "sessionToken"
    private const val KEY_EXPIRES = "expiresAt"

    private val CONNECT_TIMEOUT_MS = TimeUnit.SECONDS.toMillis(8).toInt()
    private val READ_TIMEOUT_MS = TimeUnit.SECONDS.toMillis(10).toInt()

    /** Default UDP probe port, matching the server's own relationship between its two ports. */
    fun observePortFor(url: String): Int = httpPortFor(url) + 1

    fun hostFor(url: String): String =
        try {
            URL(normalize(url)).host.orEmpty()
        } catch (e: Exception) {
            ""
        }

    private fun httpPortFor(url: String): Int =
        try {
            val parsed = URL(normalize(url))
            if (parsed.port > 0) parsed.port else if (parsed.protocol == "https") 443 else 80
        } catch (e: Exception) {
            8080
        }

    private fun normalize(url: String): String {
        val trimmed = url.trim().trimEnd('/')
        return if (trimmed.startsWith("http://") || trimmed.startsWith("https://")) {
            trimmed
        } else {
            "http://$trimmed"
        }
    }

    fun newObserveToken(): String {
        val bytes = ByteArray(16)
        SecureRandom().nextBytes(bytes)
        return bytes.joinToString("") { "%02x".format(it) }
    }

    // ------------------------------------------------------------------ stored session

    fun savedUrl(context: Context): String =
        prefs(context).getString(KEY_URL, "").orEmpty()

    fun savedAccountId(context: Context): String =
        prefs(context).getString(KEY_ACCOUNT, "").orEmpty()

    /** A stored token is only useful while it is valid; treat an expired one as absent. */
    fun savedSessionToken(context: Context): String {
        val p = prefs(context)
        val expiresAt = p.getLong(KEY_EXPIRES, 0L)
        if (expiresAt in 1..System.currentTimeMillis()) return ""
        return p.getString(KEY_SESSION, "").orEmpty()
    }

    /**
     * Remembers where the user was signing in to, before knowing whether it worked.
     *
     * These are not secrets, and tying them to a successful login meant every failed attempt
     * threw away the server address and made the next try start from an empty form.
     */
    fun rememberEndpoint(context: Context, url: String, accountId: String) {
        prefs(context).edit()
            .putString(KEY_URL, normalize(url))
            .putString(KEY_ACCOUNT, accountId)
            .apply()
    }

    fun saveSession(context: Context, url: String, accountId: String, token: String, expiresAt: Long) {
        prefs(context).edit()
            .putString(KEY_URL, normalize(url))
            .putString(KEY_ACCOUNT, accountId)
            .putString(KEY_SESSION, token)
            .putLong(KEY_EXPIRES, expiresAt)
            .apply()
    }

    /** Forgets the token but keeps the server and id, so signing back in is one field. */
    fun clearSession(context: Context) {
        prefs(context).edit()
            .remove(KEY_SESSION)
            .remove(KEY_EXPIRES)
            .apply()
    }

    private fun prefs(context: Context) =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    // ------------------------------------------------------------------ api

    fun login(url: String, id: String, password: String): Pair<String, Long> {
        val body = JSONObject().put("id", id).put("pw", password)
        val response = post(url, "/api/login", body, null)
        val token = response.optString("sessionToken")
        if (token.isEmpty()) throw DirectoryException("server did not return a session")
        return token to response.optLong("expiresAt", 0L)
    }

    fun hosts(url: String, sessionToken: String): List<Host> {
        val response = get(url, "/api/hosts", sessionToken)
        val array = response.optJSONArray("hosts") ?: return emptyList()
        val out = ArrayList<Host>(array.length())
        for (i in 0 until array.length()) {
            val item = array.optJSONObject(i) ?: continue
            out.add(
                Host(
                    hostId = item.optString("hostId"),
                    hostName = item.optString("hostName", "PC"),
                    online = item.optBoolean("online", false),
                    lastSeen = item.optLong("lastSeen", 0L),
                )
            )
        }
        return out
    }

    fun connect(url: String, sessionToken: String, hostId: String, observeToken: String): ConnectTarget {
        val body = JSONObject().put("hostId", hostId).put("observeToken", observeToken)
        val response = post(url, "/api/connect", body, sessionToken)
        val ip = response.optString("hostPublicIp")
        val port = response.optInt("hostPublicUdpPort")
        if (ip.isEmpty() || port <= 0) throw DirectoryException("host address unavailable")
        return ConnectTarget(ip, port)
    }

    // ------------------------------------------------------------------ transport

    private fun post(url: String, path: String, body: JSONObject, token: String?): JSONObject =
        request(url, path, "POST", body, token)

    private fun get(url: String, path: String, token: String?): JSONObject =
        request(url, path, "GET", null, token)

    private fun request(
        url: String,
        path: String,
        method: String,
        body: JSONObject?,
        token: String?,
    ): JSONObject {
        val connection = URL(normalize(url) + path).openConnection() as HttpURLConnection
        try {
            connection.requestMethod = method
            connection.connectTimeout = CONNECT_TIMEOUT_MS
            connection.readTimeout = READ_TIMEOUT_MS
            connection.useCaches = false
            if (!token.isNullOrEmpty()) {
                connection.setRequestProperty("Authorization", "Bearer $token")
            }
            if (body != null) {
                connection.doOutput = true
                connection.setRequestProperty("Content-Type", "application/json")
                connection.outputStream.use { it.write(body.toString().toByteArray()) }
            }

            val status = connection.responseCode
            val stream = if (status in 200..299) connection.inputStream else connection.errorStream
            val text = stream?.bufferedReader()?.use(BufferedReader::readText).orEmpty()
            val json = if (text.isBlank()) JSONObject() else JSONObject(text)
            if (status !in 200..299) {
                throw DirectoryException(describe(status, json.optString("error")))
            }
            return json
        } finally {
            connection.disconnect()
        }
    }

    /** Server messages are terse and English; turn the ones users hit into plain guidance. */
    private fun describe(status: Int, serverError: String): String = when {
        status == 401 && serverError.contains("login", true) -> "로그인이 필요합니다"
        status == 401 -> "아이디 또는 비밀번호가 맞지 않습니다"
        status == 404 -> "해당 호스트를 찾을 수 없습니다"
        status == 409 -> "호스트가 오프라인입니다"
        status == 429 -> "잠시 후 다시 시도해 주세요"
        serverError.isNotEmpty() -> serverError
        else -> "서버 오류 ($status)"
    }
}
