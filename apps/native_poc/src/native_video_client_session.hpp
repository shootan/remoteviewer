#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "native_socket.hpp"
#include "native_video_client_shared_core.hpp"

namespace remote60::native_poc {

class ClientEncodedFrameSink {
 public:
  virtual ~ClientEncodedFrameSink() = default;
  virtual void OnEncodedH264Frame(UdpH264AssembledFrame&& frame) = 0;
  virtual void OnVideoStreamReset() = 0;
  virtual void OnWindowSelectionControlResult(const ControlWindowSelectedMessage& /* msg */) {}
};

enum class ClientSessionState : uint8_t {
  Disconnected = 0,
  Connecting = 1,
  Connected = 2,
  Error = 3,
};

struct ClientSessionTransportStatus {
  bool tcpControlConnected = false;
  bool udpVideoReady = false;
};

struct ClientSessionConnectArgs {
  std::string host;
  int videoPort = 0;
  int controlPort = 0;
  bool requireUdpHello = true;
  bool requireTcpControl = true;
  uint32_t controlIntervalMs = 1000;
  uint32_t udpHandshakeTimeoutMs = 800;
  ClientEncodedFrameSink* encodedFrameSink = nullptr;
};

struct ClientSessionSnapshot {
  ClientSessionState state = ClientSessionState::Disconnected;
  std::string status = "disconnected";
  std::string lastError;
  std::string host;
  int videoPort = 0;
  int controlPort = 0;
  ClientSessionTransportStatus transport{};
  bool sessionThreadActive = false;
  bool controlLoopActive = false;
  uint32_t latestWindowListCount = 0;
  uint64_t selectedWindowId = 0;
  std::string selectedWindowTitle = "desktop";
};

class ClientSessionController {
 public:
  ClientSessionController();
  ~ClientSessionController();

  bool Connect(const ClientSessionConnectArgs& args);
  void Disconnect();
  ClientSessionSnapshot Snapshot() const;
  WindowPanelSnapshot WindowPanelSnapshotCopy() const;
  bool RequestWindowList();
  bool RequestWindowSelect(uint64_t windowId);
  bool RequestDesktopMode();
  bool RequestRuntimeConfig(uint32_t bitrate, uint32_t fps);

 private:
  ClientSessionController(const ClientSessionController&) = delete;
  ClientSessionController& operator=(const ClientSessionController&) = delete;

  void WorkerMain(ClientSessionConnectArgs args);
  void VideoReceiveMain();
  void StopWorker();
  void FinalizeWorkerExit();
  void FailWorker(const std::string& error);
  void SignalRuntimeFailure(const std::string& error);
  bool ConnectTcpControlWithRetry(const ClientSessionConnectArgs& args, std::string* error);
  bool ConnectTcpControl(const ClientSessionConnectArgs& args, std::string* error);
  bool ConnectUdpVideo(const ClientSessionConnectArgs& args, std::string* error);
  bool CanQueueControlRequestLocked() const;
  void UpdateConnectedStatusLocked(const std::string& detail);
  void SyncWindowPanelSnapshotLocked(const WindowPanelSnapshot& panelSnapshot);
  void CloseSocketsUnlocked();
  void ResetUnlocked();
  static bool IsValidPort(int port);

  mutable std::mutex mu_;
  ClientSessionSnapshot snapshot_;
  WindowPanelStateModel windowPanel_;
  CaptureModeRequestState captureMode_;
  KeyframeRequestState keyframeRequests_;
  RuntimeTuneState runtimeTune_;
  ClientInputQueue inputQueue_;
  ClientControlScheduler controlScheduler_;
  ClientEncodedFrameSink* encodedFrameSink_ = nullptr;
  std::thread workerThread_;
  std::thread videoThread_;
  std::atomic<bool> stopRequested_{false};
  SocketHandle tcpControlSocket_ = kInvalidSocket;
  SocketHandle udpVideoSocket_ = kInvalidSocket;
};

}  // namespace remote60::native_poc
