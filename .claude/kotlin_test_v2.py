import io

p = 'apps/android_direct_client/app/src/test/java/com/remote60/androiddirect/UpdateManifestTest.kt'
d = io.open(p, encoding='utf-8').read()

old = '''    @Test
    fun loadReturnsFieldsOnlyAfterTheSignaturePasses() {
        val ok = UpdateManifest.load(document, signatureHex, publicKeyHex, "windows")
        assertEquals(UpdateManifest.Status.Ok, ok.status)
        val fields = ok.fields
        assertNotNull(fields)
        assertEquals("0.2.105", fields!!.version)
        assertEquals("GNLinkSetup-0.2.105.exe", fields.artifact)
        assertEquals(3475968L, fields.size)
        assertEquals(64, fields.sha256.length)'''
new = '''    @Test
    fun loadReturnsFieldsOnlyAfterTheSignaturePasses() {
        // The shared vectors describe a WINDOWS release, so the arch it names is x64. Android
        // reading it is still the useful test: all three runtimes must agree on one artifact.
        val ok = UpdateManifest.load(document, signatureHex, publicKeyHex, "windows", "x64")
        assertEquals(UpdateManifest.Status.Ok, ok.status)
        val fields = ok.fields
        assertNotNull(fields)
        assertEquals("0.2.105", fields!!.version)
        assertEquals("r-0.2.105-test", fields.releaseId)
        assertEquals("x64", fields.arch)
        // Three artifacts of different sizes and contents, so nothing passes by treating them as
        // interchangeable -- and GNLinkSetup.exe is one, because the installer travels in its
        // own package.
        assertEquals(3, fields.artifacts.size)
        assertTrue("the Setup is a member",
            fields.artifacts.any { it.name == "GNLinkSetup.exe" })
        assertEquals("sizes must differ", 3, fields.artifacts.map { it.size }.toSet().size)
        assertTrue("every URL is https", fields.artifacts.all { it.url.startsWith("https://") })'''
assert old in d, 'kotlin happy path'
d = d.replace(old, new, 1)

d = d.replace('''        val bad = UpdateManifest.load(document, "ab".repeat(64), publicKeyHex, "windows")''',
              '''        val bad = UpdateManifest.load(document, "ab".repeat(64), publicKeyHex, "windows", "x64")''', 1)

d = d.replace('''        val r = UpdateManifest.load(garbage, "ab".repeat(64), publicKeyHex, "windows")''',
              '''        val r = UpdateManifest.load(garbage, "ab".repeat(64), publicKeyHex, "windows", "x64")''', 1)

old = '''    @Test
    fun platformMismatchIsNotAnError() {
        val r = UpdateManifest.load(document, signatureHex, publicKeyHex, "android")
        assertEquals(UpdateManifest.Status.WrongPlatform, r.status)
        assertNull("a manifest for another platform exposes nothing", r.fields)
    }'''
new = '''    @Test
    fun platformMismatchIsNotAnError() {
        val r = UpdateManifest.load(document, signatureHex, publicKeyHex, "android", "x64")
        assertEquals(UpdateManifest.Status.WrongPlatform, r.status)
        assertNull("a manifest for another platform exposes nothing", r.fields)
    }

    /** An architecture mismatch is the same kind of answer: not for this device, not an error. */
    @Test
    fun archMismatchIsNotAnError() {
        val r = UpdateManifest.load(document, signatureHex, publicKeyHex, "windows", "arm64")
        assertEquals(UpdateManifest.Status.WrongPlatform, r.status)
        assertNull(r.fields)
    }'''
assert old in d, 'kotlin platform'
d = d.replace(old, new, 1)

old = '''    @Test
    fun fieldRules() {
        assertEquals("missing schema", UpdateManifest.parseFields("platform=android\\n").second)
        assertEquals("line without '='", UpdateManifest.parseFields("schema=1\\nnonsense\\n").second)
        assertEquals("schema is not a number", UpdateManifest.parseFields("schema=x\\n").second)
        assertEquals("size is not a number", UpdateManifest.parseFields("schema=1\\nsize=big\\n").second)

        val (fields, err) = UpdateManifest.parseFields(
            "# comment\\n\\nschema=1\\nplatform=android\\nversionCode=12\\nfutureField=whatever\\n"
        )
        assertEquals("", err)
        assertNotNull(fields)
        assertEquals(1, fields!!.schema)
        assertEquals(12L, fields.versionCode)
    }'''
new = '''    @Test
    fun fieldRules() {
        assertEquals("missing schema", UpdateManifest.parseFields("platform=android\\n").second)
        assertEquals("line without '='", UpdateManifest.parseFields("schema=2\\nnonsense\\n").second)
        assertEquals("schema is not a number", UpdateManifest.parseFields("schema=x\\n").second)
        assertEquals(
            "artifact line needs name|size|sha256|url",
            UpdateManifest.parseFields("schema=2\\nartifact=a|1|onlythree\\n").second
        )
        assertEquals(
            "artifact size is not a number",
            UpdateManifest.parseFields("schema=2\\nartifact=a|big|x|y\\n").second
        )

        val (fields, err) = UpdateManifest.parseFields(
            "# comment\\n\\nschema=2\\nplatform=android\\nversionCode=12\\nfutureField=whatever\\n"
        )
        assertEquals("", err)
        assertNotNull(fields)
        assertEquals(2, fields!!.schema)
        assertEquals(12L, fields.versionCode)
    }

    /**
     * The artifact-list rules, each with a real signature so the only thing rejecting the case is
     * the rule under test.
     */
    @Test
    fun artifactListRules() {
        val generator = java.security.KeyPairGenerator.getInstance("EC")
        generator.initialize(java.security.spec.ECGenParameterSpec("secp256r1"))
        val pair = generator.generateKeyPair()
        val jwkPub = pair.public as java.security.interfaces.ECPublicKey
        fun pad(b: java.math.BigInteger): ByteArray {
            val raw = b.toByteArray()
            val out = ByteArray(32)
            val src = if (raw.size > 32) raw.copyOfRange(raw.size - 32, raw.size) else raw
            System.arraycopy(src, 0, out, 32 - src.size, src.size)
            return out
        }
        val keyHex = (pad(jwkPub.w.affineX) + pad(jwkPub.w.affineY))
            .joinToString("") { "%02x".format(it) }

        fun sign(doc: String): String {
            val der = java.security.Signature.getInstance("SHA256withECDSA").run {
                initSign(pair.private)
                update(doc.toByteArray(Charsets.UTF_8))
                sign()
            }
            return derToRaw(der)!!
        }

        val head = "schema=2\\nreleaseId=r\\nplatform=android\\narch=arm64\\nversion=1.0.0\\n"
        val hash = "0".repeat(64)
        val cases = listOf(
            Triple("well formed", "artifact=a.apk|1|$hash|https://u.example/a\\n",
                UpdateManifest.Status.Ok),
            Triple("no artifacts", "", UpdateManifest.Status.Malformed),
            Triple("zero size", "artifact=a.apk|0|$hash|https://u.example/a\\n",
                UpdateManifest.Status.Malformed),
            Triple("uppercase sha256", "artifact=a.apk|1|${"A".repeat(64)}|https://u.example/a\\n",
                UpdateManifest.Status.Malformed),
            Triple("http url", "artifact=a.apk|1|$hash|http://u.example/a\\n",
                UpdateManifest.Status.Malformed),
            Triple("url with credentials", "artifact=a.apk|1|$hash|https://evil@u.example/a\\n",
                UpdateManifest.Status.Malformed),
        )
        for ((name, body, expect) in cases) {
            val doc = head + body
            val r = UpdateManifest.load(doc, sign(doc), keyHex, "android", "arm64")
            assertEquals(name, expect, r.status)
            if (expect != UpdateManifest.Status.Ok) assertNull(name, r.fields)
        }

        // Schema 1 is no longer accepted.
        val old = "schema=1\\nreleaseId=r\\nplatform=android\\narch=arm64\\nversion=1\\n" +
            "artifact=a.apk|1|$hash|https://u.example/a\\n"
        assertEquals(
            UpdateManifest.Status.UnsupportedSchema,
            UpdateManifest.load(old, sign(old), keyHex, "android", "arm64").status
        )
    }'''
assert old in d, 'kotlin field rules'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('Kotlin test moved to schema 2')
