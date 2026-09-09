package com.remote60.androiddirect

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Where this phone sends its address probe.
 *
 * It used to be the http port plus one, always. Behind TLS on 443 that is 444: nothing answers
 * there, so the observation never lands, and the connect goes ahead with the directory holding no
 * address for this phone. What the user sees is a connection that does not work, which reads like
 * the PC being off rather than a server that needs one setting.
 *
 * The same vectors the native suite uses, so the two clients cannot drift on a rule the server
 * has to agree with.
 */
class DirectoryObserveTest {

    private fun json(text: String) = JSONObject(text)

    // ---------------------------------------------------------------- the port rule

    @Test
    fun `an advertised port wins over every default`() {
        val advertised = DirectoryClient.ObserveEndpoint(known = true, port = 29181)
        assertEquals(29181, DirectoryClient.observePortFor(advertised, 8080, false))
        assertEquals(29181, DirectoryClient.observePortFor(advertised, 443, true))
    }

    @Test
    fun `http with no advertisement is one above the http port`() {
        assertEquals(
            8081,
            DirectoryClient.observePortFor(DirectoryClient.ObserveEndpoint(), 8080, false)
        )
    }

    @Test
    fun `https with no advertisement refuses rather than dialling 444`() {
        val port = DirectoryClient.observePortFor(DirectoryClient.ObserveEndpoint(), 443, true)
        assertEquals(0, port)
        assertFalse("444 is not a fallback", port == 444)
    }

    @Test
    fun `the last port has nowhere to add one`() {
        assertEquals(
            0,
            DirectoryClient.observePortFor(DirectoryClient.ObserveEndpoint(), 65535, false)
        )
    }

    @Test
    fun `the url form agrees with the explicit one`() {
        assertEquals(29181, DirectoryClient.observePortFor("http://rem.example:29180"))
        assertEquals(0, DirectoryClient.observePortFor("https://rem.example"))
        assertEquals(
            29181,
            DirectoryClient.observePortFor(
                "https://rem.example",
                DirectoryClient.ObserveEndpoint(known = true, port = 29181)
            )
        )
    }

    // ---------------------------------------------------------------- the scheme

    @Test
    fun `the scheme is read without case`() {
        assertTrue(DirectoryClient.urlIsSecure("https://rem.example"))
        assertTrue(DirectoryClient.urlIsSecure("HTTPS://rem.example"))
        assertTrue(DirectoryClient.urlIsSecure("HtTpS://rem.example"))
        assertTrue(DirectoryClient.urlIsSecure("  https://rem.example"))
        assertFalse(DirectoryClient.urlIsSecure("http://rem.example"))
        assertFalse(DirectoryClient.urlIsSecure("HTTP://rem.example"))
        assertFalse("a name that merely begins with https is not a scheme",
            DirectoryClient.urlIsSecure("httpsx://rem.example"))
        assertFalse(DirectoryClient.urlIsSecure(""))
    }

    // ---------------------------------------------------------------- what the server said

    @Test
    fun `the port is read from inside the observe object`() {
        val got = DirectoryClient.parseObserveMetadata(
            json("""{"sessionToken":"t","observe":{"port":29181}}""")
        )
        assertTrue(got.known)
        assertEquals(29181, got.port)
    }

    @Test
    fun `a port belonging to something else is not the observe port`() {
        val got = DirectoryClient.parseObserveMetadata(
            json("""{"port":9999,"relay":{"port":43000}}""")
        )
        assertFalse(got.known)
        assertEquals(0, got.port)
    }

    @Test
    fun `an older directory says nothing and that is not an error`() {
        val got = DirectoryClient.parseObserveMetadata(json("""{"sessionToken":"t"}"""))
        assertFalse(got.known)
        // And absence takes the documented http default rather than refusing outright.
        assertEquals(8081, DirectoryClient.observePortFor(got, 8080, false))
    }

    @Test
    fun `a port that is not a whole number is not a port`() {
        assertFalse(
            DirectoryClient.parseObserveMetadata(json("""{"observe":{"port":29181.5}}""")).known
        )
        assertFalse(
            DirectoryClient.parseObserveMetadata(json("""{"observe":{"port":"29181"}}""")).known
        )
        assertFalse(
            DirectoryClient.parseObserveMetadata(json("""{"observe":{"port":-1}}""")).known
        )
        assertFalse(
            DirectoryClient.parseObserveMetadata(json("""{"observe":{"port":65536}}""")).known
        )
        assertFalse(
            DirectoryClient.parseObserveMetadata(json("""{"observe":{"port":0}}""")).known
        )
    }

    // ---------------------------------------------------------------- the advertised host
    //
    // The server does not validate this: it is sent as whatever the operator typed. So it is
    // checked here, and a bad one does not take a good port down with it.

    @Test
    fun `a usable host is taken`() {
        val got = DirectoryClient.parseObserveMetadata(
            json("""{"observe":{"port":29181,"host":"observe.example.net"}}""")
        )
        assertEquals("observe.example.net", got.host)
        assertFalse(got.hostRejected)
    }

    @Test
    fun `a host that is not a host is refused, and the port survives`() {
        for (bad in listOf(
            "http://observe.example",
            "observe.example:29181",
            "observe.example/path",
            "observe example",
            "observe..example",
            "observe.example.",
            "-observe.example",
            "observe.example-",
            "a".repeat(300),
        )) {
            val got = DirectoryClient.parseObserveMetadata(
                json(JSONObject().put("observe", JSONObject().put("port", 29181).put("host", bad))
                    .toString())
            )
            assertTrue("still known: $bad", got.known)
            assertEquals("port kept: $bad", 29181, got.port)
            assertEquals("host dropped: $bad", "", got.host)
            assertTrue("rejection recorded: $bad", got.hostRejected)
        }
    }

    @Test
    fun `whitespace is absence, not a host`() {
        val got = DirectoryClient.parseObserveMetadata(
            json("""{"observe":{"port":29181,"host":"   "}}""")
        )
        assertTrue(got.known)
        assertEquals("", got.host)
        assertFalse("nothing was rejected; nothing was sent", got.hostRejected)
    }

    @Test
    fun `the fallback host is the directory's own`() {
        assertEquals(
            "rem.example",
            DirectoryClient.observeHostFor("https://rem.example", DirectoryClient.ObserveEndpoint())
        )
        assertEquals(
            "observe.example",
            DirectoryClient.observeHostFor(
                "https://rem.example",
                DirectoryClient.ObserveEndpoint(known = true, port = 1, host = "observe.example")
            )
        )
        assertEquals(
            "a rejected host falls back to the directory",
            "rem.example",
            DirectoryClient.observeHostFor(
                "https://rem.example",
                DirectoryClient.ObserveEndpoint(known = true, port = 1, hostRejected = true)
            )
        )
    }

    // ---------------------------------------------------------------- the url, normalised once

    @Test
    fun `an https url is never rewritten to http`() {
        assertEquals("https://rem.example", DirectoryClient.normalizedUrl("https://rem.example"))
        assertEquals("https://rem.example", DirectoryClient.normalizedUrl(" https://rem.example/ "))
        // A bare host still gets http, which is what every current deployment is -- but it is one
        // decision, in one place, and the log uploader now asks it instead of answering it.
        assertEquals("http://rem.example", DirectoryClient.normalizedUrl("rem.example"))
    }
}
