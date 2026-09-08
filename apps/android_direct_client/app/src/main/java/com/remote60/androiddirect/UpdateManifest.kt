package com.remote60.androiddirect

import java.math.BigInteger
import java.security.AlgorithmParameters
import java.security.KeyFactory
import java.security.Signature
import java.security.spec.ECGenParameterSpec
import java.security.spec.ECParameterSpec
import java.security.spec.ECPoint
import java.security.spec.ECPublicKeySpec

/**
 * The update manifest on the Android side: the same document, the same signature, the same rules
 * the Windows and server implementations use.
 *
 * All three read the same fixed vectors from `apps/shared/update_manifest/`, so agreement is
 * demonstrated against one artifact rather than each side agreeing with itself. The contract is
 * in that directory's README.txt; the design is `docs/업데이트_기능_설계.md` 4.1-4.3.
 *
 * Nothing here implements cryptography -- the curve, the hash and the verification come from
 * `java.security`. What this file does have to do is convert between representations: the
 * signature travels as raw r||s (which is what CNG wants on Windows) while JCA expects DER, and
 * the public key travels as raw X||Y rather than as an encoded SubjectPublicKeyInfo.
 */
object UpdateManifest {

    // Schema 1 named one artifact and was never published. Schema 2 carries a release
    // identity, an architecture and a list of files, and the signature covers the whole set.
    const val SUPPORTED_SCHEMA = 2

    /** Limits on an artifact list, so a signed-but-absurd manifest cannot exhaust a device. */
    const val MAX_ARTIFACTS = 64
    const val MAX_ARTIFACT_BYTES = 512L * 1024 * 1024
    const val MAX_TOTAL_BYTES = 2048L * 1024 * 1024

    /** Mirrors the C++ ManifestStatus and the server's Status, name for name. */
    enum class Status { Ok, SignatureInvalid, Malformed, WrongPlatform, UnsupportedSchema }

    /**
     * One file in a release.
     *
     * Every field is covered by the manifest's single signature, so a name cannot be paired with a
     * different hash or URL without the signature failing. That is why there is no archive: the
     * manifest is the container, and it holds identities rather than bytes.
     */
    data class Artifact(
        val name: String,
        val size: Long,
        val sha256: String,
        val url: String,
    )

    data class Fields(
        val schema: Int = 0,
        val releaseId: String = "",
        val platform: String = "",
        val arch: String = "",
        val version: String = "",
        val versionCode: Long = 0,
        val artifacts: List<Artifact> = emptyList(),
    )

    /** `fields` is non-null only when [status] is [Status.Ok]. */
    data class Result(val status: Status, val fields: Fields? = null, val detail: String = "")

    // ------------------------------------------------------------------ signature

    private fun hexToBytes(hex: String): ByteArray? {
        if (hex.length % 2 != 0) return null
        val out = ByteArray(hex.length / 2)
        for (i in out.indices) {
            val hi = Character.digit(hex[i * 2], 16)
            val lo = Character.digit(hex[i * 2 + 1], 16)
            if (hi < 0 || lo < 0) return null
            out[i] = ((hi shl 4) or lo).toByte()
        }
        return out
    }

    /** DER INTEGER: minimal, and prefixed with 0x00 when the high bit would read as negative. */
    private fun derInteger(value: ByteArray): ByteArray {
        var start = 0
        while (start < value.size - 1 && value[start] == 0.toByte()) start++
        val trimmed = value.copyOfRange(start, value.size)
        val needsPad = (trimmed[0].toInt() and 0x80) != 0
        val body = if (needsPad) byteArrayOf(0) + trimmed else trimmed
        return byteArrayOf(0x02, body.size.toByte()) + body
    }

    /**
     * Raw r||s to the DER SEQUENCE that JCA expects.
     *
     * The wire format is raw because that is what CNG verifies on Windows; converting here rather
     * than changing the wire keeps one representation across three runtimes.
     */
    private fun rawSignatureToDer(raw: ByteArray): ByteArray? {
        if (raw.size != 64) return null
        val r = derInteger(raw.copyOfRange(0, 32))
        val s = derInteger(raw.copyOfRange(32, 64))
        val body = r + s
        // A P-256 signature never reaches the long-form length encoding, but refusing rather than
        // emitting something malformed is the safer branch.
        if (body.size > 127) return null
        return byteArrayOf(0x30, body.size.toByte()) + body
    }

    private fun publicKeyFromRawXY(raw: ByteArray): java.security.PublicKey? = try {
        val params = AlgorithmParameters.getInstance("EC").apply {
            init(ECGenParameterSpec("secp256r1"))
        }
        val spec = params.getParameterSpec(ECParameterSpec::class.java)
        val point = ECPoint(
            BigInteger(1, raw.copyOfRange(0, 32)),
            BigInteger(1, raw.copyOfRange(32, 64)),
        )
        KeyFactory.getInstance("EC").generatePublic(ECPublicKeySpec(point, spec))
    } catch (_: Exception) {
        null
    }

    /**
     * Verifies a detached ECDSA P-256/SHA-256 signature over [document].
     *
     * Every failure returns false. A caller cannot act differently on "forged" than on "could not
     * check", and both mean the artifact is not known to be ours.
     */
    @JvmStatic
    fun verifySignature(document: String, signatureHex: String, publicKeyHex: String): Boolean {
        if (signatureHex.length != 128 || publicKeyHex.length != 128) return false
        val rawSig = hexToBytes(signatureHex) ?: return false
        val rawKey = hexToBytes(publicKeyHex) ?: return false
        if (rawSig.size != 64 || rawKey.size != 64) return false

        val der = rawSignatureToDer(rawSig) ?: return false
        val key = publicKeyFromRawXY(rawKey) ?: return false
        return try {
            Signature.getInstance("SHA256withECDSA").run {
                initVerify(key)
                update(document.toByteArray(Charsets.UTF_8))
                verify(der)
            }
        } catch (_: Exception) {
            false
        }
    }

    // ------------------------------------------------------------------ parsing

    private fun asNumber(text: String): Long? =
        if (Regex("^\\d{1,18}$").matches(text)) text.toLongOrNull() else null

    /** Returns the fields, or a reason string when the document does not parse. */
    fun parseFields(document: String): Pair<Fields?, String> {
        var schema = 0
        var releaseId = ""
        var platform = ""
        var arch = ""
        var version = ""
        var versionCode = 0L
        val artifacts = mutableListOf<Artifact>()
        var sawSchema = false

        for (rawLine in document.split("\n")) {
            val line = rawLine.removeSuffix("\r").trim()
            if (line.isEmpty() || line.startsWith("#")) continue

            val eq = line.indexOf('=')
            if (eq < 0) return null to "line without '='"
            val key = line.substring(0, eq).trim()
            val value = line.substring(eq + 1).trim()
            if (key.isEmpty()) return null to "empty key"

            when (key) {
                "schema" -> {
                    val n = asNumber(value) ?: return null to "schema is not a number"
                    schema = n.toInt()
                    sawSchema = true
                }
                "platform" -> platform = value
                "arch" -> arch = value
                "releaseId" -> releaseId = value
                "version" -> version = value
                "artifact" -> {
                    // name|size|sha256|url. Pipe-separated because the document is signed as
                    // bytes, and a format with one obvious reading has nothing to disagree about.
                    val parts = value.split("|")
                    if (parts.size != 4) return null to "artifact line needs name|size|sha256|url"
                    val size = asNumber(parts[1].trim())
                        ?: return null to "artifact size is not a number"
                    artifacts.add(
                        Artifact(parts[0].trim(), size, parts[2].trim(), parts[3].trim())
                    )
                }
                "versionCode" ->
                    versionCode = asNumber(value) ?: return null to "versionCode is not a number"
                // Unknown keys are ignored, not rejected: a newer publisher adding a field must
                // not brick an older client. Incompatible changes go through schema.
                else -> Unit
            }
        }
        if (!sawSchema) return null to "missing schema"
        return Fields(schema, releaseId, platform, arch, version, versionCode, artifacts) to ""
    }

    /**
     * Checks the signature and, only if it passes, parses the document.
     *
     * The ordering is observable: a document that is both malformed and badly signed reports
     * [Status.SignatureInvalid], never [Status.Malformed]. And [Result.fields] is null unless the
     * status is Ok, so there is no way to read a version out of something unverified.
     */
    @JvmStatic
    fun load(
        document: String,
        signatureHex: String,
        publicKeyHex: String,
        expectedPlatform: String,
        expectedArch: String = "arm64",
    ): Result {
        if (!verifySignature(document, signatureHex, publicKeyHex)) {
            return Result(Status.SignatureInvalid, null, "signature did not verify")
        }

        val (fields, error) = parseFields(document)
        if (fields == null) return Result(Status.Malformed, null, error)

        if (fields.schema != SUPPORTED_SCHEMA) {
            return Result(Status.UnsupportedSchema, null, "schema ${fields.schema}")
        }
        if (fields.platform.isEmpty() || fields.version.isEmpty()) {
            return Result(Status.Malformed, null, "missing platform or version")
        }
        if (fields.releaseId.isEmpty()) return Result(Status.Malformed, null, "missing releaseId")
        if (fields.arch.isEmpty()) return Result(Status.Malformed, null, "missing arch")
        if (fields.artifacts.isEmpty()) {
            return Result(Status.Malformed, null, "no artifacts listed")
        }
        if (fields.artifacts.size > MAX_ARTIFACTS) {
            return Result(Status.Malformed, null, "too many artifacts")
        }

        var total = 0L
        fields.artifacts.forEachIndexed { i, a ->
            if (a.size <= 0 || a.size > MAX_ARTIFACT_BYTES) {
                return Result(Status.Malformed, null, "artifact $i size out of range")
            }
            total += a.size
            if (total > MAX_TOTAL_BYTES) {
                return Result(Status.Malformed, null, "artifacts exceed the total size limit")
            }
            if (!Regex("^[0-9a-f]{64}$").matches(a.sha256)) {
                return Result(Status.Malformed, null, "artifact $i sha256 is not 64 lowercase hex")
            }
            // The transport boundary, enforced where the manifest is read rather than only where
            // it is fetched -- so a client that downloads some other way cannot sidestep it.
            if (!a.url.startsWith("https://", ignoreCase = true) ||
                Regex("^https://[^/]*@", RegexOption.IGNORE_CASE).containsMatchIn(a.url)
            ) {
                return Result(
                    Status.Malformed, null,
                    "artifact $i url must be https and carry no credentials"
                )
            }
        }

        if (expectedPlatform.isNotEmpty() && fields.platform != expectedPlatform) {
            return Result(Status.WrongPlatform, null, fields.platform)
        }
        if (expectedArch.isNotEmpty() && fields.arch != expectedArch) {
            return Result(Status.WrongPlatform, null, "arch ${fields.arch}")
        }
        return Result(Status.Ok, fields)
    }

    /**
     * The key this build trusts, as raw X||Y hex.
     *
     * Empty, exactly as on the Windows side. No release key is compiled in, so nothing verifies
     * until one is deliberately put here -- and a placeholder that happened to verify something
     * would be worse than no key at all. Choosing and storing a real key is a separate approved
     * decision (design 4.5).
     */
    @JvmStatic
    fun trustedPublicKeyHex(): String = ""
}
