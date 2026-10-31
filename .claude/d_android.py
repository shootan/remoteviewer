import io

A = 'apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/'

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

edit(A + 'DirectoryClient.kt', [
 # ---------------------------------------------------------------- the contract
 ('''    class DirectoryException(message: String) : Exception(message)''',
  '''    /**
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

    data class LoginResult(
        val sessionToken: String,
        val expiresAt: Long,
        val advertised: ObserveEndpoint,
    )'''),
 # ---------------------------------------------------------------- the port rule
 ('''    /**
     * Default UDP probe port, matching the server's own relationship between its two ports.
     * 0 when the http port is the last one: +1 has nowhere to go, and a url on 65535 has to name
     * its observe port itself rather than have us dial 65536 (or, cast narrower, port 0).
     */
    fun observePortFor(url: String): Int {
        val http = httpPortFor(url)
        return if (http >= 65535) 0 else http + 1
    }''',
  '''    /**
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

    /**
     * Asks /healthz where observations go, for the path that never logs in.
     *
     * The metadata rides on the login response, so a phone that reopens with a stored session
     * would never see it -- and on an https directory that means refusing to observe, so
     * reconnecting would fail where a fresh sign-in works. This route needs no session.
     *
     * A directory that says nothing is not an error: absence has its own rule above.
     */
    fun observeFromHealth(url: String): ObserveEndpoint =
        try {
            parseObserveMetadata(get(url, "/healthz", null))
        } catch (e: Exception) {
            ObserveEndpoint()
        }'''),
 # ---------------------------------------------------------------- login carries it
 ('''    fun login(url: String, id: String, password: String): Pair<String, Long> {
        val body = JSONObject().put("id", id).put("pw", password)
        val response = post(url, "/api/login", body, null)
        val token = response.optString("sessionToken")
        if (token.isEmpty()) throw DirectoryException("server did not return a session")
        return token to response.optLong("expiresAt", 0L)
    }''',
  '''    fun login(url: String, id: String, password: String): LoginResult {
        val body = JSONObject().put("id", id).put("pw", password)
        val response = post(url, "/api/login", body, null)
        val token = response.optString("sessionToken")
        if (token.isEmpty()) throw DirectoryException("server did not return a session")
        // Optional and additive: an older directory does not send it, and that is a documented
        // state rather than an error -- see observePortFor.
        return LoginResult(token, response.optLong("expiresAt", 0L), parseObserveMetadata(response))
    }'''),
 # ---------------------------------------------------------------- errors carry the status
 ('''            if (status !in 200..299) {
                throw DirectoryException(describe(status, json.optString("error")))
            }''',
  '''            if (status !in 200..299) {
                val serverError = json.optString("error")
                throw DirectoryException(describe(status, serverError), status, serverError)
            }'''),
 ('''        status == 404 -> "해당 호스트를 찾을 수 없습니다"
        status == 409 -> "호스트가 오프라인입니다"''',
  '''        status == 404 -> "해당 호스트를 찾을 수 없습니다"
        // Every 409 used to be "the host is offline". The directory now also answers 409 when it
        // has no address observation for this phone, which is a different thing entirely and one
        // the phone can fix by sending another -- telling the user their PC is off would send
        // them to the wrong machine.
        status == 409 && serverError.startsWith("observation") ->
            "주소 확인 정보가 없어 다시 시도합니다"
        status == 409 -> "호스트가 오프라인입니다"'''),
 # ---------------------------------------------------------------- one normalizer, shared
 ('''    private fun normalize(url: String): String {''',
  '''    /** Public because the log uploader needs the same answer; two of these is how they drift. */
    fun normalizedUrl(url: String): String = normalize(url)

    private fun normalize(url: String): String {'''),
])

# ------------------------------------------------------------------ the log uploader
edit(A + 'LogUploader.kt', [
 ('''            val base = if (directoryUrl.startsWith("http")) directoryUrl else "http://$directoryUrl"''',
  '''            // The same normalisation the rest of the app uses, rather than a second one here.
            // The old test was startsWith("http"), which is true of "https://" and also of
            // "httpx://" -- and for anything else it prepended http:// itself, so this file
            // decided the scheme for a url the directory client had already decided about. A
            // session token goes out in a header on this request; which scheme carries it is not
            // a question two places should answer separately.
            val base = DirectoryClient.normalizedUrl(directoryUrl)'''),
])
