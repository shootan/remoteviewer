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

edit('host_capture_session.hpp', [(
    '''  std::atomic<uint64_t> lastAttachUs{0};''',
    '''  std::atomic<uint64_t> lastAttachUs{0};
  // capture-lifecycle r1 (UA-2): the backend as the main loop last applied it, for the control thread's
  // ControlDesktopBackendStatus (90) -- requested | effective<<16 | active<<32 | flags<<48 (wire codes) -- how
  // many times it changed, and the (epoch<<32 | seq) of the last backend request the main loop handled.
  std::atomic<uint64_t> backendStatusPacked{0};
  std::atomic<uint32_t> backendStatusChanges{0};
  std::atomic<uint64_t> backendAppliedKey{0};''')])

edit('host_stage_backend.cpp', [
    ('''namespace remote60::native_poc {

Flow stage_backend(HostContext& hx, TickContext& tc) {''',
     '''namespace remote60::native_poc {

namespace {
uint16_t backend_wire_code(DesktopCaptureBackend b) {
  switch (b) {
    case DesktopCaptureBackend::Dxgi: return 1;
    case DesktopCaptureBackend::Wgc: return 2;
    case DesktopCaptureBackend::Gdi: return 3;
    default: return 0;
  }
}
// capture-lifecycle r1 (UA-2): what the control thread answers a status request with (90).
void publish_backend_status(HostContext& hx) {
  auto& backend = hx.backend;
  auto& capture = hx.capture;
  uint16_t flags = kDesktopBackendStatusFlagSessionScoped;
  if (backend.dxgiQuarantined) flags |= kDesktopBackendStatusFlagDxgiQuarantined;
  if (capture.windowModeActive.load(std::memory_order_acquire)) flags |= kDesktopBackendStatusFlagWindowTarget;
  if (!capture.preloginServingDesktop.empty()) flags |= kDesktopBackendStatusFlagBeforeLogin;
  const uint64_t packed = static_cast<uint64_t>(backend_wire_code(backend.requested)) |
                          (static_cast<uint64_t>(backend_wire_code(backend.EffectiveRequested())) << 16) |
                          (static_cast<uint64_t>(backend_wire_code(backend.active)) << 32) |
                          (static_cast<uint64_t>(flags) << 48);
  if (capture.backendStatusPacked.exchange(packed, std::memory_order_acq_rel) != packed) {
    capture.backendStatusChanges.fetch_add(1, std::memory_order_acq_rel);
  }
}
}  // namespace

Flow stage_backend(HostContext& hx, TickContext& tc) {'''),
    ('''  if (const auto backendReq = hx.mailbox.TakeBackendRequest(hx.clientSession.epoch.load(std::memory_order_acquire))) {
    const uint32_t reqSeq = backendReq->seq;''',
     '''  publish_backend_status(hx);  // capture-lifecycle r1 (UA-2): whatever changed since the last tick
  if (const auto backendReq = hx.mailbox.TakeBackendRequest(hx.clientSession.epoch.load(std::memory_order_acquire))) {
    const uint32_t reqSeq = backendReq->seq;
    // capture-lifecycle r1 (UA-2): handled (applied, stored or refused) once this block ends -- the control
    // thread answers a status request then. A struct with its destructor at the block's end, every path.
    struct AppliedMark {
      HostContext& hx;
      uint64_t key;
      ~AppliedMark() {
        publish_backend_status(hx);
        hx.capture.backendAppliedKey.store(key, std::memory_order_release);
      }
    } appliedMark{hx, (hx.clientSession.epoch.load(std::memory_order_acquire) << 32) | reqSeq};'''),
])

edit('host_control_session.cpp', [
    ('''        if (offered && gUnlockRelay.LockCredRetrySupported())
          pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagLockCredRetryV1;''',
     '''        if (offered && gUnlockRelay.LockCredRetrySupported())
          pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagLockCredRetryV1;
        // capture-lifecycle r1 (UA-2): a backend asked for is this session's, and a request may ask its answer (90).
        pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagDesktopBackendStatusV1;'''),
    ('''      if (req.backend == 1 || req.backend == 2 || req.backend == 3) {
        mailbox.PostBackendRequest({servedEpoch, req.seq, req.backend});
        std::cout << "[native-video-host][control] desktop-backend-request seq=" << req.seq
                  << " backend="
                  << (req.backend == 2 ? "wgc" : (req.backend == 3 ? "gdi" : "dxgi"))
                  << "''' + NL + '''";
      }
      continue;''',
     '''      if (req.backend == 1 || req.backend == 2 || req.backend == 3) {
        mailbox.PostBackendRequest({servedEpoch, req.seq, req.backend});
        std::cout << "[native-video-host][control] desktop-backend-request seq=" << req.seq
                  << " backend="
                  << (req.backend == 2 ? "wgc" : (req.backend == 3 ? "gdi" : "dxgi"))
                  << (req.flags & kDesktopBackendRequestFlagStatusV1 ? " status=asked" : "") << "''' + NL + '''";
      }
      // capture-lifecycle r1 (UA-2): the answer, if this request asked for it (only a client that saw
      // kCaptureFlagDesktopBackendStatusV1 does). A change is answered once the main loop handled it --
      // bounded; past that, as it is now and said so. backend 0 = a question only: answered at once.
      if ((req.flags & kDesktopBackendRequestFlagStatusV1) != 0) {
        bool applied = true;
        if (req.backend == 1 || req.backend == 2 || req.backend == 3) {
          const uint64_t key = (servedEpoch << 32) | req.seq;
          const uint64_t until = GetTickCount64() + 2000;
          applied = false;
          while (GetTickCount64() < until && clientSession.epoch.load(std::memory_order_acquire) == servedEpoch) {
            if (capture.backendAppliedKey.load(std::memory_order_acquire) == key) {
              applied = true;
              break;
            }
            Sleep(5);
          }
        }
        const uint64_t packed = capture.backendStatusPacked.load(std::memory_order_acquire);
        ControlDesktopBackendStatusMessage st{};
        st.header.magic = remote60::native_poc::kMagic;
        st.header.type = static_cast<uint16_t>(MessageType::ControlDesktopBackendStatus);
        st.header.size = static_cast<uint16_t>(sizeof(st));
        st.seq = req.seq;
        st.requested = static_cast<uint16_t>(packed & 0xffffu);
        st.effective = static_cast<uint16_t>((packed >> 16) & 0xffffu);
        st.active = static_cast<uint16_t>((packed >> 32) & 0xffffu);
        st.flags = static_cast<uint16_t>((packed >> 48) & 0xffffu);
        if (!applied) st.flags |= kDesktopBackendStatusFlagNotYetApplied;
        st.changeCount = capture.backendStatusChanges.load(std::memory_order_acquire);
        st.hostSendQpcUs = qpc_now_us();
        std::cout << "[native-video-host][control] desktop-backend-status seq=" << st.seq << " requested=" << st.requested
                  << " effective=" << st.effective << " active=" << st.active << " flags=0x" << std::hex << st.flags
                  << std::dec << "''' + NL + '''";
        if (!link.Write(&st, sizeof(st))) break;
      }
      continue;'''),
])
print('ok')
