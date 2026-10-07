package com.remote60.androiddirect

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * The phone's remote-reboot menu (prelogin-ui r1, item 8): the shared rule, the confirm popup's text,
 * one confirm -> one request, and what a lost answer or the restart's disconnect show.
 */
class RemoteRebootTest {

    private fun vectorsFile(): File {
        var dir: File? = File(System.getProperty("user.dir") ?: ".").absoluteFile
        val tried = mutableListOf<String>()
        while (dir != null) {
            val candidate = File(dir, "apps/shared/reboot_menu_vectors.txt")
            tried += candidate.path
            if (candidate.isFile) return candidate
            dir = dir.parentFile
        }
        throw AssertionError("shared vectors file not found; tried:\n" + tried.joinToString("\n"))
    }

    @Test
    fun `the shared vectors are answered the same way here`() {
        var rows = 0
        vectorsFile().readLines().forEach { raw ->
            val line = raw.trim()
            if (line.isEmpty() || line.startsWith("#")) return@forEach
            val f = line.split(Regex("\\s+"))
            val flags = f[0].removePrefix("0x").toInt(16)
            val m = RemoteRebootPolicy.menu(flags)
            assertEquals(line, f[2] == "1", m.enabled)
            assertEquals(line, f[3] == "1", m.disabledBecauseOldHost)
            assertEquals(line, f[4] == "1", m.showPreloginWarning)
            if (f[1] == "android") {
                // The phone never offers the sealed entry (PC-first, v1); it shows the note instead.
                assertEquals(line, "0", f[5])
                assertEquals(line, f[6] == "1", RemoteRebootPolicy.showCredentialUpdateNote(flags))
            }
            rows++
        }
        assertTrue("rows: $rows", rows >= 10)
    }

    @Test
    fun `the popup says what a restart costs, and warns when the PC will not be reachable before login`() {
        val plain = RemoteRebootPolicy.confirmMessage(RemoteRebootPolicy.menu(0x4000 or 0x8000))
        assertEquals("원격 PC를 다시 시작합니다. 저장하지 않은 작업은 사라질 수 있습니다.", plain)
        val warned = RemoteRebootPolicy.confirmMessage(RemoteRebootPolicy.menu(0x4000))
        assertTrue(warned.startsWith(plain))
        assertTrue(warned.contains("로그인 전 원격 접속이 꺼져 있어, 다시 시작한 뒤에는 그 PC 에서 로그인할 때까지 접속할 수 없습니다."))
        // A logged-in host with the feature on is never warned (R1: durable-enabled, not "in prelogin now").
        assertFalse(RemoteRebootPolicy.menu(0x4000 or 0x8000).showPreloginWarning)
    }

    @Test
    fun `cancel sends nothing, confirm sends once, a second tap sends nothing more`() {
        var sent = 0
        val flow = RemoteRebootFlow { sent++; 0x5151 }
        val menu = RemoteRebootPolicy.menu(0x4000)
        assertTrue(flow.onMenu(menu))
        flow.onCancel()
        assertEquals(0, sent)
        assertEquals(RemoteRebootFlow.Phase.Idle, flow.phase)
        assertTrue(flow.onMenu(menu))
        assertTrue(flow.onConfirm())
        assertFalse("a double tap is not a second request", flow.onConfirm())
        assertFalse("the menu is not reopened while a request is out", flow.onMenu(menu))
        assertEquals(1, sent)
        assertEquals(0x5151, flow.requestId)
        assertEquals("다시 시작을 요청하는 중…", flow.statusText())
    }

    @Test
    fun `an old host's item is disabled and opens nothing`() {
        var sent = 0
        val flow = RemoteRebootFlow { sent++; 7 }
        val menu = RemoteRebootPolicy.menu(0)
        assertFalse(menu.enabled)
        assertFalse(flow.onMenu(menu))
        assertFalse(flow.onConfirm())
        assertEquals(0, sent)
        assertNull(flow.statusText())
    }

    @Test
    fun `accepted is not complete, and the restart's disconnect is not an error`() {
        val flow = RemoteRebootFlow { 9 }
        flow.onMenu(RemoteRebootPolicy.menu(0x4000))
        flow.onConfirm()
        flow.onOutcome(8, 0, 0)  // another request's answer changes nothing
        assertEquals(RemoteRebootFlow.Phase.Requesting, flow.phase)
        flow.onOutcome(9, 0, 0)
        assertEquals(RemoteRebootFlow.Phase.Accepted, flow.phase)
        assertEquals("재시작 요청을 받았습니다. 원격 PC가 다시 켜지면 다시 접속하세요.", flow.statusText())
        assertTrue(flow.onDisconnected())
    }

    @Test
    fun `a lost answer is reported as not known and never sent again by itself`() {
        var sent = 0
        val flow = RemoteRebootFlow { sent++; 11 }
        flow.onMenu(RemoteRebootPolicy.menu(0x4000))
        flow.onConfirm()
        flow.onOutcome(11, RemoteRebootFlow.OUTCOME_NO_REPLY, 0)
        assertEquals(RemoteRebootFlow.Phase.Unknown, flow.phase)
        assertTrue(flow.statusText()!!.contains("자동으로 다시 요청하지 않습니다"))
        assertFalse(flow.onConfirm())
        assertEquals(1, sent)
        assertTrue(flow.onDisconnected())
    }

    @Test
    fun `refused, not authorized and failed say so`() {
        for ((outcome, phase) in listOf(1 to RemoteRebootFlow.Phase.Refused, 2 to RemoteRebootFlow.Phase.NotAuthorized,
                                        3 to RemoteRebootFlow.Phase.Failed)) {
            val flow = RemoteRebootFlow { 5 }
            flow.onMenu(RemoteRebootPolicy.menu(0x4000))
            flow.onConfirm()
            flow.onOutcome(5, outcome, 1115)
            assertEquals(phase, flow.phase)
            assertFalse("a refusal is not a restart", flow.onDisconnected())
        }
        val failed = RemoteRebootFlow { 5 }
        failed.onMenu(RemoteRebootPolicy.menu(0x4000))
        failed.onConfirm()
        failed.onOutcome(5, 3, 1115)
        assertTrue(failed.statusText()!!.contains("1115"))
    }

    @Test
    fun `the pre-login screen gets the update note, never a plaintext entry`() {
        assertTrue(RemoteRebootPolicy.showCredentialUpdateNote(0x2000))
        assertFalse(RemoteRebootPolicy.showCredentialUpdateNote(0x4000 or 0x8000))
        assertTrue(RemoteRebootPolicy.CREDENTIAL_UPDATE_NOTE.contains("앱 업데이트 뒤"))
    }
}
