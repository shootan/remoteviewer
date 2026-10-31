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
    ('''  // PS-A: the bounded full-frame reacquire when nothing cached may be shown (main loop only). An episode
  // ends with the next real published frame (PublishFrame clears fullFrameEpisodeOpen).
  uint64_t fullFrameAskedAtUs = 0;
  uint32_t fullFrameAsks = 0;
  std::atomic<bool> fullFrameEpisodeOpen{false};''',
     '''  // PS-A: the bounded full-frame reacquire when nothing cached may be shown (main loop only). An episode
  // ends with the next real published frame of the current provenance (PublishFrame clears
  // fullFrameEpisodeOpen -- a refused frame never does).
  // r2 (Codex N4): an episode belongs to ONE source: desktop generation, stream generation, control session and
  // the source boundary below. Any of them moved -> a new source, a new bounded budget; the same source keeps
  // its budget across its own reacquire (DXGI duplication remakes, the full-frame WGC restart).
  struct FullFrameSourceKey {
    uint64_t desktopGeneration = 0;
    uint64_t streamGeneration = 0;
    uint64_t sessionEpoch = 0;
    uint64_t sourceBoundary = 0;
    bool operator==(const FullFrameSourceKey& o) const {
      return desktopGeneration == o.desktopGeneration && streamGeneration == o.streamGeneration &&
             sessionEpoch == o.sessionEpoch && sourceBoundary == o.sourceBoundary;
    }
    bool operator!=(const FullFrameSourceKey& o) const { return !(*this == o); }
  };
  FullFrameSourceKey fullFrameEpisodeKey;
  uint64_t fullFrameEpisodes = 0;  // for the log
  // Bumped by every capture (re)attach (DetachCaptureSession) -- idle reattach, backend switch, climb-back,
  // desktop rebuild, selection, geometry -- EXCEPT the full-frame WGC restart, the episode's own reacquire
  // (fullFrameOwnRestart, set around it by the backend stage). Main loop only.
  uint64_t fullFrameSourceBoundary = 0;
  bool fullFrameOwnRestart = false;
  uint64_t fullFrameAskedAtUs = 0;
  uint32_t fullFrameAsks = 0;
  std::atomic<bool> fullFrameEpisodeOpen{false};'''),
])

edit('host_capture_session.cpp', [
    ('''  capture.InvalidateBootstrapCache("capture-detached");
  capture.lastAttachUs.store(qpc_now_us(), std::memory_order_release);''',
     '''  capture.InvalidateBootstrapCache("capture-detached");
  capture.lastAttachUs.store(qpc_now_us(), std::memory_order_release);
  // capture-lifecycle r2 (Codex N4): a new source for the full-frame budget -- unless this is that episode's own
  // WGC restart.
  if (!capture.fullFrameOwnRestart) ++capture.fullFrameSourceBoundary;'''),
])

edit('host_stage_backend.cpp', [
    ('''    powerKeepalive.SetStreaming(true, true);
    const bool ok = restart_capture_session(hx);
    if (ok) restarted_capture("full-frame-wgc-restart");''',
     '''    powerKeepalive.SetStreaming(true, true);
    capture.fullFrameOwnRestart = true;  // r2 (Codex N4): the episode's own reacquire keeps its budget
    const bool ok = restart_capture_session(hx);
    capture.fullFrameOwnRestart = false;
    if (ok) restarted_capture("full-frame-wgc-restart");'''),
])

edit('host_stage_pop_frame.cpp', [
    ('''  if (!capture.fullFrameEpisodeOpen.exchange(true, std::memory_order_acq_rel)) {
    capture.fullFrameAsks = 0;
    capture.fullFrameAskedAtUs = 0;
  }
  if (capture.fullFrameAsks >= CaptureState::kMaxFullFrameAsks) return false;''',
     '''  // r2 (Codex N4): the budget is the current source's. A closed episode (a frame of the current provenance came)
  // or another source (desktop / stream generation, control session, an external (re)attach) opens a new one;
  // the same source asking again keeps counting -- never reset per kick, poll or its own reacquire.
  const CaptureState::FullFrameSourceKey key{capture.desktopGeneration.load(std::memory_order_acquire),
                                             capture.streamGenerationState.load(std::memory_order_acquire),
                                             hx.clientSession.epoch.load(std::memory_order_acquire),
                                             capture.fullFrameSourceBoundary};
  const bool wasOpen = capture.fullFrameEpisodeOpen.exchange(true, std::memory_order_acq_rel);
  if (!wasOpen || key != capture.fullFrameEpisodeKey) {
    capture.fullFrameEpisodeKey = key;
    ++capture.fullFrameEpisodes;
    capture.fullFrameAsks = 0;
    capture.fullFrameAskedAtUs = 0;
  }
  if (capture.fullFrameAsks >= CaptureState::kMaxFullFrameAsks) return false;'''),
    ('''  std::cout << "[native-video-host] full-frame ask=" << capture.fullFrameAsks << "/" << CaptureState::kMaxFullFrameAsks
            << " via=" << how << " from=" << where''',
     '''  // Two different bounds, logged apart: this host asks at most kMaxFullFrameAsks times per episode; the DXGI
  // library makes its duplication anew at most 3 times per ask ("full-frame reacquire: duplication made anew
  // attempt=", capture_backend_dxgi.cpp) -- an episode's remakes are at most asks x 3, not 3.
  std::cout << "[native-video-host] full-frame ask=" << capture.fullFrameAsks << "/" << CaptureState::kMaxFullFrameAsks
            << " episode=" << capture.fullFrameEpisodes << " desktopGen=" << key.desktopGeneration
            << " streamGen=" << key.streamGeneration << " epoch=" << key.sessionEpoch
            << " source=" << key.sourceBoundary << " via=" << how << " from=" << where'''),
])
print('ok')
