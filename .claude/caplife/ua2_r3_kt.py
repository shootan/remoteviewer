base = 'C:/work/gnlink-wt/apk-prelogin-select/apps/android_direct_client/app/src/'

def edit(rel, pairs):
    p = base + rel
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (rel, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

P = 'main/java/com/remote60/androiddirect/DesktopBackendPresentation.kt'
edit(P, [
    (''' * The lit button is this connection's desktop method as the host HANDLED it (status.requested), and only then.''',
     ''' * The lit button is this connection's desktop method as the host HANDLED it (status.requested), and only then.
 *
 * r3 (codex_ua2_apk_src_r2.md N1): "handled" is the host having taken the request (stored, refused, or restarted
 * with it) -- not the capture having switched. APPLIED only when what captures now (active) IS the request; the
 * host's policy putting another in its place (effective, e.g. DXGI quarantined) is POLICY_REPLACED; a request taken
 * while the desktop still captures with another (idle desktop, or a restart that fell back) is RECEIVED -- the facts
 * only: no cause is made up the wire does not carry.'''),
    ('''        /** The current request is not answered yet (not sent, an older answer, or NotYetApplied). */
        APPLYING,
        APPLIED,''', '''        /** The current request is not answered yet (not sent, an older answer, or NotYetApplied). */
        APPLYING,
        /** Handled, and the desktop captures with it now. */
        APPLIED,
        /** Handled; the host's policy uses another in its place (effective), and that one captures now. */
        POLICY_REPLACED,
        /** Handled, but the desktop captures with another now (stored while idle, or a restart that fell back). */
        RECEIVED,'''),
    ('''        val requested: Int,
        val active: Int,
        val windowTarget: Boolean,''', '''        val requested: Int,
        /** The host's policy pick for the request (0: none said). */
        val effective: Int,
        val active: Int,
        val windowTarget: Boolean,'''),
    ('''            state, lit, connected, asked, s?.requested ?: 0, s?.active ?: 0,''',
     '''            state, lit, connected, asked, s?.requested ?: 0, s?.effective ?: 0, s?.active ?: 0,'''),
    ('''        if (!connected) return View(State.NOT_CONNECTED, null, false, null, 0, 0, false, false, emptyList())''',
     '''        if (!connected) return View(State.NOT_CONNECTED, null, false, null, 0, 0, 0, false, false, emptyList())'''),
    ('''        if (asked == null) return view(State.IDLE, handledLit, status, reasons)''',
     '''        if (asked == null) {
            // r3 (N1): no request of ours -- the host's own, once it has one: an answer with no state yet (0 fields,
            // NotYetApplied) is still waiting, not a confirmed "now: ?".
            val noStateYet = status.flags and FLAG_NOT_YET_APPLIED != 0 ||
                (status.flags and FLAG_WINDOW_TARGET == 0 && status.active !in DXGI..GDI)
            return if (noStateYet) view(State.CHECKING) else view(State.IDLE, handledLit, status, reasons)
        }'''),
    ('''        if (status.flags and FLAG_WINDOW_TARGET != 0) return view(State.STORED_FOR_DESKTOP, handledLit, status, reasons)
        return view(State.APPLIED, handledLit, status, reasons)''',
     '''        if (status.flags and FLAG_WINDOW_TARGET != 0) return view(State.STORED_FOR_DESKTOP, handledLit, status, reasons)
        // r3 (N1): handled is not switched -- what captures now decides the word
        val effective = status.effective.takeIf { it in DXGI..GDI } ?: status.requested
        if (effective != status.requested && status.active == effective) return view(State.POLICY_REPLACED, handledLit, status, reasons)
        if (status.active != status.requested) return view(State.RECEIVED, handledLit, status, reasons)
        return view(State.APPLIED, handledLit, status, reasons)'''),
])

edit('main/java/com/remote60/androiddirect/MainActivity.kt', [
    ('''            DesktopBackendPresentation.State.APPLIED -> getString(R.string.settings_desktop_backend_applied, asked ?: "?", now)''',
     '''            DesktopBackendPresentation.State.APPLIED -> getString(R.string.settings_desktop_backend_applied, asked ?: "?", now)
            DesktopBackendPresentation.State.POLICY_REPLACED ->
                getString(R.string.settings_desktop_backend_policy, asked ?: "?", p.label(v.effective), now)
            DesktopBackendPresentation.State.RECEIVED -> getString(R.string.settings_desktop_backend_received, asked ?: "?", now)'''),
])

edit('main/res/values/strings.xml', [
    ('''    <string name="settings_desktop_backend_applied">요청: %1$s 적용됨 · %2$s</string>''',
     '''    <string name="settings_desktop_backend_applied">요청: %1$s 적용됨 · %2$s</string>
    <!-- ua2-apk r3: handled, but not (or not as asked) capturing now -- the facts only -->
    <string name="settings_desktop_backend_policy">요청: %1$s 접수 · PC 정책으로 %2$s 사용 · %3$s</string>
    <string name="settings_desktop_backend_received">요청: %1$s 접수 · 아직 이 방식으로 캡처하지 않음 · %2$s</string>'''),
])

T = 'test/java/com/remote60/androiddirect/DesktopBackendPresentationTest.kt'
edit(T, [
    ('''    private fun st(seq: Long, requested: Int, active: Int, flags: Int = p.FLAG_SESSION_SCOPED,
                   asked: Long = 0, sent: Long = 0, sentSeq: Long = 0, valid: Boolean = true, answers: Boolean = true,
                   roleKnown: Boolean = true) =
        HostStatus(valid, seq, requested, requested, active, flags, 1, answers, roleKnown, asked, sent, sentSeq)''',
     '''    private fun st(seq: Long, requested: Int, active: Int, flags: Int = p.FLAG_SESSION_SCOPED,
                   asked: Long = 0, sent: Long = 0, sentSeq: Long = 0, valid: Boolean = true, answers: Boolean = true,
                   roleKnown: Boolean = true, effective: Int = requested) =
        HostStatus(valid, seq, requested, effective, active, flags, 1, answers, roleKnown, asked, sent, sentSeq)'''),
    ('''    @Test fun quarantineShowsTheRealOne() {
        val v = p.present(true, p.DXGI, st(2, p.DXGI, p.WGC, flags = p.FLAG_SESSION_SCOPED or p.FLAG_DXGI_QUARANTINED,
            asked = 1, sent = 1, sentSeq = 2))
        assertEquals(State.APPLIED, v.state)
        assertEquals(p.WGC, v.active)
        assertEquals(listOf(Reason.DXGI_QUARANTINED), v.reasons)
    }''', '''    // r3 (codex_ua2_apk_src_r2.md N1 (b)): a policy replacement is not "DXGI applied"
    @Test fun quarantineShowsTheRealOne() {
        val v = p.present(true, p.DXGI, st(2, p.DXGI, p.WGC, flags = p.FLAG_SESSION_SCOPED or p.FLAG_DXGI_QUARANTINED,
            asked = 1, sent = 1, sentSeq = 2, effective = p.WGC))
        assertEquals(State.POLICY_REPLACED, v.state)
        assertEquals(p.WGC, v.active)
        assertEquals(p.WGC, v.effective)
        assertEquals(listOf(Reason.DXGI_QUARANTINED), v.reasons)
    }

    // r3 N1 (a): handled GDI, requested = effective = GDI, but DXGI captures (idle desktop stored it, or the restart fell
    // back) -- "received", not "applied"; and it is terminal (no endless applying), the request still lit as handled
    @Test fun handledButNotCapturingIsReceivedNotApplied() {
        val v = p.present(true, p.GDI, st(4, p.GDI, p.DXGI, asked = 2, sent = 2, sentSeq = 4))
        assertEquals(State.RECEIVED, v.state)
        assertEquals(p.GDI, v.lit)
        assertEquals(p.DXGI, v.active)
        assertEquals(State.RECEIVED, p.present(true, p.GDI, st(9, p.GDI, p.DXGI, asked = 2, sent = 2, sentSeq = 4)).state)
        // the same request once the desktop does capture with it: applied (d)
        assertEquals(State.APPLIED, p.present(true, p.GDI, st(10, p.GDI, p.GDI, asked = 2, sent = 2, sentSeq = 4)).state)
    }

    // r3 N1 (c): nothing asked on this connection; an answer with a seq but no state yet (0 fields, NotYetApplied)
    @Test fun noRequestAndNoStateYetIsCheckingNotIdle() {
        assertEquals(State.CHECKING, p.present(true, null, st(1, 0, 0, flags = p.FLAG_SESSION_SCOPED or p.FLAG_NOT_YET_APPLIED)).state)
        assertEquals(State.CHECKING, p.present(true, null, st(1, 0, 0)).state)
        assertNull(p.present(true, null, st(1, 0, 0)).lit)
        // a window target with no desktop state is not "waiting": WGC now is a fact
        assertEquals(State.IDLE, p.present(true, null, st(1, 0, 0, flags = p.FLAG_SESSION_SCOPED or p.FLAG_WINDOW_TARGET)).state)
    }

    // r3 (Codex note): an OLD answer whose requested equals the latest request (A, seq 3 < sentSeq 5) proves nothing
    @Test fun anOldAnswerWithTheSameCodeIsNotTheLatestRequestsAnswer() {
        val old = p.present(true, p.WGC, st(3, p.WGC, p.WGC, asked = 3, sent = 3, sentSeq = 5))
        assertEquals(State.APPLYING, old.state)
        assertNull(old.lit)
    }'''),
])
print('ok')
