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

    /**
     * The vault as a few fields. `failSave`, `failCounter`, `failOwed` and `failErase` are each
     * one write that does not happen; `owedUnreadable` is a list of sign-outs that cannot be read.
     */
    private class MemoryVault : LoginFlow.Vault {
        var stored: LoginFlow.StoredLogin? = null
        var unreadable = false
        var counter = 0L
        var failSave = false
        var failCounter = false
        var failOwed = false
        var failErase = false
        var owedUnreadable = false
        var setAside = 0
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
        override fun erase(): Boolean {
            if (failErase) return false
            stored = null; unreadable = false; return true
        }
        override fun generation(): Long = counter
        override fun bumpGeneration(): Long = if (failCounter) 0L else ++counter
        override fun owed(): LoginFlow.OwedRead =
            if (owedUnreadable) LoginFlow.OwedRead.Unreadable("fixture")
            else LoginFlow.OwedRead.Ok(owedList.toList())
        // The same refusals KeystoreLoginVault makes: not over an unreadable list, not past the bound.
        override fun addOwed(owed: LoginFlow.OwedSignOut): Boolean {
            if (failOwed || owedUnreadable) return false
            if (owedList.count { it.deviceId != owed.deviceId } >= LoginFlow.MAX_OWED) return false
            owedList.removeAll { it.deviceId == owed.deviceId }
            owedList.add(owed)
            return true
        }
        override fun removeOwed(deviceId: String): Boolean {
            if (failOwed || owedUnreadable) return false
            owedList.removeAll { it.deviceId == deviceId }
            return true
        }
        override fun setAsideOwed(): Boolean {
            if (!owedUnreadable) return false
            owedUnreadable = false
            owedList.clear()
            ++setAside
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

    // ---------------------------------------------------------------- r5: a write that fails stops what follows

    @Test
    fun `r5-1 the counter cannot be written - the sign-out is not made and a late answer is not stored`() {
        val c = login("counter")
        val vault = MemoryVault().apply { stored = c }
        val dir = FakeDirectory()
        val g = LoginFlow.beginSignIn(vault, log)
        assertNotEquals(0L, g)
        vault.failCounter = true
        val out = LoginFlow.signOut(vault, dir, origin, log)
        assertFalse("THE SIGN-OUT IS NOT MADE, and says so", out.localDone)
        assertEquals("the sign-out could not be recorded", out.detail)
        assertEquals("nothing was erased", c, vault.stored)
        assertTrue("nothing was sent", dir.revoked.isEmpty())
        assertTrue("nothing is owed", vault.owedList.isEmpty())

        // The answer to the sign-in from before arrives now. Nothing made it late, so it must
        // not be kept: the store whose counter cannot be written keeps nothing.
        val late = signedIn("counter-late")
        assertEquals("THE ANSWER THAT ARRIVES AFTERWARDS IS NOT STORED", LoginFlow.Remembered.NOT_SAVED,
            LoginFlow.rememberSignIn(vault, dir, origin, g, "tester", late, log))
        assertEquals(c, vault.stored)
        assertTrue("and the device it carried is ended", dir.revoked.contains(late.deviceId))

        assertEquals("a sign-in cannot even begin", 0L, LoginFlow.beginSignIn(vault, log))

        vault.failCounter = false
        assertTrue("once the counter can be written the sign-out is made",
            LoginFlow.signOut(vault, dir, origin, log).localDone)
        assertEquals(null, vault.stored)
    }

    @Test
    fun `r5-1 a refused or owed credential is not erased without the counter`() {
        val vault = MemoryVault().apply { stored = login("refused-nocounter"); failCounter = true }
        val dir = FakeDirectory().apply { refreshAnswer = LoginFlow.Call.REJECTED }
        assertEquals(LoginFlow.Return.REJECTED, LoginFlow.comeBack(vault, dir, origin, log).outcome)
        assertEquals(login("refused-nocounter"), vault.stored)

        val c = login("owed-nocounter")
        vault.stored = c
        vault.owedList.add(LoginFlow.OwedSignOut(origin, c.deviceId, c.revokeToken))
        val before = dir.presented.size
        assertEquals(LoginFlow.Return.SIGNED_OUT, LoginFlow.comeBack(vault, dir, origin, log).outcome)
        assertEquals("still not presented", before, dir.presented.size)
        assertEquals(c, vault.stored)
    }

    @Test
    fun `r5-2 the sign-out cannot be written down - the credential stays unless the directory ends it`() {
        val c = login("unwritten")
        val vault = MemoryVault().apply { stored = c; failOwed = true }
        val dir = FakeDirectory().apply { revokeAnswer = LoginFlow.Call.UNREACHABLE }
        val out = LoginFlow.signOut(vault, dir, origin, log)
        assertFalse("IT IS NOT MADE", out.localDone)
        assertFalse(out.owed)
        assertEquals("the sign-out could not be written down and the server could not be told", out.detail)
        assertEquals("THE CREDENTIAL, AND WITH IT THE MEANS OF ENDING THE DEVICE, IS STILL THERE", c, vault.stored)

        dir.revokeAnswer = LoginFlow.Call.OK
        val again = LoginFlow.signOut(vault, dir, origin, log)
        assertTrue("when the directory confirms the device is ended, the credential goes", again.localDone)
        assertTrue(again.serverTold)
        assertEquals(null, vault.stored)
    }

    @Test
    fun `r5-2 a sign-in does not write over one it could neither record nor end`() {
        val before = login("before")
        val vault = MemoryVault().apply { stored = before }
        val dir = FakeDirectory().apply { revokeAnswer = LoginFlow.Call.UNREACHABLE }
        val g = LoginFlow.beginSignIn(vault, log)
        vault.failOwed = true
        val s = signedIn("replacing")
        assertEquals(LoginFlow.Remembered.NOT_SAVED,
            LoginFlow.rememberSignIn(vault, dir, origin, g, "tester", s, log))
        assertEquals("the one before is still stored", before, vault.stored)
        assertEquals("the directory was asked to end both", listOf(before.deviceId, s.deviceId), dir.revoked)

        dir.revokeAnswer = LoginFlow.Call.OK
        val g2 = LoginFlow.beginSignIn(vault, log)
        val s2 = signedIn("replacing-2")
        assertEquals("when the directory confirms the one before is ended, the new one is stored",
            LoginFlow.Remembered.STORED, LoginFlow.rememberSignIn(vault, dir, origin, g2, "tester", s2, log))
        assertEquals(s2.deviceId, vault.stored!!.deviceId)
    }

    @Test
    fun `r5-3 the seventeenth does not push the first out`() {
        val vault = MemoryVault()
        for (i in 0 until LoginFlow.MAX_OWED) {
            vault.owedList.add(LoginFlow.OwedSignOut(origin, "owed-$i", "revoke-owed-$i"))
        }
        val c = login("seventeenth")
        vault.stored = c
        val dir = FakeDirectory().apply { revokeAnswer = LoginFlow.Call.UNREACHABLE }
        val out = LoginFlow.signOut(vault, dir, origin, log)
        assertEquals("all sixteen are still owed", (0 until 16).map { "owed-$it" }, vault.owedList.map { it.deviceId })
        assertFalse("the seventeenth sign-out is not made", out.localDone)
        assertEquals("its credential is kept", c, vault.stored)

        dir.revokeAnswer = LoginFlow.Call.OK
        assertEquals("the sixteen are settled", 0, LoginFlow.settleOwedSignOuts(vault, dir, origin, log))
        assertTrue(LoginFlow.signOut(vault, dir, origin, log).localDone)
        assertEquals(null, vault.stored)
    }

    @Test
    fun `r5-3 a list that cannot be read is not empty, not written over, and blocks coming back`() {
        val c = login("unreadable-list")
        val vault = MemoryVault().apply { stored = c; owedUnreadable = true }
        val dir = FakeDirectory()
        assertEquals("THE STORED CREDENTIAL IS NOT PRESENTED", LoginFlow.Return.UNREADABLE,
            LoginFlow.comeBack(vault, dir, origin, log).outcome)
        assertTrue(dir.presented.isEmpty())
        assertFalse("NOTHING IS WRITTEN OVER IT",
            vault.addOwed(LoginFlow.OwedSignOut(origin, "x", "y")))
        assertTrue(vault.owedUnreadable)
        assertEquals("settling sends nothing and says something is still owed", 1,
            LoginFlow.settleOwedSignOuts(vault, dir, origin, log))
        assertTrue(dir.revoked.isEmpty())

        val g = LoginFlow.beginSignIn(vault, log)
        val s = signedIn("on-purpose")
        assertEquals("a sign-in made on purpose is stored", LoginFlow.Remembered.STORED,
            LoginFlow.rememberSignIn(vault, dir, origin, g, "tester", s, log))
        assertEquals("the unreadable list is set aside, not overwritten", 1, vault.setAside)
        assertEquals("the credential it replaced is owed in a new one", listOf(c.deviceId),
            vault.owedList.map { it.deviceId })
    }

    @Test
    fun `r5-4 a 404 from the logout route is not a device ended`() {
        val c = login("old-route")
        val vault = MemoryVault().apply { stored = c }
        val dir = FakeDirectory().apply { revokeAnswer = LoginFlow.Call.UNSUPPORTED }
        val out = LoginFlow.signOut(vault, dir, origin, log)
        assertTrue("the credential goes", out.localDone)
        assertEquals(null, vault.stored)
        assertTrue("A 404 IS NOT A DEVICE ENDED: the sign-out is still owed", out.owed)
        assertEquals(listOf(c.deviceId), vault.owedList.map { it.deviceId })
        assertEquals(1, LoginFlow.settleOwedSignOuts(vault, dir, origin, log))
        assertEquals("stays owed while the server has no such route", 1, vault.owedList.size)

        dir.revokeAnswer = LoginFlow.Call.OK
        assertEquals("a server with the route is told, and the debt is settled", 0,
            LoginFlow.settleOwedSignOuts(vault, dir, origin, log))
        assertTrue(vault.owedList.isEmpty())

        // A device that is not kept and meets a 404 is owed too.
        val g = LoginFlow.beginSignIn(vault, log)
        LoginFlow.signOut(vault, dir, origin, log)
        dir.revokeAnswer = LoginFlow.Call.UNSUPPORTED
        val late = signedIn("late-404")
        assertEquals(LoginFlow.Remembered.SUPERSEDED,
            LoginFlow.rememberSignIn(vault, dir, origin, g, "tester", late, log))
        assertEquals(listOf(late.deviceId), vault.owedList.map { it.deviceId })

        dir.revokeAnswer = LoginFlow.Call.REJECTED
        assertEquals("a 401 still means there is nothing more to send", 0,
            LoginFlow.settleOwedSignOuts(vault, dir, origin, log))
    }

    @Test
    fun `r5-5 nothing issued - the account before is erased and owed, and a late one is not used`() {
        val a = login("account-a")
        val vault = MemoryVault().apply { stored = a }
        val dir = FakeDirectory()
        val g = LoginFlow.beginSignIn(vault, log)
        assertEquals("signing in as B with nothing issued: the sign-in stands",
            LoginFlow.Remembered.NOT_ISSUED, LoginFlow.rememberSignIn(
                vault, dir, origin, g, "account-b", LoginFlow.SignedIn("b-session", "", "", ""), log))
        assertEquals("A'S STORED SIGN-IN IS GONE", null, vault.stored)
        assertEquals("A's device is owed a sign-out", listOf(a.deviceId), vault.owedList.map { it.deviceId })
        assertEquals(LoginFlow.Return.NO_CREDENTIAL, LoginFlow.comeBack(vault, dir, origin, log).outcome)

        val g2 = LoginFlow.beginSignIn(vault, log)
        LoginFlow.signOut(vault, dir, origin, log)
        assertEquals("AN ANSWER WITH NOTHING ISSUED IS LATE LIKE ANY OTHER", LoginFlow.Remembered.SUPERSEDED,
            LoginFlow.rememberSignIn(vault, dir, origin, g2, "account-b",
                LoginFlow.SignedIn("b-late-session", "", "", ""), log))
    }

    @Test
    fun `r5-5 the app does not sign in without the store`() {
        // Static: the source no longer has the fallback. Behaviour is in the case above
        // (beginSignIn returns 0 and the flow refuses what follows).
        val activity = codeOf(main("java/com/remote60/androiddirect/MainActivity.kt").readText())
        assertFalse(activity.contains("loginWithDevice(url, id, password, null)"))
        assertTrue(activity.contains("if (generation == 0L) {"))
    }

    @Test
    fun `r5-6 another account's stored sign-in is not presented for this screen`() {
        val b = login("account-b").copy(accountId = "account-b")
        val vault = MemoryVault().apply { stored = b }
        val dir = FakeDirectory()
        val r = LoginFlow.comeBack(vault, dir, origin, log, onlyForAccount = "account-a")
        assertEquals(LoginFlow.Return.OTHER_ACCOUNT, r.outcome)
        assertEquals("", r.sessionToken)
        assertTrue("nothing was sent", dir.presented.isEmpty())
        assertEquals("left as it is, for its owner", b, vault.stored)

        val own = LoginFlow.comeBack(vault, dir, origin, log, onlyForAccount = "account-b")
        assertEquals(LoginFlow.Return.SIGNED_IN, own.outcome)
        assertEquals("account-b", own.accountId)

        val anyone = LoginFlow.comeBack(vault, dir, origin, log)
        assertEquals("with nobody signed in yet it comes back as whoever is stored, and says who",
            "account-b", anyone.accountId)
    }

    // ---------------------------------------------------------------- r6

    @Test
    fun `r6-1 a late answer to the sign-out of A does not erase B, stored meanwhile by another sign-in`() {
        val a = login("r6-a")
        val b = login("r6-b")
        val vault = MemoryVault().apply { stored = a; failOwed = true }
        // The directory's answer is held until another sign-in has stored B.
        val dir = object : LoginFlow.Directory {
            val revoked = ArrayList<String>()
            override fun refresh(deviceId: String, deviceCredential: String) =
                LoginFlow.Refreshed(LoginFlow.Call.UNREACHABLE)
            override fun revoke(deviceId: String, revokeToken: String): LoginFlow.Call {
                revoked.add(deviceId)
                vault.stored = b      // the other window, complete
                return LoginFlow.Call.OK
            }
        }
        val out = LoginFlow.signOut(vault, dir, origin, log)
        assertEquals("A LATE ANSWER TO THE SIGN-OUT OF A DOES NOT ERASE B", b, vault.stored)
        assertTrue("A's sign-out is done: ended at the directory, not on disk", out.localDone && out.serverTold)
        assertEquals(listOf(a.deviceId), dir.revoked)

        // With nobody in between, A still goes (r5-2).
        val alone = MemoryVault().apply { stored = a; failOwed = true }
        assertTrue(LoginFlow.signOut(alone, FakeDirectory(), origin, log).localDone)
        assertEquals(null, alone.stored)
    }

    @Test
    fun `r6-2 the session and its account are held together, past the Activity`() {
        try {
            DirectoryClient.adoptSession("r6-session-3c1f", "account-r6")
            // A recreated Activity has none of the old one's fields; what it can read is this.
            assertEquals("r6-session-3c1f", DirectoryClient.session())
            assertEquals("account-r6", DirectoryClient.sessionAccount())

            // The session is refused later; the stored sign-in brings it back for that account,
            // without a password.
            val vault = MemoryVault().apply { stored = login("r6-2").copy(accountId = "account-r6") }
            val back = LoginFlow.comeBack(vault, FakeDirectory(), origin, log,
                onlyForAccount = DirectoryClient.sessionAccount())
            assertEquals(LoginFlow.Return.SIGNED_IN, back.outcome)
            DirectoryClient.adoptSession(back.sessionToken, back.accountId)
            assertEquals("account-r6", DirectoryClient.sessionAccount())

            DirectoryClient.dropSession()
            assertEquals("", DirectoryClient.session())
            assertEquals("the account goes with the session", "", DirectoryClient.sessionAccount())
        } finally {
            DirectoryClient.dropSession()
        }
        // Static: the Activity keeps no copy of its own that a recreation would lose.
        val activity = codeOf(main("java/com/remote60/androiddirect/MainActivity.kt").readText())
        assertFalse(activity.contains("sessionAccountId"))
        assertTrue(activity.contains("val account = DirectoryClient.sessionAccount()"))
    }

    @Test
    fun `r6-3 an unreadable list with nothing stored is set aside by a sign-in, and the next start comes back`() {
        val vault = MemoryVault().apply { owedUnreadable = true }
        val dir = FakeDirectory()
        val g = LoginFlow.beginSignIn(vault, log)
        assertEquals(LoginFlow.Remembered.STORED,
            LoginFlow.rememberSignIn(vault, dir, origin, g, "tester", signedIn("r6-3"), log))
        assertEquals("THE UNREADABLE LIST IS SET ASIDE, though nothing was stored beside it", 1, vault.setAside)
        assertEquals("the next start comes back signed in", LoginFlow.Return.SIGNED_IN,
            LoginFlow.comeBack(vault, dir, origin, log).outcome)
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
