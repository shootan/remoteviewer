base = 'C:/work/gnlink-wt/apk-prelogin-select/apps/android_direct_client/app/src/main/'

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

edit('java/com/remote60/androiddirect/DesktopBackendPresentation.kt', [
    ('SessionBackendPresentationTest drives it', 'DesktopBackendPresentationTest drives it')])

# ---------------------------------------------------------------- JNI
edit('cpp/native_bridge.cpp', [
    ('''Java_com_remote60_androiddirect_NativeSessionBridge_nativeRequestDesktopCaptureBackend(''',
     '''Java_com_remote60_androiddirect_NativeSessionBridge_nativeQueryDesktopCaptureBackend(
    JNIEnv* /* env */, jobject /* this */) {
  // ua2-apk r1: ask what runs now (changes nothing); false toward a host that does not answer.
  return g_session_controller.QueryDesktopCaptureBackend() ? JNI_TRUE : JNI_FALSE;
}

// ua2-apk r1: [valid, seq, requested, effective, active, flags, changeCount, hostAnswers] -- this connection's.
extern "C" JNIEXPORT jlongArray JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetDesktopBackendStatus(
    JNIEnv* env, jobject /* this */) {
  const auto s = g_session_controller.DesktopCaptureBackendStatus();
  const jlong v[8] = {s.valid ? 1 : 0, static_cast<jlong>(s.seq), static_cast<jlong>(s.requested),
                      static_cast<jlong>(s.effective), static_cast<jlong>(s.active), static_cast<jlong>(s.flags),
                      static_cast<jlong>(s.changeCount),
                      g_session_controller.HostAnswersDesktopBackendStatus() ? 1 : 0};
  jlongArray out = env->NewLongArray(8);
  if (out) env->SetLongArrayRegion(out, 0, 8, v);
  return out;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeRequestDesktopCaptureBackend('''),
])

edit('java/com/remote60/androiddirect/NativeSessionBridge.kt', [
    ('''    external fun nativeRequestDesktopCaptureBackend(backend: Int): Boolean''',
     '''    external fun nativeRequestDesktopCaptureBackend(backend: Int): Boolean
    /** ua2-apk r1: ask the host what runs now (false: nothing asked -- a host that does not answer). */
    external fun nativeQueryDesktopCaptureBackend(): Boolean
    /** ua2-apk r1: DesktopBackendPresentation.HostStatus.of(...) reads it. */
    external fun nativeGetDesktopBackendStatus(): LongArray?'''),
])

# ---------------------------------------------------------------- strings / layout
edit('res/values/strings.xml', [
    ('''    <string name="settings_desktop_backend_label">Desktop Capture Backend</string>
    <string name="settings_desktop_backend_hint">Choose DXGI, WGC, or isolated GDI for desktop mode. Window-target capture remains WGC.</string>''',
     '''    <string name="settings_desktop_backend_label">바탕화면 캡처 방식</string>
    <!-- ua2-apk r1 (Codex codex_rel_0163_r1.md §1): the choice is this connection's only; the host decides what runs. -->
    <string name="settings_desktop_backend_hint">이 PC의 이번 연결에만 적용 · 다음 연결은 PC 기본값으로 시작 · 창 캡처는 항상 WGC</string>
    <string name="settings_desktop_backend_not_connected">연결한 뒤 고를 수 있습니다</string>
    <string name="settings_desktop_backend_no_answer">이 PC는 적용 결과를 알려주지 않습니다 (이번 연결에만 적용되는지 확인 불가)</string>
    <string name="settings_desktop_backend_no_answer_asked">요청: %1$s · 이 PC는 적용 결과를 알려주지 않습니다 (확인 불가)</string>
    <string name="settings_desktop_backend_checking">PC가 쓰는 방식을 확인하는 중…</string>
    <string name="settings_desktop_backend_applying">요청: %1$s · 적용 중… (지금: %2$s)</string>
    <string name="settings_desktop_backend_applied">지금 PC가 쓰는 방식: %1$s</string>
    <string name="settings_desktop_backend_applied_asked">요청: %1$s · 실제 적용: %2$s</string>
    <string name="settings_desktop_backend_reason_window">창 캡처는 WGC</string>
    <string name="settings_desktop_backend_reason_quarantine">DXGI 문제로 격리 중</string>
    <string name="settings_desktop_backend_reason_logon">로그인 전 화면은 DXGI만</string>
    <string name="settings_desktop_backend_reason_not_yet">아직 적용되지 않음</string>'''),
    ('''<string name="ui_request_summary">요청: %1$s · %2$d fps · 바탕화면 캡처 %3$s</string>''',
     '''<string name="ui_request_summary">요청: %1$s · %2$d fps</string>'''),
])

edit('res/layout/activity_main.xml', [
    ('''                <Button
                    android:id="@+id/settingsDesktopBackendGdiButton"
                    android:layout_width="0dp"
                    android:layout_height="wrap_content"
                    android:layout_marginStart="10dp"
                    android:layout_weight="1"
                    android:background="@drawable/panel_button_background"
                    android:minHeight="@dimen/ui_touch"
                    android:stateListAnimator="@null"
                    android:textAllCaps="false"
                    android:text="@string/settings_desktop_backend_gdi"
                    android:textColor="@color/panel_button_text" />
            </LinearLayout>''',
     '''                <Button
                    android:id="@+id/settingsDesktopBackendGdiButton"
                    android:layout_width="0dp"
                    android:layout_height="wrap_content"
                    android:layout_marginStart="10dp"
                    android:layout_weight="1"
                    android:background="@drawable/panel_button_background"
                    android:minHeight="@dimen/ui_touch"
                    android:stateListAnimator="@null"
                    android:textAllCaps="false"
                    android:text="@string/settings_desktop_backend_gdi"
                    android:textColor="@color/panel_button_text" />
            </LinearLayout>

            <!-- ua2-apk r1: what the host says runs now (DesktopBackendPresentation) -->
            <TextView
                android:id="@+id/settingsDesktopBackendStatusText"
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:layout_marginTop="6dp"
                android:text="@string/settings_desktop_backend_not_connected"
                android:textColor="@color/ui_text_secondary"
                android:textSize="12sp" />'''),
])

# ---------------------------------------------------------------- persistence: no global backend any more
edit('java/com/remote60/androiddirect/SessionPersistence.kt', [
    ('''    val fps: Int = ViewerQualitySettings.DEFAULT_FPS,
    val desktopBackendCode: Int = 1,
)''', '''    val fps: Int = ViewerQualitySettings.DEFAULT_FPS,
)'''),
    ('''    private const val KEY_DESKTOP_BACKEND = "desktop_backend"''',
     '''    // ua2-apk r1 (Codex codex_rel_0163_r1.md §1): the capture backend is no longer stored -- the choice is one
    // connection's. A value an older APK left (it re-sent it on every connect, to every host) is removed on the
    // next save and never read.
    private const val KEY_DESKTOP_BACKEND = "desktop_backend"'''),
    ('''            fps = resolved.fps,
            desktopBackendCode = prefs.getInt(KEY_DESKTOP_BACKEND, 1),
        )''', '''            fps = resolved.fps,
        )'''),
    ('''        fps: Int,
        desktopBackendCode: Int,
    ) {''', '''        fps: Int,
    ) {'''),
    ('''            .putInt(KEY_DESKTOP_BACKEND, desktopBackendCode)
            .apply()''', '''            .remove(KEY_DESKTOP_BACKEND)
            .apply()'''),
])

# ---------------------------------------------------------------- MainActivity
edit('java/com/remote60/androiddirect/MainActivity.kt', [
    ('''    private lateinit var settingsDesktopBackendGdiButton: Button''',
     '''    private lateinit var settingsDesktopBackendGdiButton: Button
    private lateinit var settingsDesktopBackendStatusText: TextView'''),
    ('''        settingsDesktopBackendGdiButton = findViewById(R.id.settingsDesktopBackendGdiButton)''',
     '''        settingsDesktopBackendGdiButton = findViewById(R.id.settingsDesktopBackendGdiButton)
        settingsDesktopBackendStatusText = findViewById(R.id.settingsDesktopBackendStatusText)'''),
    ('''    private var requestedDesktopBackend = DesktopCaptureBackendOption.DXGI''',
     '''    // ua2-apk r1: what THIS connection asked for (null = nothing: the host's default). Never stored; a new
    // connection starts without one (Codex codex_rel_0163_r1.md §1).
    private var sessionDesktopBackend: DesktopCaptureBackendOption? = null
    private var desktopBackendQueryAtMs = 0L
    /** renderStatus's own reading of the session (status "connected"), for the backend controls. */
    private var sessionConnectedNow = false'''),
    ('''        val isConnected = statusValue.startsWith("connected")
''', '''        val isConnected = statusValue.startsWith("connected")
        sessionConnectedNow = isConnected
'''),
    ('''    private fun syncConnectedClientPreferences(isConnected: Boolean) {
        if (!isConnected) {
            lastAppliedStreamActive = null
            return
        }''', '''    private fun syncConnectedClientPreferences(isConnected: Boolean) {
        if (!isConnected) {
            lastAppliedStreamActive = null
            updateDesktopBackendButtons()  // ua2-apk r1: disabled, nothing lit
            return
        }'''),
    ('''        requestedDesktopBackend = DesktopCaptureBackendOption.fromCode(savedEndpoint.desktopBackendCode)
''', ''),
    ('''        settingsDesktopBackendDxgiButton.setOnClickListener {
            requestedDesktopBackend = DesktopCaptureBackendOption.DXGI
            updateDesktopBackendButtons()
        }
        settingsDesktopBackendWgcButton.setOnClickListener {
            requestedDesktopBackend = DesktopCaptureBackendOption.WGC
            updateDesktopBackendButtons()
        }
        settingsDesktopBackendGdiButton.setOnClickListener {
            requestedDesktopBackend = DesktopCaptureBackendOption.GDI
            updateDesktopBackendButtons()
        }''', '''        settingsDesktopBackendDxgiButton.setOnClickListener { requestSessionDesktopBackend(DesktopCaptureBackendOption.DXGI) }
        settingsDesktopBackendWgcButton.setOnClickListener { requestSessionDesktopBackend(DesktopCaptureBackendOption.WGC) }
        settingsDesktopBackendGdiButton.setOnClickListener { requestSessionDesktopBackend(DesktopCaptureBackendOption.GDI) }'''),
    ('''            val backendOk =
                NativeSessionBridge.nativeRequestDesktopCaptureBackend(requestedDesktopBackend.code)
            if (backendOk) {
                pendingDesktopBackendSync = false
                diagnosticsLog.log(
                    "desktop_backend_request",
                    "backend=${requestedDesktopBackend.label.lowercase()}"
                )
            } else {
                messages += getString(R.string.ui_request_backend_failed)
                diagnosticsLog.log(
                    "desktop_backend_failed",
                    "backend=${requestedDesktopBackend.label.lowercase()}"
                )
            }''', '''            // ua2-apk r1: the capture backend is not part of Apply any more -- its buttons ask the connected PC at
            // once, for this connection only.'''),
    ('''            pendingRuntimeConfigSync = true
            pendingDesktopBackendSync = true
            desiredStreamActive = false
            lastAppliedStreamActive = null
            clearPendingSelection()
            currentScene = UiScene.TARGETS
            NativeSessionBridge.nativeRequestWindowList()
        } else {''', '''            pendingRuntimeConfigSync = true
            resetSessionDesktopBackend("connect")
            desiredStreamActive = false
            lastAppliedStreamActive = null
            clearPendingSelection()
            currentScene = UiScene.TARGETS
            NativeSessionBridge.nativeRequestWindowList()
        } else {'''),
    ('''                    pendingRuntimeConfigSync = true
                    pendingDesktopBackendSync = true
                    desiredStreamActive = false''', '''                    pendingRuntimeConfigSync = true
                    resetSessionDesktopBackend("directory_connect")
                    desiredStreamActive = false'''),
    ('''    private fun updateDesktopBackendButtons() {
        settingsDesktopBackendDxgiButton.text = "DXGI"
        settingsDesktopBackendDxgiButton.isSelected = requestedDesktopBackend == DesktopCaptureBackendOption.DXGI
        settingsDesktopBackendWgcButton.text = "WGC"
        settingsDesktopBackendWgcButton.isSelected = requestedDesktopBackend == DesktopCaptureBackendOption.WGC
        settingsDesktopBackendGdiButton.text = "GDI"
        settingsDesktopBackendGdiButton.isSelected = requestedDesktopBackend == DesktopCaptureBackendOption.GDI
    }''', '''    /** ua2-apk r1: a new connection (or none): no choice of its own -- the host's default until the user picks. */
    private fun resetSessionDesktopBackend(reason: String) {
        sessionDesktopBackend = null
        desktopBackendQueryAtMs = 0L
        diagnosticsLog.log("desktop_backend_session_reset", "reason=$reason")
        updateDesktopBackendButtons()
    }

    /** ua2-apk r1: the user's pick goes to the CONNECTED PC now, for this connection only. */
    private fun requestSessionDesktopBackend(choice: DesktopCaptureBackendOption) {
        if (!desktopBackendConnected()) {
            updateDesktopBackendButtons()
            return
        }
        if (NativeSessionBridge.nativeRequestDesktopCaptureBackend(choice.code)) {
            sessionDesktopBackend = choice
            desktopBackendQueryAtMs = SystemClock.elapsedRealtime()  // the request asks for the answer itself
            diagnosticsLog.log("desktop_backend_request", "backend=${choice.label.lowercase()} scope=connection")
        } else {
            Toast.makeText(this, R.string.ui_request_backend_failed, Toast.LENGTH_SHORT).show()
            diagnosticsLog.log("desktop_backend_failed", "backend=${choice.label.lowercase()}")
        }
        updateDesktopBackendButtons()
    }

    private fun desktopBackendConnected(): Boolean = parked == null && sessionConnectedNow

    /** ua2-apk r1: the buttons and the line show what the HOST says runs now (DesktopBackendPresentation). */
    private fun updateDesktopBackendButtons() {
        if (!::settingsDesktopBackendStatusText.isInitialized) return
        val p = DesktopBackendPresentation
        val connected = desktopBackendConnected()
        val status = if (connected) p.HostStatus.of(NativeSessionBridge.nativeGetDesktopBackendStatus()) else null
        val v = p.present(connected, sessionDesktopBackend?.code, status)
        for ((button, code) in listOf(settingsDesktopBackendDxgiButton to p.DXGI, settingsDesktopBackendWgcButton to p.WGC,
                                      settingsDesktopBackendGdiButton to p.GDI)) {
            button.text = p.label(code)
            button.isSelected = v.lit == code
            button.isEnabled = v.enabled
            button.alpha = if (v.enabled) 1f else 0.45f
        }
        val asked = v.asked?.let { p.label(it) }
        val line = when (v.state) {
            DesktopBackendPresentation.State.NOT_CONNECTED -> getString(R.string.settings_desktop_backend_not_connected)
            DesktopBackendPresentation.State.NO_ANSWER_HOST ->
                if (asked != null) getString(R.string.settings_desktop_backend_no_answer_asked, asked)
                else getString(R.string.settings_desktop_backend_no_answer)
            DesktopBackendPresentation.State.CHECKING -> getString(R.string.settings_desktop_backend_checking)
            DesktopBackendPresentation.State.APPLYING ->
                getString(R.string.settings_desktop_backend_applying, asked ?: "?", p.label(v.active))
            DesktopBackendPresentation.State.APPLIED ->
                if (asked != null) getString(R.string.settings_desktop_backend_applied_asked, asked, p.label(v.active))
                else getString(R.string.settings_desktop_backend_applied, p.label(v.active))
        }
        val reasons = v.reasons.map {
            getString(when (it) {
                DesktopBackendPresentation.Reason.WINDOW_TARGET -> R.string.settings_desktop_backend_reason_window
                DesktopBackendPresentation.Reason.DXGI_QUARANTINED -> R.string.settings_desktop_backend_reason_quarantine
                DesktopBackendPresentation.Reason.BEFORE_LOGIN -> R.string.settings_desktop_backend_reason_logon
                DesktopBackendPresentation.Reason.NOT_YET_APPLIED -> R.string.settings_desktop_backend_reason_not_yet
            })
        }
        val text = if (reasons.isEmpty()) line else line + " (" + reasons.joinToString(", ") + ")"
        if (settingsDesktopBackendStatusText.text?.toString() != text) {
            settingsDesktopBackendStatusText.text = text
            diagnosticsLog.log("desktop_backend_ui", "state=${v.state} lit=${v.lit?.let { p.label(it) } ?: "none"} " +
                "asked=${asked ?: "none"} enabled=${v.enabled}")
        }
    }'''),
    ('''        if (pendingDesktopBackendSync) {
            if (NativeSessionBridge.nativeRequestDesktopCaptureBackend(requestedDesktopBackend.code)) {
                pendingDesktopBackendSync = false
                settingsStatusMessage =
                    requestSummary()
                settingsAppliedText.text = settingsStatusMessage
                diagnosticsLog.log(
                    "desktop_backend_sync",
                    "backend=${requestedDesktopBackend.label.lowercase()}"
                )
            }
        }''', '''        // ua2-apk r1: nothing is re-sent on connect (no stored backend). While the settings screen shows, the host
        // is asked what runs now -- every 2 s at most -- and the answer is what the buttons show.
        if (activeTargetTab == TargetTab.SETTINGS && currentScene == UiScene.TARGETS) {
            val nowMs = SystemClock.elapsedRealtime()
            if (nowMs - desktopBackendQueryAtMs >= 2000L) {
                desktopBackendQueryAtMs = nowMs
                NativeSessionBridge.nativeQueryDesktopCaptureBackend()
            }
        }
        updateDesktopBackendButtons()'''),
    ('''        getString(R.string.ui_request_summary, qualityLabel(), requestedRuntimeFps, requestedDesktopBackend.label)''',
     '''        getString(R.string.ui_request_summary, qualityLabel(), requestedRuntimeFps)'''),
    ('''            fps,
            requestedDesktopBackend.code
        )''', '''            fps,
        )'''),
    ('''    private var pendingDesktopBackendSync = false
''', ''),
])
print('ok')
