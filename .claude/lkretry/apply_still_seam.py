import re
src = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\apps\\native_poc\\src\\'
d = r'D:\remote\remote\.claude\lkretry' + '\\'

def load(name):
    s = open(src + name, 'rb').read().decode('utf-8')
    return s, ('\r\n' in s)

def save(name, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(src + name, 'wb').write(s.encode('utf-8'))

def rep(s, old, new):
    assert s.count(old) == 1, (s.count(old), old[:100])
    return s.replace(old, new)

# 1) e2e: remove the cover, pass the still seam instead
s, c = load('viewer_prelogin_login_e2e_test.cpp')
s = s.replace('\r\n', '\n')
cover = open(d + 'ps_cover.cpp.txt', encoding='utf-8').read()
s = rep(s, cover + '\n', '')
s = rep(s, '''    StillCover cover;
    const bool covered = !noCover && cover.Start();
    check("PS: the primary monitor is covered by a still window for this run (--no-cover: the desktop as it is)",
          noCover || covered);
    Sleep(1500);  // the cover is composed; whatever moved before is behind it now
''', '')
s = rep(s, '    cover.Stop();\n', '')
s = rep(s, '    if (a == L"--no-cover") noCover = true;\n', '')
s = rep(s, '  bool noCover = false;      // ...without the still cover (the desktop as it is)\n', '')
s = rep(s, '''                const std::wstring& extraEnv2 = std::wstring()) {''',
        '''                const std::wstring& extraEnv2 = std::wstring(), const std::wstring& extraEnv3 = std::wstring()) {''')
s = rep(s, '''  if (!extraEnv2.empty()) fixtures.push_back(extraEnv2);''',
        '''  if (!extraEnv2.empty()) fixtures.push_back(extraEnv2);
  if (!extraEnv3.empty()) fixtures.push_back(extraEnv3);''')
s = rep(s, '''                                  : (stillScreen && !noAlias) ? std::wstring(L"GNLINK_STREAM_TEST_DESKTOP_ALIAS=Winlogon")
                                                              : std::wstring());''',
        '''                                  : (stillScreen && !noAlias) ? std::wstring(L"GNLINK_STREAM_TEST_DESKTOP_ALIAS=Winlogon")
                                                              : std::wstring(),
                                  // capture-lifecycle r1 (PS): this PC's desktop is never still (60 content
                                  // updates/s measured 10-26, a user at work on it) -- the test build stands
                                  // it in for a still logon screen (host_capture_session.cpp).
                                  stillScreen ? std::wstring(L"GNLINK_STREAM_TEST_STILL_SCREEN=1") : std::wstring());''')
s = rep(s, '''  // datagrams. What differs from the product, said once: the worker serves "Winlogon" on this test user's
  // "Default" desktop (GNLINK_STREAM_TEST_DESKTOP_ALIAS -- the name it is shown, desktop_name_seam.hpp), so
  // the pixels are this desktop's; the present thread stamps what the renderer would show (no window).''',
        '''  // datagrams. What differs from the product, said once: the worker serves "Winlogon" on this test user's
  // "Default" desktop (GNLINK_STREAM_TEST_DESKTOP_ALIAS -- the name it is shown, desktop_name_seam.hpp), so
  // the pixels are this desktop's; that desktop is made STILL by the test build
  // (GNLINK_STREAM_TEST_STILL_SCREEN: of what duplication reports, only what a still screen gives passes);
  // the present thread stamps what the renderer would show (no window).''')
save('viewer_prelogin_login_e2e_test.cpp', s, c)

# 2) host: the still-screen seam in the DXGI frame handler
s, c = load('host_capture_session.cpp')
s = s.replace('\r\n', '\n')
s = rep(s, '''      const uint64_t dxgiAttachmentCookie = capture.attachmentCookie.load(std::memory_order_acquire);
      auto startDxgi = [&, dxgiAttachmentCookie](std::string* detailOut) {
        return dxgiCaptureSession.Start(
            config,
            [&, dxgiAttachmentCookie](ID3D11Texture2D* texture, uint32_t width, uint32_t height,
                                      uint32_t accumulatedFrames) {
              if (stop.load()) return;''',
        '''      const uint64_t dxgiAttachmentCookie = capture.attachmentCookie.load(std::memory_order_acquire);
      // TEST BUILD ONLY use (capture-lifecycle r1, the still-screen seam below): this attachment's first frame.
      auto dxgiFirstFrameSeen = std::make_shared<std::atomic<bool>>(false);
      auto startDxgi = [&, dxgiAttachmentCookie, dxgiFirstFrameSeen](std::string* detailOut) {
        return dxgiCaptureSession.Start(
            config,
            [&, dxgiAttachmentCookie, dxgiFirstFrameSeen](ID3D11Texture2D* texture, uint32_t width, uint32_t height,
                                      uint32_t accumulatedFrames) {
              if (stop.load()) return;
#ifdef REMOTE60_STREAM_TEST_SEAM
              // TEST BUILD ONLY (capture-lifecycle r1, PS): GNLINK_STREAM_TEST_STILL_SCREEN=1 -- the test user's
              // desktop stands in for a STILL logon screen (this PC's never is). Of what duplication reports,
              // only what a still screen gives passes: the first frame of this attachment (a new duplication's
              // whole image) and the frame a full-frame request is answered with. Nothing else ever changed.
              {
                static const bool stillSeam = [] {
                  const char* v = std::getenv("GNLINK_STREAM_TEST_STILL_SCREEN");
                  return v && v[0] == '1';
                }();
                if (stillSeam) {
                  const bool first = !dxgiFirstFrameSeen->exchange(true);
                  const bool asked = capture.seamStillFullFrameAsked.exchange(false);
                  if (!first && !asked) return;
                  std::cout << "[native-video-host][test-seam] still screen: frame passed (" << (first ? "first" : "asked")
                            << ")\\n";
                }
              }
#endif''')
save('host_capture_session.cpp', s, c)

s, c = load('host_capture_session.hpp')
s = s.replace('\r\n', '\n')
s = rep(s, '''  std::atomic<bool> wgcFullFrameRestartWanted{false};  // WGC: the main loop's bounded restart does it''',
        '''  std::atomic<bool> wgcFullFrameRestartWanted{false};  // WGC: the main loop's bounded restart does it
  std::atomic<bool> seamStillFullFrameAsked{false};    // TEST BUILD ONLY: the still-screen seam's "asked"''')
save('host_capture_session.hpp', s, c)

s, c = load('host_stage_pop_frame.cpp')
s = s.replace('\r\n', '\n')
s = rep(s, '''    hx.res.dxgiCaptureSession.RequestFullFrame();
    how = "dxgi";''',
        '''    hx.res.dxgiCaptureSession.RequestFullFrame();
#ifdef REMOTE60_STREAM_TEST_SEAM
    capture.seamStillFullFrameAsked.store(true, std::memory_order_release);  // the still-screen seam lets it pass
#endif
    how = "dxgi";''')
save('host_stage_pop_frame.cpp', s, c)
print('ok')
