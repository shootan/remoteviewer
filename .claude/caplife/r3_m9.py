wt = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\'
L = wt + 'libs\\capture\\src\\'
S = wt + 'apps\\native_poc\\src\\'

def edit(path, pairs):
    s = open(path, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (path, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(path, 'wb').write(s.encode('utf-8'))

edit(L + 'capture_backend_dxgi.hpp', [
    ('''  // bounded remake gives up). Off = the product.
  enum class TestStill : uint8_t { Off, Still, StillNoFirstFrame };''',
     '''  // bounded remake gives up). StillPointerFirst (r3) = Still, but while a full-frame request is open a new
  // duplication's first acquire is handed on as POINTER-ONLY (no desktop image) -- the case that must not
  // answer the request -- and its next image acquire is the first frame. Off = the product.
  enum class TestStill : uint8_t { Off, Still, StillNoFirstFrame, StillPointerFirst };'''),
])

edit(L + 'capture_backend_dxgi.cpp', [
    ('''  bool testDeliveredSinceDuplication = false;  // TestStill: the current duplication's first frame went out''',
     '''  bool testDeliveredSinceDuplication = false;  // TestStill: the current duplication's first frame went out
  bool testPointerSinceDuplication = false;    // StillPointerFirst: its pointer-only acquire went out
  bool fullFrameDeclinedLogged = false;        // the pointer-only refusal is said once per request'''),
    ('''    testDeliveredSinceDuplication = false;''',
     '''    testDeliveredSinceDuplication = false;
    testPointerSinceDuplication = false;'''),
    ('''      bool testFirstOfDuplication = false;
      bool idle = hr == DXGI_ERROR_WAIT_TIMEOUT;
      if (SUCCEEDED(hr) && config.testStill != DxgiDesktopCaptureConfig::TestStill::Off) {
        // (Its first acquire that carries a desktop IMAGE -- a pointer-only one does not; see below.)
        testFirstOfDuplication = config.testStill == DxgiDesktopCaptureConfig::TestStill::Still &&
                                 !testDeliveredSinceDuplication && frameInfo.LastPresentTime.QuadPart != 0;
        if (!testFirstOfDuplication) {''',
     '''      bool testFirstOfDuplication = false;
      bool testPointerOnly = false;  // StillPointerFirst: this acquire is handed on as carrying no image
      bool idle = hr == DXGI_ERROR_WAIT_TIMEOUT;
      if (SUCCEEDED(hr) && config.testStill != DxgiDesktopCaptureConfig::TestStill::Off) {
        const bool pointerFirst = config.testStill == DxgiDesktopCaptureConfig::TestStill::StillPointerFirst;
        if (pointerFirst && !testDeliveredSinceDuplication && !testPointerSinceDuplication &&
            fullFrameRequestedUs.load(std::memory_order_acquire) != 0) {
          testPointerOnly = true;
          testPointerSinceDuplication = true;
        }
        // (Its first acquire that carries a desktop IMAGE -- a pointer-only one does not; see below.)
        testFirstOfDuplication =
            !testPointerOnly &&
            (config.testStill == DxgiDesktopCaptureConfig::TestStill::Still || pointerFirst) &&
            !testDeliveredSinceDuplication && frameInfo.LastPresentTime.QuadPart != 0;
        if (!testFirstOfDuplication && !testPointerOnly) {'''),
    ('''      uint32_t handedAccumulated = frameInfo.AccumulatedFrames;
      if (testFirstOfDuplication && handedAccumulated == 0) handedAccumulated = 1;  // a new duplication's whole image
      const bool carriesImage = frameInfo.LastPresentTime.QuadPart != 0;
      if (carriesImage && fullFrameRequestedUs.exchange(0, std::memory_order_acq_rel) != 0) {
        if (handedAccumulated == 0) handedAccumulated = 1;
        log("capture", "full-frame reacquire: delivered after " + std::to_string(fullFrameRecreates) +
                           " duplication(s) content=1");
        fullFrameRecreates = 0;
      }''',
     '''      uint32_t handedAccumulated = testPointerOnly ? 0u : frameInfo.AccumulatedFrames;
      if (testFirstOfDuplication && handedAccumulated == 0) handedAccumulated = 1;  // a new duplication's whole image
      const bool carriesImage = !testPointerOnly && frameInfo.LastPresentTime.QuadPart != 0;
      if (carriesImage && fullFrameRequestedUs.exchange(0, std::memory_order_acq_rel) != 0) {
        if (handedAccumulated == 0) handedAccumulated = 1;
        log("capture", "full-frame reacquire: delivered after " + std::to_string(fullFrameRecreates) +
                           " duplication(s) content=1");
        fullFrameRecreates = 0;
        fullFrameDeclinedLogged = false;
      } else if (!carriesImage && fullFrameRequestedUs.load(std::memory_order_acquire) != 0 && !fullFrameDeclinedLogged) {
        fullFrameDeclinedLogged = true;
        log("capture", "full-frame reacquire: a pointer-only acquire does not answer it (request kept)");
      }'''),
])

edit(S + 'host_capture_session.cpp', [
    ('''        if (still && std::strcmp(still, "nofirst") == 0) config.testStill = DxgiDesktopCaptureConfig::TestStill::StillNoFirstFrame;''',
     '''        if (still && std::strcmp(still, "nofirst") == 0) config.testStill = DxgiDesktopCaptureConfig::TestStill::StillNoFirstFrame;
        if (still && std::strcmp(still, "pointerfirst") == 0) config.testStill = DxgiDesktopCaptureConfig::TestStill::StillPointerFirst;'''),
])

edit(S + 'viewer_prelogin_login_e2e_test.cpp', [
    ('''    if (a == L"--still-nofirst") stillNoFirst = true;  // r2: not even a new duplication's first frame''',
     '''    if (a == L"--still-nofirst") stillNoFirst = true;  // r2: not even a new duplication's first frame
    if (a == L"--still-pointerfirst") stillPointerFirst = true;  // r3: a remade duplication's first acquire is pointer-only'''),
    ('''  bool stillNoFirst = false;  // capture-lifecycle r2: ...that gives no frame at all (the bounded reacquire gives up)''',
     '''  bool stillNoFirst = false;  // capture-lifecycle r2: ...that gives no frame at all (the bounded reacquire gives up)
  bool stillPointerFirst = false;  // capture-lifecycle r3: ...whose remade duplication's first acquire is pointer-only'''),
    ('''                                  stillScreen ? std::wstring(stillNoFirst ? L"GNLINK_STREAM_TEST_STILL_SCREEN=nofirst"
                                                                          : L"GNLINK_STREAM_TEST_STILL_SCREEN=1")
                                              : std::wstring());''',
     '''                                  stillScreen ? std::wstring(stillNoFirst        ? L"GNLINK_STREAM_TEST_STILL_SCREEN=nofirst"
                                                             : stillPointerFirst ? L"GNLINK_STREAM_TEST_STILL_SCREEN=pointerfirst"
                                                                                 : L"GNLINK_STREAM_TEST_STILL_SCREEN=1")
                                              : std::wstring());'''),
    ('''      check("PS: ...and its first frame was delivered (\\"reacquire: delivered after\\")",
            host.Count("full-frame reacquire: delivered after") >= 1,
            std::to_string(host.Count("full-frame reacquire: delivered after")) + " deliveries");''',
     '''      check("PS: ...and its first frame was delivered (\\"reacquire: delivered after\\")",
            host.Count("full-frame reacquire: delivered after") >= 1,
            std::to_string(host.Count("full-frame reacquire: delivered after")) + " deliveries");
      if (stillPointerFirst) {
        // r3 (Codex M9): the remade duplication's first acquire is pointer-only (the test build says so) -- it does
        // NOT answer the request; the next acquire with an image does. In that order.
        const std::string log = host.log();
        const size_t declined = log.find("full-frame reacquire: a pointer-only acquire does not answer it");
        const size_t delivered = log.find("full-frame reacquire: delivered after");
        check("PS pointer-first: A POINTER-ONLY ACQUIRE DID NOT ANSWER THE FULL-FRAME REQUEST (request kept)",
              declined != std::string::npos);
        check("PS pointer-first: ...the image acquire after it did (delivered after the refusal)",
              declined != std::string::npos && delivered != std::string::npos && delivered > declined);
      }'''),
])
print('ok')
