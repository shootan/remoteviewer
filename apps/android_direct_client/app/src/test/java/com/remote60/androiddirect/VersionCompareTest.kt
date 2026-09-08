package com.remote60.androiddirect

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * Checks the Kotlin side of the version-comparison contract against the shared vectors.
 *
 * The same file is read by the C++ test (`remote60_version_compare_test`) and the directory
 * server's JS test. Three implementations, one set of expected answers -- if they drift apart,
 * one of the three suites fails rather than the disagreement shipping quietly.
 *
 * This is a plain JVM unit test (`src/test`), not an instrumented one: the contract is pure
 * string logic and needs no device.
 */
class VersionCompareTest {

    /** Normalises to -1/0/1 so a sign convention change cannot pass unnoticed. */
    private fun signOf(v: Int): Int = if (v < 0) -1 else if (v > 0) 1 else 0

    /**
     * Walks up from the module directory to the repository root. Gradle runs unit tests with the
     * module as the working directory, but that is a convention rather than a guarantee, so the
     * search is explicit and fails with the paths it tried.
     */
    private fun vectorsFile(): File {
        var dir: File? = File(System.getProperty("user.dir") ?: ".").absoluteFile
        val tried = mutableListOf<String>()
        while (dir != null) {
            val candidate = File(dir, "apps/shared/version_compare_vectors.txt")
            tried += candidate.path
            if (candidate.isFile) return candidate
            dir = dir.parentFile
        }
        throw AssertionError("shared vectors file not found; tried:\n" + tried.joinToString("\n"))
    }

    @Test
    fun matchesSharedVectors() {
        val file = vectorsFile()
        var vectors = 0

        file.readLines().forEachIndexed { index, raw ->
            val line = raw.removeSuffix("\r")
            if (line.isEmpty() || line.startsWith("#")) return@forEachIndexed

            val parts = line.split("|")
            assertEquals("malformed vector at line ${index + 1}: $line", 3, parts.size)
            val (left, right, expectText) = parts
            val expect = expectText.trim().toInt()
            vectors++

            val label = "\"$left\" vs \"$right\" (line ${index + 1})"
            assertEquals(label, expect, signOf(VersionCompare.compare(left, right)))

            // Reversing the arguments must reverse the sign. Not listed in the vectors file
            // because it has to hold for every one of them.
            assertEquals(
                "$label antisymmetric",
                -expect,
                signOf(VersionCompare.compare(right, left))
            )
        }

        // A vectors file that silently became empty would otherwise report a clean pass.
        assertTrue("vectors file was empty: ${file.path}", vectors > 0)
    }

    /**
     * A null has to compare as older rather than throw: this reads a manifest field that came
     * over the wire. Kotlin-only concern, so it is not in the shared vectors.
     */
    @Test
    fun nullComparesAsMissing() {
        assertEquals(-1, signOf(VersionCompare.compare(null, "0.0.1")))
        assertEquals(1, signOf(VersionCompare.compare("0.0.1", null)))
        assertEquals(0, signOf(VersionCompare.compare(null, "")))
        assertEquals(0, signOf(VersionCompare.compare(null, null)))
    }
}
