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
        val ok = UpdateManifest.load(document, signatureHex, publicKeyHex, "windows")
        assertEquals(UpdateManifest.Status.Ok, ok.status)
        val fields = ok.fields
        assertNotNull(fields)
        assertEquals("0.2.105", fields!!.version)
        assertEquals("GNLinkSetup-0.2.105.exe", fields.artifact)
        assertEquals(3475968L, fields.size)
        assertEquals(64, fields.sha256.length)

        val bad = UpdateManifest.load(document, "ab".repeat(64), publicKeyHex, "windows")
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
        val r = UpdateManifest.load(garbage, "ab".repeat(64), publicKeyHex, "windows")
        assertEquals(UpdateManifest.Status.SignatureInvalid, r.status)
        assertNull(r.fields)
    }

    @Test
    fun platformMismatchIsNotAnError() {
        val r = UpdateManifest.load(document, signatureHex, publicKeyHex, "android")
        assertEquals(UpdateManifest.Status.WrongPlatform, r.status)
        assertNull("a manifest for another platform exposes nothing", r.fields)
    }

    @Test
    fun fieldRules() {
        assertEquals("missing schema", UpdateManifest.parseFields("platform=android\n").second)
        assertEquals("line without '='", UpdateManifest.parseFields("schema=1\nnonsense\n").second)
        assertEquals("schema is not a number", UpdateManifest.parseFields("schema=x\n").second)
        assertEquals("size is not a number", UpdateManifest.parseFields("schema=1\nsize=big\n").second)

        val (fields, err) = UpdateManifest.parseFields(
            "# comment\n\nschema=1\nplatform=android\nversionCode=12\nfutureField=whatever\n"
        )
        assertEquals("", err)
        assertNotNull(fields)
        assertEquals(1, fields!!.schema)
        assertEquals(12L, fields.versionCode)
    }

    /**
     * No release key is compiled in, and the Android side must be fail-closed exactly as Windows
     * is. A placeholder that happened to verify something would be worse than no key at all, so
     * this fails loudly if one appears.
     */
    @Test
    fun noReleaseKeyIsCompiledIn() {
        assertEquals("", UpdateManifest.trustedPublicKeyHex())
        assertFalse("the empty trusted key must verify nothing",
            UpdateManifest.verifySignature(document, signatureHex,
                UpdateManifest.trustedPublicKeyHex()))
    }
}
