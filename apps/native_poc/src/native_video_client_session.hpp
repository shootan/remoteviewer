#pragma once

#include <cstddef>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

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
  bool RequestStreamActive(bool active);
  bool RequestRuntimeConfig(uint32_t bitrate, uint32_t fps);
  bool RequestDesktopCaptureBackend(uint16_t backend);
  bool QueueInputEvent(uint16_t kind, int32_t x, int32_t y, int32_t wheelDelta,
                       uint32_t keyCode, uint16_t buttons);
  bool QueueInputText(const uint16_t* text, size_t count);

  struct WindowThumbnail {
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t version = 0;  // host timestamp; changes when the preview content changes
    std::vector<uint8_t> rgba;
  };
  // Copies the cached preview for a window id (0 = desktop). Returns false if none yet.
  bool CopyWindowThumbnail(uint64_t windowId, WindowThumbnail* out) const;
  // Cheap change marker so the UI can skip re-decoding unchanged previews.
  uint64_t WindowThumbnailVersion(uint64_t windowId) const;
  /** Total UDP video bytes received this session, for the on-screen data meter. */
  uint64_t SessionBytesReceived() const;

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
  StreamStateControl streamState_;
  CaptureModeRequestState captureMode_;
  KeyframeRequestState keyframeRequests_;
  RuntimeTuneState runtimeTune_;
  DesktopBackendControl desktopBackend_;
  ClientInputQueue inputQueue_;
  ClientControlScheduler controlScheduler_;
  ClientEncodedFrameSink* encodedFrameSink_ = nullptr;
  std::thread workerThread_;
  std::thread videoThread_;
  std::atomic<bool> stopRequested_{false};
  SocketHandle tcpControlSocket_ = kInvalidSocket;
  SocketHandle udpVideoSocket_ = kInvalidSocket;

  int FetchOneThumbnailLocked(SocketHandle controlSocket);
  void QueueThumbnailFetchesFromPanel();
  mutable std::mutex thumbMu_;
  std::unordered_map<uint64_t, WindowThumbnail> thumbs_;
  std::deque<uint64_t> thumbFetchQueue_;
  std::atomic<bool> hostSupportsThumbnails_{false};
  std::atomic<uint64_t> sessionBytesReceived_{0};
};

}  // namespace remote60::native_poc
