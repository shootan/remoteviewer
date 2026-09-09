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
        val punchToken: String,
        /**
         * Every address the host may be reachable at, most-preferred first, as "ip:port|kind".
         *
         * One address cannot serve two networks that filter in opposite directions: a residential
         * ISP blocks the well-known port inbound, a company Wi-Fi blocks the high one outbound.
         * The client cannot tell which side it is on from here -- both look like silence -- so it
         * tries them all and keeps whichever answers.
         *
         * Falls back to the single [ip]:[port] when talking to a directory that predates this.
         */
        val candidates: List<String>,
    )

    /**
     * [status] is the http status when the server answered at all, and [serverError] the name it
     * gave. The phone needs both: a 409 saying the directory has no address observation is a
     * thing this end can repair, while every other 409 is not, and they used to be told apart by
     * the Korean sentence shown to the user.
     */
    class DirectoryException(
        message: String,
        val status: Int = 0,
        val serverError: String = "",
    ) : Exception(message)

    /**
     * Where the directory says address observations should be sent.
     *
     * [known] false means the server said nothing -- an older directory, which is a documented
     * state and not an error. The port rule below decides what that means.
     */
    data class ObserveEndpoint(
        val known: Boolean = false,
        val port: Int = 0,
        val host: String = "",
        val hostRejected: Boolean = false,
    )

    /** Whether the health probe got an answer at all, and what it said if it did. */
    data class HealthObserve(
        val reached: Boolean,
        val advertised: ObserveEndpoint,
        val error: String = "",
    )

    data class LoginResult(
        val sessionToken: String,
        val expiresAt: Long,
        val advertised: ObserveEndpoint,
    )

    private const val PREFS = "remote60_directory"
    private const val KEY_URL = "url"
    private const val KEY_ACCOUNT = "accountId"
    private const val KEY_SESSION = "sessionToken"
    private const val KEY_EXPIRES = "expiresAt"

    private val CONNECT_TIMEOUT_MS = TimeUnit.SECONDS.toMillis(8).toInt()
    private val READ_TIMEOUT_MS = TimeUnit.SECONDS.toMillis(10).toInt()

    /**
     * Where to send the address probe.
     *
     * The advertised port wins: the server knows which port it listens on and nothing here does.
     * Absent that, one above the http port -- the documented relationship between the server's
     * two ports, and what every current deployment is.
     *
     * On https there is no such default. 443 + 1 is 444, which nothing answers: the probe would
     * be sent into silence, the connect would go ahead without an observation, and the failure
     * would look like a network problem rather than a server that needs one setting. 0 means
     * refuse and say so.
     *
     * 65535 has the same answer for a different reason: +1 has nowhere to go, and the cast would
     * make it port 0.
     */
    fun observePortFor(advertised: ObserveEndpoint, httpPort: Int, secure: Boolean): Int = when {
        advertised.known -> advertised.port
        secure -> 0
        httpPort >= 65535 -> 0
        else -> httpPort + 1
    }

    /** The same rule, for a caller that has a url and whatever the directory advertised. */
    fun observePortFor(url: String, advertised: ObserveEndpoint = ObserveEndpoint()): Int =
        observePortFor(advertised, httpPortFor(url), urlIsSecure(url))

    /** The advertised host when it is usable, and the directory's own host otherwise. */
    fun observeHostFor(url: String, advertised: ObserveEndpoint = ObserveEndpoint()): String =
        if (advertised.host.isNotEmpty()) advertised.host else hostFor(url)

    /**
     * Whether a url asks for TLS.
     *
     * Compared without case and after any leading space, which is what a scheme is. The native
     * client had three of these and they disagreed on `HTTPS://`; this is the only one here.
     */
    fun urlIsSecure(url: String): Boolean =
        url.trimStart().startsWith("https://", ignoreCase = true)

    /**
     * Reads the observe endpoint out of a server response.
     *
     * The port is read from inside the "observe" object, not from anywhere a "port" happens to
     * appear, and only a whole number in 1..65535 counts -- a fractional or out-of-range value is
     * a server that needs fixing, not a port to dial.
     *
     * The host is checked here because the server does not check it. It is sent to whatever the
     * operator typed, and a value with a scheme, a path, a port or a space in it is not a host
     * name. A bad host does not discard a good port: [hostRejected] records the fallback so a
     * server-side mistake is findable instead of showing up as a timeout.
     */
    fun parseObserveMetadata(response: JSONObject): ObserveEndpoint {
        val observe = response.optJSONObject("observe") ?: return ObserveEndpoint()
        if (!observe.has("port")) return ObserveEndpoint()
        val port = exactPort(observe.opt("port")) ?: return ObserveEndpoint()
        val rawHost = observe.optString("host", "").trim()
        if (rawHost.isEmpty()) return ObserveEndpoint(known = true, port = port)
        return if (hostIsUsable(rawHost)) {
            ObserveEndpoint(known = true, port = port, host = rawHost)
        } else {
            ObserveEndpoint(known = true, port = port, hostRejected = true)
        }
    }

    /** Null unless the value is a whole number in 1..65535; 29181.5 and "29181" are not ports. */
    private fun exactPort(value: Any?): Int? {
        val number = value as? Number ?: return null
        val asDouble = number.toDouble()
        if (asDouble != Math.floor(asDouble) || asDouble < 1.0 || asDouble > 65535.0) return null
        return asDouble.toInt()
    }

    private fun hostIsUsable(host: String): Boolean {
        if (host.isEmpty() || host.length > 253) return false
        if (host.any { it.isWhitespace() || it.code < 0x21 || it.code > 0x7e }) return false
        if (host.contains("://") || host.contains('/') || host.contains(':')) return false
        if (host.endsWith(".")) return false
        return host.split('.').all { label ->
            label.isNotEmpty() && label.length <= 63 &&
                !label.startsWith("-") && !label.endsWith("-")
        }
    }

    /** Where the probe should go, or why it cannot go anywhere. */
    sealed class ObserveTarget {
        data class Ready(
            val host: String,
            val port: Int,
            val advertised: ObserveEndpoint,
        ) : ObserveTarget()

        data class Refused(val message: String) : ObserveTarget()
    }

    /**
     * Everything between "the user tapped a PC" and "send the probe here".
     *
     * A function rather than a stretch of the activity, because this is where the mistakes are:
     * three states arrive here -- already known, learned from the health route, and not learned
     * at all -- and two of them used to end up looking identical. [probe] is a parameter so the
     * decision can be exercised without a server, which is the only way the not-learned-at-all
     * branches get tested at all.
     *
     * Unreachable and silent are answered differently on purpose. On https, silent means "this
     * server needs one setting"; saying that about a server that is simply down sends whoever
     * reads it to configure a machine that is not the problem. On http neither matters -- the
     * documented default still applies -- so a failed probe there is not fatal.
     */
    fun resolveObserveTarget(
        url: String,
        cached: ObserveEndpoint = ObserveEndpoint(),
        probe: (String) -> HealthObserve = ::observeFromHealth,
    ): ObserveTarget {
        var advertised = cached
        if (!advertised.known) {
            val health = probe(url)
            if (!health.reached && urlIsSecure(url)) {
                return ObserveTarget.Refused(
                    "디렉터리 서버에 연결할 수 없습니다" +
                        if (health.error.isEmpty()) "" else " (${health.error})"
                )
            }
            advertised = health.advertised
        }
        val port = observePortFor(url, advertised)
        if (port == 0) {
            return ObserveTarget.Refused("이 디렉터리 서버에 주소 확인 포트가 설정되어 있지 않습니다")
        }
        return ObserveTarget.Ready(observeHostFor(url, advertised), port, advertised)
    }

    /**
     * Asks /healthz where observations go, for the path that never logs in.
     *
     * The metadata rides on the login response, so a phone that reopens with a stored session
     * would never see it -- and on an https directory that means refusing to observe, so
     * reconnecting would fail where a fresh sign-in works. This route needs no session.
     *
     * A directory that says nothing is not an error: absence has its own rule above.
     */
    fun observeFromHealth(url: String): HealthObserve =
        try {
            HealthObserve(true, parseObserveMetadata(get(url, "/healthz", null)))
        } catch (e: Exception) {
            // Reached or not is kept apart from said-nothing-or-not. They are different servers to
            // be looking at: one is down, the other needs a setting. Both used to arrive here as
            // an empty endpoint, and on https both would then be reported as a missing setting.
            HealthObserve(false, ObserveEndpoint(), e.message.orEmpty())
        }

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

    /** Public because the log uploader needs the same answer; two of these is how they drift. */
    fun normalizedUrl(url: String): String = normalize(url)

    /**
     * "scheme://host:port" for deciding whether two spellings mean the same server.
     *
     * A trailing slash and a written-out default port are the same server; http and https are
     * not. Used for comparison only -- what the user typed stays stored and shown, because
     * rewriting that under them is its own surprise.
     */
    fun originKey(url: String): String =
        try {
            val parsed = java.net.URL(normalize(url))
            val port = if (parsed.port > 0) parsed.port else if (parsed.protocol == "https") 443 else 80
            parsed.protocol.lowercase() + "://" + parsed.host.lowercase() + ":" + port
        } catch (e: Exception) {
            normalize(url).lowercase()
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

    fun login(url: String, id: String, password: String): LoginResult {
        val body = JSONObject().put("id", id).put("pw", password)
        val response = post(url, "/api/login", body, null)
        val token = response.optString("sessionToken")
        if (token.isEmpty()) throw DirectoryException("server did not return a session")
        // Optional and additive: an older directory does not send it, and that is a documented
        // state rather than an error -- see observePortFor.
        return LoginResult(token, response.optLong("expiresAt", 0L), parseObserveMetadata(response))
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
        val punchToken = response.optString("punchToken")
        if (punchToken.length != 32) throw DirectoryException("connection authorization unavailable")

        val candidates = mutableListOf<String>()
        val array = response.optJSONArray("candidates")
        if (array != null) {
            for (i in 0 until array.length()) {
                val entry = array.optJSONObject(i) ?: continue
                val candidateIp = entry.optString("ip")
                val candidatePort = entry.optInt("port")
                if (candidateIp.isEmpty() || candidatePort <= 0 || candidatePort > 65535) continue
                val kind = entry.optString("kind", "unknown")
                candidates.add("$candidateIp:$candidatePort|$kind")
            }
        }
        // An older directory sends no list; the single address it does send is still valid.
        if (candidates.isEmpty()) candidates.add("$ip:$port|public")
        return ConnectTarget(ip, port, punchToken, candidates)
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
                val serverError = json.optString("error")
                throw DirectoryException(describe(status, serverError), status, serverError)
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
        // Every 409 used to be "the host is offline". The directory now also answers 409 when it
        // has no address observation for this phone, which is a different thing entirely and one
        // the phone can fix by sending another -- telling the user their PC is off would send
        // them to the wrong machine.
        status == 409 && serverError.startsWith("observation") ->
            "주소 확인 정보가 없어 다시 시도합니다"
        status == 409 -> "호스트가 오프라인입니다"
        status == 429 -> "잠시 후 다시 시도해 주세요"
        serverError.isNotEmpty() -> serverError
        else -> "서버 오류 ($status)"
    }
}
