import io

A = 'apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/'

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

# ------------------------------------------------------------------ the snapshot
edit(A + 'DirectoryClient.kt', [
 ('''    /** Where the probe should go, or why it cannot go anywhere. */''',
  '''    /**
     * One update fetch, decided once, where the answers are known.
     *
     * A snapshot rather than loose values: reading the url back out of stored state later and
     * deciding *then* whether to attach a credential is the shape of the mistake. Between the
     * decision and the send, the account can change and the server address can change.
     *
     * [derived] selects the wire shape and is not inferred from a response -- our directory
     * answers one object carrying both fields, an operator's own url is a document with a
     * detached `url.sig` beside it, and a truncated answer of one kind looks like a valid answer
     * of the other.
     *
     * [credentialHeader] may only be sent to [origin]. An override gets none even when it points
     * at the same host: what decides is where the url came from, not what it looks like.
     */
    data class UpdateEndpoint(
        val url: String = "",
        val derived: Boolean = false,
        val credentialHeader: String = "",
        val origin: String = "",
        val ownerEpoch: Long = 0,
    ) {
        val configured: Boolean get() = url.isNotEmpty()

        /**
         * Whether this credential may go to [targetUrl] -- the destination of the request about
         * to be made, not the one this snapshot was built from. A signed manifest says its
         * artifacts are the bytes signed for; it says nothing about whether our directory's
         * credential belongs to the host they are published on.
         */
        fun credentialAllowedFor(targetUrl: String): Boolean =
            derived && credentialHeader.isNotEmpty() && origin.isNotEmpty() &&
                targetUrl.isNotEmpty() && originKey(targetUrl) == origin
    }

    /** Builds that snapshot: override wins and carries no credential, otherwise derived. */
    fun updateEndpointFor(
        override: String,
        directoryUrl: String,
        sessionToken: String = "",
        platform: String = "android",
        ownerEpoch: Long = 0,
    ): UpdateEndpoint {
        if (override.isNotBlank()) return UpdateEndpoint(override, false, "", "", ownerEpoch)
        val url = updateManifestUrlFor(directoryUrl, platform)
        if (url.isEmpty()) return UpdateEndpoint(ownerEpoch = ownerEpoch)
        return UpdateEndpoint(
            url = url,
            derived = true,
            credentialHeader =
                if (sessionToken.isBlank()) "" else "Authorization: Bearer $sessionToken",
            origin = originKey(directoryUrl),
            ownerEpoch = ownerEpoch,
        )
    }

    /** Where the probe should go, or why it cannot go anywhere. */'''),
])

# ------------------------------------------------------------------ the fetch
edit(A + 'UpdateFlow.kt', [
 ('''    private fun openHttps(url: String): HttpsURLConnection? {
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
        openHttps(url)?.use { connection ->''',
  '''    private fun openHttps(url: String, credentialHeader: String = ""): HttpsURLConnection? {
        val connection = URL(url).openConnection()
        if (connection !is HttpsURLConnection) return null
        connection.connectTimeout = 15_000
        connection.readTimeout = 30_000
        // Redirects are followed by the platform, but never from https to http: that downgrade is
        // refused by HttpsURLConnection itself, which is the behaviour we want and the reason no
        // custom redirect handling is written here.
        //
        // A request carrying a credential follows nothing at all. A redirect asks for this
        // request to be repeated at an address chosen by whoever answered, and that is the one
        // party a credential must not be handed to on request. https://elsewhere is still https,
        // so the downgrade rule alone does not cover it.
        connection.instanceFollowRedirects = credentialHeader.isEmpty()
        if (credentialHeader.isNotEmpty()) {
            val split = credentialHeader.indexOf(':')
            if (split > 0) {
                connection.setRequestProperty(
                    credentialHeader.substring(0, split).trim(),
                    credentialHeader.substring(split + 1).trim(),
                )
            }
        }
        return connection
    }

    private fun fetchText(url: String, credentialHeader: String = ""): String? = try {
        openHttps(url, credentialHeader)?.use { connection ->'''),
 ('''    fun check(
        manifestUrl: String,
        trustedPublicKeyHex: String,
        installedVersionCode: Long,
    ): Outcome {
        if (manifestUrl.isEmpty() || trustedPublicKeyHex.isEmpty()) {''',
  '''    fun check(
        endpoint: DirectoryClient.UpdateEndpoint,
        trustedPublicKeyHex: String,
        installedVersionCode: Long,
    ): Outcome {
        val manifestUrl = endpoint.url
        if (manifestUrl.isEmpty() || trustedPublicKeyHex.isEmpty()) {'''),
 ('''        val body = fetchText(manifestUrl) ?: return Outcome(
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

        return evaluateDocument(document, signature, trustedPublicKeyHex, installedVersionCode)''',
  '''        // The credential goes to the destination of THIS request, judged now, or nowhere.
        val credential =
            if (endpoint.credentialAllowedFor(manifestUrl)) endpoint.credentialHeader else ""

        if (!endpoint.derived) {
            // An operator's own url: two files, and nothing sent with either. This is somebody
            // else's server by definition.
            val document = fetchText(manifestUrl) ?: return Outcome(
                UpdateDecision.Verdict.NotForUs, null, "", "could not reach the update server")
            val signature = fetchText("$manifestUrl.sig")?.trim() ?: return Outcome(
                UpdateDecision.Verdict.NotForUs, null, "", "could not fetch the signature")
            return evaluateDocument(document, signature, trustedPublicKeyHex, installedVersionCode)
        }

        // Our directory: one response carrying both. It used to ask for "$manifestUrl.sig" here
        // too, which the server has never served -- its route is
        // /api/update/manifest?platform=android, so appending ".sig" lands on a query value, not
        // a file. Nobody noticed because the url came from a build constant that was empty, so
        // this line never ran. Deriving the url is what made it reachable.
        val body = fetchText(manifestUrl, credential) ?: return Outcome(
            UpdateDecision.Verdict.NotForUs, null, "", "could not reach the update server")
        val document = jsonStringField(body, "manifest") ?: return Outcome(
            UpdateDecision.Verdict.NotForUs, null, "", "the update server sent no manifest")
        val signature = jsonStringField(body, "signature")?.trim() ?: return Outcome(
            UpdateDecision.Verdict.NotForUs, null, "", "the update server sent no signature")

        return evaluateDocument(document, signature, trustedPublicKeyHex, installedVersionCode)'''),
])
