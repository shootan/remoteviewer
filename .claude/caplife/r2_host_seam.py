src = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\apps\\native_poc\\src\\'
import re

def edit(name, pairs):
    p = src + name
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (name, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

p = src + 'host_capture_session.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
start = s.index('#ifdef REMOTE60_STREAM_TEST_SEAM\n              // TEST BUILD ONLY (capture-lifecycle r1, PS): GNLINK_STREAM_TEST_STILL_SCREEN=1')
end = s.index('#endif\n', start) + len('#endif\n')
s = s[:start] + s[end:]
s = s.replace('''      // TEST BUILD ONLY use (capture-lifecycle r1, the still-screen seam below): this attachment's first frame.
      auto dxgiFirstFrameSeen = std::make_shared<std::atomic<bool>>(false);
      auto startDxgi = [&, dxgiAttachmentCookie, dxgiFirstFrameSeen](std::string* detailOut) {''',
'''#ifdef REMOTE60_STREAM_TEST_SEAM
      // TEST BUILD ONLY (capture-lifecycle r2): GNLINK_STREAM_TEST_STILL_SCREEN=1 / =nofirst -- the test user's
      // desktop stands in for a STILL logon screen (this PC's never is): the capture library itself takes every
      // acquire but a new duplication's first as a timeout, so the full-frame reacquire runs its real
      // duplication remake (=nofirst: not even the first -- the bounded remake gives up).
      {
        const char* still = std::getenv("GNLINK_STREAM_TEST_STILL_SCREEN");
        if (still && std::strcmp(still, "1") == 0) config.testStill = DxgiDesktopCaptureConfig::TestStill::Still;
        if (still && std::strcmp(still, "nofirst") == 0) config.testStill = DxgiDesktopCaptureConfig::TestStill::StillNoFirstFrame;
      }
#endif
      auto startDxgi = [&, dxgiAttachmentCookie](std::string* detailOut) {''')
s = s.replace('''            [&, dxgiAttachmentCookie, dxgiFirstFrameSeen](ID3D11Texture2D* texture, uint32_t width, uint32_t height,
                                      uint32_t accumulatedFrames) {''',
'''            [&, dxgiAttachmentCookie](ID3D11Texture2D* texture, uint32_t width, uint32_t height,
                                      uint32_t accumulatedFrames) {''')
assert 'dxgiFirstFrameSeen' not in s
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

edit('host_capture_session.hpp', [(
    '''  std::atomic<bool> seamStillFullFrameAsked{false};    // TEST BUILD ONLY: the still-screen seam's "asked"\n''', '')])
edit('host_stage_pop_frame.cpp', [(
    '''#ifdef REMOTE60_STREAM_TEST_SEAM
    capture.seamStillFullFrameAsked.store(true, std::memory_order_release);  // the still-screen seam lets it pass
#endif
''', '')])
edit('viewer_prelogin_login_e2e_test.cpp', [(
    '''  // (GNLINK_STREAM_TEST_STILL_SCREEN: of what duplication reports, only what a still screen gives passes);''',
    '''  // (GNLINK_STREAM_TEST_STILL_SCREEN: the capture library takes every acquire but a new duplication's first
  // as a timeout -- so the full-frame reacquire runs its real duplication remake);''')])
print('ok')
