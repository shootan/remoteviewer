package com.remote60.androiddirect

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.security.KeyPairGenerator
import java.security.Signature
import java.security.spec.ECGenParameterSpec

/**
 * The Android side of the manifest, checked against the same fixed vectors the C++ and server
 * suites verify.
 *
 * The point is cross-implementation agreement on one artifact: this must accept exactly the
 * signature Windows accepts, over exactly the same bytes, and reject the same tampering. Three
 * implementations agreeing on a shared vector is a much stronger statement than any one of them
 * agreeing with itself.
 *
 * Plain JVM unit test -- the manifest is string and crypto work and needs no device.
 */
class UpdateManifestTest {

    private fun vectorsDir(): File {
        // An override, so this can be pointed at a fixture without touching the shared vectors.
        //
        // The C++ suite takes the directory as argv[1] and the server suite as argv[2]; this one
        // could only ever read apps/shared/update_manifest, which meant checking a newly created
        // signing key here would have required editing the vectors the three runtimes agree on --
        // and those exist precisely so that nobody edits them to make something pass.
        // ⚠️ Gradle must be run with --no-daemon for this to be seen. Test workers are forked
        // from the daemon, and a daemon started earlier keeps the environment it started with --
        // so setting the variable and running normally gives a PASS that used the shared vectors
        // and never looked at the override at all. That happened, and it was caught only by
        // pointing the variable at a directory that does not exist and checking the run FAILS.
        // If it passes with a bad path, the override is not in effect and any result is about
        // some other key.
        val override = System.getenv("GNLINK_UPDATE_VECTORS")
        if (!override.isNullOrBlank()) {
            val chosen = File(override)
            if (chosen.isDirectory) return chosen
            throw AssertionError("GNLINK_UPDATE_VECTORS is set but is not a directory: $override")
        }
        var dir: File? = File(System.getProperty("user.dir") ?: ".").absoluteFile
        val tried = mutableListOf<String>()
        while (dir != null) {
            val candidate = File(dir, "apps/shared/update_manifest")
            tried += candidate.path
            if (candidate.isDirectory) return candidate
            dir = dir.parentFile
        }
        throw AssertionError("shared update_manifest vectors not found; tried:\n" + tried.joinToString("\n"))
    }

    // Read as bytes then decoded, because the signature covers exact bytes and nothing may
    // translate line endings on the way in.
    private val dir by lazy { vectorsDir() }
    private val document by lazy { String(File(dir, "test_manifest.txt").readBytes(), Charsets.UTF_8) }
    private val signatureHex by lazy { File(dir, "test_manifest.sig").readText().trim() }
    private val publicKeyHex by lazy { File(dir, "test_public_key.txt").readText().trim() }

    @Test
    fun vectorsHaveTheExpectedShape() {
        assertEquals(128, signatureHex.length)
        assertEquals(128, publicKeyHex.length)
        assertTrue(document.contains("version=0.2.105"))
    }

    @Test
    fun verifiesTheSignatureWindowsAlsoVerifies() {
        assertTrue("the genuine signature must verify",
            UpdateManifest.verifySignature(document, signatureHex, publicKeyHex))
    }

    @Test
    fun rejectsTampering() {
        assertFalse("a changed document",
            UpdateManifest.verifySignature(document.replace("0.2.105", "0.2.106"),
                signatureHex, publicKeyHex))

        val flipped = signatureHex.toCharArray()
        flipped[0] = if (flipped[0] == '0') '1' else '0'
        assertFalse("a changed signature",
            UpdateManifest.verifySignature(document, String(flipped), publicKeyHex))

        val otherKey = publicKeyHex.toCharArray()
        otherKey[0] = if (otherKey[0] == '0') '1' else '0'
        assertFalse("a changed key",
            UpdateManifest.verifySignature(document, signatureHex, String(otherKey)))

        assertFalse("a short signature",
            UpdateManifest.verifySignature(document, "ab".repeat(31), publicKeyHex))
        assertFalse("non-hex", UpdateManifest.verifySignature(document, "z".repeat(128), publicKeyHex))
        assertFalse("empty", UpdateManifest.verifySignature(document, "", ""))
    }

    /** The case that catches a verifier checking shape rather than provenance. */
    @Test
    fun rejectsAValidSignatureFromTheWrongKey() {
        val generator = KeyPairGenerator.getInstance("EC")
        generator.initialize(ECGenParameterSpec("secp256r1"))
        val pair = generator.generateKeyPair()

        val der = Signature.getInstance("SHA256withECDSA").run {
            initSign(pair.private)
            update(document.toByteArray(Charsets.UTF_8))
            sign()
        }
        // JCA signs to DER; the wire format is raw r||s, so it has to be unwrapped to be offered
        // to our verifier at all. Doing it here also exercises the inverse of the production path.
        val raw = derToRaw(der)
        assertNotNull("DER signature should unwrap", raw)
        assertFalse("a signature from another key must not pass under our key",
            UpdateManifest.verifySignature(document, raw!!, publicKeyHex))
    }

    /** DER SEQUENCE{INTEGER r, INTEGER s} back to 64-byte raw r||s hex. Test-side only. */
    private fun derToRaw(der: ByteArray): String? {
        var i = 0
        if (der[i++] != 0x30.toByte()) return null
        i++ // sequence length
        fun readInt(): ByteArray? {
            if (der[i++] != 0x02.toByte()) return null
            val len = der[i++].toInt() and 0xFF
            val v = der.copyOfRange(i, i + len)
            i += len
            val out = ByteArray(32)
            val src = if (v.size > 32) v.copyOfRange(v.size - 32, v.size) else v
            System.arraycopy(src, 0, out, 32 - src.size, src.size)
            return out
        }
        val r = readInt() ?: return null
        val s = readInt() ?: return null
        return (r + s).joinToString("") { "%02x".format(it) }
    }

    @Test
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
        assertTrue("every URL is https", fields.artifacts.all { it.url.startsWith("https://") })

        val bad = UpdateManifest.load(document, "ab".repeat(64), publicKeyHex, "windows", "x64")
        assertEquals(UpdateManifest.Status.SignatureInvalid, bad.status)
        assertNull("no fields are exposed without a valid signature", bad.fields)
    }

    /**
     * The ordering test. Both malformed and badly signed must report SignatureInvalid -- a
     * parse-first implementation would say Malformed instead.
     */
    @Test
    fun signatureIsCheckedBeforeParsing() {
        val garbage = "not a manifest\nno equals here\n"
        val r = UpdateManifest.load(garbage, "ab".repeat(64), publicKeyHex, "windows", "x64")
        assertEquals(UpdateManifest.Status.SignatureInvalid, r.status)
        assertNull(r.fields)
    }

    @Test
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
    }

    @Test
    fun fieldRules() {
        assertEquals("missing schema", UpdateManifest.parseFields("platform=android\n").second)
        assertEquals("line without '='", UpdateManifest.parseFields("schema=2\nnonsense\n").second)
        assertEquals("schema is not a number", UpdateManifest.parseFields("schema=x\n").second)
        assertEquals(
            "artifact line needs name|size|sha256|url",
            UpdateManifest.parseFields("schema=2\nartifact=a|1|onlythree\n").second
        )
        assertEquals(
            "artifact size is not a number",
            UpdateManifest.parseFields("schema=2\nartifact=a|big|x|y\n").second
        )

        val (fields, err) = UpdateManifest.parseFields(
            "# comment\n\nschema=2\nplatform=android\nversionCode=12\nfutureField=whatever\n"
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

        val head = "schema=2\nreleaseId=r\nplatform=android\narch=arm64\nversion=1.0.0\n"
        val hash = "0".repeat(64)
        val cases = listOf(
            Triple("well formed", "artifact=a.apk|1|$hash|https://u.example/a\n",
                UpdateManifest.Status.Ok),
            Triple("no artifacts", "", UpdateManifest.Status.Malformed),
            Triple("zero size", "artifact=a.apk|0|$hash|https://u.example/a\n",
                UpdateManifest.Status.Malformed),
            Triple("uppercase sha256", "artifact=a.apk|1|${"A".repeat(64)}|https://u.example/a\n",
                UpdateManifest.Status.Malformed),
            Triple("http url", "artifact=a.apk|1|$hash|http://u.example/a\n",
                UpdateManifest.Status.Malformed),
            Triple("url with credentials", "artifact=a.apk|1|$hash|https://evil@u.example/a\n",
                UpdateManifest.Status.Malformed),
        )
        for ((name, body, expect) in cases) {
            val doc = head + body
            val r = UpdateManifest.load(doc, sign(doc), keyHex, "android", "arm64")
            assertEquals(name, expect, r.status)
            if (expect != UpdateManifest.Status.Ok) assertNull(name, r.fields)
        }

        // Schema 1 is no longer accepted.
        val old = "schema=1\nreleaseId=r\nplatform=android\narch=arm64\nversion=1\n" +
            "artifact=a.apk|1|$hash|https://u.example/a\n"
        assertEquals(
            UpdateManifest.Status.UnsupportedSchema,
            UpdateManifest.load(old, sign(old), keyHex, "android", "arm64").status
        )
    }

    /**
     * The release key is compiled in now, and the same safety property is asserted from the other
     * side: the shipped key must refuse a document signed by any other key. The shared vectors are
     * signed by exactly such a key, so they are the right thing to hand it.
     *
     * This used to assert the key was empty. That was a statement about a temporary state, and
     * keeping it would now mean asserting the app cannot check updates at all.
     *
     * The two encodings are easy to swap and the mistake is silent: the SPKI form decodes and then
     * fails a length check inside the verifier, so every update is refused with no error anywhere.
     */
    @Test
    fun theShippedKeyIsRawXyAndRefusesOtherKeys() {
        val key = UpdateManifest.trustedPublicKeyHex()
        assertTrue("a release key is compiled in", key.isNotEmpty())
        assertEquals("raw X||Y is 64 bytes", 128, key.length)
        assertTrue("lower-case hex and nothing else", key.all { it in "0123456789abcdef" })
        // 182 would be the 91-byte SPKI DER form -- the value that silently disables every check.
        assertTrue("not the SPKI form", key.length != 182)

        assertFalse("the shipped key must not verify a document signed by another key",
            UpdateManifest.verifySignature(document, signatureHex, key))
    }
}
