src = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\apps\\native_poc\\src\\'

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

edit('d3d_capture_readback.hpp', [
    ('''  // 0 means "no cookie carried" (e.g. the GDI path) and is never checked.
  uint64_t attachmentCookie = 0;''',
     '''  // 0 means "no cookie carried" and is never checked by the readback itself; the host's PublishFrame
  // (capture-lifecycle r3) refuses it -- every capture path (WGC, DXGI, GDI) now carries its own.
  uint64_t attachmentCookie = 0;
  // capture-lifecycle r3 (Codex N1-b): the host's bootstrap-cache continuity token when the frame was taken
  // (CaptureState::cacheContinuity). Every cache invalidation moves it; a frame taken before one is never
  // cached after it, however late its readback completes.
  uint64_t cacheContinuity = 0;'''),
])

edit('host_capture_session.hpp', [
    ('''  std::atomic<bool> desktopSuspect{false};''',
     '''  std::atomic<bool> desktopSuspect{false};
  // r3 (Codex N1-b): moved by every bootstrap-cache invalidation (under bootstrapCacheMu, before the clear); a
  // frame carries the value it was taken under (CaptureFrameMeta::cacheContinuity) and is cached only while it
  // is still current -- an Unknown look, a candidate name, a detach or an inactive change in between: never.
  std::atomic<uint64_t> cacheContinuity{1};'''),
])

edit('host_capture_session.cpp', [
    # stamp WGC/DXGI (shared submit)
    ('''  meta.streamGeneration = capture.streamGenerationState.load(std::memory_order_acquire);
  meta.attachmentCookie = sourceAttachmentCookie;
  if (capture.windowModeActive && capture.windowClientOnlyActive) {''',
     '''  meta.streamGeneration = capture.streamGenerationState.load(std::memory_order_acquire);
  meta.attachmentCookie = sourceAttachmentCookie;
  meta.cacheContinuity = capture.cacheContinuity.load(std::memory_order_acquire);  // r3 (Codex N1-b)
  if (capture.windowModeActive && capture.windowClientOnlyActive) {'''),
    # GDI: its attachment's own cookie
    ('''      std::string gdiDetail;
      const bool started = gdiCaptureProcess.Start(
          config,
          [&](std::shared_ptr<std::vector<uint8_t>> pixels, uint32_t width,''',
     '''      std::string gdiDetail;
      // capture-lifecycle r3 (Codex N1-b): the attachment this GDI child belongs to, by value -- its frames carry
      // it like WGC's and DXGI's (a 0 cookie is refused at PublishFrame), so an old child's late callback after
      // a detach is never adopted under the new attachment.
      const uint64_t gdiAttachmentCookie = capture.attachmentCookie.load(std::memory_order_acquire);
      const bool started = gdiCaptureProcess.Start(
          config,
          [&, gdiAttachmentCookie](std::shared_ptr<std::vector<uint8_t>> pixels, uint32_t width,'''),
    ('''            meta.streamGeneration =
                capture.streamGenerationState.load(std::memory_order_acquire);
            capturePublishFn(std::move(pixels), width, height, stride, meta,
                             0, 0, parentCopyUs);''',
     '''            meta.streamGeneration =
                capture.streamGenerationState.load(std::memory_order_acquire);
            meta.attachmentCookie = gdiAttachmentCookie;
            meta.cacheContinuity = capture.cacheContinuity.load(std::memory_order_acquire);
            capturePublishFn(std::move(pixels), width, height, stride, meta,
                             0, 0, parentCopyUs);'''),
    # invalidation moves the token
    ('''    std::lock_guard<std::mutex> lk(bootstrapCacheMu);
    had = static_cast<bool>(bootstrapCache.payload);
    bootstrapCache.payload.reset();
  }
  if (had) std::cout << "[native-video-host] bootstrap-cache cleared reason=" << (why ? why : "unknown") << "\\n";''',
     '''    std::lock_guard<std::mutex> lk(bootstrapCacheMu);
    cacheContinuity.fetch_add(1, std::memory_order_acq_rel);  // r3 (Codex N1-b): before the clear, under the lock
    had = static_cast<bool>(bootstrapCache.payload);
    bootstrapCache.payload.reset();
  }
  if (had) std::cout << "[native-video-host] bootstrap-cache cleared reason=" << (why ? why : "unknown") << "\\n";'''),
    # the confirmed edge clears under the lock too
    ('''    hadCache = static_cast<bool>(bootstrapCache.payload);
    bootstrapCache.payload.reset();  // pixels of the desktop that was: never shown on this one''',
     '''    hadCache = static_cast<bool>(bootstrapCache.payload);
    cacheContinuity.fetch_add(1, std::memory_order_acq_rel);  // r3: as every invalidation
    bootstrapCache.payload.reset();  // pixels of the desktop that was: never shown on this one'''),
    # provenance: nonzero cookie required
    ('''  const auto provenanceCurrent = [&]() {
    return (meta.attachmentCookie == 0 ||
            meta.attachmentCookie == capture.attachmentCookie.load(std::memory_order_acquire)) &&''',
     '''  // r3 (Codex N1-b): no cookie, no provenance -- every capture path carries one; 0 is refused, never waved through.
  const auto provenanceCurrent = [&]() {
    return meta.attachmentCookie != 0 &&
           meta.attachmentCookie == capture.attachmentCookie.load(std::memory_order_acquire) &&'''),
    ('''    if (provenanceCurrent() && !capture.desktopSuspect.load(std::memory_order_acquire)) {
      capture.bootstrapCache.desktopName = capture.desktopName;
      capture.bootstrapCache.desktopGeneration = capture.desktopGeneration.load(std::memory_order_acquire);
      capture.bootstrapCache.attachmentCookie =
          meta.attachmentCookie != 0 ? meta.attachmentCookie : capture.attachmentCookie.load(std::memory_order_acquire);''',
     '''    // r3 (Codex N1-b): ...and taken after the last invalidation (its continuity token is still the current one):
    // a readback submitted before an Unknown look / a candidate name completes after the desktop is confirmed
    // again with the same cookie and generation -- it is still not cached. (Raw adoption is not refused for
    // it: no edge was confirmed, and dropping live frames on every Unknown look would freeze the picture.)
    if (provenanceCurrent() && !capture.desktopSuspect.load(std::memory_order_acquire) &&
        meta.cacheContinuity == capture.cacheContinuity.load(std::memory_order_acquire)) {
      capture.bootstrapCache.desktopName = capture.desktopName;
      capture.bootstrapCache.desktopGeneration = capture.desktopGeneration.load(std::memory_order_acquire);
      capture.bootstrapCache.attachmentCookie = meta.attachmentCookie;'''),
])
print('ok')
