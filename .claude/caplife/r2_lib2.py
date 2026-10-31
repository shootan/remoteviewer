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

edit('capture_backend_dxgi.cpp', [
    ('''      if (SUCCEEDED(hr) && config.testStill != DxgiDesktopCaptureConfig::TestStill::Off) {
        testFirstOfDuplication =
            config.testStill == DxgiDesktopCaptureConfig::TestStill::Still && !testDeliveredSinceDuplication;''',
     '''      if (SUCCEEDED(hr) && config.testStill != DxgiDesktopCaptureConfig::TestStill::Off) {
        // (Its first acquire that carries a desktop IMAGE -- a pointer-only one does not; see below.)
        testFirstOfDuplication = config.testStill == DxgiDesktopCaptureConfig::TestStill::Still &&
                                 !testDeliveredSinceDuplication && frameInfo.LastPresentTime.QuadPart != 0;'''),
    ('''      // capture-lifecycle r1 (PS-A): the frame a full-frame request waits for -- whatever this acquire
      // carried, its surface is the current desktop image; handed on as content (at least 1 accumulated).
      uint32_t handedAccumulated = frameInfo.AccumulatedFrames;
      if (testFirstOfDuplication && handedAccumulated == 0) handedAccumulated = 1;  // a new duplication's whole image
      if (fullFrameRequestedUs.exchange(0, std::memory_order_acq_rel) != 0) {
        if (handedAccumulated == 0) handedAccumulated = 1;''',
     '''      // capture-lifecycle r1 (PS-A): the frame a full-frame request waits for, handed on as content (at least 1
      // accumulated). r2: only an acquire that carries a desktop IMAGE (LastPresentTime != 0) answers it -- a
      // pointer-only acquire's surface may hold no image at all (a new duplication's first one, measured
      // 10-26: delivered as "the full frame", it was black -- luma spread 0). That one goes on as what it is
      // (pointer only) and the request stays open (the bounded remake above still runs out).
      uint32_t handedAccumulated = frameInfo.AccumulatedFrames;
      if (testFirstOfDuplication && handedAccumulated == 0) handedAccumulated = 1;  // a new duplication's whole image
      const bool carriesImage = frameInfo.LastPresentTime.QuadPart != 0;
      if (carriesImage && fullFrameRequestedUs.exchange(0, std::memory_order_acq_rel) != 0) {
        if (handedAccumulated == 0) handedAccumulated = 1;'''),
    ('''        log("capture", "full-frame reacquire: delivered after " + std::to_string(fullFrameRecreates) +
                           " duplication(s) content=" + (frameInfo.LastPresentTime.QuadPart != 0 ? "1" : "0"));''',
     '''        log("capture", "full-frame reacquire: delivered after " + std::to_string(fullFrameRecreates) +
                           " duplication(s) content=1");'''),
])
print('ok')
