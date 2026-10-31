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

edit('host_stage_pop_frame.cpp', [
    ('''// episode (one ends with the next real published frame). Window capture is left to WGC as before.
void ask_full_frame(HostContext& hx, uint64_t nowUs, CaptureState::KickMiss why, const char* where) {
  auto& capture = hx.capture;
  if (why != CaptureState::KickMiss::NothingCached && why != CaptureState::KickMiss::NotCurrent) return;
  if (capture.windowModeActive.load(std::memory_order_acquire)) return;
  // A capture (re)attached just now delivers its own first frame (a new duplication / WGC session).
  const uint64_t attachedUs = capture.lastAttachUs.load(std::memory_order_acquire);
  if (attachedUs != 0 && nowUs >= attachedUs && nowUs - attachedUs < CaptureState::kFullFrameAttachGraceUs) return;
  if (!capture.fullFrameEpisodeOpen.exchange(true, std::memory_order_acq_rel)) {
    capture.fullFrameAsks = 0;
    capture.fullFrameAskedAtUs = 0;
  }
  if (capture.fullFrameAsks >= CaptureState::kMaxFullFrameAsks) return;
  if (capture.fullFrameAskedAtUs != 0 && nowUs - capture.fullFrameAskedAtUs < CaptureState::kFullFrameAskIntervalUs) return;''',
     '''// episode (one ends with the next real published frame). Window capture is left to WGC as before.
// True while a later look is still worth it (the caller may re-arm its kick): this ask was held for a fresh
// attachment's own first frame, or was made / is waiting its interval with asks left.
bool ask_full_frame(HostContext& hx, uint64_t nowUs, CaptureState::KickMiss why, const char* where) {
  auto& capture = hx.capture;
  if (why != CaptureState::KickMiss::NothingCached && why != CaptureState::KickMiss::NotCurrent) return false;
  if (capture.windowModeActive.load(std::memory_order_acquire)) return false;
  // A capture (re)attached just now delivers its own first frame (a new duplication / WGC session).
  const uint64_t attachedUs = capture.lastAttachUs.load(std::memory_order_acquire);
  if (attachedUs != 0 && nowUs >= attachedUs && nowUs - attachedUs < CaptureState::kFullFrameAttachGraceUs) return true;
  if (!capture.fullFrameEpisodeOpen.exchange(true, std::memory_order_acq_rel)) {
    capture.fullFrameAsks = 0;
    capture.fullFrameAskedAtUs = 0;
  }
  if (capture.fullFrameAsks >= CaptureState::kMaxFullFrameAsks) return false;
  if (capture.fullFrameAskedAtUs != 0 && nowUs - capture.fullFrameAskedAtUs < CaptureState::kFullFrameAskIntervalUs) return true;'''),
    ('''            << " reason=" << (why == CaptureState::KickMiss::NothingCached ? "nothing-cached" : "not-current") << "\\n";
}''',
     '''            << " reason=" << (why == CaptureState::KickMiss::NothingCached ? "nothing-cached" : "not-current") << "\\n";
  return capture.fullFrameAsks < CaptureState::kMaxFullFrameAsks;
}'''),
    ('''      if (needKick && !servedBootstrap && clientSession.streamControlActive.load(std::memory_order_acquire)) {
        ask_full_frame(hx, nowUs, kickMiss, "kick");
      }''',
     '''      if (needKick && !servedBootstrap && clientSession.streamControlActive.load(std::memory_order_acquire) &&
          ask_full_frame(hx, nowUs, kickMiss, "kick") && (barrierClosed || encoder.forceKeyNext)) {
        rearm = true;  // an IDR is still owed: look again (bounded by the asks above), not one-shot
      }'''),
])
print('ok')
