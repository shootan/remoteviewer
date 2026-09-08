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
  ControlPhysicalKeyMessage physicalKey{};
  // P0 telemetry (#351): when the UI generated this event, kept CLIENT-LOCAL (never on the wire, so
  // no Android/old-host size contract change). clientSendQpcUs on the wire is overwritten at send.
  uint64_t generatedUs = 0;
};

class ClientInputQueue {
 public:
  uint32_t NextSequence();
  void Enqueue(const QueuedControlInputMessage& msg);
  bool TryDequeue(QueuedControlInputMessage* out);
  uint64_t dropped_count() const;
  uint64_t coalesced_move_count() const;  // P0 (#351): moves replaced by a newer one (latest-wins)
  void Reset();

 private:
  mutable std::mutex mu_;
  std::deque<QueuedControlInputMessage> queue_;
  std::atomic<uint32_t> nextSeq_{0};
  std::atomic<uint64_t> dropped_{0};
  std::atomic<uint64_t> coalescedMoves_{0};  // P0 (#351)
};

// The two input messages, built one way for every client (viewer ledger F-09: the Android session
// and the Windows viewer each assembled them by hand, field for field).
//
// One ControlInputEvent: header, the queue's next sequence, the fields, the send stamp. `buttons`
// is masked to the three mouse bits the host reads.
QueuedControlInputMessage make_control_input_event(ClientInputQueue& queue, uint16_t kind,
                                                   uint16_t buttons, int32_t x, int32_t y,
                                                   int32_t wheelDelta, uint32_t keyCode,
                                                   uint64_t nowUs);
// Splits UTF-16 text into ControlInputText messages of at most kControlInputTextMaxUtf16 units,
// sequenced and queued in order. Returns how many were queued (0 for no text).
size_t enqueue_control_input_text(ClientInputQueue& queue, const uint16_t* text, size_t count,
                                  uint64_t nowUs);

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
  PhysicalKey,
  MonitorListRequest,
  MonitorSelect,
  UnlockChallengeRequest,
  UnlockSealedRequest,
  UnlockStatusRequest,
  ImeStateRequest,
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
  ControlPhysicalKeyMessage physicalKey{};
  ControlUnlockChallengeRequestMessage unlockChallengeReq{};
  ControlUnlockSealedRequestMessage unlockSealed{};
  ControlUnlockStatusRequestMessage unlockStatusReq{};
  ControlImeStateRequestMessage imeStateReq{};
  uint64_t inputGeneratedUs = 0;  // P0 (#351): local diagnostic — when the UI generated this input
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
  // In-order hold on (ConfigureInOrderHold): this datagram completed its AU, which now waits in
  // sequence order for PopDelivery instead of being handed out here. (Windows NACK wiring.)
  Queued,
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
  // Same, stamping the AU's first arrival with `nowUs` (the in-order hold clock below).
  UdpH264AssemblyStepResult PushDatagram(const uint8_t* data, size_t len, uint64_t nowUs);

  // --- In-order delivery hold (video NACK) ---
  // Without it a completed AU is delivered the moment it completes and every OLDER incomplete AU
  // is discarded with it (the seq gap then reads as loss). At 60 fps the next P completes ~16 ms
  // after a lost chunk, before a NACK round (25 ms grace) can bring the chunk back -- so NACK only
  // ever helped a sparse (static-screen) stream. With maxHoldUs > 0, PushDatagram returns Queued
  // for a completed AU and PopDelivery hands AUs out in sequence order: a completed AU waits behind
  // an older incomplete one for up to maxHoldUs (its retransmit may still land); past that the
  // older AU is dropped and delivery resumes carrying droppedPreviousIncomplete. maxHoldUs = 0
  // keeps the legacy immediate delivery (the Android path is unchanged). `maxConcurrent` bounds
  // the assemblies held (legacy 3; a hold of ~120 ms at 60 fps needs ~8).
  // `maxHeldBytes` bounds the payload held across assemblies (0 = unbounded); past it the
  // oldest assembly is given up, exactly like the count cap. (Codex condition 1.)
  void ConfigureInOrderHold(uint64_t maxHoldUs, size_t maxConcurrent,
                            size_t maxHeldBytes = 8u * 1024u * 1024u);
  bool InOrderHoldEnabled() const { return holdMaxUs_ > 0; }
  size_t HeldBytes() const;
  // Deliver the next AU in sequence order if one is ready, or if the incomplete AU ahead of it has
  // outlived its hold. `repairNonKey` false: an IDR is awaited, so an incomplete NON-key head is
  // not worth waiting for and is released at once (an incomplete keyframe still is -- it is the
  // only recovery point). Call after every datagram and on every receive timeout, until false.
  bool PopDelivery(uint64_t nowUs, bool repairNonKey, UdpH264AssemblyStepResult* out);
  size_t PendingCount() const { return assemblies_.size(); }
  // True when a held assembly is complete -- PopDelivery has something to release once the
  // incomplete head ahead of it is given up. False on a quiet link with only the stuck head.
  bool AnyComplete() const;
  // Gives up exactly the incomplete assembly (generation, seq) the caller judged -- never an
  // "oldest" by arrival or sequence order -- because its repair is over (A04 rule). False, and
  // nothing removed, when it is not held or completed meanwhile (the give-up is cancelled). The
  // next delivery reports the seq gap exactly as an expired hold does.
  //
  // The identity is remembered (a tombstone), because the host may still be sending that AU's
  // chunks: without it the next late chunk re-creates the very assembly that was just abandoned
  // and blocks the head again, so the caller gives the same AU up over and over (measured: the
  // same seq abandoned 3 times, and the good AU behind it never delivered). `nowUs` only stamps
  // the record for diagnostics -- retirement is by boundary, not by clock (see abandoned_).
  bool GiveUpIncomplete(uint64_t generation, uint32_t seq, uint64_t nowUs = 0);
  // True while (generation, seq) is tombstoned: its chunks are ignored as stale traffic.
  bool IsAbandoned(uint64_t generation, uint32_t seq) const;
  size_t AbandonedCount() const { return abandoned_.size(); }

  struct IncompleteAuInfo {
    uint32_t seq = 0;
    uint64_t generation = 0;
    uint16_t chunkCount = 0;
    uint16_t missingTotal = 0;  // total missing data chunks (may exceed what fit in missingOut)
    // Highest received data-chunk index + 1. A missing index < highWater is a confirmed hole (a
    // later chunk already arrived, so its tail is here); a missing index >= highWater is the
    // still-in-flight tail and must NOT be NACKed early (premature NACK ignites a retransmit flood
    // on large frames whose send time exceeds the reorder grace). (Codex: frame-end aware NACK.)
    uint16_t highWater = 0;
    bool keyFrame = false;  // repairing the keyframe is allowed even while waiting for a keyframe
    // When the AU's first datagram arrived (PushDatagram's nowUs; 0 on the legacy overload), so the
    // NACK graces run from the AU's own age rather than from when the scheduler first saw it.
    uint64_t firstPacketUs = 0;
    // Last time this AU made progress: a NEW data chunk arrived or FEC recovered one. A duplicate
    // chunk, a parity packet that repairs nothing, control traffic or another AU do not count.
    uint64_t lastProgressUs = 0;
  };
  // Video NACK: describe the oldest still-incomplete AU (the one blocking delivery) and list up to
  // `maxMissing` of its missing data-chunk indices in `missingOut` (indices are ascending, so the
  // caller can split holes < highWater from the in-flight tail). Returns false when every held
  // assembly has all its data chunks. Pure query. (video NACK.)
  bool OldestIncomplete(uint16_t* missingOut, uint16_t maxMissing, IncompleteAuInfo* info) const;

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
    // In-order hold bookkeeping: when the first datagram of this AU arrived (the hold clock), whether
    // every data chunk is in (awaiting PopDelivery), and how many chunks parity repaired.
    uint64_t firstPacketUs = 0;
    uint64_t lastProgressUs = 0;  // see IncompleteAuInfo::lastProgressUs
    bool complete = false;
    uint32_t fecRecoveredChunks = 0;
  };

  UdpH264AssemblyStepResult DeliverAssembly(Assembly& assembly);

  // A04: identities given up on. Deliberately NOT expressed by moving lastDeliveredSeq_ forward:
  // that would claim the AU was delivered, change what the next delivery reports as a sequence
  // gap (and with it the IDR recovery contract), and swallow AUs that were never judged. A
  // tombstone blocks exactly the judged (generation, seq) and nothing else -- another generation
  // restarts the seq space and never matches one.
  //
  // Retirement is by BOUNDARY, not by a clock: a host that keeps resending the abandoned AU for
  // longer than any timeout would otherwise get it re-assembled and abandoned a second time. An
  // entry is retired when delivery has moved past it in its own generation (`lastDeliveredSeq_`
  // is at or beyond it): from then on the ordinary stale guard covers those chunks, so the
  // tombstone is redundant. Reset() clears everything with the rest of the assembler state.
  //
  // UNFINISHED, RELEASE-BLOCKING: what happens when many identities are abandoned with no
  // delivery in between is not decided yet. Dropping the oldest record was rejected -- forgetting
  // an identity means accepting it again, which is the very defect this exists to prevent -- and
  // a time-to-live was rejected for the same reason. Today the list simply grows in that case
  // (each entry is 24 bytes and the give-up rule makes many outstanding give-ups without a single
  // delivery a pathological case), and the saturation design that closes it properly is being
  // confirmed. Retirement on a generation change is likewise not settled: the assembler must not
  // follow a late packet's generation, and today nothing calls Reset() from the receive path.
  struct AbandonedAu {
    uint64_t generation = 0;
    uint32_t seq = 0;
    uint64_t atUs = 0;  // diagnostics only
  };
  std::deque<AbandonedAu> abandoned_;

  std::deque<Assembly> assemblies_;
  bool deliveredAny_ = false;
  uint32_t lastDeliveredSeq_ = 0;
  uint64_t holdMaxUs_ = 0;      // 0 = legacy immediate delivery
  size_t maxConcurrent_ = 3;    // kMaxConcurrentVideoAssemblies unless ConfigureInOrderHold raised it
  size_t maxHeldBytes_ = 0;     // hold mode only; 0 = unbounded
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
