base = 'C:/work/gnlink-wt/apk-prelogin-select/apps/android_direct_client/app/src/main/'

# ---------------------------------------------------------------- strings
p = base + 'res/values/strings.xml'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
a = s.index('    <string name="settings_desktop_backend_hint">')
b = s.index('    <string name="settings_desktop_backend_reason_not_yet">')
b = s.index('\n', b) + 1
s = s[:a] + '''    <!-- ua2-apk r2: the scope ("this connection only") is said only when the PC confirms it (status line). -->
    <string name="settings_desktop_backend_hint">PC가 바탕화면을 캡처하는 방식 · 연결한 PC에 바로 요청 · 창 캡처는 항상 WGC</string>
    <string name="settings_desktop_backend_not_connected">연결한 뒤 고를 수 있습니다</string>
    <string name="settings_desktop_backend_checking">이 PC가 결과를 알려주는지 확인하는 중…</string>
    <string name="settings_desktop_backend_no_answer">이 PC는 적용 결과를 알려주지 않습니다 · 이번 연결에만 적용되는지도 보장할 수 없습니다</string>
    <string name="settings_desktop_backend_no_answer_asked">요청: %1$s 보냄 · 이 PC는 적용 결과를 알려주지 않습니다 · 이번 연결에만 적용되는지도 보장할 수 없습니다</string>
    <string name="settings_desktop_backend_now_desktop">지금 바탕화면 캡처: %1$s</string>
    <string name="settings_desktop_backend_now_window">지금 창 캡처: WGC</string>
    <string name="settings_desktop_backend_idle">%1$s</string>
    <string name="settings_desktop_backend_applying">요청: %1$s · 적용 중… (%2$s)</string>
    <string name="settings_desktop_backend_applied">요청: %1$s 적용됨 · %2$s</string>
    <string name="settings_desktop_backend_stored">요청: %1$s · 바탕화면을 볼 때 적용 · %2$s</string>
    <string name="settings_desktop_backend_refused">요청: %1$s 적용 안 됨 · %2$s</string>
    <string name="settings_desktop_backend_scope">이 연결에만 적용</string>
    <string name="settings_desktop_backend_reason_window">창은 WGC로 캡처</string>
    <string name="settings_desktop_backend_reason_quarantine">DXGI 문제로 WGC 사용</string>
    <string name="settings_desktop_backend_reason_logon">로그인 전 화면은 DXGI만</string>
''' + s[b:]
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

# ---------------------------------------------------------------- MainActivity
p = base + 'java/com/remote60/androiddirect/MainActivity.kt'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
a = s.index('    /** ua2-apk r1: the buttons and the line show what the HOST says runs now (DesktopBackendPresentation). */')
b = s.index('    private fun syncConnectedClientPreferences')
s = s[:a] + '''    /**
     * ua2-apk r1/r2: the buttons and the line under them (DesktopBackendPresentation): the host's capability, the
     * CURRENT request (matched by its wire seq), and what captures now -- kept apart. Nothing is lit until the host has
     * handled this connection's method; the session scope is said only when the host confirms it.
     */
    private fun updateDesktopBackendButtons() {
        if (!::settingsDesktopBackendStatusText.isInitialized) return
        val p = DesktopBackendPresentation
        val connected = desktopBackendConnected()
        val status = if (connected) DesktopBackendPresentation.HostStatus.of(NativeSessionBridge.nativeGetDesktopBackendStatus()) else null
        val v = p.present(connected, sessionDesktopBackend?.code, status)
        for ((button, code) in listOf(settingsDesktopBackendDxgiButton to p.DXGI, settingsDesktopBackendWgcButton to p.WGC,
                                      settingsDesktopBackendGdiButton to p.GDI)) {
            button.text = p.label(code)
            button.isSelected = v.lit == code
            button.isEnabled = v.enabled
            button.alpha = if (v.enabled) 1f else 0.45f
        }
        val asked = v.asked?.let { p.label(it) }
        val reasons = v.reasons.map {
            getString(when (it) {
                DesktopBackendPresentation.Reason.WINDOW_TARGET -> R.string.settings_desktop_backend_reason_window
                DesktopBackendPresentation.Reason.DXGI_QUARANTINED -> R.string.settings_desktop_backend_reason_quarantine
                DesktopBackendPresentation.Reason.BEFORE_LOGIN -> R.string.settings_desktop_backend_reason_logon
            })
        }.filter { it != getString(R.string.settings_desktop_backend_reason_window) }  // said by "지금 창 캡처" itself
        var now = if (v.windowTarget) getString(R.string.settings_desktop_backend_now_window)
            else getString(R.string.settings_desktop_backend_now_desktop, p.label(v.active))
        if (reasons.isNotEmpty()) now += " (" + reasons.joinToString(", ") + ")"
        var text = when (v.state) {
            DesktopBackendPresentation.State.NOT_CONNECTED -> getString(R.string.settings_desktop_backend_not_connected)
            DesktopBackendPresentation.State.CHECKING -> getString(R.string.settings_desktop_backend_checking)
            DesktopBackendPresentation.State.NO_ANSWER_HOST ->
                if (asked != null) getString(R.string.settings_desktop_backend_no_answer_asked, asked)
                else getString(R.string.settings_desktop_backend_no_answer)
            DesktopBackendPresentation.State.IDLE -> getString(R.string.settings_desktop_backend_idle, now)
            DesktopBackendPresentation.State.APPLYING -> getString(R.string.settings_desktop_backend_applying, asked ?: "?", now)
            DesktopBackendPresentation.State.APPLIED -> getString(R.string.settings_desktop_backend_applied, asked ?: "?", now)
            DesktopBackendPresentation.State.STORED_FOR_DESKTOP -> getString(R.string.settings_desktop_backend_stored, asked ?: "?", now)
            DesktopBackendPresentation.State.REFUSED -> getString(R.string.settings_desktop_backend_refused, asked ?: "?", now)
        }
        if (v.sessionScoped && v.state != DesktopBackendPresentation.State.NOT_CONNECTED &&
            v.state != DesktopBackendPresentation.State.CHECKING) {
            text += " · " + getString(R.string.settings_desktop_backend_scope)
        }
        if (settingsDesktopBackendStatusText.text?.toString() != text) {
            settingsDesktopBackendStatusText.text = text
            diagnosticsLog.log("desktop_backend_ui", "state=${v.state} lit=${v.lit?.let { p.label(it) } ?: "none"} " +
                "asked=${asked ?: "none"} window=${v.windowTarget} scoped=${v.sessionScoped} enabled=${v.enabled} " +
                "seq=${status?.seq ?: 0} sentSeq=${status?.intentSentSeq ?: 0}")
        }
    }

''' + s[b:]
s = s.replace('''        // ua2-apk r1: nothing is re-sent on connect (no stored backend). While the settings screen shows, the host
        // is asked what runs now -- every 2 s at most -- and the answer is what the buttons show.''',
              '''        // ua2-apk r1: nothing is re-sent on connect (no stored backend). While the settings screen shows, the host
        // is asked what runs now -- about every 2 s (checked on the status poll) -- and the answer is what the buttons show.''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
