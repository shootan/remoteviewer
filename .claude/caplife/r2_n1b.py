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

edit('host_backend_policy.hpp', [
    ('''  // capture-lifecycle r1 (UA-1, Codex §4): the WGC capture is rebuilt on each confirmed input-desktop
  // change (Default <-> a secure desktop) -- once per change, retried a bounded number of times with
  // backoff, never in a loop. Main loop only.
  uint64_t wgcRebuiltForDesktopGen = 0;
  uint32_t wgcRebuildAttempts = 0;
  uint64_t wgcRebuildRetryAtUs = 0;
  static constexpr uint32_t kMaxWgcRebuildAttempts = 3;''',
     '''  // capture-lifecycle r1 (UA-1, Codex §4) / r2 (N1): the running capture -- any backend -- is rebuilt after each
  // confirmed input-desktop change (Default <-> a secure desktop): its attachment was made under the desktop
  // that was, and nothing of it is adopted any more. Counted per desktop generation (a newer change starts its
  // own count; an older one's attempts are never settled against it), retried a bounded number of times with
  // backoff, never in a loop. Main loop only.
  uint64_t desktopRebuildGen = 0;
  uint32_t desktopRebuildAttempts = 0;
  uint64_t desktopRebuildRetryAtUs = 0;
  static constexpr uint32_t kMaxDesktopRebuildAttempts = 3;'''),
])

edit('host_stage_backend.cpp', [
    ('''  // capture-lifecycle r1 (PS-A / UA-1, Codex §4 -- one contract): the input desktop tracker, owned here.
  // A confirmed change clears the bootstrap cache and moves the desktop generation.
  capture.TrackInputDesktop(qpc_now_us());
  // UA-1: a WGC desktop capture is rebuilt on every confirmed desktop change, whatever was requested --
  // the 10-23 field: requested=active=WGC never crossed a UAC prompt (no frames of it, then black after
  // it), and two reconnects were what rebuilt it. Once per change; bounded retries with backoff.
  {
    const uint64_t dgen = capture.desktopGeneration.load(std::memory_order_acquire);
    if (backend.wgcRebuiltForDesktopGen == 0) backend.wgcRebuiltForDesktopGen = dgen;  // the first look
    if (dgen != backend.wgcRebuiltForDesktopGen) {
      const bool applicable = streamActiveNow && desktopMode && !beforeLogin &&
                              backend.active == DesktopCaptureBackend::Wgc && !capture.idleDetached;
      const uint64_t nowRebuildUs = qpc_now_us();
      if (!applicable) {
        backend.wgcRebuiltForDesktopGen = dgen;  // nothing running on WGC now: a later (re)attach is fresh anyway
        backend.wgcRebuildAttempts = 0;
      } else if (nowRebuildUs >= backend.wgcRebuildRetryAtUs) {
        ++backend.wgcRebuildAttempts;
        powerKeepalive.SetStreaming(true, true);
        const bool ok = restart_capture_session(hx);
        if (ok) restarted_capture("desktop-edge-wgc-rebuild");
        std::cout << "[native-video-host][desktop] wgc-rebuild desktopGen=" << dgen << " attempt="
                  << backend.wgcRebuildAttempts << " ok=" << (ok ? 1 : 0)
                  << " active=" << desktop_capture_backend_name(backend.active) << "''' + NL + '''";
        if (ok || backend.wgcRebuildAttempts >= DesktopBackendState::kMaxWgcRebuildAttempts) {
          backend.wgcRebuiltForDesktopGen = dgen;
          backend.wgcRebuildAttempts = 0;
          backend.wgcRebuildRetryAtUs = 0;
        } else {
          backend.wgcRebuildRetryAtUs = nowRebuildUs + 500000ULL * backend.wgcRebuildAttempts;
        }
      }
    }
  }''',
     '''  // capture-lifecycle r1 (PS-A / UA-1, Codex §4 -- one contract): the input desktop tracker, owned here.
  // A confirmed change clears the bootstrap cache and moves the desktop generation.
  if (capture.TrackInputDesktop(qpc_now_us())) {
    // r2 (Codex N1): the media fence comes FIRST, whatever a rebuild below does or fails to do -- the input
    // epoch moves (the sender drops every AU encoded before it), the raw frame waiting for the encoder goes,
    // and a key is owed. Nothing captured on the desktop that was crosses the edge; the attachment's later
    // frames are refused at adoption (PublishFrame: their attachment's desktop generation is stale).
    capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
    capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
    capture.lastCallbackUs.store(0, std::memory_order_release);
    encoder.ResetTimelineAnchors(capture);
    encoder.RequestKey(kHostKeyReasonCaptureRestart);
    capture.FlushCapturePipelineState(res, frameGating, stats, "desktop-edge");
    std::cout << "[native-video-host][desktop] edge fence desktopGen="
              << capture.desktopGeneration.load(std::memory_order_acquire) << "''' + NL + '''";
  }
  // UA-1 / r2 N1: the running capture is rebuilt after every confirmed desktop change, whatever backend --
  // the 10-23 field: requested=active=WGC never crossed a UAC prompt (no frames of it, then black after it),
  // and two reconnects were what rebuilt it. The rule is the provenance itself: the attachment running was
  // made under an older desktop generation. Detached (idle) -> nothing to do, the reattach is made under the
  // current one. No stream -> nothing now (no capture work for a stream nobody watches); a resume rebuilds.
  // Bounded per desktop generation, backoff between attempts.
  {
    const uint64_t dgen = capture.desktopGeneration.load(std::memory_order_acquire);
    const bool stale = capture.attachmentDesktopGen.load(std::memory_order_acquire) != dgen;
    if (stale && streamActiveNow && !capture.idleDetached) {
      if (backend.desktopRebuildGen != dgen) {
        backend.desktopRebuildGen = dgen;
        backend.desktopRebuildAttempts = 0;
        backend.desktopRebuildRetryAtUs = 0;
      }
      const uint64_t nowRebuildUs = qpc_now_us();
      if (backend.desktopRebuildAttempts < DesktopBackendState::kMaxDesktopRebuildAttempts &&
          nowRebuildUs >= backend.desktopRebuildRetryAtUs) {
        ++backend.desktopRebuildAttempts;
        powerKeepalive.SetStreaming(true, true);
        const bool ok = restart_capture_session(hx);
        if (ok) restarted_capture("desktop-edge-rebuild");
        std::cout << "[native-video-host][desktop] rebuild desktopGen=" << dgen << " attempt="
                  << backend.desktopRebuildAttempts << " ok=" << (ok ? 1 : 0)
                  << " active=" << desktop_capture_backend_name(backend.active) << "''' + NL + '''";
        if (!ok) {
          backend.desktopRebuildRetryAtUs = nowRebuildUs + 500000ULL * backend.desktopRebuildAttempts;
          if (backend.desktopRebuildAttempts >= DesktopBackendState::kMaxDesktopRebuildAttempts) {
            // Fail closed: the old attachment's frames stay refused (no picture of the desktop that was);
            // any later attach (reconnect, idle reattach, backend change) is made under this generation.
            std::cout << "[native-video-host][desktop] rebuild gave up desktopGen=" << dgen << "''' + NL + '''";
          }
        }
      }
    }
  }'''),
])
print('ok')
