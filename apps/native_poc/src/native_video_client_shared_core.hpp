#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "poc_protocol.hpp"

namespace remote60::native_poc {

class WindowPanelStateModel;

struct QueuedControlInputMessage {
  MessageType type = MessageType::ControlInputEvent;
  ControlInputEventMessage inputEvent{};
  ControlInputTextMessage inputText{};
};

class ClientInputQueue {
 public:
  uint32_t NextSequence();
  void Enqueue(const QueuedControlInputMessage& msg);
  bool TryDequeue(QueuedControlInputMessage* out);
  uint64_t dropped_count() const;
  void Reset();

 private:
  mutable std::mutex mu_;
  std::deque<QueuedControlInputMessage> queue_;
  std::atomic<uint32_t> nextSeq_{0};
  std::atomic<uint64_t> dropped_{0};
};

struct KeyframeRequestAttempt {
  bool queued = false;
  const char* throttleCause = "none";
  uint64_t throttledCount = 0;
};

class KeyframeRequestState {
 public:
  KeyframeRequestState(uint64_t minIntervalUs, uint64_t tokenRefillUs, uint32_t tokenCapacity);

  void Configure(uint64_t minIntervalUs, uint64_t tokenRefillUs, uint32_t tokenCapacity);
  void Reset();
  KeyframeRequestAttempt Request(uint16_t reason, uint64_t nowUs);
  bool ConsumePending(uint16_t* outReason);
  uint32_t NextSequence();
  uint64_t min_interval_us() const;
  uint64_t token_refill_us() const;
  uint32_t token_capacity() const;

 private:
  std::atomic<bool> pending_{false};
  std::atomic<uint16_t> pendingReason_{0};
  std::atomic<uint32_t> nextRequestSeq_{0};
  std::atomic<uint64_t> lastRequestUs_{0};
  std::atomic<uint64_t> minIntervalUs_;
  std::atomic<uint64_t> tokenRefillUs_;
  std::atomic<uint32_t> tokenCapacity_;
  std::atomic<uint64_t> throttledCount_{0};
  mutable std::mutex limiterMu_;
  double tokens_ = 1.0;
  uint64_t lastRefillUs_ = 0;
};

struct PendingCaptureModeRequest {
  uint32_t seq = 0;
  uint16_t mode = 0;
  uint32_t xPermille = 5000;
  uint32_t yPermille = 5000;
};

class CaptureModeRequestState {
 public:
  void Reset();
  void Request(uint16_t mode, uint32_t xPermille, uint32_t yPermille);
  bool ConsumePending(PendingCaptureModeRequest* out);

 private:
  std::atomic<bool> pending_{false};
  std::atomic<uint32_t> nextSeq_{0};
  std::atomic<uint16_t> mode_{0};
  std::atomic<uint32_t> xPermille_{5000};
  std::atomic<uint32_t> yPermille_{5000};
};

struct PendingStreamStateRequest {
  uint32_t seq = 0;
  bool active = false;
};

class StreamStateControl {
 public:
  void Reset();
  void Request(bool active);
  bool ConsumePending(PendingStreamStateRequest* out);

 private:
  std::atomic<bool> pending_{false};
  std::atomic<uint32_t> nextSeq_{0};
  std::atomic<bool> active_{false};
};

struct PendingDesktopBackendRequest {
  uint32_t seq = 0;
  uint16_t backend = 1;  // 1:dxgi, 2:wgc
};

class DesktopBackendControl {
 public:
  void Reset();
  void Request(uint16_t backend);
  bool ConsumePending(PendingDesktopBackendRequest* out);

private:
  std::atomic<bool> pending_{false};
  std::atomic<uint32_t> nextSeq_{0};
  std::atomic<uint16_t> backend_{2};
};

struct PendingRuntimeTuneRequest {
  ControlRuntimeEncoderConfigMessage message{};
};

class RuntimeTuneState {
 public:
  RuntimeTuneState(uint32_t bitrateMin, uint32_t bitrateMax, uint32_t bitrateStep,
                   uint32_t keyintMin, uint32_t keyintMax);

  void Reset(uint32_t bitrate, uint32_t keyint, uint32_t fps = 0);
  void SetEnabled(bool enabled);
  bool enabled() const;
  void MarkDirty();
  void SetTargets(uint32_t bitrate, uint32_t keyint, uint32_t fps);
  void ApplyDelta(int bitrateStepCount, int keyintStepCount, uint32_t observedRecvMbpsX1000);
  bool ConsumePending(uint64_t nowUs, uint32_t observedRecvMbpsX1000, PendingRuntimeTuneRequest* out);

 private:
  void EnsureDefaults(uint32_t observedRecvMbpsX1000);

  const uint32_t bitrateMin_;
  const uint32_t bitrateMax_;
  const uint32_t bitrateStep_;
  const uint32_t keyintMin_;
  const uint32_t keyintMax_;
  std::atomic<bool> enabled_{false};
  std::atomic<bool> dirty_{false};
  std::atomic<uint32_t> nextSeq_{0};
  std::atomic<uint32_t> targetBitrate_{0};
  std::atomic<uint32_t> targetKeyint_{0};
  std::atomic<uint32_t> targetFps_{0};
  std::atomic<uint64_t> lastSentUs_{0};
};

struct ClientControlMetricsSnapshot {
  ControlClientMetricsMessage message{};
  uint64_t updatedQpcUs = 0;
};

enum class ControlOutboundActionKind : uint8_t {
  None = 0,
  Ping,
  WindowListRequest,
  WindowSelect,
  StreamState,
  CaptureMode,
  Metrics,
  KeyframeRequest,
  RuntimeTune,
  DesktopBackend,
  InputEvent,
  InputText,
  MonitorListRequest,
  MonitorSelect,
};

struct ControlOutboundAction {
  ControlOutboundActionKind kind = ControlOutboundActionKind::None;
  std::optional<MessageType> expectedResponseType;
  uint16_t expectedResponseSize = 0;
  ControlPingMessage ping{};
  ControlWindowListRequestMessage windowListRequest{};
  ControlWindowSelectMessage windowSelect{};
  ControlMonitorListRequestMessage monitorListRequest{};
  ControlMonitorSelectMessage monitorSelect{};
  ControlStreamStateMessage streamState{};
  ControlCaptureModeRequestMessage captureMode{};
  ControlClientMetricsMessage metrics{};
  ControlRequestKeyFrameMessage keyframe{};
  ControlRuntimeEncoderConfigMessage runtimeTune{};
  ControlDesktopBackendRequestMessage desktopBackend{};
  ControlInputEventMessage inputEvent{};
  ControlInputTextMessage inputText{};
};

class ClientControlScheduler {
 public:
  void Reset(uint32_t controlIntervalMs, uint64_t nowUs);
  void OnPingCompleted(uint64_t doneUs);
  bool NextAction(uint64_t nowUs,
                 const ClientControlMetricsSnapshot& metrics,
                 WindowPanelStateModel* windowPanel,
                 StreamStateControl* streamState,
                 CaptureModeRequestState* captureMode,
                 KeyframeRequestState* keyframeRequests,
                 RuntimeTuneState* runtimeTune,
                 ClientInputQueue* inputQueue,
                  ControlOutboundAction* out,
                 DesktopBackendControl* desktopBackend = nullptr);
  uint64_t RecordInputAck(uint32_t inputLogEvery);

 private:
  uint32_t nextPingSeq_ = 0;
  uint32_t nextMetricsSeq_ = 0;
  uint32_t nextWindowListSeq_ = 0;
  uint32_t nextWindowSelectSeq_ = 0;
  uint32_t nextMonitorSeq_ = 0;
  uint64_t nextPingUs_ = 0;
  uint64_t lastMetricsSentUs_ = 0;
  uint64_t inputAckCount_ = 0;
  uint32_t controlIntervalMs_ = 1000;
};

enum class UdpH264AssemblyDisposition : uint8_t {
  Ignored = 0,
  Partial,
  Completed,
  Malformed,
  Dropped,
};

struct UdpH264AssembledFrame {
  EncodedFrameHeader header{};
  std::vector<uint8_t> payload;
};

struct UdpH264AssemblyStepResult {
  UdpH264AssemblyDisposition disposition = UdpH264AssemblyDisposition::Ignored;
  bool startedNewAssembly = false;
  bool droppedPreviousIncomplete = false;
  bool reorderDetected = false;
  bool oversizePayload = false;
  bool fecRecovered = false;
  uint32_t fecRecoveredChunks = 0;
  uint32_t packetSeq = 0;
  uint32_t expectedSeq = 0;
  uint32_t packetChunkOffset = 0;
  uint32_t expectedNextOffset = 0;
  uint32_t rejectedPayloadSize = 0;
  UdpH264AssembledFrame frame{};
};

class UdpH264FrameAssembler {
 public:
  void Reset();
  UdpH264AssemblyStepResult PushDatagram(const uint8_t* data, size_t len);

 private:
  struct Assembly {
    uint32_t seq = 0;
    uint32_t payloadSize = 0;
    uint16_t chunkCount = 0;
    uint32_t chunkStride = 0;
    uint32_t receivedCount = 0;
    EncodedFrameHeader header{};
    std::vector<uint8_t> payload;
    std::vector<uint8_t> received;
    std::vector<std::vector<uint8_t>> parity;
    std::vector<uint8_t> parityReceived;
    // Which layout this frame's parity uses, learned from the first parity packet to arrive.
    uint8_t parityInterleaved = 0;
  };

  std::deque<Assembly> assemblies_;
  bool deliveredAny_ = false;
  uint32_t lastDeliveredSeq_ = 0;
};

struct WindowTargetUiEntry {
  uint64_t id = 0;
  uint32_t pid = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  bool minimized = false;
  std::string title;
};

// One attached screen. The host orders them primary-first then left to right, so the index is
// what the UI numbers: "monitor 1" is the same screen every time.
struct MonitorEntry {
  uint32_t id = 0;
  int32_t x = 0;
  int32_t y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  bool primary = false;
};

struct WindowPanelSnapshot {
  std::vector<WindowTargetUiEntry> items;
  uint64_t selectedId = 0;
  std::string selectedTitle = "desktop";
  uint32_t selectedWidth = 0;
  uint32_t selectedHeight = 0;
  bool selectionLocked = false;
  std::string status = "waiting_control";
  uint32_t lastSelectSeq = 0;
  bool lastSelectOk = false;
  uint64_t lastSelectWindowId = 0;
  uint64_t lastSelectStreamGeneration = 0;
  uint64_t lastSelectHostSendQpcUs = 0;
  int scrollIndex = 0;
  // The attached screens, and which one desktop mode is showing. Empty until the host answers,
  // and it only will if it advertised support -- an older one drains the request in silence.
  bool hostSupportsMonitors = false;
  uint32_t selectedMonitorId = 0;
  std::vector<MonitorEntry> monitors;
};

struct WindowListApplyResult {
  std::string logLine;
};

struct WindowSelectApplyResult {
  std::string logLine;
  bool ok = false;
};

class WindowPanelStateModel {
 public:
  void Reset();
  void RequestList(const char* statusText = nullptr);
  bool TakeListRequest();
  bool RequestSelect(uint64_t windowId, const char* statusText = nullptr);
  bool TakeSelectRequest(uint64_t* outWindowId);
  void RequestMonitorList();
  bool TakeMonitorListRequest();
  /** Returns true when support was newly discovered, so the caller can fetch the list once. */
  bool SetHostSupportsMonitors(bool supported);
  bool RequestMonitorSelect(uint32_t monitorId);
  bool TakeMonitorSelectRequest(uint32_t* outMonitorId);
  void ApplyMonitorList(const ControlMonitorListMessage& msg);
  void SetStatus(const std::string& status);
  WindowListApplyResult ApplyWindowList(const ControlWindowListMessage& msg, int visibleCount);
  WindowSelectApplyResult ApplyWindowSelected(const ControlWindowSelectedMessage& msg);
  void Scroll(int deltaSteps, int visibleCount);
  bool TryResolveWindowIdForVisibleRow(int row, int visibleCount, uint64_t* outWindowId) const;
  WindowPanelSnapshot Snapshot() const;
  bool IsDesktopSelected() const;

 private:
  mutable std::mutex mu_;
  WindowPanelSnapshot state_;
  bool listRequestPending_ = false;
  bool selectRequestPending_ = false;
  uint64_t pendingSelectId_ = 0;
  bool monitorListRequestPending_ = false;
  bool monitorSelectRequestPending_ = false;
  uint32_t pendingMonitorId_ = 0;
};

}  // namespace remote60::native_poc
