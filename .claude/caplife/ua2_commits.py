import json
R = 'C:/work/gnlink-wt/apk-prelogin-select'
D = 'D:/remote/remote/.claude/caplife/'
A = 'apps/android_direct_client/app/src/'
N = 'apps/native_poc/src/'
commits = [
    ('ua2_p', [A + 'main/java/com/remote60/androiddirect/MainActivity.kt', A + 'main/java/com/remote60/androiddirect/SessionPersistence.kt',
               A + 'main/java/com/remote60/androiddirect/NativeSessionBridge.kt', A + 'main/java/com/remote60/androiddirect/DesktopBackendPresentation.kt',
               A + 'main/res/values/strings.xml', A + 'main/res/layout/activity_main.xml', A + 'main/cpp/native_bridge.cpp',
               N + 'native_video_client_session.cpp', N + 'native_video_client_shared_core.cpp'],
     """feat(apk, ua2-apk r1): the desktop capture backend is this connection's only, and shows what the PC runs

Codex codex_rel_0163_r1.md §1 (user 10-22: a WGC picked once on the phone was re-sent on every connect, to every
host, and the PC's UAC screen went dark).

- Nothing is stored any more: SessionPersistence no longer reads desktop_backend and removes the key an older APK
  left on the next save; nothing is re-sent on connect (pendingDesktopBackendSync is gone). Other settings and the
  sharpness choice are untouched.
- The buttons ask the CONNECTED PC at once, for this connection only ("이 PC의 이번 연결에만 적용"); a new
  connection starts with no choice (the host's default). Apply no longer carries the backend.
- The buttons show the HOST's answer (ControlDesktopBackendStatus via two new JNI calls: query / status), apart
  from what was asked ("요청: GDI · 실제 적용: DXGI"), with the host's reasons (window target = WGC, DXGI
  quarantine, logon screen DXGI only, not yet applied). Before an answer: "확인하는 중"; a host that does not answer:
  "확인 불가" -- no button is lit as if applied. Not connected: disabled. While the settings screen shows, the host
  is asked every 2 s at most. DesktopBackendPresentation is the rule, pure and unit-tested.
- The shared session took backends 1..2 only, so the GDI button (5dbfc1a, the host's isolated GDI = 3) never
  reached the host since 2026-08-03: 1..3 now.

Product / test / docs: product (APK settings UI, persistence, JNI, shared session backend range) / test (none in
this commit) / docs (none).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""),
    ('ua2_t', [A + 'test/java/com/remote60/androiddirect/DesktopBackendPresentationTest.kt', N + 'native_video_client_shared_core_test.cpp',
               'automation/sh_filter_avd.py'],
     """test(ua2-apk r1): the backend line's rule, GDI through the shared session, and the real APK on the AVD

DesktopBackendPresentationTest (8): not connected, older host never shown applied, checking, the host's active lit
not the request, applying, new connection = host default, reasons, raw parsing. shared core: Request(3) goes out,
4 does not. sh_filter_avd.py --scenario ua2 (seam-free APK, test host, off-screen layered pattern window): an older
APK's stored global WGC + 강하게 seeded -> on connect NO backend request at the host; connected, the rendered
buttons are enabled and the one lit is the host's (DXGI, "창 캡처는 WGC"); a real tap on GDI sends ONE request and
the line says asked vs applied; disconnect + connect asks nothing and shows the host's current one; the old key is
gone, 강하게 kept -- 13/13. (The settings tab is not reachable before a connection: recorded N/A.)

Product / test / docs: product (none) / test (unit, shared core, AVD scenario) / docs (none).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""),
]
for key, files, msg in commits:
    json.dump({'repo_path': R, 'files': files}, open(D + key + '_add.json', 'w', encoding='utf-8'))
    json.dump({'repo_path': R, 'message': msg}, open(D + key + '_commit.json', 'w', encoding='utf-8'), ensure_ascii=False)
print('ok')
