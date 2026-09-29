package com.remote60.androiddirect

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

/**
 * The stored sign-in on the phone: what each step leaves behind.
 *
 * [LoginFlow] is the app's. The vault here is a map and the directory is an object this file
 * owns, which counts what it is asked, answers what a case tells it to, and can be made to
 * answer late -- a real server cannot be told to be slow at exactly the moment a sign-out
 * happens.
 *
 * What this does not show: the Android Keystore ([KeystoreLoginVault] needs a device), the
 * screen, a real directory, or the app being killed mid-write. Those are device checks.
 */
class LoginFlowTest {

    private val origin = "https://gnlink.example:443"

    /** The vault as a few fields. `failSave` is the disk being full. */
    private class MemoryVault : LoginFlow.Vault {
        var stored: LoginFlow.StoredLogin? = null
        var unreadable = false
        var counter = 0L
        var failSave = false
        val owedList = ArrayList<LoginFlow.OwedSignOut>()

        override fun load(): LoginFlow.VaultRead = when {
            unreadable -> LoginFlow.VaultRead.Unreadable("fixture")
            stored == null -> LoginFlow.VaultRead.None
            else -> LoginFlow.VaultRead.Ok(stored!!)
        }
        override fun save(login: LoginFlow.StoredLogin): Boolean {
            if (failSave) return false
            stored = login
            unreadable = false
            return true
        }
        override fun erase(): Boolean { stored = null; unreadable = false; return true }
        override fun generation(): Long = counter
        override fun bumpGeneration(): Long = ++counter
        override fun owed(): List<LoginFlow.OwedSignOut> = owedList.toList()
        override fun addOwed(owed: LoginFlow.OwedSignOut): Boolean {
            owedList.removeAll { it.deviceId == owed.deviceId }
            owedList.add(owed)
            while (owedList.size > LoginFlow.MAX_OWED) owedList.removeAt(0)
            return true
        }
        override fun removeOwed(deviceId: String): Boolean {
            owedList.removeAll { it.deviceId == deviceId }
            return true
        }
    }

    private class FakeDirectory : LoginFlow.Directory {
        val presented = ArrayList<String>()
        val revoked = ArrayList<String>()
        val revokeTokens = ArrayList<String>()
        var refreshAnswer = LoginFlow.Call.OK
        var revokeAnswer = LoginFlow.Call.OK
        var issued = 0
        var hold: CountDownLatch? = null
        var entered: CountDownLatch? = null

        override fun refresh(deviceId: String, deviceCredential: String): LoginFlow.Refreshed {
            synchronized(this) { presented.add(deviceCredential) }
            entered?.countDown()
            hold?.await(20, TimeUnit.SECONDS)
            if (refreshAnswer != LoginFlow.Call.OK) return LoginFlow.Refreshed(refreshAnswer)
            val n = synchronized(this) { ++issued }
            return LoginFlow.Refreshed(LoginFlow.Call.OK, "session-$n-5e0a71c9", "rotated-$n-91aa04f6")
        }

        override fun revoke(deviceId: String, revokeToken: String): LoginFlow.Call {
            synchronized(this) { revoked.add(deviceId); revokeTokens.add(revokeToken) }
            return revokeAnswer
        }
    }

    private val lines = ArrayList<String>()
    private val log: (String) -> Unit = { synchronized(lines) { lines.add(it) } }

    private fun login(n: String, at: String = origin) = LoginFlow.StoredLogin(
        at, "tester", "device-$n-0123456789abcdef", "credential-$n-7c1e9a44", "revoke-$n-b20f6d33")

    private fun signedIn(n: String) = LoginFlow.SignedIn(
        "login-session-$n-0d5b", "login-device-$n-fedcba98", "login-credential-$n-3f8e",
        "login-revoke-$n-a61c")

    // ---------------------------------------------------------------- coming back

    @Test
    fun `nothing stored is the sign-in form, and the directory is not asked`() {
        val dir = FakeDirectory()
        val r = LoginFlow.comeBack(MemoryVault(), dir, origin, log)
        assertEquals(LoginFlow.Return.NO_CREDENTIAL, r.outcome)
        assertTrue(dir.presented.isEmpty())
    }

    @Test
    fun `a stored credential signs in, and the next one is stored`() {
        val vault = MemoryVault().apply { stored = login("ok") }
        val dir = FakeDirectory()
        val r = LoginFlow.comeBack(vault, dir, origin, log)
        assertEquals(LoginFlow.Return.SIGNED_IN, r.outcome)
        assertEquals("session-1-5e0a71c9", r.sessionToken)
        assertEquals("tester", r.accountId)
        assertEquals(listOf("credential-ok-7c1e9a44"), dir.presented)
        assertEquals("rotated-1-91aa04f6", vault.stored!!.deviceCredential)
        assertEquals("revoke-ok-b20f6d33", vault.stored!!.revokeToken)
        assertEquals(0L, vault.counter)
    }

    @Test
    fun `a refused credential is erased and the id is kept for the form`() {
        val vault = MemoryVault().apply { stored = login("refused") }
        val dir = FakeDirectory().apply { refreshAnswer = LoginFlow.Call.REJECTED }
        val r = LoginFlow.comeBack(vault, dir, origin, log)
        assertEquals(LoginFlow.Return.REJECTED, r.outcome)
        assertEquals("tester", r.accountId)
        assertEquals(null, vault.stored)
        assertEquals(1L, vault.counter)
    }

    @Test
    fun `no answer about the credential keeps it`() {
        for (answer in listOf(LoginFlow.Call.LIMITED, LoginFlow.Call.FAILED, LoginFlow.Call.UNREACHABLE)) {
            val vault = MemoryVault().apply { stored = login("later") }
            val dir = FakeDirectory().apply { refreshAnswer = answer }
            val r = LoginFlow.comeBack(vault, dir, origin, log)
            assertEquals(answer.name, LoginFlow.Return.TRY_LATER, r.outcome)
            assertEquals(answer.name, login("later"), vault.stored)
            assertEquals("", r.sessionToken)
        }
    }

    @Test
    fun `a directory with no refresh route keeps it and shows the form`() {
        val vault = MemoryVault().apply { stored = login("old-server") }
        val dir = FakeDirectory().apply { refreshAnswer = LoginFlow.Call.UNSUPPORTED }
        assertEquals(LoginFlow.Return.SERVER_CANNOT, LoginFlow.comeBack(vault, dir, origin, log).outcome)
        assertEquals(login("old-server"), vault.stored)
    }

    @Test
    fun `a credential issued by another server is not presented and not erased`() {
        val other = login("elsewhere", "https://another.example:443")
        val vault = MemoryVault().apply { stored = other }
        val dir = FakeDirectory()
        assertEquals(LoginFlow.Return.NO_CREDENTIAL, LoginFlow.comeBack(vault, dir, origin, log).outcome)
        assertTrue(dir.presented.isEmpty())
        assertEquals(other, vault.stored)
    }

    @Test
    fun `a vault that cannot be read sends nothing`() {
        val vault = MemoryVault().apply { stored = login("damaged"); unreadable = true }
        val dir = FakeDirectory()
        assertEquals(LoginFlow.Return.UNREADABLE, LoginFlow.comeBack(vault, dir, origin, log).outcome)
        assertTrue(dir.presented.isEmpty())
        assertEquals(login("damaged"), vault.stored)
    }

    @Test
    fun `a sign-out that was written down wins over the credential beside it`() {
        val c = login("half-out")
        val vault = MemoryVault().apply {
            stored = c
            owedList.add(LoginFlow.OwedSignOut(origin, c.deviceId, c.revokeToken))
        }
        val dir = FakeDirectory()
        assertEquals(LoginFlow.Return.SIGNED_OUT, LoginFlow.comeBack(vault, dir, origin, log).outcome)
        assertTrue(dir.presented.isEmpty())
        assertEquals(null, vault.stored)
        assertEquals(1, vault.owedList.size)
    }

    @Test
    fun `a refresh whose new credential cannot be stored is not a sign-in`() {
        val vault = MemoryVault().apply { stored = login("full"); failSave = true }
        val r = LoginFlow.comeBack(vault, FakeDirectory(), origin, log)
        assertEquals(LoginFlow.Return.NOT_SAVED, r.outcome)
        assertEquals("", r.sessionToken)
    }

    @Test
    fun `two at once - the second presents what the first had just stored`() {
        val vault = MemoryVault().apply { stored = login("race") }
        val dir = FakeDirectory().apply { hold = CountDownLatch(1); entered = CountDownLatch(1) }
        var first: LoginFlow.ReturnResult? = null
        var second: LoginFlow.ReturnResult? = null
        val a = thread { first = LoginFlow.comeBack(vault, dir, origin, log, 15000) }
        assertTrue(dir.entered!!.await(10, TimeUnit.SECONDS))
        val b = thread { second = LoginFlow.comeBack(vault, dir, origin, log, 15000) }
        Thread.sleep(400)
        assertEquals("the second has sent nothing while the first holds the vault",
            1, synchronized(dir) { dir.presented.size })
        dir.hold!!.countDown()
        a.join(); b.join()
        assertEquals(LoginFlow.Return.SIGNED_IN, first!!.outcome)
        assertEquals(LoginFlow.Return.SIGNED_IN, second!!.outcome)
        assertEquals(listOf("credential-race-7c1e9a44", "rotated-1-91aa04f6"), dir.presented)
        assertEquals("rotated-2-91aa04f6", vault.stored!!.deviceCredential)
    }

    @Test
    fun `a vault somebody else holds is busy, and nothing is sent`() {
        val vault = MemoryVault().apply { stored = login("busy") }
        val dir = FakeDirectory().apply { hold = CountDownLatch(1); entered = CountDownLatch(1) }
        val holder = thread { LoginFlow.comeBack(vault, dir, origin, log, 15000) }
        assertTrue(dir.entered!!.await(10, TimeUnit.SECONDS))
        val r = LoginFlow.comeBack(vault, dir, origin, log, 300)
        assertEquals(LoginFlow.Return.BUSY, r.outcome)
        assertEquals(1, synchronized(dir) { dir.presented.size })
        dir.hold!!.countDown()
        holder.join()
    }

    // ---------------------------------------------------------------- signing in

    @Test
    fun `a sign-in that succeeded is stored, and one after it ends the device before`() {
        val vault = MemoryVault()
        val dir = FakeDirectory()
        val g1 = LoginFlow.beginSignIn(vault, log)
        val first = signedIn("first")
        assertEquals(LoginFlow.Remembered.STORED,
            LoginFlow.rememberSignIn(vault, dir, origin, g1, "tester", first, log))
        assertEquals(first.deviceCredential, vault.stored!!.deviceCredential)
        assertEquals(origin, vault.stored!!.serverOrigin)
        assertTrue(dir.revoked.isEmpty())

        val g2 = LoginFlow.beginSignIn(vault, log)
        val second = signedIn("second")
        assertEquals(LoginFlow.Remembered.STORED,
            LoginFlow.rememberSignIn(vault, dir, origin, g2, "tester", second, log))
        assertEquals(second.deviceId, vault.stored!!.deviceId)
        assertEquals(listOf(first.deviceId), vault.owedList.map { it.deviceId })
        assertEquals(0, LoginFlow.settleOwedSignOuts(vault, dir, origin, log))
        assertEquals(listOf(first.deviceId), dir.revoked)
        assertTrue(vault.owedList.isEmpty())
    }

    @Test
    fun `a directory that issued no credential - the sign-in stands, nothing is stored`() {
        val vault = MemoryVault()
        val dir = FakeDirectory()
        val g = LoginFlow.beginSignIn(vault, log)
        assertEquals(LoginFlow.Remembered.NOT_ISSUED, LoginFlow.rememberSignIn(
            vault, dir, origin, g, "tester", LoginFlow.SignedIn("plain-session", "", "", ""), log))
        assertEquals(null, vault.stored)
        assertTrue(dir.revoked.isEmpty())
    }

    @Test
    fun `a sign-in answered after the sign-out is not stored, and its device is ended`() {
        val vault = MemoryVault()
        val dir = FakeDirectory()
        val g = LoginFlow.beginSignIn(vault, log)
        LoginFlow.signOut(vault, dir, origin, log)
        val late = signedIn("late")
        assertEquals(LoginFlow.Remembered.SUPERSEDED,
            LoginFlow.rememberSignIn(vault, dir, origin, g, "tester", late, log))
        assertEquals(null, vault.stored)
        assertEquals(listOf(late.deviceId), dir.revoked)
        assertEquals(listOf(late.revokeToken), dir.revokeTokens)
    }

    @Test
    fun `when the directory cannot be told, ending that device is owed`() {
        val vault = MemoryVault()
        val dir = FakeDirectory()
        val g = LoginFlow.beginSignIn(vault, log)
        LoginFlow.signOut(vault, dir, origin, log)
        dir.revokeAnswer = LoginFlow.Call.UNREACHABLE
        val late = signedIn("late-offline")
        assertEquals(LoginFlow.Remembered.SUPERSEDED,
            LoginFlow.rememberSignIn(vault, dir, origin, g, "tester", late, log))
        assertEquals(null, vault.stored)
        assertEquals(listOf(late.deviceId), vault.owedList.map { it.deviceId })
    }

    @Test
    fun `an earlier sign-in answered after a later one does not replace it`() {
        val vault = MemoryVault()
        val dir = FakeDirectory()
        val g1 = LoginFlow.beginSignIn(vault, log)
        val g2 = LoginFlow.beginSignIn(vault, log)
        val newer = signedIn("newer")
        val older = signedIn("older")
        assertEquals(LoginFlow.Remembered.STORED,
            LoginFlow.rememberSignIn(vault, dir, origin, g2, "tester", newer, log))
        assertEquals(LoginFlow.Remembered.SUPERSEDED,
            LoginFlow.rememberSignIn(vault, dir, origin, g1, "tester", older, log))
        assertEquals(newer.deviceId, vault.stored!!.deviceId)
        assertEquals(listOf(older.deviceId), dir.revoked)
    }

    @Test
    fun `a sign-in that cannot be written is not kept, and its device is ended`() {
        val vault = MemoryVault()
        val dir = FakeDirectory()
        val g = LoginFlow.beginSignIn(vault, log)
        vault.failSave = true
        val s = signedIn("full")
        assertEquals(LoginFlow.Remembered.NOT_SAVED,
            LoginFlow.rememberSignIn(vault, dir, origin, g, "tester", s, log))
        assertEquals(null, vault.stored)
        assertEquals(listOf(s.deviceId), dir.revoked)
    }

    // ---------------------------------------------------------------- signing out

    @Test
    fun `signing out erases the credential and tells the directory`() {
        val c = login("out")
        val vault = MemoryVault().apply { stored = c }
        val dir = FakeDirectory()
        val r = LoginFlow.signOut(vault, dir, origin, log)
        assertTrue(r.localDone && r.serverTold && !r.owed)
        assertEquals(null, vault.stored)
        assertEquals(listOf(c.deviceId), dir.revoked)
        assertEquals(listOf(c.revokeToken), dir.revokeTokens)
        assertTrue(vault.owedList.isEmpty())
        assertEquals(1L, vault.counter)
        assertEquals(LoginFlow.Return.NO_CREDENTIAL, LoginFlow.comeBack(vault, dir, origin, log).outcome)
        assertTrue(dir.presented.isEmpty())
    }

    @Test
    fun `with the directory out of reach the credential still goes, and the sign-out is owed`() {
        val c = login("offline")
        val vault = MemoryVault().apply { stored = c }
        val dir = FakeDirectory().apply { revokeAnswer = LoginFlow.Call.UNREACHABLE }
        val r = LoginFlow.signOut(vault, dir, origin, log)
        assertTrue(r.localDone && !r.serverTold && r.owed)
        assertEquals(null, vault.stored)
        assertEquals(listOf(LoginFlow.OwedSignOut(origin, c.deviceId, c.revokeToken)), vault.owedList)
        assertEquals(1, LoginFlow.settleOwedSignOuts(vault, dir, origin, log))
        dir.revokeAnswer = LoginFlow.Call.OK
        assertEquals(0, LoginFlow.settleOwedSignOuts(vault, dir, origin, log))
        assertTrue(vault.owedList.isEmpty())
    }

    @Test
    fun `another server's credential and debts are not sent to this one`() {
        val other = login("theirs", "https://another.example:443")
        val vault = MemoryVault().apply {
            stored = other
            owedList.add(LoginFlow.OwedSignOut(other.serverOrigin, other.deviceId, other.revokeToken))
        }
        val dir = FakeDirectory()
        LoginFlow.signOut(vault, dir, origin, log)
        assertEquals(other, vault.stored)
        assertEquals(1, LoginFlow.settleOwedSignOuts(vault, dir, origin, log))
        assertTrue(dir.revoked.isEmpty())
        assertEquals(1, vault.owedList.size)
    }

    @Test
    fun `with nothing stored there is nothing to tell the directory`() {
        val dir = FakeDirectory()
        val r = LoginFlow.signOut(MemoryVault(), dir, origin, log)
        assertTrue(r.localDone)
        assertTrue(dir.revoked.isEmpty())
    }

    // ---------------------------------------------------------------- what is written down

    @Test
    fun `no credential, revoke token or session is in any log line or toString`() {
        val vault = MemoryVault().apply { stored = login("logged") }
        val dir = FakeDirectory()
        LoginFlow.comeBack(vault, dir, origin, log)
        val g = LoginFlow.beginSignIn(vault, log)
        val s = signedIn("logged")
        LoginFlow.rememberSignIn(vault, dir, origin, g, "tester", s, log)
        dir.revokeAnswer = LoginFlow.Call.UNREACHABLE
        LoginFlow.signOut(vault, dir, origin, log)
        LoginFlow.settleOwedSignOuts(vault, dir, origin, log)
        dir.refreshAnswer = LoginFlow.Call.REJECTED
        vault.stored = login("logged-2")
        vault.owedList.clear()
        val back = LoginFlow.comeBack(vault, dir, origin, log)

        val written = synchronized(lines) { lines.toList() } + listOf(
            login("logged").toString(), s.toString(), back.toString(),
            LoginFlow.OwedSignOut(origin, s.deviceId, s.revokeToken).toString(),
            DirectoryClient.DeviceLogin(s, 0L, DirectoryClient.ObserveEndpoint()).toString())
        assertTrue("the cases logged something to search", written.size >= 8)
        val secrets = listOf(
            "credential-logged-7c1e9a44", "revoke-logged-b20f6d33", "rotated-1-91aa04f6",
            "session-1-5e0a71c9", s.sessionToken, s.deviceCredential, s.revokeToken,
            "credential-logged-2-7c1e9a44", "revoke-logged-2-b20f6d33")
        for (line in written) {
            for (secret in secrets) assertFalse("$secret in: $line", line.contains(secret))
            // A device is named by eight characters, never the whole id.
            assertFalse(line, line.contains(s.deviceId))
        }
    }

    // ---------------------------------------------------------------- the directory's answers

    @Test
    fun `only 401 is an answer about the credential`() {
        assertEquals(LoginFlow.Call.OK, DirectoryClient.callFor(200))
        assertEquals(LoginFlow.Call.REJECTED, DirectoryClient.callFor(401))
        assertEquals(LoginFlow.Call.UNSUPPORTED, DirectoryClient.callFor(404))
        assertEquals(LoginFlow.Call.UNSUPPORTED, DirectoryClient.callFor(405))
        assertEquals(LoginFlow.Call.LIMITED, DirectoryClient.callFor(429))
        for (status in listOf(301, 302, 307, 400, 403, 409, 500, 502, 503)) {
            assertEquals(status.toString(), LoginFlow.Call.FAILED, DirectoryClient.callFor(status))
        }
    }

    // ---------------------------------------------------------------- the session an older version stored

    @Test
    fun `a session an older version stored is used only if it is still good and from this server`() {
        val server = "https://gnlink.example"
        val former = listOf("https://rem.example")
        val now = 1_800_000_000_000L
        fun usable(url: String, token: String, expires: Long) =
            DirectoryClient.legacySessionUsable(url, token, expires, now, server, former)

        assertTrue(usable("https://gnlink.example", "t", now + 1000))
        assertTrue("a listed former name", usable("https://rem.example", "t", now + 1000))
        assertTrue("no expiry recorded", usable("https://gnlink.example", "t", 0))
        assertFalse("expired", usable("https://gnlink.example", "t", now - 1))
        assertFalse("expired exactly now", usable("https://gnlink.example", "t", now))
        assertFalse("no token", usable("https://gnlink.example", "", now + 1000))
        assertFalse("another server", usable("http://127.0.0.1:1", "t", now + 1000))
        assertFalse("the former name over http", usable("http://rem.example", "t", now + 1000))
        assertFalse("no address at all", usable("", "t", now + 1000))
    }

    // ---------------------------------------------------------------- the source

    private fun main(relative: String): File {
        val file = File("src/main/$relative")
        assertTrue("not found: ${file.absolutePath}", file.exists())
        return file
    }

    @Test
    fun `nothing writes a session to preferences`() {
        val sources = main("java/com/remote60/androiddirect").listFiles { f -> f.extension == "kt" }!!
        for (source in sources) {
            val text = source.readText()
            assertFalse(source.name, Regex("""put(String|Long)\(\s*KEY_(SESSION|EXPIRES)""").containsMatchIn(text))
            assertFalse(source.name, text.contains("fun saveSession("))
        }
        val client = main("java/com/remote60/androiddirect/DirectoryClient.kt").readText()
        assertTrue("the stored copy is removed",
            client.contains(".remove(KEY_SESSION).remove(KEY_EXPIRES).commit()"))
    }

    /** The code of a source file: no comments, and nothing that is inside a string literal. */
    private fun codeOf(text: String): String =
        text.lines()
            .filterNot { it.trim().startsWith("*") || it.trim().startsWith("//") || it.trim().startsWith("/*") }
            .joinToString("\n") { withoutLiterals(it) }

    /** One line with what is between double quotes taken out, and a trailing comment with it. */
    private fun withoutLiterals(line: String): String {
        val out = StringBuilder()
        var inside = false
        var i = 0
        while (i < line.length) {
            val c = line[i]
            if (inside) {
                if (c == '\\') i++ else if (c == '"') inside = false
            } else if (c == '"') {
                inside = true
                out.append("\"\"")
            } else if (c == '/' && i + 1 < line.length && line[i + 1] == '/') {
                break
            } else {
                out.append(c)
            }
            i++
        }
        return out.toString()
    }

    @Test
    fun `the password is never handed to the vault`() {
        val activity = main("java/com/remote60/androiddirect/MainActivity.kt").readText()
        // Neither the flow nor the vault has a parameter, a field or a call that names one. The
        // word does appear in what they SAY ("signed in without a password"), which is why
        // string literals are taken out before looking.
        for (name in listOf("LoginFlow.kt", "KeystoreLoginVault.kt")) {
            val code = codeOf(main("java/com/remote60/androiddirect/$name").readText())
            assertFalse(name, code.contains("password", ignoreCase = true))
            assertFalse(name, Regex("""\bpw\b""").containsMatchIn(code))
        }
        // The stripping itself: a literal and a trailing comment go, the code around them stays.
        assertEquals("val x = \"\" ", codeOf("val x = \"a password\" // password"))
        assertTrue(codeOf("fun f(password: String)").contains("password"))
        assertTrue(activity.contains("loginPasswordInput.setText(\"\")"))
    }

    @Test
    fun `backups leave the stored sign-in and the old session file out`() {
        val manifest = main("AndroidManifest.xml").readText()
        assertTrue(manifest.contains("android:fullBackupContent=\"@xml/backup_rules\""))
        assertTrue(manifest.contains("android:dataExtractionRules=\"@xml/data_extraction_rules\""))
        val vault = main("java/com/remote60/androiddirect/KeystoreLoginVault.kt").readText()
        assertTrue(vault.contains("const val PREFS = \"gnlink_login\""))
        assertTrue(vault.contains("const val KEY_ALIAS = \"gnlink.login.v1\""))
        for (rules in listOf("res/xml/backup_rules.xml", "res/xml/data_extraction_rules.xml")) {
            val text = main(rules).readText()
            assertTrue(rules, text.contains("<exclude domain=\"sharedpref\" path=\"gnlink_login.xml\""))
            assertTrue(rules, text.contains("<exclude domain=\"sharedpref\" path=\"remote60_directory.xml\""))
        }
        val transfer = main("res/xml/data_extraction_rules.xml").readText()
        assertTrue(transfer.contains("<cloud-backup>") && transfer.contains("<device-transfer>"))
        assertNotEquals(-1, transfer.indexOf("gnlink_login.xml", transfer.indexOf("<device-transfer>")))
    }

    @Test
    fun `the sign-in screen has a retry that starts hidden`() {
        val layout = main("res/layout/activity_main.xml").readText()
        val retry = Regex("""<Button[^>]*android:id="@\+id/loginRetryButton"[^>]*/>""").find(layout)
        assertTrue(retry != null)
        assertTrue(retry!!.value.contains("android:visibility=\"gone\""))
    }
}
