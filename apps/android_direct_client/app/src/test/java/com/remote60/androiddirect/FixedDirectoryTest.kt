package com.remote60.androiddirect

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * The app has one directory server and no field to type another.
 *
 * The sign-in screen used to ask for the address and keep the answer in preferences. What is
 * checked here is the part that needs no phone: the address the build carries, the rule for a
 * session token that was stored beside some other address, and what the sign-in layout
 * declares. That the screen looks right on a device is not shown by any of this.
 */
class FixedDirectoryTest {

    // ---------------------------------------------------------------- the address

    @Test
    fun `the build carries the one directory address`() {
        // A debug build may have been pointed at a fixture (-Pgnlink.testDirectoryUrl); a build
        // made without that property carries the product's address, and so does every release.
        val override = System.getProperty("gnlink.testDirectoryUrl").orEmpty()
        val expected = override.ifEmpty { "https://gnlink.shotan.net" }
        assertEquals(expected, BuildConfig.DIRECTORY_URL)
        assertEquals(BuildConfig.DIRECTORY_URL, DirectoryClient.directoryUrl)
    }

    @Test
    fun `the update endpoint is derived from that address`() {
        if (!BuildConfig.DIRECTORY_URL.startsWith("https://")) return  // a fixture build
        val endpoint = DirectoryClient.updateEndpointFor("", DirectoryClient.directoryUrl, "")
        assertTrue(endpoint.url, endpoint.url.startsWith("https://gnlink.shotan.net:443/api/update/manifest"))
    }

    // ---------------------------------------------------------------- a stored token

    private val server = "https://gnlink.example"
    private val former = listOf("https://rem.example")

    private fun classify(stored: String, list: List<String> = former) =
        DirectoryClient.classifyStoredOrigin(stored, server, list)

    @Test
    fun `a token stored beside this server is the server's`() {
        assertEquals(DirectoryClient.StoredOrigin.SAME, classify("https://gnlink.example"))
        // The same server spelt differently is still the same server.
        assertEquals(DirectoryClient.StoredOrigin.SAME, classify("https://gnlink.example/"))
        assertEquals(DirectoryClient.StoredOrigin.SAME, classify("https://GNLink.example:443"))
    }

    @Test
    fun `a token stored beside a listed former name may be presented`() {
        assertEquals(DirectoryClient.StoredOrigin.MIGRATABLE, classify("https://rem.example"))
        assertEquals(DirectoryClient.StoredOrigin.MIGRATABLE, classify("https://REM.example:443/"))
    }

    @Test
    fun `a token stored beside anything else is neither`() {
        val unlisted = DirectoryClient.StoredOrigin.UNLISTED
        assertEquals(unlisted, classify("http://rem.example"))
        assertEquals(unlisted, classify("https://rem.example:8443"))
        assertEquals(unlisted, classify("https://rem.example.evil.test"))
        assertEquals(unlisted, classify("https://xrem.example"))
        assertEquals(unlisted, classify("http://gnlink.example"))
        assertEquals(unlisted, classify("http://127.0.0.1:1"))
        // Nothing stored is not "the default server": a token with no address has no issuer.
        assertEquals(unlisted, classify(""))
        assertEquals(unlisted, classify("   "))
        // With nothing listed, only the server itself is usable.
        assertEquals(unlisted, classify("https://rem.example", emptyList()))
        assertEquals(unlisted, classify("https://rem.example", listOf("", "  ")))
    }

    @Test
    fun `the build lists the one former name, as an exact https origin`() {
        if (System.getProperty("gnlink.testDirectoryUrl").orEmpty().isNotEmpty()) return
        assertEquals(listOf("https://rem.shotan.net"), DirectoryClient.migratableOrigins)
        for (origin in DirectoryClient.migratableOrigins) {
            assertEquals("$origin:443", DirectoryClient.originKey(origin))
            assertFalse(DirectoryClient.originKey(origin) ==
                DirectoryClient.originKey(DirectoryClient.directoryUrl))
        }
    }

    // ---------------------------------------------------------------- redirects

    /**
     * A loopback server that records what it is sent and answers as told.
     *
     * A socket and a thread rather than a library: the unit-test classpath is the phone's, which
     * has no HTTP server in it.
     */
    private class Recorder(private val status: Int, private val location: String?) : AutoCloseable {
        val received = java.util.concurrent.CopyOnWriteArrayList<String>()
        private val socket = java.net.ServerSocket(0, 8, java.net.InetAddress.getByName("127.0.0.1"))
        val url: String get() = "http://127.0.0.1:${socket.localPort}"
        private val thread = Thread {
            while (!socket.isClosed) {
                val client = try { socket.accept() } catch (e: Exception) { break }
                client.use { c ->
                    c.soTimeout = 3000
                    val reader = c.getInputStream().bufferedReader()
                    val head = StringBuilder()
                    while (true) {
                        val line = reader.readLine() ?: break
                        if (line.isEmpty()) break
                        head.append(line).append('\n')
                    }
                    received += head.toString()
                    val path = head.lineSequence().firstOrNull()?.split(' ')?.getOrNull(1).orEmpty()
                    val body = if (status in 300..399) "" else "{\"hosts\":[]}"
                    val out = StringBuilder("HTTP/1.1 $status X\r\n")
                    location?.let { out.append("Location: ").append(it).append(path).append("\r\n") }
                    out.append("Content-Length: ${body.length}\r\nConnection: close\r\n\r\n").append(body)
                    c.getOutputStream().write(out.toString().toByteArray())
                    c.getOutputStream().flush()
                }
            }
        }.apply { isDaemon = true; start() }

        override fun close() {
            socket.close()
            thread.join(2000)
        }
    }

    @Test
    fun `a redirect answered to a request carrying the session is not followed`() {
        // The JVM's own HTTP client, not the phone's: this shows the request is told to stay
        // put and that a client which honours that does. What Android's client does with the
        // same setting is a device check.
        for (status in listOf(301, 302, 307, 308)) {
            Recorder(200, null).use { elsewhere ->
                Recorder(status, elsewhere.url).use { first ->
                    var refused = false
                    try {
                        DirectoryClient.hosts(first.url, "fixture-session-token")
                    } catch (e: DirectoryClient.DirectoryException) {
                        refused = e.status == status
                    }
                    assertTrue("$status is reported as the answer", refused)
                    assertEquals("$status: the server was asked once", 1, first.received.size)
                    assertTrue(first.received[0].contains("fixture-session-token"))
                    assertEquals("$status: the address it pointed at received nothing",
                        emptyList<String>(), elsewhere.received.toList())
                }
            }
        }
    }

    // ---------------------------------------------------------------- the sign-in layout

    private fun mainSource(relative: String): File {
        // Unit tests run with the module directory as the working directory.
        val file = File("src/main/$relative")
        assertTrue("not found: ${file.absolutePath}", file.exists())
        return file
    }

    @Test
    fun `the sign-in layout declares an id and a password and no server field`() {
        val layout = mainSource("res/layout/activity_main.xml").readText()
        val loginInputs = Regex("""<EditText[^>]*android:id="@\+id/(login\w+)"""")
            .findAll(layout).map { it.groupValues[1] }.toList()
        assertEquals(listOf("loginIdInput", "loginPasswordInput"), loginInputs)
        assertFalse(layout.contains("loginServerInput"))
        assertFalse(layout.contains("login_server_hint"))
    }

    @Test
    fun `no text asks for a server address`() {
        for (values in listOf("values", "values-ko")) {
            val strings = mainSource("res/$values/strings.xml").readText()
            assertFalse(values, strings.contains("login_needs_server"))
            assertFalse(values, strings.contains("login_server_hint"))
        }
    }

    @Test
    fun `nothing in the app reads a stored server address`() {
        val sources = mainSource("java/com/remote60/androiddirect").listFiles { f -> f.extension == "kt" }!!
        val readers = sources.filter { it.readText().contains("savedUrl(") }.map { it.name }
        assertEquals(emptyList<String>(), readers)
    }
}
