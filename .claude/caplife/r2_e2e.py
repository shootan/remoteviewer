src = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\apps\\native_poc\\src\\'
NL = chr(92) + 'n'

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

# ---- host: one line per attachment, its first adopted (real) frame
edit('host_capture_session.hpp', [
    ('''  std::atomic<uint64_t> provenanceRefused{0};        // frames refused at adoption (log rate limit)''',
     '''  std::atomic<uint64_t> provenanceRefused{0};        // frames refused at adoption (log rate limit)
  std::atomic<uint64_t> firstAdoptedLoggedCookie{0};  // r2: the attachment whose first adopted frame was logged'''),
])
edit('host_capture_session.cpp', [
    ('''    // capture-lifecycle r2 (Codex N4): a frame of the current provenance ends a full-frame reacquire episode --
    // only such a frame (a refused one above never does).
    capture.fullFrameEpisodeOpen.store(false, std::memory_order_release);''',
     '''    // capture-lifecycle r2 (Codex N4): a frame of the current provenance ends a full-frame reacquire episode --
    // only such a frame (a refused one above never does).
    capture.fullFrameEpisodeOpen.store(false, std::memory_order_release);
    // ...and the first such frame of each attachment is said once (its capture time: what a later key frame
    // of a real source must not predate -- the e2e evidence joins on it).
    const uint64_t liveCookie = capture.attachmentCookie.load(std::memory_order_acquire);
    if (capture.firstAdoptedLoggedCookie.exchange(liveCookie, std::memory_order_acq_rel) != liveCookie) {
      std::cout << "[native-video-host][desktop] adopted first frame attachment=" << liveCookie
                << " desktopGen=" << capture.attachmentDesktopGen.load(std::memory_order_acquire)
                << " captureUs=" << meta.captureUs << "''' + NL + '''";
    }'''),
])

# ---- UA e2e: a real (non-synthetic) key frame, captured no earlier than the new attachment's first real frame
edit('host_capture_lifecycle_ua_e2e_test.cpp', [
    ('''  uint64_t captureUs = 0;
  bool key = false;
};''',
     '''  uint64_t captureUs = 0;
  bool key = false;
  bool synthetic = false;  // r2: a re-served cached frame (kEncodedFrameFlagSynthetic) -- its stamp is "now"
};'''),
    ('''  // A key frame whose capture came after `afterUs`, decoded by now.
  bool KeyCapturedAfter(uint64_t afterUs, Seen* out = nullptr) {
    std::lock_guard<std::mutex> lk(mu);
    for (const Seen& s : v) {
      if (s.key && s.captureUs > afterUs) {''',
     '''  // A key frame of a REAL capture (not a re-served cached frame, whose stamp is the time it was re-served --
  // capture-lifecycle r2, Codex: a synthetic stamp is not a new source) made after `afterUs`, decoded by now.
  bool KeyCapturedAfter(uint64_t afterUs, Seen* out = nullptr) {
    std::lock_guard<std::mutex> lk(mu);
    for (const Seen& s : v) {
      if (s.key && !s.synthetic && s.captureUs > afterUs) {'''),
    ('''        log->Add(Seen{qpc_now_us(), f.header.captureQpcUs, key});''',
     '''        log->Add(Seen{qpc_now_us(), f.header.captureQpcUs, key, (f.header.flags & kEncodedFrameFlagSynthetic) != 0});'''),
])
print('ok')
