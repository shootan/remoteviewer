#include "native_video_client_session.hpp"

#include "native_video_client_tcp_control.hpp"
#include "poc_protocol.hpp"

#include <array>
#include <chrono>
#include <string>
#include <thread>

namespace remote60::native_poc {

namespace {

constexpr uint32_t kDefaultControlResponseTimeoutMs = 1000;
constexpr uint32_t kVideoReceiveTimeoutMs = 100;
constexpr uint32_t kTcpControlConnectRetryMs = 4000;
constexpr uint32_t kTcpControlConnectRetrySleepMs = 50;

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
  const auto panelSnapshot = windowPanel_.Snapshot();
  std::lock_guard<std::mutex> lock(mu_);
  if (!CanQueueControlRequestLocked()) return false;
  SyncWindowPanelSnapshotLocked(panelSnapshot);
  UpdateConnectedStatusLocked(panelSnapshot.status);
  return true;
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

  {
    std::lock_guard<std::mutex> lock(mu_);
    controlScheduler_.Reset(args.controlIntervalMs, now_us());
    if (args.requireTcpControl) {
      windowPanel_.RequestList("window_list_request pending");
    }
    snapshot_.state = ClientSessionState::Connected;
    snapshot_.lastError.clear();
    snapshot_.controlLoopActive = args.requireTcpControl;
    UpdateConnectedStatusLocked("");
  }

  if (!args.requireTcpControl) {
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
    if (controlScheduler_.NextAction(loopNowUs, metrics, &windowPanel_, &captureMode_,
                                     &keyframeRequests_, &runtimeTune_, &inputQueue_, &action)) {
      SocketHandle controlSocket = kInvalidSocket;
      {
        std::lock_guard<std::mutex> lock(mu_);
        controlSocket = tcpControlSocket_;
      }
      if (controlSocket == kInvalidSocket) {
        if (!stopRequested_.load(std::memory_order_acquire)) {
          SignalRuntimeFailure("tcp control socket closed");
        }
        break;
      }

      TcpControlResponse response{};
      if (!execute_tcp_control_action(controlSocket, action, &response)) {
        if (!stopRequested_.load(std::memory_order_acquire)) {
          SignalRuntimeFailure("tcp control loop failed");
        }
        break;
      }
      didWork = true;

      switch (response.kind) {
        case TcpControlResponseKind::Pong:
          controlScheduler_.OnPingCompleted(now_us());
          break;
        case TcpControlResponseKind::WindowList: {
          windowPanel_.ApplyWindowList(response.windowList, 4);
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

    if (!didWork) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  FinalizeWorkerExit();
}

void ClientSessionController::VideoReceiveMain() {
  UdpH264FrameAssembler assembler;
  std::array<uint8_t, 1600> datagram{};
  uint64_t assemblyDropped = 0;

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
      if (last_socket_error_is_retryable()) continue;
      SignalRuntimeFailure("udp video receive failed");
      break;
    }
    if (n < static_cast<int>(sizeof(UdpVideoChunkHeader))) continue;

    auto assembleResult = assembler.PushDatagram(datagram.data(), static_cast<size_t>(n));
    if (assembleResult.droppedPreviousIncomplete) {
      ++assemblyDropped;
    }
    if (assembleResult.disposition == UdpH264AssemblyDisposition::Malformed) {
      ++assemblyDropped;
      continue;
    }
    if (assembleResult.disposition == UdpH264AssemblyDisposition::Dropped) {
      ++assemblyDropped;
      if ((assemblyDropped % 20) == 1) {
        (void)keyframeRequests_.Request(2, now_us());
      }
      continue;
    }
    if (assembleResult.disposition == UdpH264AssemblyDisposition::Completed) {
      sink->OnEncodedH264Frame(std::move(assembleResult.frame));
    }
  }
}

void ClientSessionController::StopWorker() {
  stopRequested_.store(true, std::memory_order_release);
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
  SocketHandle connected =
      connect_first_endpoint(args.host, args.videoPort, SOCK_DGRAM, IPPROTO_UDP, error);
  if (connected == kInvalidSocket) {
    if (error && error->empty()) *error = "udp video connect failed";
    return false;
  }

  (void)set_recv_timeout(connected, args.udpHandshakeTimeoutMs);
  UdpHelloPacket hello{};
  const int sent = send(connected, reinterpret_cast<const char*>(&hello), sizeof(hello), 0);
  if (sent != static_cast<int>(sizeof(hello))) {
    if (error) *error = "udp hello send failed";
    close_socket(&connected);
    return false;
  }

  UdpHelloPacket ack{};
  const int received = recv(connected, reinterpret_cast<char*>(&ack), sizeof(ack), 0);
  if (received < static_cast<int>(sizeof(UdpHelloPacket)) ||
      ack.magic != kMagic ||
      ack.kind != static_cast<uint16_t>(UdpPacketKind::HelloAck)) {
    if (error) *error = "udp hello ack failed";
    close_socket(&connected);
    return false;
  }

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
  return snapshot_.state == ClientSessionState::Connected &&
         snapshot_.controlLoopActive &&
         tcpControlSocket_ != kInvalidSocket;
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
  captureMode_.Reset();
  keyframeRequests_.Reset();
  runtimeTune_.Reset(0, 0);
  inputQueue_.Reset();
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
