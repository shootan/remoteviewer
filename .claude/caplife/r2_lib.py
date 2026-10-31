src = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\libs\\capture\\src\\'

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

edit('capture_backend_dxgi.hpp', [
    ('''  DxgiDesktopPointerHandler onPointer;  // optional; see DxgiDesktopPointerHandler
};''',
     '''  DxgiDesktopPointerHandler onPointer;  // optional; see DxgiDesktopPointerHandler
  // TEST BUILDS ONLY (capture-lifecycle r2): set solely by a host built with REMOTE60_STREAM_TEST_SEAM, never by
  // the product. A desktop that never changes, emulated here so the full-frame reacquire's real duplication
  // remake runs: Still = of what duplication reports, only the first frame of each (new) duplication is
  // handed on; every other acquire is released and taken as a timeout. StillNoFirstFrame = not even that (the
  // bounded remake gives up). Off = the product.
  enum class TestStill : uint8_t { Off, Still, StillNoFirstFrame };
  TestStill testStill = TestStill::Off;
};'''),
])

edit('capture_backend_dxgi.cpp', [
    ('''  std::atomic<uint64_t> fullFrameRequestedUs{0};
  uint32_t fullFrameRecreates = 0;''',
     '''  std::atomic<uint64_t> fullFrameRequestedUs{0};
  uint32_t fullFrameRecreates = 0;
  bool testDeliveredSinceDuplication = false;  // TestStill: the current duplication's first frame went out'''),
    ('''  bool recreate_duplication(std::string* detailOut) {
    duplication.Reset();''',
     '''  bool recreate_duplication(std::string* detailOut) {
    duplication.Reset();
    testDeliveredSinceDuplication = false;'''),
    ('''      const HRESULT hr = duplication->AcquireNextFrame(config.acquireTimeoutMs, &frameInfo, &resource);
      if (progress) progress->lastAcquireHr.store(static_cast<int32_t>(hr), std::memory_order_release);
      markProgress();
      if (hr == DXGI_ERROR_WAIT_TIMEOUT) {''',
     '''      const HRESULT hr = duplication->AcquireNextFrame(config.acquireTimeoutMs, &frameInfo, &resource);
      if (progress) progress->lastAcquireHr.store(static_cast<int32_t>(hr), std::memory_order_release);
      markProgress();
      // TEST BUILDS ONLY (config.testStill): an acquire a still desktop would not have given is released and
      // taken as the timeout it would have been.
      bool testFirstOfDuplication = false;
      bool idle = hr == DXGI_ERROR_WAIT_TIMEOUT;
      if (SUCCEEDED(hr) && config.testStill != DxgiDesktopCaptureConfig::TestStill::Off) {
        testFirstOfDuplication =
            config.testStill == DxgiDesktopCaptureConfig::TestStill::Still && !testDeliveredSinceDuplication;
        if (!testFirstOfDuplication) {
          duplication->ReleaseFrame();
          resource.Reset();
          idle = true;
        } else {
          testDeliveredSinceDuplication = true;
        }
      }
      if (idle) {'''),
    ('''      uint32_t handedAccumulated = frameInfo.AccumulatedFrames;''',
     '''      uint32_t handedAccumulated = frameInfo.AccumulatedFrames;
      if (testFirstOfDuplication && handedAccumulated == 0) handedAccumulated = 1;  // a new duplication's whole image'''),
])
print('ok')
