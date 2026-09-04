#include "native_video_client_session.hpp"

#include "native_video_client_tcp_control.hpp"
#include "poc_protocol.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace remote60::native_poc {

namespace {

constexpr uint32_t kDefaultControlResponseTimeoutMs = 1000;
constexpr uint32_t kVideoReceiveTimeoutMs = 100;
constexpr uint32_t kTcpControlConnectRetryMs = 4000;
constexpr uint32_t kTcpControlConnectRetrySleepMs = 50;
// Generous: a window list or thumbnail crossing a slow link may need several retransmit
// rounds, and giving up early would drop a session that was about to recover.
constexpr uint32_t kUdpControlReadTimeoutMs = 12000;

uint64_t now_us() {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

bool should_append_selected_title(const std::string& detail) {
  return detail.rfind("window_list_received", 0) == 0 ||
         detail.rfind("window_selected", 0) == 0 ||
         detail.rfind("window_select_failed", 0) == 0;
}

}  // namespace

ClientSessionController::ClientSessionController()
    : keyframeRequests_(120000, 300000, 3),
      runtimeTune_(300000, 30000000, 250000, 1, 240) {}

ClientSessionController::~ClientSessionController() {
  Disconnect();
}

bool ClientSessionController::Connect(const ClientSessionConnectArgs& args) {
  std::string error;
  if (!initialize_sockets(&error)) {
    std::lock_guard<std::mutex> lock(mu_);
    ResetUnlocked();
    snapshot_.state = ClientSessionState::Error;
    snapshot_.status = "error";
    snapshot_.lastError = error;
    return false;
  }

  if (args.host.empty()) {
    std::lock_guard<std::mutex> lock(mu_);
    ResetUnlocked();
    snapshot_.state = ClientSessionState::Error;
    snapshot_.status = "error";
    snapshot_.lastError = "host is required";
    return false;
  }
  if (!IsValidPort(args.videoPort)) {
    std::lock_guard<std::mutex> lock(mu_);
    ResetUnlocked();
    snapshot_.state = ClientSessionState::Error;
    snapshot_.status = "error";
    snapshot_.lastError = "video port is invalid";
    return false;
  }
  if (!IsValidPort(args.controlPort)) {
    std::lock_guard<std::mutex> lock(mu_);
    ResetUnlocked();
    snapshot_.state = ClientSessionState::Error;
    snapshot_.status = "error";
    snapshot_.lastError = "control port is invalid";
    return false;
  }

  StopWorker();

  {
    std::lock_guard<std::mutex> lock(mu_);
    ResetUnlocked();
    snapshot_.state = ClientSessionState::Connecting;
    snapshot_.status = "connecting";
    snapshot_.host = args.host;
    snapshot_.videoPort = args.videoPort;
    snapshot_.controlPort = args.controlPort;
    snapshot_.sessionThreadActive = true;
    encodedFrameSink_ = args.encodedFrameSink;
  }

  stopRequested_.store(false, std::memory_order_release);
  try {
    workerThread_ = std::thread(&ClientSessionController::WorkerMain, this, args);
  } catch (...) {
    std::lock_guard<std::mutex> lock(mu_);
    ResetUnlocked();
    snapshot_.state = ClientSessionState::Error;
    snapshot_.status = "error";
    snapshot_.lastError = "failed to start session worker";
    return false;
  }
  return true;
}

void ClientSessionController::Disconnect() {
  StopWorker();
  std::lock_guard<std::mutex> lock(mu_);
  ResetUnlocked();
}

ClientSessionSnapshot ClientSessionController::Snapshot() const {
  std::lock_guard<std::mutex> lock(mu_);
  return snapshot_;
}

WindowPanelSnapshot ClientSessionController::WindowPanelSnapshotCopy() const {
  return windowPanel_.Snapshot();
}

bool ClientSessionController::RequestWindowList() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!CanQueueControlRequestLocked()) return false;
  }

  windowPanel_.RequestList("window_list_request pending");
  {
    // A manual refresh should also refresh the previews.
    std::lock_guard<std::mutex> lk(thumbMu_);
    thumbs_.clear();
    thumbFetchQueue_.clear();
  }
  const auto panelSnapshot = windowPanel_.Snapshot();
  std::lock_guard<std::mutex> lock(mu_);
  if (!CanQueueControlRequestLocked()) return false;
  SyncWindowPanelSnapshotLocked(panelSnapshot);
  UpdateConnectedStatusLocked(panelSnapshot.status);
  return true;
}

bool ClientSessionController::RequestMonitorList() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!CanQueueControlRequestLocked()) return false;
  }
  windowPanel_.RequestMonitorList();
  return true;
}

bool ClientSessionController::RequestMonitorSelect(uint32_t monitorId) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!CanQueueControlRequestLocked()) return false;
  }
  // Refused when the host never advertised support, so the caller can say so rather than leave
  // the user waiting on a request that will not be sent.
  return windowPanel_.RequestMonitorSelect(monitorId);
}

bool ClientSessionController::RequestWindowSelect(uint64_t windowId) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!CanQueueControlRequestLocked()) return false;
  }

  const char* statusText = (windowId == 0) ? "desktop_select_requested" : "window_select_requested";
  if (!windowPanel_.RequestSelect(windowId, statusText)) {
    return false;
  }

  const auto panelSnapshot = windowPanel_.Snapshot();
  std::lock_guard<std::mutex> lock(mu_);
  if (!CanQueueControlRequestLocked()) return false;
  SyncWindowPanelSnapshotLocked(panelSnapshot);
  UpdateConnectedStatusLocked(panelSnapshot.status);
  return true;
}

bool ClientSessionController::RequestDesktopMode() {
  return RequestWindowSelect(0);
}

bool ClientSessionController::RequestStreamActive(bool active) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!CanQueueControlRequestLocked()) return false;
  }
  streamState_.Request(active);
  return true;
}

bool ClientSessionController::RequestRuntimeConfig(uint32_t bitrate, uint32_t fps) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!CanQueueControlRequestLocked()) return false;
  }
  if (bitrate == 0 && fps == 0) return false;
  runtimeTune_.SetEnabled(true);
  runtimeTune_.SetTargets(bitrate, 0, fps);
  return true;
}

bool ClientSessionController::RequestDesktopCaptureBackend(uint16_t backend) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!CanQueueControlRequestLocked()) return false;
  }
  if (backend < 1 || backend > 2) return false;
  desktopBackend_.Request(backend);
  return true;
}

bool ClientSessionController::QueueInputEvent(uint16_t kind, int32_t x, int32_t y, int32_t wheelDelta,
                                              uint32_t keyCode, uint16_t buttons) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!CanQueueControlRequestLocked()) return false;
  }
  if (kind < 1 || kind > 6) return false;

  inputQueue_.Enqueue(make_control_input_event(inputQueue_, kind, buttons, x, y, wheelDelta,
                                               keyCode, now_us()));
  return true;
}

bool ClientSessionController::QueueInputText(const uint16_t* text, size_t count) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!CanQueueControlRequestLocked()) return false;
  }
  return enqueue_control_input_text(inputQueue_, text, count, now_us()) > 0;
}

bool ClientSessionController::IsValidPort(int port) {
  return port > 0 && port <= 65535;
}

void ClientSessionController::WorkerMain(ClientSessionConnectArgs args) {
  if (encodedFrameSink_) {
    encodedFrameSink_->OnVideoStreamReset();
  }

  if (args.requireUdpHello) {
    std::string error;
    if (!ConnectUdpVideo(args, &error)) {
      if (!stopRequested_.load(std::memory_order_acquire)) FailWorker(error);
      FinalizeWorkerExit();
      return;
    }
  }

  if (encodedFrameSink_ && udpVideoSocket_ != kInvalidSocket) {
    try {
      videoThread_ = std::thread(&ClientSessionController::VideoReceiveMain, this);
    } catch (...) {
      if (!stopRequested_.load(std::memory_order_acquire)) {
        FailWorker("failed to start video receive worker");
      }
      FinalizeWorkerExit();
      return;
    }
  }

  if (args.requireTcpControl) {
    std::string error;
    if (!ConnectTcpControlWithRetry(args, &error)) {
      if (!stopRequested_.load(std::memory_order_acquire)) FailWorker(error);
      FinalizeWorkerExit();
      return;
    }
  }

  // Control rides the video socket when there is no way to open a second connection to the
  // host. The video receive thread is already reading that socket, so it hands control
  // datagrams to the channel and this loop drives the exchange.
  std::unique_ptr<ControlLink> controlLink;
  if (args.controlOverUdp && udpVideoSocket_ != kInvalidSocket) {
    const SocketHandle videoSocket = udpVideoSocket_;
    udpControl_.Configure(
        [videoSocket](const void* data, size_t len) -> bool {
          return send(videoSocket, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
        },
        kUdpControlStreamClientToHost, kUdpControlStreamHostToClient, 1200);
    controlOverUdp_.store(true, std::memory_order_release);
    controlLink = std::make_unique<UdpControlLink>(&udpControl_, kUdpControlReadTimeoutMs);
  } else if (args.requireTcpControl) {
    controlLink = std::make_unique<TcpControlLink>([this]() {
      std::lock_guard<std::mutex> lock(mu_);
      return tcpControlSocket_;
    });
  }

  const bool controlActive = controlLink != nullptr;

  {
    std::lock_guard<std::mutex> lock(mu_);
    controlScheduler_.Reset(args.controlIntervalMs, now_us());
    if (controlActive) {
      windowPanel_.RequestList("window_list_request pending");
    }
    snapshot_.state = ClientSessionState::Connected;
    snapshot_.lastError.clear();
    snapshot_.controlLoopActive = controlActive;
    UpdateConnectedStatusLocked("");
  }

  if (!controlActive) {
    while (!stopRequested_.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    FinalizeWorkerExit();
    return;
  }

  while (!stopRequested_.load(std::memory_order_acquire)) {
    bool didWork = false;
    const uint64_t loopNowUs = now_us();
    ControlOutboundAction action{};
    ClientControlMetricsSnapshot metrics{};
    // Report what the display is doing back to the host once a second. Without this the host
    // only ever saw receive/decode counters, which stay healthy through visible stutter.
    if (encodedFrameSink_) {
      static uint64_t lastPresentReportUs = 0;
      if (loopNowUs >= lastPresentReportUs + 1000000ULL) {
        ClientPresentationStats present{};
        if (encodedFrameSink_->DrainPresentationStats(&present) && present.sampleCount > 0) {
          metrics.message.presentTargetIntervalUs = present.targetIntervalUs;
          metrics.message.presentFpsX100 = present.fpsX100;
          metrics.message.presentGapP50Us = present.gapP50Us;
          metrics.message.presentGapP95Us = present.gapP95Us;
          metrics.message.presentGapMaxUs = present.gapMaxUs;
          metrics.message.presentOver1_5xCount = present.over1_5xCount;
          metrics.message.presentOver2xCount = present.over2xCount;
          metrics.message.presentSampleCount = present.sampleCount;
          metrics.message.presentScheduledCount = present.scheduledCount;
          metrics.message.presentImmediateCount = present.immediateCount;
          metrics.message.presentReanchorCount = present.reanchorCount;
          metrics.message.presentDisplayedCount = present.displayedCount;
          metrics.updatedQpcUs = loopNowUs;
          lastPresentReportUs = loopNowUs;
        }
      }
    }
    if (controlScheduler_.NextAction(loopNowUs, metrics, &windowPanel_, &streamState_, &captureMode_,
                                     &keyframeRequests_, &runtimeTune_, &inputQueue_, &action,
                                     &desktopBackend_)) {
      if (!controlLink->Alive()) {
        if (!stopRequested_.load(std::memory_order_acquire)) {
          SignalRuntimeFailure("control link closed");
        }
        break;
      }

      TcpControlResponse response{};
      if (!execute_control_action(*controlLink, action, &response)) {
        if (!stopRequested_.load(std::memory_order_acquire)) {
          SignalRuntimeFailure("control loop failed");
        }
        break;
      }
      didWork = true;

      switch (response.kind) {
        case TcpControlResponseKind::Pong:
          controlScheduler_.OnPingCompleted(now_us());
          // The host says here whether a UAC prompt or the lock screen is in front. It is the
          // only signal the viewer has for "the picture is frozen because Windows is showing
          // something we cannot capture", and the client can offer to unlock when it is set.
          hostSecureDesktopActive_.store(
              (response.pong.captureTargetFlags & kCaptureFlagSecureDesktopActive) != 0,
              std::memory_order_relaxed);
          break;
        case TcpControlResponseKind::MonitorList: {
          windowPanel_.ApplyMonitorList(response.monitorList);
          const auto panelSnapshot = windowPanel_.Snapshot();
          std::lock_guard<std::mutex> lock(mu_);
          SyncWindowPanelSnapshotLocked(panelSnapshot);
          break;
        }
        case TcpControlResponseKind::WindowList: {
          windowPanel_.ApplyWindowList(response.windowList, 4);
          hostSupportsThumbnails_.store(
              (response.windowList.flags & kControlWindowListFlagThumbnails) != 0,
              std::memory_order_relaxed);
          // The window list is fetched on every connect, so it is where the host says whether it
          // knows the monitor messages at all. Asking one that does not would hang the loop.
          if (windowPanel_.SetHostSupportsMonitors(
                  (response.windowList.flags & kControlWindowListFlagMonitors) != 0)) {
            windowPanel_.RequestMonitorList();
          }
          QueueThumbnailFetchesFromPanel();
          const auto panelSnapshot = windowPanel_.Snapshot();
          std::lock_guard<std::mutex> lock(mu_);
          SyncWindowPanelSnapshotLocked(panelSnapshot);
          UpdateConnectedStatusLocked(panelSnapshot.status);
          break;
        }
        case TcpControlResponseKind::WindowSelected: {
          windowPanel_.ApplyWindowSelected(response.windowSelected);
          if (encodedFrameSink_) {
            encodedFrameSink_->OnWindowSelectionControlResult(response.windowSelected);
          }
          const auto panelSnapshot = windowPanel_.Snapshot();
          std::lock_guard<std::mutex> lock(mu_);
          SyncWindowPanelSnapshotLocked(panelSnapshot);
          UpdateConnectedStatusLocked(panelSnapshot.status);
          break;
        }
        case TcpControlResponseKind::InputAck:
        case TcpControlResponseKind::None:
        default:
          break;
      }
    }

    if (!didWork && controlLink->Alive()) {
      const int fetched = FetchOneThumbnailLocked(*controlLink);
      if (fetched < 0) {
        if (!stopRequested_.load(std::memory_order_acquire)) {
          SignalRuntimeFailure("thumbnail fetch failed");
        }
        break;
      }
      didWork = (fetched > 0);
    }
    if (!didWork) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  FinalizeWorkerExit();
}

void ClientSessionController::QueueThumbnailFetchesFromPanel() {
  if (!hostSupportsThumbnails_.load(std::memory_order_relaxed)) return;
  const WindowPanelSnapshot snap = windowPanel_.Snapshot();
  std::lock_guard<std::mutex> lk(thumbMu_);
  auto want = [&](uint64_t id) {
    if (thumbs_.count(id) != 0) return;  // refreshed on the next list roundtrip instead
    if (std::find(thumbFetchQueue_.begin(), thumbFetchQueue_.end(), id) !=
        thumbFetchQueue_.end()) {
      return;
    }
    thumbFetchQueue_.push_back(id);
  };
  want(0);
  for (const auto& item : snap.items) want(item.id);
}

// Returns 1 if a preview was fetched, 0 if there was nothing to do, -1 on a socket error
// (the strict request/response stream is then desynced and the session must drop).
int ClientSessionController::FetchOneThumbnailLocked(ControlLink& link) {
  if (!hostSupportsThumbnails_.load(std::memory_order_relaxed)) return 0;
  uint64_t id = 0;
  {
    std::lock_guard<std::mutex> lk(thumbMu_);
    if (thumbFetchQueue_.empty()) return 0;
    id = thumbFetchQueue_.front();
    thumbFetchQueue_.pop_front();
  }
  // The exchange is the shared fetch_window_thumbnail (F-09); this side only picks the card and
  // converts the pixels.
  WindowThumbnailReply reply;
  if (!fetch_window_thumbnail(link, id, 256, 160, now_us(), &reply)) return -1;
  if (reply.present) {
    // Wire format is BGRA; Android Bitmap.copyPixelsFromBuffer wants RGBA byte order.
    for (size_t i = 0; i + 3 < reply.bgra.size(); i += 4) {
      std::swap(reply.bgra[i], reply.bgra[i + 2]);
    }
    std::lock_guard<std::mutex> lk(thumbMu_);
    auto& t = thumbs_[id];
    t.width = reply.width;
    t.height = reply.height;
    t.version = reply.version;
    t.rgba = std::move(reply.bgra);
  }
  return 1;
}

bool ClientSessionController::CopyWindowThumbnail(uint64_t windowId, WindowThumbnail* out) const {
  if (!out) return false;
  std::lock_guard<std::mutex> lk(thumbMu_);
  const auto it = thumbs_.find(windowId);
  if (it == thumbs_.end() || it->second.rgba.empty()) return false;
  *out = it->second;
  return true;
}

uint64_t ClientSessionController::SessionBytesReceived() const {
  return sessionBytesReceived_.load(std::memory_order_relaxed);
}

uint64_t ClientSessionController::WindowThumbnailVersion(uint64_t windowId) const {
  std::lock_guard<std::mutex> lk(thumbMu_);
  const auto it = thumbs_.find(windowId);
  return (it == thumbs_.end()) ? 0 : it->second.version;
}

void ClientSessionController::VideoReceiveMain() {
  UdpH264FrameAssembler assembler;
  std::array<uint8_t, 1600> datagram{};
  uint64_t assemblyDropped = 0;
  uint64_t oversizePayloadDropCount = 0;
  uint64_t fecRecoveredCount = 0;
  bool waitForKeyframe = true;

  // Video NACK: ask the host to replay the missing chunks of the oldest stuck AU, instead of
  // waiting for the next (on a static screen, far-off) frame to reveal the loss and then eating a
  // full IDR. Only when the host advertised support. Bounded by a reorder grace and a few rounds;
  // if it does not recover, the existing keyframe path takes over. (video NACK.)
  uint32_t nackSeq = 0;
  uint64_t nackFirstUs = 0;
  uint64_t nackLastUs = 0;
  uint32_t nackRounds = 0;
  auto maybe_send_nack = [&](SocketHandle sock) {
    if (sock == kInvalidSocket || !hostSupportsNack_.load(std::memory_order_relaxed)) return;
    if (waitForKeyframe) {  // already resyncing to an IDR -> repairing the old AU is moot
      nackSeq = 0;
      nackRounds = 0;
      return;
    }
    uint16_t missing[remote60::native_poc::kUdpVideoNackMaxMissing];
    UdpH264FrameAssembler::IncompleteAuInfo info{};
    if (!assembler.OldestIncomplete(missing, remote60::native_poc::kUdpVideoNackMaxMissing, &info)) {
      nackSeq = 0;
      nackRounds = 0;
      return;
    }
    const uint64_t now = now_us();
    if (info.seq != nackSeq) {  // a different AU is now the blocker -> restart the round schedule
      nackSeq = info.seq;
      nackFirstUs = now;
      nackLastUs = 0;
      nackRounds = 0;
    }
    constexpr uint64_t kNackGraceUs = 12000;   // let a brief reorder settle before asking
    constexpr uint64_t kNackRoundUs = 18000;   // spacing between rounds (a few RTTs on this path)
    constexpr uint32_t kNackMaxRounds = 4;     // then give up and let the IDR path recover
    if (now - nackFirstUs < kNackGraceUs) return;
    if (nackRounds >= kNackMaxRounds) return;
    if (nackLastUs != 0 && now - nackLastUs < kNackRoundUs) return;
    const uint16_t count =
        std::min<uint16_t>(info.missingTotal, remote60::native_poc::kUdpVideoNackMaxMissing);
    if (count == 0) return;
    UdpVideoNackPacket nack{};
    nack.streamGeneration = info.generation;
    nack.seq = info.seq;
    nack.chunkCount = info.chunkCount;
    nack.missingCount = count;
    nack.round = static_cast<uint16_t>(nackRounds);
    std::memcpy(nack.missing, missing, static_cast<size_t>(count) * sizeof(uint16_t));
    (void)send(sock, reinterpret_cast<const char*>(&nack), sizeof(nack), 0);
    ++nackRounds;
    nackLastUs = now;
  };

  while (!stopRequested_.load(std::memory_order_acquire)) {
    SocketHandle udpSocket = kInvalidSocket;
    ClientEncodedFrameSink* sink = nullptr;
    {
      std::lock_guard<std::mutex> lock(mu_);
      udpSocket = udpVideoSocket_;
      sink = encodedFrameSink_;
    }
    if (udpSocket == kInvalidSocket || !sink) break;

    const int n = recv(udpSocket, reinterpret_cast<char*>(datagram.data()), static_cast<int>(datagram.size()), 0);
    if (n <= 0) {
      if (stopRequested_.load(std::memory_order_acquire)) break;
      if (last_socket_error_is_retryable()) {
        // The read timeout is also the channel's heartbeat: without it a stalled control
        // transfer would sit unrecovered on an otherwise silent link.
        if (controlOverUdp_.load(std::memory_order_acquire)) udpControl_.Tick();
        // A quiet socket on a static screen is exactly when a lost chunk goes unnoticed: use the
        // timeout wakeup to NACK the stuck AU rather than wait for the next frame. (video NACK.)
        maybe_send_nack(udpSocket);
        continue;
      }
      SignalRuntimeFailure("udp video receive failed");
      break;
    }

    if (controlOverUdp_.load(std::memory_order_acquire) &&
        udpControl_.OnPacket(datagram.data(), static_cast<size_t>(n))) {
      sessionBytesReceived_.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
      continue;
    }
    if (n < static_cast<int>(sizeof(UdpVideoChunkHeader))) continue;

    sessionBytesReceived_.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
    auto assembleResult = assembler.PushDatagram(datagram.data(), static_cast<size_t>(n));
    if (assembleResult.fecRecovered) {
      fecRecoveredCount += assembleResult.fecRecoveredChunks;
      if ((fecRecoveredCount % 120ULL) == 1ULL) {
        std::fprintf(stderr,
                     "[native-video-client-session] udp fec recovered chunks=%llu\n",
                     static_cast<unsigned long long>(fecRecoveredCount));
      }
    }
    if (assembleResult.droppedPreviousIncomplete) {
      ++assemblyDropped;
      if (!waitForKeyframe) sink->OnVideoDiscontinuity();
      waitForKeyframe = true;
      (void)keyframeRequests_.Request(2, now_us());
    }
    if (assembleResult.disposition == UdpH264AssemblyDisposition::Malformed) {
      ++assemblyDropped;
      if (!waitForKeyframe) sink->OnVideoDiscontinuity();
      waitForKeyframe = true;
      (void)keyframeRequests_.Request(2, now_us());
      if (assembleResult.oversizePayload && ((++oversizePayloadDropCount % 30ULL) == 1ULL)) {
        std::fprintf(stderr,
                     "[native-video-client-session] dropped oversized udp payload bytes=%u count=%llu\n",
                     assembleResult.rejectedPayloadSize,
                     static_cast<unsigned long long>(oversizePayloadDropCount));
      }
      continue;
    }
    if (assembleResult.disposition == UdpH264AssemblyDisposition::Dropped) {
      ++assemblyDropped;
      if (!waitForKeyframe) sink->OnVideoDiscontinuity();
      waitForKeyframe = true;
      // Request immediately. KeyframeRequestState owns the time/token limiter, so repeated
      // late datagrams cannot create an IDR storm.
      (void)keyframeRequests_.Request(2, now_us());
      continue;
    }
    if (assembleResult.disposition == UdpH264AssemblyDisposition::Completed) {
      const bool keyFrame = (assembleResult.frame.header.flags & 1u) != 0;
      if (waitForKeyframe && !keyFrame) {
        (void)keyframeRequests_.Request(2, now_us());
        continue;
      }
      if (keyFrame) waitForKeyframe = false;
      sink->OnEncodedH264Frame(std::move(assembleResult.frame));
      // The decoder may have had to discard what it was just handed. Ask for an IDR now
      // rather than letting every later delta decode against a reference that never arrived.
      if (sink->ConsumeDecoderKeyframeRequest()) {
        (void)keyframeRequests_.Request(2, now_us());
      }
    }
    // After each datagram, on a busy link, also nudge the NACK for any still-stuck earlier AU
    // (round-interval gated inside). (video NACK.)
    maybe_send_nack(udpSocket);
  }
}

void ClientSessionController::StopWorker() {
  stopRequested_.store(true, std::memory_order_release);
  // Wakes the control loop out of a blocking read before the sockets go away.
  udpControl_.Close();
  {
    std::lock_guard<std::mutex> lock(mu_);
    snapshot_.controlLoopActive = false;
    shutdown_socket(&tcpControlSocket_);
    shutdown_socket(&udpVideoSocket_);
  }
  if (workerThread_.joinable()) {
    workerThread_.join();
  }
  if (videoThread_.joinable()) {
    videoThread_.join();
  }
  stopRequested_.store(false, std::memory_order_release);
}

void ClientSessionController::FinalizeWorkerExit() {
  std::lock_guard<std::mutex> lock(mu_);
  CloseSocketsUnlocked();
  snapshot_.transport = {};
  snapshot_.controlLoopActive = false;
  snapshot_.sessionThreadActive = false;
}

void ClientSessionController::FailWorker(const std::string& error) {
  std::lock_guard<std::mutex> lock(mu_);
  if (stopRequested_.load(std::memory_order_acquire)) return;
  snapshot_.state = ClientSessionState::Error;
  snapshot_.status = "error";
  snapshot_.lastError = error.empty() ? "session failed" : error;
  snapshot_.controlLoopActive = false;
}

void ClientSessionController::SignalRuntimeFailure(const std::string& error) {
  bool expected = false;
  if (!stopRequested_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
    return;
  }

  std::lock_guard<std::mutex> lock(mu_);
  snapshot_.state = ClientSessionState::Error;
  snapshot_.status = "error";
  snapshot_.lastError = error.empty() ? "session failed" : error;
  snapshot_.controlLoopActive = false;
  shutdown_socket(&tcpControlSocket_);
  shutdown_socket(&udpVideoSocket_);
}

bool ClientSessionController::ConnectTcpControlWithRetry(const ClientSessionConnectArgs& args, std::string* error) {
  const uint64_t deadlineUs = now_us() + (static_cast<uint64_t>(kTcpControlConnectRetryMs) * 1000ULL);
  std::string lastError;
  while (!stopRequested_.load(std::memory_order_acquire)) {
    if (ConnectTcpControl(args, &lastError)) {
      if (error) error->clear();
      return true;
    }
    if (now_us() >= deadlineUs) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(kTcpControlConnectRetrySleepMs));
  }
  if (error) {
    *error = lastError.empty() ? "tcp control connect failed" : lastError;
  }
  return false;
}

bool ClientSessionController::ConnectTcpControl(const ClientSessionConnectArgs& args, std::string* error) {
  SocketHandle connected =
      connect_first_endpoint(args.host, args.controlPort, SOCK_STREAM, IPPROTO_TCP, error);
  if (connected == kInvalidSocket) {
    if (error && error->empty()) *error = "tcp control connect failed";
    return false;
  }

  (void)set_tcp_nodelay(connected);
  (void)set_recv_timeout(connected, kDefaultControlResponseTimeoutMs);
  if (stopRequested_.load(std::memory_order_acquire)) {
    close_socket(&connected);
    return false;
  }

  std::lock_guard<std::mutex> lock(mu_);
  if (stopRequested_.load(std::memory_order_acquire)) {
    close_socket(&connected);
    return false;
  }
  close_socket(&tcpControlSocket_);
  tcpControlSocket_ = connected;
  snapshot_.transport.tcpControlConnected = true;
  return true;
}

bool ClientSessionController::ConnectUdpVideo(const ClientSessionConnectArgs& args, std::string* error) {
  SocketHandle connected = kInvalidSocket;
  if (args.preparedUdpSocket != kInvalidSocket) {
    // Already punched. connect() only fixes the default peer for send/recv; it leaves the
    // local binding, and therefore the NAT mapping the host was told about, untouched.
    connected = args.preparedUdpSocket;
    addrinfo* results = nullptr;
    if (!resolve_endpoint(args.host, args.videoPort, SOCK_DGRAM, IPPROTO_UDP, &results, error)) {
      close_socket(&connected);
      if (error && error->empty()) *error = "cannot resolve punched host address";
      return false;
    }
    bool bound = false;
    for (addrinfo* it = results; it != nullptr; it = it->ai_next) {
      if (connect(connected, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0) {
        bound = true;
        break;
      }
    }
    freeaddrinfo(results);
    if (!bound) {
      close_socket(&connected);
      if (error) *error = "punched socket connect failed";
      return false;
    }
  } else {
    connected = connect_first_endpoint(args.host, args.videoPort, SOCK_DGRAM, IPPROTO_UDP, error);
  }
  if (connected == kInvalidSocket) {
    if (error && error->empty()) *error = "udp video connect failed";
    return false;
  }

  // A single 1080p keyframe arrives as a burst of well over a hundred datagrams. Android's
  // default UDP receive buffer is around 100 KB, so the tail of that burst was being dropped
  // by the kernel before the receive thread could drain it, which showed up as the picture
  // breaking up and then slowly recovering.
  {
    int recvBuf = 4 * 1024 * 1024;
    if (setsockopt(connected, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf),
                   sizeof(recvBuf)) != 0) {
      recvBuf = 1024 * 1024;
      (void)setsockopt(connected, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf),
                       sizeof(recvBuf));
    }
  }

  // A directory connect and the host heartbeat are independent HTTP requests. The first
  // authenticated Hello can therefore reach the host a few milliseconds before its matching
  // capability. Retry on the already-punched socket instead of turning that harmless race into
  // the intermittent "connecting -> error" seen by the mobile client. The exchange itself is the
  // shared udp_hello_handshake (F-09); a failed send stays fatal here (retrySleepMs = 0).
  UdpHelloOptions hello;
  hello.authToken = args.peerAuthToken;
  hello.budgetMs = args.peerAuthToken.empty() ? std::max<uint32_t>(1, args.udpHandshakeTimeoutMs)
                                              : std::max<uint32_t>(3000, args.udpHandshakeTimeoutMs);
  hello.requestNack = true;  // ask for selective retransmit (video NACK.)
  uint32_t ackFeatures = 0;
  if (!udp_hello_handshake(connected, hello, &stopRequested_, error, &ackFeatures)) {
    close_socket(&connected);
    return false;
  }
  hostSupportsNack_.store(
      (ackFeatures & remote60::native_poc::kUdpFeatureVideoNack) != 0, std::memory_order_relaxed);

  (void)set_recv_timeout(connected, kVideoReceiveTimeoutMs);
  if (stopRequested_.load(std::memory_order_acquire)) {
    close_socket(&connected);
    return false;
  }

  std::lock_guard<std::mutex> lock(mu_);
  if (stopRequested_.load(std::memory_order_acquire)) {
    close_socket(&connected);
    return false;
  }
  udpVideoSocket_ = connected;
  snapshot_.transport.udpVideoReady = true;
  return true;
}

bool ClientSessionController::CanQueueControlRequestLocked() const {
  // What matters is that a control link exists, not which transport carries it. Checking the
  // TCP socket alone silently refused every request once control moved onto the video socket.
  const bool haveTransport =
      tcpControlSocket_ != kInvalidSocket ||
      (controlOverUdp_.load(std::memory_order_acquire) && udpVideoSocket_ != kInvalidSocket);
  return snapshot_.state == ClientSessionState::Connected &&
         snapshot_.controlLoopActive && haveTransport;
}

void ClientSessionController::UpdateConnectedStatusLocked(const std::string& detail) {
  snapshot_.status = "connected";
  if (!detail.empty()) {
    snapshot_.status += " ";
    snapshot_.status += detail;
    if (should_append_selected_title(detail)) {
      snapshot_.status += " selected=";
      snapshot_.status += snapshot_.selectedWindowTitle;
    }
  }
}

void ClientSessionController::SyncWindowPanelSnapshotLocked(const WindowPanelSnapshot& panelSnapshot) {
  snapshot_.latestWindowListCount = static_cast<uint32_t>(panelSnapshot.items.size());
  snapshot_.selectedWindowId = panelSnapshot.selectedId;
  snapshot_.selectedWindowTitle =
      panelSnapshot.selectedTitle.empty() ? std::string("desktop") : panelSnapshot.selectedTitle;
}

void ClientSessionController::ResetUnlocked() {
  CloseSocketsUnlocked();
  snapshot_ = ClientSessionSnapshot{};
  windowPanel_.Reset();
  streamState_.Reset();
  captureMode_.Reset();
  keyframeRequests_.Reset();
  runtimeTune_.Reset(0, 0);
  desktopBackend_.Reset();
  inputQueue_.Reset();
  {
    std::lock_guard<std::mutex> lk(thumbMu_);
    thumbs_.clear();
    thumbFetchQueue_.clear();
  }
  hostSupportsThumbnails_.store(false, std::memory_order_relaxed);
  sessionBytesReceived_.store(0, std::memory_order_relaxed);
  controlOverUdp_.store(false, std::memory_order_release);
  udpControl_.Reset();
  if (encodedFrameSink_) {
    encodedFrameSink_->OnVideoStreamReset();
  }
  encodedFrameSink_ = nullptr;
}

void ClientSessionController::CloseSocketsUnlocked() {
  close_socket(&tcpControlSocket_);
  close_socket(&udpVideoSocket_);
}

}  // namespace remote60::native_poc
