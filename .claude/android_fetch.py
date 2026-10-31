import io

p = 'apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/UpdateFlow.kt'
d = io.open(p, encoding='utf-8').read()

old = '''        val document = fetchText(manifestUrl) ?: return Outcome(
            UpdateDecision.Verdict.NotForUs, null, "", "could not reach the update server")
        val signature = fetchText("$manifestUrl.sig")?.trim() ?: return Outcome(
            UpdateDecision.Verdict.NotForUs, null, "", "could not fetch the signature")

        return evaluateDocument(document, signature, trustedPublicKeyHex, installedVersionCode)'''

new = '''        val body = fetchText(manifestUrl) ?: return Outcome(
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

        return evaluateDocument(document, signature, trustedPublicKeyHex, installedVersionCode)'''
assert d.count(old) == 1, 'fetch shape'
d = d.replace(old, new, 1)

anchor = '''    /**
     * The gates between a fetched document and a decision to install.'''
helper = '''    /**
     * One string field out of the server's small response.
     *
     * By hand rather than with JSONObject, for the same reason the Windows client parses it by
     * hand: the document's exact bytes decide whether its signature verifies, so the extraction
     * has to be something that can be read and checked rather than trusted. Only the escapes a
     * manifest actually contains are decoded.
     */
    internal fun jsonStringField(body: String, key: String): String? {
        val needle = "\\"" + key + "\\":\\""
        val at = body.indexOf(needle)
        if (at < 0) return null
        val out = StringBuilder()
        var i = at + needle.length
        while (i < body.length && body[i] != '"') {
            if (body[i] == '\\\\' && i + 1 < body.length) {
                when (body[i + 1]) {
                    'n' -> out.append('\\n')
                    'r' -> out.append('\\r')
                    't' -> out.append('\\t')
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

''' + anchor
assert d.count(anchor) == 1, 'helper anchor'
d = d.replace(anchor, helper, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok update flow')
