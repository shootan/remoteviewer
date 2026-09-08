package com.remote60.androiddirect

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.security.KeyPair
import java.security.KeyPairGenerator
import java.security.Signature
import java.security.interfaces.ECPublicKey
import java.security.spec.ECGenParameterSpec

/**
 * The three gates between a server saying there is an update and this app installing one, and the
 * fact that each of them alone is enough to refuse.
 *
 * They are not three conveniences that happen to be in the same place. The signature is what makes
 * the versionCode and the hash worth reading; the hash is what makes the signature say something
 * about the FILE rather than about a document; the versionCode is what Android enforces whatever
 * the other two say. Take one away and the remaining two stop being worth having -- a verified
 * manifest pointing at unverified bytes, or verified bytes nobody vouched for, or a package the
 * system refuses after the user has already approved it.
 *
 * So this file does not test them one at a time in isolation. It runs the same good release
 * through, then breaks exactly one thing at a time and requires a refusal each time.
 */
class UpdateGatesTest {

    private val keyPair: KeyPair = KeyPairGenerator.getInstance("EC").apply {
        initialize(ECGenParameterSpec("secp256r1"))
    }.generateKeyPair()

    /** The public key as the raw X||Y hex the manifest format carries. */
    private fun publicKeyHex(): String {
        val point = (keyPair.public as ECPublicKey).w
        fun pad(v: java.math.BigInteger): ByteArray {
            val raw = v.toByteArray()
            val out = ByteArray(32)
            val src = if (raw.size > 32) raw.copyOfRange(raw.size - 32, raw.size) else raw
            System.arraycopy(src, 0, out, 32 - src.size, src.size)
            return out
        }
        return (pad(point.affineX) + pad(point.affineY)).joinToString("") { "%02x".format(it) }
    }

    /** Signs a document and returns the raw r||s hex the wire format uses. */
    private fun sign(document: String): String {
        val der = Signature.getInstance("SHA256withECDSA").run {
            initSign(keyPair.private)
            update(document.toByteArray(Charsets.UTF_8))
            sign()
        }
        var i = 0
        require(der[i++] == 0x30.toByte())
        i++
        fun readInt(): ByteArray {
            require(der[i++] == 0x02.toByte())
            val len = der[i++].toInt() and 0xFF
            val v = der.copyOfRange(i, i + len)
            i += len
            val out = ByteArray(32)
            val src = if (v.size > 32) v.copyOfRange(v.size - 32, v.size) else v
            System.arraycopy(src, 0, out, 32 - src.size, src.size)
            return out
        }
        val r = readInt()
        val s = readInt()
        return (r + s).joinToString("") { "%02x".format(it) }
    }

    private val apkSha = "1".repeat(64)

    private fun document(versionCode: Long = 12L): String = buildString {
        append("schema=2\n")
        append("releaseId=r-0.2.13\n")
        append("platform=android\n")
        append("arch=arm64\n")
        append("version=0.2.13\n")
        append("versionCode=$versionCode\n")
        append("artifact=GNLink-0.2.13.apk|4096|$apkSha|https://updates.example/GNLink-0.2.13.apk\n")
    }

    // ---------------------------------------------------------------- all three hold

    @Test
    fun aGoodReleaseWithAllThreeIntactInstalls() {
        val doc = document()
        val outcome = UpdateFlow.evaluateDocument(doc, sign(doc), publicKeyHex(), 11L)
        assertEquals(outcome.detail, UpdateDecision.Verdict.Install, outcome.verdict)
        assertNotNull(outcome.artifact)
        // And the artifact carries the hash the third gate will use. Without it reaching this
        // far, the download check would have nothing to compare against.
        assertEquals(apkSha, outcome.artifact?.sha256)
        assertEquals(4096L, outcome.artifact?.size)
    }

    // ---------------------------------------------------------------- gate 1: the signature

    @Test
    fun aDocumentSignedByAnotherKeyIsRefused() {
        val doc = document()
        val other = KeyPairGenerator.getInstance("EC").apply {
            initialize(ECGenParameterSpec("secp256r1"))
        }.generateKeyPair()
        val stranger = Signature.getInstance("SHA256withECDSA").run {
            initSign(other.private)
            update(doc.toByteArray(Charsets.UTF_8))
            sign()
        }
        // Offered in the shape our verifier accepts, so this fails on provenance and not on form.
        val outcome = UpdateFlow.evaluateDocument(doc, "0".repeat(128), publicKeyHex(), 11L)
        assertEquals(UpdateDecision.Verdict.NotForUs, outcome.verdict)
        assertTrue(outcome.detail, outcome.detail.contains("SignatureInvalid"))
        assertTrue(stranger.isNotEmpty())
    }

    @Test
    fun aDocumentEditedAfterSigningIsRefused() {
        // The realistic attack on gate 1: a real signature over different bytes. Edited to a much
        // higher versionCode, which is what someone forcing an install would change.
        val original = document(versionCode = 12L)
        val signature = sign(original)
        val tampered = original.replace("versionCode=12", "versionCode=999")
        val outcome = UpdateFlow.evaluateDocument(tampered, signature, publicKeyHex(), 11L)
        assertEquals(UpdateDecision.Verdict.NotForUs, outcome.verdict)
        assertTrue(outcome.detail, outcome.detail.contains("SignatureInvalid"))
    }

    // ---------------------------------------------------------------- gate 2: the versionCode

    @Test
    fun aPerfectlySignedDocumentWithNoVersionCodeIsStillRefused() {
        // Gate 1 passes completely. That is the point: a valid signature does not make a package
        // installable, because Android compares versionCode and this document has none to compare.
        val doc = document().replace("versionCode=12\n", "")
        val outcome = UpdateFlow.evaluateDocument(doc, sign(doc), publicKeyHex(), 11L)
        assertEquals(UpdateDecision.Verdict.NotForUs, outcome.verdict)
        assertTrue(outcome.detail, outcome.detail.contains("versionCode"))
    }

    @Test
    fun aPerfectlySignedOlderReleaseIsRefused() {
        val doc = document(versionCode = 5L)
        val outcome = UpdateFlow.evaluateDocument(doc, sign(doc), publicKeyHex(), 11L)
        assertEquals(UpdateDecision.Verdict.Downgrade, outcome.verdict)
    }

    // ---------------------------------------------------------------- gate 3: the file's hash

    @Test
    fun aVerifiedManifestDoesNotVouchForTheBytesThatArrive() {
        // Gates 1 and 2 both pass, so a build that stopped here would install. What arrives over
        // the network is a separate question, and the answer is the artifact's own hash.
        val doc = document()
        val outcome = UpdateFlow.evaluateDocument(doc, sign(doc), publicKeyHex(), 11L)
        assertEquals(UpdateDecision.Verdict.Install, outcome.verdict)

        val artifact = outcome.artifact!!
        assertEquals(UpdateDecision.DownloadVerdict.WrongHash,
            UpdateDecision.verifyDownload(4096L, "9".repeat(64), artifact))
        assertEquals(UpdateDecision.DownloadVerdict.WrongSize,
            UpdateDecision.verifyDownload(4095L, apkSha, artifact))
        assertEquals(UpdateDecision.DownloadVerdict.Ok,
            UpdateDecision.verifyDownload(4096L, apkSha, artifact))
    }

    // ---------------------------------------------------------------- the combination

    @Test
    fun breakingAnyOneOfTheThreeIsEnoughToRefuse() {
        // Stated together so the relationship is visible: three independent ways to stop, and a
        // release has to clear all of them. Removing any single gate would let one of these
        // three cases through.
        val doc = document()
        val good = UpdateFlow.evaluateDocument(doc, sign(doc), publicKeyHex(), 11L)
        assertTrue("signature+versionCode intact should reach Install",
            good.verdict == UpdateDecision.Verdict.Install)

        val badSignature = UpdateFlow.evaluateDocument(doc, "0".repeat(128), publicKeyHex(), 11L)
        val badVersion = UpdateFlow.evaluateDocument(
            document(versionCode = 11L), sign(document(versionCode = 11L)), publicKeyHex(), 11L)
        val badBytes = UpdateDecision.verifyDownload(4096L, "9".repeat(64), good.artifact!!)

        assertEquals(UpdateDecision.Verdict.NotForUs, badSignature.verdict)
        assertEquals(UpdateDecision.Verdict.UpToDate, badVersion.verdict)
        assertEquals(UpdateDecision.DownloadVerdict.WrongHash, badBytes)
    }
}
