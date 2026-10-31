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

# new header -> CRLF like its neighbours
p = src + 'host_backend_owner.hpp'
s = open(p, 'rb').read().decode('utf-8').replace('\r\n', '\n').replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

edit('host_main_loop_mailbox.hpp', [
    ('''#include <cstdint>
#include <mutex>
#include <optional>''',
     '''#include <cstdint>
#include <mutex>
#include <optional>

#include "host_backend_owner.hpp"'''),
    ('''  std::optional<BackendRequest> TakeBackendRequest(uint64_t currentEpoch = 0) {
    return TakeForEpoch(backend_, currentEpoch);
  }''',
     '''  std::optional<BackendRequest> TakeBackendRequest(uint64_t currentEpoch = 0) {
    return TakeForEpoch(backend_, currentEpoch);
  }
  // capture-lifecycle r2 (Codex N2): taken under the owner the main loop settled THIS tick (ownerEpoch, from its
  // one epoch read). Older -> dropped; NEWER -> left in place for the tick that makes its epoch the owner
  // first (TakeForEpoch would have dropped it, and the new session's choice with it). See host_backend_owner.hpp.
  std::optional<BackendRequest> TakeBackendRequestForOwner(uint64_t ownerEpoch) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!backend_) return std::nullopt;
    switch (ClassifyBackendRequest(backend_->epoch, ownerEpoch)) {
      case BackendRequestFate::Leave:
        return std::nullopt;
      case BackendRequestFate::Drop:
        backend_.reset();
        ++staleDropped_;
        return std::nullopt;
      case BackendRequestFate::Handle:
        break;
    }
    std::optional<BackendRequest> out;
    out.swap(backend_);
    return out;
  }'''),
])

edit('host_session.hpp', [
    ('''  std::atomic<bool> controlResumePending{false};
''',
     '''  std::atomic<bool> controlResumePending{false};
  // capture-lifecycle r2 (Codex N2): the epoch whose control session really ended -- its Serve() returned while
  // it was still current and no resume was pending (the same rule that turns its stream off). The main loop
  // puts that owner's backend choice back to the host default, once (host_backend_owner.hpp).
  std::atomic<uint64_t> backendSessionEndedEpoch{0};
'''),
])

edit('host_control_session.cpp', [
    ('''  if (clientSession.epoch.load(std::memory_order_acquire) == servedEpoch &&
      !clientSession.controlResumePending.load(std::memory_order_acquire)) {
    clientSession.streamControlActive.store(false, std::memory_order_release);
  }
}''',
     '''  if (clientSession.epoch.load(std::memory_order_acquire) == servedEpoch &&
      !clientSession.controlResumePending.load(std::memory_order_acquire)) {
    clientSession.streamControlActive.store(false, std::memory_order_release);
    // capture-lifecycle r2 (Codex N2): the real end of this logical session -- its backend choice ends with it.
    clientSession.backendSessionEndedEpoch.store(servedEpoch, std::memory_order_release);
  }
}'''),
    ('''      if (req.backend == 1 || req.backend == 2 || req.backend == 3) {
        mailbox.PostBackendRequest({servedEpoch, req.seq, req.backend});''',
     '''      if (req.backend == 1 || req.backend == 2 || req.backend == 3) {
        // capture-lifecycle r2 (Codex N3): the change this session has asked for and the main loop has not yet
        // handled -- a later question (backend 0) is answered NotYetApplied until it is.
        capture.backendChangePostedKey.store((servedEpoch << 32) | req.seq, std::memory_order_release);
        mailbox.PostBackendRequest({servedEpoch, req.seq, req.backend});'''),
    ('''      if ((req.flags & kDesktopBackendRequestFlagStatusV1) != 0) {
        bool applied = true;
        if (req.backend == 1 || req.backend == 2 || req.backend == 3) {
          const uint64_t key = (servedEpoch << 32) | req.seq;
          const uint64_t until = GetTickCount64() + 250;
          applied = false;
          while (GetTickCount64() < until && clientSession.epoch.load(std::memory_order_acquire) == servedEpoch) {
            if (capture.backendAppliedKey.load(std::memory_order_acquire) == key) {
              applied = true;
              break;
            }
            Sleep(5);
          }
        }
        const uint64_t packed = capture.backendStatusPacked.load(std::memory_order_acquire);''',
     '''      if ((req.flags & kDesktopBackendRequestFlagStatusV1) != 0) {
        // capture-lifecycle r2 (Codex N3): "applied" = the status published is THIS session's (the main loop has
        // made servedEpoch the owner -- never the previous session's choice as this connection's answer) and no
        // change of this session is waiting (for a change: this one has been handled; for a question: the last
        // change asked, if any). Waited for up to 250 ms, else answered as it is with NotYetApplied.
        const bool isChange = req.backend == 1 || req.backend == 2 || req.backend == 3;
        const uint64_t until = GetTickCount64() + 250;
        bool applied = false;
        uint64_t packed = 0;
        for (;;) {
          const uint64_t statusEpoch = capture.backendStatusEpoch.load(std::memory_order_acquire);
          packed = capture.backendStatusPacked.load(std::memory_order_acquire);
          const uint64_t posted = capture.backendChangePostedKey.load(std::memory_order_acquire);
          const uint64_t handled = capture.backendAppliedKey.load(std::memory_order_acquire);
          const uint64_t wanted = isChange ? ((servedEpoch << 32) | req.seq) : posted;
          const bool changeDone = isChange ? handled == wanted : ((posted >> 32) != servedEpoch || handled == posted);
          // The epoch read again after the packed status: both of the same publish (one owner).
          applied = statusEpoch == servedEpoch && changeDone &&
                    capture.backendStatusEpoch.load(std::memory_order_acquire) == statusEpoch;
          if (applied || GetTickCount64() >= until ||
              clientSession.epoch.load(std::memory_order_acquire) != servedEpoch) {
            break;
          }
          Sleep(5);
        }'''),
])

edit('host_capture_session.hpp', [
    ('''  std::atomic<uint64_t> backendAppliedKey{0};''',
     '''  std::atomic<uint64_t> backendAppliedKey{0};
  // capture-lifecycle r2 (Codex N2/N3): the owner epoch backendStatusPacked was published for (stored after it,
  // read around it by the control thread), and the (epoch<<32 | seq) of the last backend CHANGE a control thread
  // posted (a question is answered NotYetApplied while that one is not yet handled).
  std::atomic<uint64_t> backendStatusEpoch{0};
  std::atomic<uint64_t> backendChangePostedKey{0};'''),
])

edit('host_backend_policy.hpp', [
    ('''  DesktopCaptureBackend startupRequested = DesktopCaptureBackend::Dxgi;
  uint64_t ownerEpoch = 0;''',
     '''  DesktopCaptureBackend startupRequested = DesktopCaptureBackend::Dxgi;
  BackendOwnerState owner;  // r2 (Codex N2): the one owner decision, host_backend_owner.hpp'''),
])

edit('host_stage_backend.cpp', [
    ('''#include "host_backend_policy.hpp"''',
     '''#include "host_backend_owner.hpp"
#include "host_backend_policy.hpp"'''),
    ('''  if (capture.backendStatusPacked.exchange(packed, std::memory_order_acq_rel) != packed) {
    capture.backendStatusChanges.fetch_add(1, std::memory_order_acq_rel);
  }
}''',
     '''  if (capture.backendStatusPacked.exchange(packed, std::memory_order_acq_rel) != packed) {
    capture.backendStatusChanges.fetch_add(1, std::memory_order_acq_rel);
  }
  // r2 (Codex N3): ...of this owner (stored after the packed status: the control thread reads it around it).
  capture.backendStatusEpoch.store(backend.owner.ownerEpoch, std::memory_order_release);
}'''),
    ('''  {
    const uint64_t curEpoch = clientSession.epoch.load(std::memory_order_acquire);
    if (curEpoch != backend.ownerEpoch) {
      backend.ownerEpoch = curEpoch;
      std::cout << "[native-video-host][control] backend-owner epoch=" << curEpoch
                << " requested=" << desktop_capture_backend_name(backend.requested)
                << " active=" << desktop_capture_backend_name(backend.active) << "''' + NL + '''";
      if (!beforeLogin && backend.requested != backend.startupRequested) {
        std::cout << "[native-video-host][control] desktop-backend-reset new-session epoch=" << curEpoch''',
     '''  // r2 (Codex N2): ONE epoch read for this tick -- the owner it settles is the one every backend request below is
  // taken and marked under; its real end (backendSessionEndedEpoch) puts the choice back too, once.
  const uint64_t curEpoch = clientSession.epoch.load(std::memory_order_acquire);
  {
    const BackendOwnerStep ownerStep = StepBackendOwner(
        backend.owner, curEpoch, clientSession.backendSessionEndedEpoch.load(std::memory_order_acquire));
    if (ownerStep.ownerChanged) {
      std::cout << "[native-video-host][control] backend-owner epoch=" << curEpoch
                << " requested=" << desktop_capture_backend_name(backend.requested)
                << " active=" << desktop_capture_backend_name(backend.active) << "''' + NL + '''";
    }
    if (ownerStep.ownerChanged || ownerStep.ownerEnded) {
      if (!beforeLogin && backend.requested != backend.startupRequested) {
        std::cout << "[native-video-host][control] desktop-backend-reset "
                  << (ownerStep.ownerChanged ? "new-session" : "session-ended") << " epoch=" << curEpoch'''),
    ('''  publish_backend_status(hx);  // capture-lifecycle r1 (UA-2): whatever changed since the last tick
  if (const auto backendReq = hx.mailbox.TakeBackendRequest(hx.clientSession.epoch.load(std::memory_order_acquire))) {
    const uint32_t reqSeq = backendReq->seq;''',
     '''  publish_backend_status(hx);  // capture-lifecycle r1 (UA-2): whatever changed since the last tick
  if (const auto backendReq = hx.mailbox.TakeBackendRequestForOwner(backend.owner.ownerEpoch)) {
    const uint32_t reqSeq = backendReq->seq;'''),
    ('''    } appliedMark{hx, (hx.clientSession.epoch.load(std::memory_order_acquire) << 32) | reqSeq};''',
     '''    } appliedMark{hx, BackendRequestKey(backendReq->epoch, backend.owner.ownerEpoch, reqSeq)};  // r2 N2: its own epoch'''),
    ('''      const bool restartNeeded = desktopActive && backend.active != effective;''',
     '''      // r2 (Codex N2): no capture work for a stream nobody watches -- inactive / detached, the choice is only
      // stored; the next active attach (reattach: EffectiveRequested) or the climb-back applies it.
      const bool restartNeeded =
          desktopActive && streamActiveNow && !capture.idleDetached && backend.active != effective;'''),
])
print('ok')
