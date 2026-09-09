package com.remote60.androiddirect

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * Checks the Kotlin side of the observe-endpoint contract against the shared vectors.
 *
 * The same file is read by the C++ test (`remote60_observe_vectors_test`) and by the directory
 * server's JS test. Three implementations, one set of expected answers.
 *
 * This rule is the reason the file exists: it used to be "http port + 1" in all three, which is
 * 444 behind TLS. Nothing answers there, so the probe went into silence, the host never completed
 * a heartbeat, and the phone never reached /api/connect. Every suite passed throughout, because
 * every suite had made the same assumption as the code.
 */
class ObserveVectorsTest {

    /**
     * Walks up from the module directory to the repository root. Gradle runs unit tests with the
     * module as the working directory, but that is a convention rather than a guarantee, so the
     * search is explicit and fails with the paths it tried.
     */
    private fun vectorsFile(): File {
        var dir: File? = File(System.getProperty("user.dir") ?: ".").absoluteFile
        val tried = mutableListOf<String>()
        while (dir != null) {
            val candidate = File(dir, "apps/shared/observe_endpoint_vectors.txt")
            tried += candidate.path
            if (candidate.isFile) return candidate
            dir = dir.parentFile
        }
        throw AssertionError("shared vectors file not found; tried:\n" + tried.joinToString("\n"))
    }

    @Test
    fun `the shared vectors are answered the same way here`() {
        var parseRows = 0
        var portRows = 0

        vectorsFile().forEachLine { raw ->
            val line = raw.trim()
            if (line.isEmpty() || line.startsWith("#")) return@forEachLine
            // Not split(limit) -- a trailing empty host field must survive.
            val f = line.split("|")

            when (f[0]) {
                "parse" -> {
                    val got = DirectoryClient.parseObserveMetadata(JSONObject(f[1]))
                    assertEquals("known: ${f[1]}", f[2] == "1", got.known)
                    assertEquals("port: ${f[1]}", f[3].toInt(), got.port)
                    assertEquals("host: ${f[1]}", f[4], got.host)
                    assertEquals("rejected: ${f[1]}", f[5] == "1", got.hostRejected)
                    parseRows++
                }
                "port" -> {
                    val advertised = DirectoryClient.ObserveEndpoint(
                        known = f[1] == "1",
                        port = f[2].toInt(),
                    )
                    assertEquals(
                        "port for known=${f[1]} advertised=${f[2]} http=${f[3]} secure=${f[4]}",
                        f[5].toInt(),
                        DirectoryClient.observePortFor(advertised, f[3].toInt(), f[4] == "1"),
                    )
                    portRows++
                }
                else -> throw AssertionError("unknown row kind: ${f[0]}")
            }
        }

        // A file that stopped being read would otherwise pass with nothing in it, which is the
        // failure mode a shared-vector test has: green because it checked nothing.
        assertTrue("parse rows read: $parseRows", parseRows >= 15)
        assertTrue("port rows read: $portRows", portRows >= 8)
    }
}
