#pragma once

#include <cstddef>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "clipboard_sync.hpp"
#include "native_socket.hpp"
#include "native_video_client_shared_core.hpp"
#include "udp_control_channel.hpp"

namespace remote60::native_poc {

// One window's worth of display timing, measured where frames actually reach the screen.
struct ClientPresentationStats {
  uint32_t targetIntervalUs = 0;
  uint32_t fpsX100 = 0;
  uint32_t gapP50Us = 0;
  uint32_t gapP95Us = 0;
  uint32_t gapMaxUs = 0;
  uint32_t over1_5xCount = 0;
  uint32_t over2xCount = 0;
  uint32_t sampleCount = 0;
  uint32_t scheduledCount = 0;
  uint32_t immediateCount = 0;
  uint32_t reanchorCount = 0;
  uint32_t displayedCount = 0;
};

class ClientEncodedFrameSink {
 public:
  virtual ~ClientEncodedFrameSink() = default;
  /** Drains the presentation stats accumulated since the last call; false when the sink does
   *  not present frames itself (a decode-only sink has nothing to report). */
  virtual bool DrainPresentationStats(ClientPresentationStats* /* out */) { return false; }
  virtual void OnEncodedH264Frame(UdpH264AssembledFrame&& frame) = 0;
  virtual void OnVideoStreamReset() = 0;
  // Called when a compressed reference frame was lost. Implementations that retain decoder
  // state should flush it before the next IDR; the default keeps lightweight test sinks valid.
  virtual void OnVideoDiscontinuity() {}
  // A decoder can be forced to discard a delta of its own accord -- typically when the
  // hardware codec has no free input buffer -- which silently breaks the reference chain for
  // everything that follows. Returning true asks the session for an IDR; the flag is consumed
  // so one drop produces one request and the session's own rate limiter still applies.
  virtual bool ConsumeDecoderKeyframeRequest() { return false; }
  virtual void OnWindowSelectionControlResult(const ControlWindowSelectedMessage& /* msg */) {}
  // t-970r4zgo: the local selection a select request belongs to -- read when the request is
  // queued (the Android sink: the generation PrepareForWindowSelection armed). 0 = not tracked.
  virtual uint64_t CurrentSelectionTag() { return 0; }
  // r4 M1-A / r5 N1: non-zero while a selection is still owed an IDR of its answered generation --
  // until such an IDR is handed to the decoder. An opaque token: it changes when the duty is
  // re-armed (the decoder became able again), and the session's bounded retries start over.
  virtual uint64_t KeyframeOwedFor() { return 0; }
  // The answer to a select request, with the tag that request was queued under. A sink that
  // tracks selections applies it only to that selection; the default ignores the tag.
  virtual void OnWindowSelectionControlResultFor(const ControlWindowSelectedMessage& msg, uint64_t /* requestTag */) {
    OnWindowSelectionControlResult(msg);
  }
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
  // Tunnels control through the video socket instead of opening a TCP connection. Required
  // when the host was reached by hole punching, since only that one socket has a path.
  bool controlOverUdp = false;
  std::string peerAuthToken;
  // An already-punched socket to adopt instead of opening a new one. It must be the socket the
  // punch was performed with: a fresh one would sit behind a different NAT mapping and the
  // host's packets would be dropped. The session takes ownership.
  SocketHandle preparedUdpSocket = kInvalidSocket;
  // The uplink keepalive interval; see kClientControlIntervalMsDefault.
  uint32_t controlIntervalMs = kClientControlIntervalMsDefault;
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
  bool RequestMonitorList();
  // listRevision: the WindowPanelSnapshot::monitorListRevision the index was read from (~0 = the
  // current list). A pick made from a list that has since been replaced is refused at once.
  bool RequestMonitorSelect(uint32_t monitorId, uint64_t listRevision = ~0ULL);
  bool HostSecureDesktopActive() const {
    return hostSecureDesktopActive_.load(std::memory_order_relaxed);
  }
  bool RequestStreamActive(bool active);
  // The viewer abandoned a switch (nativeAbortVideoSwitch): an IDR it was still owed is not asked for.
  void ClearPendingDecoderKeyframe() { decoderRekeyPending_.store(false, std::memory_order_release); }
  bool RequestRuntimeConfig(uint32_t bitrate, uint32_t fps);
  bool RequestDesktopCaptureBackend(uint16_t backend);
  bool QueueInputEvent(uint16_t kind, int32_t x, int32_t y, int32_t wheelDelta,
                       uint32_t keyCode, uint16_t buttons);
  bool QueueInputText(const uint16_t* text, size_t count);

  // --- clipboard text sync (K1) ---------------------------------------------------------------
  //
  // Text is UTF-16 code units, which is what Java strings already are (jchar) and what the wire
  // carries -- see clipboard_sync.hpp for why this is u16string and not wstring.
  //
  // The phone cannot watch its clipboard the way the Windows viewer does: from Android 10 an app
  // may only read the clipboard while it holds focus. So the app calls QueueClipboardText when it
  // is in the foreground and sees a change, and the control thread sends it. The echo guard still
  // applies, so text that just arrived from the host is not sent straight back.
  //
  // Returns false when the session cannot take it (not connected, sync off, host without the
  // capability) or when the core judged it not worth sending (empty, duplicate, echo, oversize).
  bool QueueClipboardText(const uint16_t* text, size_t count);

  // Drains clipboard text that arrived from the host, if any. The app polls this from the ticker
  // it already runs, because the bridge has no native->Java callback, and applies it to the
  // Android clipboard itself (which likewise requires the foreground).
  bool TakeIncomingClipboardText(std::u16string* out);

  /**
   * True once per session, when the session wants the app to push the phone's current clipboard.
   *
   * A request rather than a call because only the app can read the clipboard on Android, and only
   * while it is in the foreground. Consumed as it is read.
   */
  bool TakeClipboardPushRequest() {
    return clipInitialPushWanted_.exchange(false, std::memory_order_acq_rel);
  }

  void SetClipboardSyncEnabled(bool enabled);
  bool ClipboardSyncEnabled() const { return clipboardEnabled_.load(std::memory_order_relaxed); }
  // Whether the connected host advertised kCaptureFlagClipboardTextV1, so the UI can show the
  // toggle as unavailable rather than appearing to work while doing nothing.
  bool HostSupportsClipboard() const {
    return hostSupportsClipboard_.load(std::memory_order_relaxed);
  }

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

  // udp-recv-exit r1/r2 test seams. Precisely (r2 U wording): the hook MEMBER and the
  // `if (udpRecvHookForTest_)` branch in VideoReceiveMain DO ship -- they are a null-default runtime
  // check that is simply never true in production (no code path, env, message or user option sets the
  // hook), so the shipped loop always calls the real recv; they are NOT claimed removed by /OPT:REF. The
  // test-only METHODS below are unreferenced by the product and are what /OPT:REF can strip. They let a
  // test drive the REAL VideoReceive loop with scripted recv results deterministically.
  void SetUdpRecvHookForTest(std::function<int(uint8_t*, size_t, int&)> hook) { udpRecvHookForTest_ = std::move(hook); }
  void RunVideoReceiveForTest(SocketHandle udpSocket, ClientEncodedFrameSink* sink);
  void RequestStopForTest() { stopRequested_.store(true, std::memory_order_release); }
  // Enable the NACK/hold receive policy for a test so the loop's maintenance tick (NACK rounds, hold
  // expiry, stuck-head give-up) is exercised while recv results are being discarded.
  void SetHostSupportsNackForTest(bool v) { hostSupportsNack_.store(v, std::memory_order_release); }
  // Enable the control-over-UDP branch, and reach the control channel, so a test can leave a control
  // message outstanding and observe the loop's udpControl_.Tick() retransmit -> PeerLost during drops.
  void SetControlOverUdpForTest(bool v) { controlOverUdp_.store(v, std::memory_order_release); }
  UdpControlChannel& ControlChannelForTest() { return udpControl_; }

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
  std::function<int(uint8_t*, size_t, int&)> udpRecvHookForTest_;  // null in production -> real recv
  std::thread workerThread_;
  std::thread videoThread_;
  std::atomic<bool> stopRequested_{false};
  SocketHandle tcpControlSocket_ = kInvalidSocket;
  SocketHandle udpVideoSocket_ = kInvalidSocket;
  UdpControlChannel udpControl_;
  std::atomic<bool> controlOverUdp_{false};

  int FetchOneThumbnailLocked(ControlLink& link);
  // Clipboard text sync (K1), run on the control thread's idle turns like the thumbnail fetch:
  // sends a queued local clipboard, otherwise polls the host on an interval.
  // Returns: 1 did work, 0 nothing to do, -1 link failure (the session must drop).
  int PumpClipboardSync(ControlLink& link);
  void QueueThumbnailFetchesFromPanel();
  mutable std::mutex thumbMu_;
  std::unordered_map<uint64_t, WindowThumbnail> thumbs_;
  // When each window was last asked about and whether that produced pixels. Separate from thumbs_
  // for the reason given in thumbnail_fetch_policy.hpp: thumbs_ holds successes only, so it cannot
  // throttle the window that never succeeds -- the one the host is skipping.
  struct ThumbAttempt {
    uint64_t lastAttemptUs = 0;
    bool failed = false;
  };
  std::unordered_map<uint64_t, ThumbAttempt> thumbAttempts_;
  std::deque<uint64_t> thumbFetchQueue_;
  std::atomic<bool> hostSupportsThumbnails_{false};
  std::atomic<bool> hostSupportsNack_{false};  // host advertised kUdpFeatureVideoNack (video NACK.)
  // Set while a UAC prompt or the lock screen is in front of the desktop. Nothing can capture
  // that, so the picture stops; saying so beats a frozen rectangle nobody can explain.
  std::atomic<bool> hostSecureDesktopActive_{false};
  std::atomic<uint64_t> sessionBytesReceived_{0};

  // Clipboard text sync (K1). clipMu_ guards the core and the two one-slot mailboxes: the app
  // thread produces outgoing text and drains incoming, the control thread does the opposite.
  // knownGeneration / lastPoll are control-thread only.
  mutable std::mutex clipMu_;
  ClipboardSyncCore clipCore_;
  bool clipHasPending_ = false;
  std::u16string clipPendingText_;
  uint64_t clipPendingHash_ = 0;
  uint32_t clipNextSeq_ = 0;
  bool clipHasIncoming_ = false;
  std::u16string clipIncomingText_;
  // The session-boundary rules (baseline poll, one-shot initial push, poll interval), shared with
  // the Windows viewer so both clients behave the same. Control thread only.
  ClipboardClientPolicy clipPolicy_;
  std::atomic<bool> clipboardEnabled_{true};
  std::atomic<bool> hostSupportsClipboard_{false};
  // r3 M1: an IDR the decoder asked for (its selection's IDR beat the answer, or a delta had to
  // be dropped) that the keyframe limiter refused for now. Kept until the limiter takes it, and
  // tried on every control-loop pass, so it does not wait for the next frame -- after a dropped
  // lone IDR there may be none.
  std::atomic<bool> decoderRekeyPending_{false};
  // r4 M1-A: asking for the IDR a selection is owed (ClientEncodedFrameSink::KeyframeOwedFor).
  // Control-loop thread only.
  uint64_t owedSelection_ = 0;
  uint32_t owedAttempts_ = 0;
  uint64_t owedNextUs_ = 0;
  std::atomic<bool> clipInitialPushWanted_{false};
};

}  // namespace remote60::native_poc
