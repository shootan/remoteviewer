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

edit('native_video_client_shared_core.hpp', [
    ('''struct PendingDesktopBackendRequest {
  uint32_t seq = 0;
  uint16_t backend = 1;  // 1:dxgi, 2:wgc
};''',
     '''struct PendingDesktopBackendRequest {
  uint32_t seq = 0;
  uint16_t backend = 1;  // 1:dxgi, 2:wgc; 0 = ask only (capture-lifecycle r1)
  bool wantsStatus = false;  // capture-lifecycle r1: ask the host's answer (90) -- only a 0x100000 host
};'''),
    ('''class DesktopBackendControl {
 public:
  void Reset();
  void Request(uint16_t backend);
  bool ConsumePending(PendingDesktopBackendRequest* out);

private:
  std::atomic<bool> pending_{false};
  std::atomic<uint32_t> nextSeq_{0};
  std::atomic<uint16_t> backend_{2};
};''',
     '''class DesktopBackendControl {
 public:
  void Reset();
  void Request(uint16_t backend);
  bool ConsumePending(PendingDesktopBackendRequest* out);
  // capture-lifecycle r1 (UA-2): whether the connected host answers (kCaptureFlagDesktopBackendStatusV1,
  // from its pong). Only then does a request ask for the answer, and may a question be asked at all.
  void SetHostAnswers(bool answers) { hostAnswers_.store(answers, std::memory_order_release); }
  bool HostAnswers() const { return hostAnswers_.load(std::memory_order_acquire); }
  // Ask what runs now, changing nothing. False (nothing queued) toward a host that does not answer.
  bool Query();

private:
  std::atomic<bool> pending_{false};
  std::atomic<uint32_t> nextSeq_{0};
  std::atomic<uint16_t> backend_{2};
  std::atomic<bool> hostAnswers_{false};
};

// capture-lifecycle r1 (UA-2): the host's last answer (ControlDesktopBackendStatus, 90). valid = one came on
// this connection; codes 1 dxgi, 2 wgc, 3 gdi; flags kDesktopBackendStatusFlag*.
struct DesktopBackendStatusSnapshot {
  bool valid = false;
  uint32_t seq = 0;
  uint16_t requested = 0;
  uint16_t effective = 0;
  uint16_t active = 0;
  uint16_t flags = 0;
  uint32_t changeCount = 0;
  uint64_t receivedUs = 0;
};'''),
])

edit('native_video_client_shared_core.cpp', [
    ('''    case MessageType::ControlInputAck:
      return static_cast<uint16_t>(sizeof(ControlInputAckMessage));
    default:
      return 0;''',
     '''    case MessageType::ControlInputAck:
      return static_cast<uint16_t>(sizeof(ControlInputAckMessage));
    case MessageType::ControlDesktopBackendStatus:  // capture-lifecycle r1
      return static_cast<uint16_t>(sizeof(ControlDesktopBackendStatusMessage));
    default:
      return 0;'''),
    ('''bool DesktopBackendControl::ConsumePending(PendingDesktopBackendRequest* out) {
  if (!out) return false;
  if (!pending_.exchange(false, std::memory_order_acq_rel)) return false;
  out->seq = nextSeq_.fetch_add(1, std::memory_order_relaxed) + 1;
  out->backend = backend_.load(std::memory_order_acquire);
  return true;
}''',
     '''bool DesktopBackendControl::ConsumePending(PendingDesktopBackendRequest* out) {
  if (!out) return false;
  if (!pending_.exchange(false, std::memory_order_acq_rel)) return false;
  out->seq = nextSeq_.fetch_add(1, std::memory_order_relaxed) + 1;
  out->backend = backend_.load(std::memory_order_acquire);
  out->wantsStatus = hostAnswers_.load(std::memory_order_acquire);
  if (out->backend == 0 && !out->wantsStatus) return false;  // a question to a host that answers none
  return true;
}

bool DesktopBackendControl::Query() {
  if (!hostAnswers_.load(std::memory_order_acquire)) return false;
  backend_.store(0, std::memory_order_release);
  pending_.store(true, std::memory_order_release);
  return true;
}'''),
    ('''    out->desktopBackend.seq = pendingDesktopBackend.seq;
    out->desktopBackend.backend = pendingDesktopBackend.backend;
    out->desktopBackend.clientSendQpcUs = nowUs;''',
     '''    out->desktopBackend.seq = pendingDesktopBackend.seq;
    out->desktopBackend.backend = pendingDesktopBackend.backend;
    out->desktopBackend.clientSendQpcUs = nowUs;
    if (pendingDesktopBackend.wantsStatus) {  // capture-lifecycle r1 (UA-2): answered with 90
      out->desktopBackend.flags |= kDesktopBackendRequestFlagStatusV1;
      out->expectedResponseType = MessageType::ControlDesktopBackendStatus;
      out->expectedResponseSize = expected_message_size(MessageType::ControlDesktopBackendStatus);
    }'''),
])

edit('native_video_client_tcp_control.hpp', [
    ('''  PreloginStatus,  // prelogin-ui r9 R5
};''',
     '''  PreloginStatus,  // prelogin-ui r9 R5
  DesktopBackendStatus,  // capture-lifecycle r1 (UA-2)
};'''),
    ('''  ControlPreloginStatusMessage preloginStatus{};  // prelogin-ui r9 R5
};''',
     '''  ControlPreloginStatusMessage preloginStatus{};  // prelogin-ui r9 R5
  ControlDesktopBackendStatusMessage desktopBackendStatus{};  // capture-lifecycle r1 (UA-2)
};'''),
])

edit('native_video_client_tcp_control.cpp', [
    ('''    case MessageType::ControlPreloginStatus:
      out->kind = TcpControlResponseKind::PreloginStatus;
      out->preloginStatus.header = header;
      return link.Read(&out->preloginStatus.requestId,
                       sizeof(out->preloginStatus) - sizeof(MessageHeader));''',
     '''    case MessageType::ControlPreloginStatus:
      out->kind = TcpControlResponseKind::PreloginStatus;
      out->preloginStatus.header = header;
      return link.Read(&out->preloginStatus.requestId,
                       sizeof(out->preloginStatus) - sizeof(MessageHeader));
    case MessageType::ControlDesktopBackendStatus:  // capture-lifecycle r1 (UA-2)
      out->kind = TcpControlResponseKind::DesktopBackendStatus;
      out->desktopBackendStatus.header = header;
      return link.Read(&out->desktopBackendStatus.seq,
                       sizeof(out->desktopBackendStatus) - sizeof(MessageHeader));'''),
])

edit('native_video_client_session.hpp', [
    ('''  bool RequestDesktopCaptureBackend(uint16_t backend);''',
     '''  bool RequestDesktopCaptureBackend(uint16_t backend);
  // capture-lifecycle r1 (UA-2): what the host said runs for THIS connection's session (its answer to a
  // request or a question; a host without kCaptureFlagDesktopBackendStatusV1 answers neither -- then
  // valid stays false and the app must not claim "this connection only"). Query asks again, changing nothing.
  bool QueryDesktopCaptureBackend();
  DesktopBackendStatusSnapshot DesktopCaptureBackendStatus() const;
  bool HostAnswersDesktopBackendStatus() const { return desktopBackend_.HostAnswers(); }'''),
    ('''  DesktopBackendControl desktopBackend_;''',
     '''  DesktopBackendControl desktopBackend_;
  mutable std::mutex backendStatusMu_;
  DesktopBackendStatusSnapshot backendStatus_;  // capture-lifecycle r1: under backendStatusMu_'''),
])

edit('native_video_client_session.cpp', [
    ('''  desktopBackend_.Request(backend);
  return true;
}''',
     '''  desktopBackend_.Request(backend);
  return true;
}

bool ClientSessionController::QueryDesktopCaptureBackend() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!CanQueueControlRequestLocked()) return false;
  }
  std::lock_guard<std::mutex> pk(parkMu_);
  if (parkInhibit_.load(std::memory_order_acquire)) return false;
  return desktopBackend_.Query();
}

DesktopBackendStatusSnapshot ClientSessionController::DesktopCaptureBackendStatus() const {
  std::lock_guard<std::mutex> lk(backendStatusMu_);
  return backendStatus_;
}'''),
    ('''          hostSecureDesktopActive_.store(
              (response.pong.captureTargetFlags & kCaptureFlagSecureDesktopActive) != 0,
              std::memory_order_relaxed);''',
     '''          hostSecureDesktopActive_.store(
              (response.pong.captureTargetFlags & kCaptureFlagSecureDesktopActive) != 0,
              std::memory_order_relaxed);
          // capture-lifecycle r1 (UA-2): whether this host answers backend requests (every pong: another host
          // after a reconnect may not).
          desktopBackend_.SetHostAnswers((response.pong.captureTargetFlags & kCaptureFlagDesktopBackendStatusV1) != 0);'''),
    ('''        case TcpControlResponseKind::MonitorList: {''',
     '''        case TcpControlResponseKind::DesktopBackendStatus: {  // capture-lifecycle r1 (UA-2)
          const auto& st = response.desktopBackendStatus;
          std::lock_guard<std::mutex> lk(backendStatusMu_);
          backendStatus_.valid = true;
          backendStatus_.seq = st.seq;
          backendStatus_.requested = st.requested;
          backendStatus_.effective = st.effective;
          backendStatus_.active = st.active;
          backendStatus_.flags = st.flags;
          backendStatus_.changeCount = st.changeCount;
          backendStatus_.receivedUs = now_us();
          break;
        }
        case TcpControlResponseKind::MonitorList: {'''),
    ('''  desktopBackend_.Reset();
  inputQueue_.Reset();''',
     '''  desktopBackend_.Reset();
  {
    std::lock_guard<std::mutex> lk(backendStatusMu_);
    backendStatus_ = DesktopBackendStatusSnapshot{};  // capture-lifecycle r1: never carried to the next connection
  }
  inputQueue_.Reset();'''),
])
print('ok')
