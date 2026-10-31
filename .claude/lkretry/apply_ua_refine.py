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

NL = chr(92) + 'n'

edit('host_capture_session.hpp', [(
    '''  std::atomic<bool> seamStillFullFrameAsked{false};    // TEST BUILD ONLY: the still-screen seam's "asked"''',
    '''  std::atomic<bool> seamStillFullFrameAsked{false};    // TEST BUILD ONLY: the still-screen seam's "asked"
  // When the current capture attachment began (DetachCaptureSession stamps it: every (re)attach detaches
  // first). A fresh attachment's own first frame is on its way: no full-frame ask in the first
  // kFullFrameAttachGraceUs (a second restart of a capture 10 ms old helps nobody).
  std::atomic<uint64_t> lastAttachUs{0};
  static constexpr uint64_t kFullFrameAttachGraceUs = 500000;''')])

edit('host_capture_session.cpp', [(
    '''  capture.InvalidateBootstrapCache("capture-detached");''',
    '''  capture.InvalidateBootstrapCache("capture-detached");
  capture.lastAttachUs.store(qpc_now_us(), std::memory_order_release);''')])

edit('host_stage_pop_frame.cpp', [(
    '''  if (capture.windowModeActive.load(std::memory_order_acquire)) return;
  if (!capture.fullFrameEpisodeOpen.exchange(true, std::memory_order_acq_rel)) {''',
    '''  if (capture.windowModeActive.load(std::memory_order_acquire)) return;
  // A capture (re)attached just now delivers its own first frame (a new duplication / WGC session).
  const uint64_t attachedUs = capture.lastAttachUs.load(std::memory_order_acquire);
  if (attachedUs != 0 && nowUs >= attachedUs && nowUs - attachedUs < CaptureState::kFullFrameAttachGraceUs) return;
  if (!capture.fullFrameEpisodeOpen.exchange(true, std::memory_order_acq_rel)) {''')])

edit('host_stage_backend.cpp', [(
    '''    if (curEpoch != backend.ownerEpoch) {
      backend.ownerEpoch = curEpoch;''',
    '''    if (curEpoch != backend.ownerEpoch) {
      backend.ownerEpoch = curEpoch;
      std::cout << "[native-video-host][control] session epoch=" << curEpoch
                << " backend requested=" << desktop_capture_backend_name(backend.requested)
                << " active=" << desktop_capture_backend_name(backend.active) << "''' + NL + '''";''')])
print('ok')
