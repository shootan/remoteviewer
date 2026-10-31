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

edit('host_capture_session.hpp', [
    ('''  // capture-lifecycle r1: the input desktop it was published under, and that desktop's generation.
  std::wstring desktopName;
  uint64_t desktopGeneration = 0;
};''',
     '''  // capture-lifecycle r1: the input desktop it was published under, and that desktop's generation.
  std::wstring desktopName;
  uint64_t desktopGeneration = 0;
  // capture-lifecycle r2 (Codex N1): the attachment that made it (its cookie) -- a cached picture is only ever
  // the current attachment's.
  uint64_t attachmentCookie = 0;
};'''),
    ('''  std::wstring desktopName;                          // under bootstrapCacheMu (tracker writes, publish reads)
  std::atomic<uint64_t> desktopGeneration{0};''',
     '''  std::wstring desktopName;                          // under bootstrapCacheMu (tracker writes, publish reads)
  std::atomic<uint64_t> desktopGeneration{0};
  // capture-lifecycle r2 (Codex N1): the desktop generation the CURRENT attachment was made under (stamped when
  // it is made -- DetachCaptureSession, which every (re)attach runs first). A frame carries its attachment's
  // cookie; with that cookie current, this is the frame's own provenance. A confirmed change makes it stale at
  // once: nothing of that attachment is adopted (raw queue or cache) until a new attachment is made.
  std::atomic<uint64_t> attachmentDesktopGen{0};
  // An unknown look, or a first look at another name: not an edge (no restart, never "locked"), but the
  // picture's continuity is no longer known -- the cache is dropped and nothing is cached until a look sees the
  // confirmed desktop again.
  std::atomic<bool> desktopSuspect{false};
  std::atomic<uint64_t> provenanceRefused{0};        // frames refused at adoption (log rate limit)'''),
])

edit('host_capture_session.cpp', [
    # stamp the attachment's desktop generation at every (re)attach
    ('''  capture.InvalidateBootstrapCache("capture-detached");
  capture.lastAttachUs.store(qpc_now_us(), std::memory_order_release);''',
     '''  capture.InvalidateBootstrapCache("capture-detached");
  capture.lastAttachUs.store(qpc_now_us(), std::memory_order_release);
  // capture-lifecycle r2 (Codex N1): the next attachment belongs to the desktop generation current NOW (an edge
  // seen later makes it stale -- the safe direction: rebuilt, never adopted as the new desktop's).
  capture.attachmentDesktopGen.store(capture.desktopGeneration.load(std::memory_order_acquire), std::memory_order_release);'''),
    # tracker
    ('''  bool known = false;
  const std::wstring now = input_desktop_name_uncached(&known);
  if (!known) return false;  // Codex §4: an unknown look is no edge (and never "locked")
  std::wstring confirmed;
  {
    std::lock_guard<std::mutex> lk(bootstrapCacheMu);
    confirmed = desktopName;
  }
  if (_wcsicmp(now.c_str(), confirmed.c_str()) == 0) {
    desktopCandidateSeen = 0;
    return false;
  }
  if (desktopCandidateSeen == 0 || _wcsicmp(now.c_str(), desktopCandidate.c_str()) != 0) {
    desktopCandidate = now;
    desktopCandidateSeen = 1;
    // The very first look of the process is taken as it is: there is no earlier desktop to debounce against.
    if (!confirmed.empty()) return false;
  } else {
    ++desktopCandidateSeen;
  }
  if (!confirmed.empty() && desktopCandidateSeen < 2) return false;
  desktopCandidateSeen = 0;
  bool hadCache = false;
  uint64_t gen = 0;
  {
    // Name, generation, the time it was seen and the cleared cache move together, under the lock
    // PublishFrame checks and stamps under.
    std::lock_guard<std::mutex> lk(bootstrapCacheMu);
    desktopName = now;
    hadCache = static_cast<bool>(bootstrapCache.payload);
    bootstrapCache.payload.reset();  // pixels of the desktop that was: never shown on this one
    gen = desktopGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    desktopChangeDetectedUs.store(nowUs, std::memory_order_release);
  }''',
     '''  bool known = false;
  const std::wstring now = input_desktop_name_uncached(&known);
  std::wstring confirmed;
  {
    std::lock_guard<std::mutex> lk(bootstrapCacheMu);
    confirmed = desktopName;
  }
  // capture-lifecycle r2 (Codex N1): a look that could not tell, or a first look at another name, is no edge
  // (Codex §4: no restart, never "locked") -- but the picture's continuity is no longer known: the cache goes and
  // nothing is cached until the confirmed desktop is seen again.
  const auto suspect = [&](const char* why) {
    if (!desktopSuspect.exchange(true, std::memory_order_acq_rel)) InvalidateBootstrapCache(why);
  };
  if (!known) {
    if (!confirmed.empty()) suspect("desktop-unknown");
    return false;
  }
  if (_wcsicmp(now.c_str(), confirmed.c_str()) == 0) {
    desktopCandidateSeen = 0;
    desktopSuspect.store(false, std::memory_order_release);
    return false;
  }
  if (confirmed.empty()) {
    // The very first look of the process: its name, no generation step (the startup attachment was made under
    // generation 0 and stays this desktop's).
    {
      std::lock_guard<std::mutex> lk(bootstrapCacheMu);
      desktopName = now;
    }
    std::string to(now.begin(), now.end());
    std::cout << "[native-video-host][desktop] input desktop <first look> -> " << to
              << " desktopGen=" << desktopGeneration.load(std::memory_order_acquire) << "''' + NL + '''";
    return false;
  }
  if (desktopCandidateSeen == 0 || _wcsicmp(now.c_str(), desktopCandidate.c_str()) != 0) {
    desktopCandidate = now;
    desktopCandidateSeen = 1;
    suspect("desktop-candidate");
    return false;
  }
  ++desktopCandidateSeen;
  if (desktopCandidateSeen < 2) return false;
  desktopCandidateSeen = 0;
  bool hadCache = false;
  uint64_t gen = 0;
  {
    // Name, generation, the time it was seen and the cleared cache move together, under the lock
    // PublishFrame checks and adopts under.
    std::lock_guard<std::mutex> lk(bootstrapCacheMu);
    desktopName = now;
    hadCache = static_cast<bool>(bootstrapCache.payload);
    bootstrapCache.payload.reset();  // pixels of the desktop that was: never shown on this one
    gen = desktopGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    desktopChangeDetectedUs.store(nowUs, std::memory_order_release);
  }
  desktopSuspect.store(false, std::memory_order_release);'''),
    ('''  std::cout << "[native-video-host][desktop] input desktop " << (from.empty() ? "<first look>" : from) << " -> " << to
            << " desktopGen=" << gen << " cacheCleared=" << (hadCache ? 1 : 0) << "''' + NL + '''";
  return !confirmed.empty();''',
     '''  std::cout << "[native-video-host][desktop] input desktop " << from << " -> " << to << " desktopGen=" << gen
            << " cacheCleared=" << (hadCache ? 1 : 0) << "''' + NL + '''";
  return true;'''),
    # KickTryFill: the cached picture is the current attachment's, of the current desktop generation
    ('''  if (snap.desktopGeneration != capture.desktopGeneration.load(std::memory_order_acquire) ||
      _wcsicmp(snap.desktopName.c_str(), desktopNow.c_str()) != 0) {
    return miss(KickMiss::NotCurrent);
  }''',
     '''  if (snap.desktopGeneration != capture.desktopGeneration.load(std::memory_order_acquire) ||
      _wcsicmp(snap.desktopName.c_str(), desktopNow.c_str()) != 0) {
    return miss(KickMiss::NotCurrent);
  }
  // capture-lifecycle r2 (Codex N1): and made by the attachment running now, whose desktop is still that one.
  if (snap.attachmentCookie != capture.attachmentCookie.load(std::memory_order_acquire) ||
      capture.attachmentDesktopGen.load(std::memory_order_acquire) != snap.desktopGeneration ||
      capture.desktopSuspect.load(std::memory_order_acquire)) {
    return miss(KickMiss::NotCurrent);
  }'''),
    # PublishFrame: adoption checks under the locks
    ('''  // capture-lifecycle r1: a real frame ends a full-frame reacquire episode.
  capture.fullFrameEpisodeOpen.store(false, std::memory_order_release);
  // ...and is cached only if its callback came after the last desktop change was seen: a frame of the
  // desktop that was must not become the picture of the one that is (Codex §2/§4).
  // (Checked and stamped under the lock the tracker changes the desktop under: never half of each.)
  {
    std::lock_guard<std::mutex> lk(capture.bootstrapCacheMu);
    if (meta.callbackUs >= capture.desktopChangeDetectedUs.load(std::memory_order_acquire)) {
      capture.bootstrapCache.desktopName = capture.desktopName;
      capture.bootstrapCache.desktopGeneration = capture.desktopGeneration.load(std::memory_order_acquire);''',
     '''  // capture-lifecycle r2 (Codex N1): adopted only with its own provenance current -- the attachment that made it
  // (its cookie, carried with the frame) is the one running, and that attachment's desktop generation is the
  // current one. Checked again AT each adoption, under the lock that adoption takes (the tracker bumps the
  // generation under bootstrapCacheMu and then empties the raw queue under res.frame.mu): an old frame is never
  // given the new desktop's name, whatever its callback time.
  const auto provenanceCurrent = [&]() {
    return (meta.attachmentCookie == 0 ||
            meta.attachmentCookie == capture.attachmentCookie.load(std::memory_order_acquire)) &&
           capture.attachmentDesktopGen.load(std::memory_order_acquire) ==
               capture.desktopGeneration.load(std::memory_order_acquire);
  };
  const auto refused = [&](const char* where) {
    const uint64_t n = capture.provenanceRefused.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n == 1 || n % 100 == 0) {
      std::cout << "[native-video-host] publish refused reason=provenance at=" << where
                << " frameCookie=" << meta.attachmentCookie
                << " liveCookie=" << capture.attachmentCookie.load(std::memory_order_acquire)
                << " attachmentDesktopGen=" << capture.attachmentDesktopGen.load(std::memory_order_acquire)
                << " desktopGen=" << capture.desktopGeneration.load(std::memory_order_acquire) << " refused=" << n << "''' + NL + '''";
    }
  };
  {
    std::lock_guard<std::mutex> lk(capture.bootstrapCacheMu);
    if (provenanceCurrent() && !capture.desktopSuspect.load(std::memory_order_acquire)) {
      capture.bootstrapCache.desktopName = capture.desktopName;
      capture.bootstrapCache.desktopGeneration = capture.desktopGeneration.load(std::memory_order_acquire);
      capture.bootstrapCache.attachmentCookie =
          meta.attachmentCookie != 0 ? meta.attachmentCookie : capture.attachmentCookie.load(std::memory_order_acquire);'''),
    ('''  uint64_t currentVersion = 0;
  {
    std::lock_guard<std::mutex> lk(res.frame.mu);
    if (res.frame.nv12Slot >= 0) {''',
     '''  uint64_t currentVersion = 0;
  {
    std::lock_guard<std::mutex> lk(res.frame.mu);
    if (!provenanceCurrent()) {
      // Not this attachment's / not this desktop's: never queued for the encoder (its lease goes back).
      if (meta.nv12Slot >= 0) res.captureReadback.ReleaseNv12Slot(meta.nv12Slot, meta.nv12Generation);
      refused("raw-queue");
      return;
    }
    // capture-lifecycle r2 (Codex N4): a frame of the current provenance ends a full-frame reacquire episode --
    // only such a frame (a refused one above never does).
    capture.fullFrameEpisodeOpen.store(false, std::memory_order_release);
    if (res.frame.nv12Slot >= 0) {'''),
])
print('ok')
