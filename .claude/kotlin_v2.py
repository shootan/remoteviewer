import io

p = 'apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/UpdateManifest.kt'
d = io.open(p, encoding='utf-8').read()

d = d.replace('    const val SUPPORTED_SCHEMA = 1',
              '''    // Schema 1 named one artifact and was never published. Schema 2 carries a release
    // identity, an architecture and a list of files, and the signature covers the whole set.
    const val SUPPORTED_SCHEMA = 2

    /** Limits on an artifact list, so a signed-but-absurd manifest cannot exhaust a device. */
    const val MAX_ARTIFACTS = 64
    const val MAX_ARTIFACT_BYTES = 512L * 1024 * 1024
    const val MAX_TOTAL_BYTES = 2048L * 1024 * 1024''', 1)

old = '''    data class Fields(
        val schema: Int = 0,
        val platform: String = "",
        val version: String = "",
        val artifact: String = "",
        val size: Long = 0,
        val sha256: String = "",
        val versionCode: Long = 0,
    )'''
new = '''    /**
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
    )'''
assert old in d, 'kotlin fields'
d = d.replace(old, new, 1)

old = '''        var schema = 0
        var platform = ""
        var version = ""
        var artifact = ""
        var size = 0L
        var sha256 = ""
        var versionCode = 0L
        var sawSchema = false'''
new = '''        var schema = 0
        var releaseId = ""
        var platform = ""
        var arch = ""
        var version = ""
        var versionCode = 0L
        val artifacts = mutableListOf<Artifact>()
        var sawSchema = false'''
assert old in d, 'kotlin locals'
d = d.replace(old, new, 1)

old = '''                "platform" -> platform = value
                "version" -> version = value
                "artifact" -> artifact = value
                "size" -> size = asNumber(value) ?: return null to "size is not a number"
                "sha256" -> sha256 = value'''
new = '''                "platform" -> platform = value
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
                }'''
assert old in d, 'kotlin cases'
d = d.replace(old, new, 1)

old = '''        if (!sawSchema) return null to "missing schema"
        return Fields(schema, platform, version, artifact, size, sha256, versionCode) to ""'''
new = '''        if (!sawSchema) return null to "missing schema"
        return Fields(schema, releaseId, platform, arch, version, versionCode, artifacts) to ""'''
assert old in d, 'kotlin return'
d = d.replace(old, new, 1)

old = '''        if (fields.platform.isEmpty() || fields.version.isEmpty() || fields.artifact.isEmpty()) {
            return Result(Status.Malformed, null, "missing platform, version or artifact")
        }
        if (fields.size == 0L) return Result(Status.Malformed, null, "missing or zero size")
        if (!Regex("^[0-9a-f]{64}$").matches(fields.sha256)) {
            return Result(Status.Malformed, null, "sha256 is not 64 lowercase hex characters")
        }
        if (expectedPlatform.isNotEmpty() && fields.platform != expectedPlatform) {
            return Result(Status.WrongPlatform, null, fields.platform)
        }
        return Result(Status.Ok, fields)'''
new = '''        if (fields.platform.isEmpty() || fields.version.isEmpty()) {
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
        return Result(Status.Ok, fields)'''
assert old in d, 'kotlin validation'
d = d.replace(old, new, 1)

d = d.replace('''        expectedPlatform: String,
    ): Result {''', '''        expectedPlatform: String,
        expectedArch: String = "arm64",
    ): Result {''', 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('Kotlin moved to schema 2')
