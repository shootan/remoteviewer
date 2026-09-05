#include "native_video_client_shared_core.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>

namespace remote60::native_poc {

namespace {

constexpr size_t kMaxInputQueueSize = 256;
constexpr uint32_t kMaxUdpAssembledPayloadBytes = 16u * 1024u * 1024u;
constexpr uint16_t kMaxUdpVideoChunks = 16384;
constexpr size_t kMaxConcurrentVideoAssemblies = 3;

bool sequence_is_newer(uint32_t value, uint32_t reference) {
  return static_cast<int32_t>(value - reference) > 0;
}

void set_selected_target_dimensions(WindowPanelSnapshot* snapshot) {
  if (!snapshot) return;
  snapshot->selectedWidth = 0;
  snapshot->selectedHeight = 0;
  if (snapshot->selectedId == 0) return;
  const auto it = std::find_if(snapshot->items.begin(), snapshot->items.end(),
                               [&](const WindowTargetUiEntry& entry) {
                                 return entry.id == snapshot->selectedId;
                               });
  if (it == snapshot->items.end()) return;
  snapshot->selectedWidth = it->width;
  snapshot->selectedHeight = it->height;
}

std::string fixed_cstr_to_string(const char* buf, size_t cap) {
  if (!buf || cap == 0) return std::string{};
  size_t n = 0;
  while (n < cap && buf[n] != '\0') ++n;
  return std::string(buf, buf + n);
}

uint16_t expected_message_size(MessageType type) {
  switch (type) {
    case MessageType::ControlPong:
      return static_cast<uint16_t>(sizeof(ControlPongMessage));
    case MessageType::ControlWindowList:
      return static_cast<uint16_t>(sizeof(ControlWindowListMessage));
    case MessageType::ControlMonitorList:
      return static_cast<uint16_t>(sizeof(ControlMonitorListMessage));
    case MessageType::ControlWindowSelected:
      return static_cast<uint16_t>(sizeof(ControlWindowSelectedMessage));
    case MessageType::ControlInputAck:
      return static_cast<uint16_t>(sizeof(ControlInputAckMessage));
    default:
      return 0;
  }
}

}  // namespace

uint32_t ClientInputQueue::NextSequence() {
  return nextSeq_.fetch_add(1, std::memory_order_relaxed) + 1;
}

void ClientInputQueue::Enqueue(const QueuedControlInputMessage& msg) {
  std::lock_guard<std::mutex> lk(mu_);
  // Only a pointer move (ControlInputEvent kind 1) is disposable; key / button / physical-key edges
  // (down/up) must never be dropped, or a modifier can strand on the host. (Codex 4th review.)
  const auto is_move = [](const QueuedControlInputMessage& m) {
    return m.type == MessageType::ControlInputEvent && m.inputEvent.kind == 1;
  };
  if (is_move(msg) && !queue_.empty() && is_move(queue_.back())) {
    queue_.back() = msg;
    coalescedMoves_.fetch_add(1, std::memory_order_relaxed);  // P0 (#351): a move replaced in place
    return;
  }
  if (queue_.size() >= kMaxInputQueueSize) {
    // Overflow: sacrifice the oldest move rather than pop_front (which could be a queued key-up).
    bool droppedMove = false;
    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
      if (is_move(*it)) {
        queue_.erase(it);
        dropped_.fetch_add(1, std::memory_order_relaxed);
        droppedMove = true;
        break;
      }
    }
    // No move to give up: drop the incoming if it is itself a move (latest-wins); otherwise let the
    // queue grow temporarily rather than lose a key/button/physical edge.
    if (!droppedMove && is_move(msg)) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return;
    }
  }
  queue_.push_back(msg);
}

bool ClientInputQueue::TryDequeue(QueuedControlInputMessage* out) {
  if (!out) return false;
  std::lock_guard<std::mutex> lk(mu_);
  if (queue_.empty()) return false;
  *out = queue_.front();
  queue_.pop_front();
  return true;
}

uint64_t ClientInputQueue::dropped_count() const {
  return dropped_.load(std::memory_order_relaxed);
}

uint64_t ClientInputQueue::coalesced_move_count() const {
  return coalescedMoves_.load(std::memory_order_relaxed);
}

void ClientInputQueue::Reset() {
  std::lock_guard<std::mutex> lk(mu_);
  queue_.clear();
  dropped_.store(0, std::memory_order_relaxed);
  coalescedMoves_.store(0, std::memory_order_relaxed);
  nextSeq_.store(0, std::memory_order_relaxed);
}

QueuedControlInputMessage make_control_input_event(ClientInputQueue& queue, uint16_t kind,
                                                   uint16_t buttons, int32_t x, int32_t y,
                                                   int32_t wheelDelta, uint32_t keyCode,
                                                   uint64_t nowUs) {
  QueuedControlInputMessage msg{};
  msg.type = MessageType::ControlInputEvent;
  msg.inputEvent.header.magic = kMagic;
  msg.inputEvent.header.type = static_cast<uint16_t>(MessageType::ControlInputEvent);
  msg.inputEvent.header.size = static_cast<uint16_t>(sizeof(msg.inputEvent));
  msg.inputEvent.seq = queue.NextSequence();
  msg.inputEvent.kind = kind;
  msg.inputEvent.buttons = static_cast<uint16_t>(buttons & 0x7u);
  msg.inputEvent.x = x;
  msg.inputEvent.y = y;
  msg.inputEvent.wheelDelta = wheelDelta;
  msg.inputEvent.keyCode = keyCode;
  msg.inputEvent.clientSendQpcUs = nowUs;
  msg.generatedUs = nowUs;  // P0 (#351): local-only generation stamp; wire clientSendQpcUs is reset at send
  return msg;
}

size_t enqueue_control_input_text(ClientInputQueue& queue, const uint16_t* text, size_t count,
                                  uint64_t nowUs) {
  if (!text || count == 0) return 0;
  size_t queued = 0;
  size_t offset = 0;
  while (offset < count) {
    const size_t remaining = count - offset;
    const size_t chunk = std::min<size_t>(remaining, kControlInputTextMaxUtf16);
    QueuedControlInputMessage msg{};
    msg.type = MessageType::ControlInputText;
    msg.inputText.header.magic = kMagic;
    msg.inputText.header.type = static_cast<uint16_t>(MessageType::ControlInputText);
    msg.inputText.header.size = static_cast<uint16_t>(sizeof(msg.inputText));
    msg.inputText.seq = queue.NextSequence();
    msg.inputText.utf16Count = static_cast<uint16_t>(chunk);
    std::memcpy(msg.inputText.utf16, text + offset, chunk * sizeof(uint16_t));
    msg.inputText.clientSendQpcUs = nowUs;
    queue.Enqueue(msg);
    offset += chunk;
    ++queued;
  }
  return queued;
}

KeyframeRequestState::KeyframeRequestState(uint64_t minIntervalUs, uint64_t tokenRefillUs, uint32_t tokenCapacity)
    : minIntervalUs_(minIntervalUs),
      tokenRefillUs_(tokenRefillUs),
      tokenCapacity_(std::max<uint32_t>(1, tokenCapacity)),
      tokens_(static_cast<double>(std::max<uint32_t>(1, tokenCapacity))) {}

void KeyframeRequestState::Configure(uint64_t minIntervalUs, uint64_t tokenRefillUs, uint32_t tokenCapacity) {
  minIntervalUs_.store(minIntervalUs, std::memory_order_relaxed);
  tokenRefillUs_.store(tokenRefillUs, std::memory_order_relaxed);
  tokenCapacity_.store(std::max<uint32_t>(1, tokenCapacity), std::memory_order_relaxed);
}

void KeyframeRequestState::Reset() {
  pending_.store(false, std::memory_order_relaxed);
  pendingReason_.store(0, std::memory_order_relaxed);
  lastRequestUs_.store(0, std::memory_order_relaxed);
  throttledCount_.store(0, std::memory_order_relaxed);
  nextRequestSeq_.store(0, std::memory_order_relaxed);
  std::lock_guard<std::mutex> lk(limiterMu_);
  tokens_ = static_cast<double>(tokenCapacity_.load(std::memory_order_relaxed));
  lastRefillUs_ = 0;
}

KeyframeRequestAttempt KeyframeRequestState::Request(uint16_t reason, uint64_t nowUs) {
  KeyframeRequestAttempt result{};
  if (reason == 0) reason = 1;
  const uint64_t minIntervalUs = minIntervalUs_.load(std::memory_order_relaxed);
  const uint64_t refillUs = tokenRefillUs_.load(std::memory_order_relaxed);
  const uint32_t capacity = std::max<uint32_t>(1, tokenCapacity_.load(std::memory_order_relaxed));

  {
    std::lock_guard<std::mutex> lk(limiterMu_);
    if (lastRefillUs_ == 0) lastRefillUs_ = nowUs;
    if (nowUs > lastRefillUs_ && refillUs > 0) {
      const double refill = static_cast<double>(nowUs - lastRefillUs_) / static_cast<double>(refillUs);
      if (refill > 0.0) {
        tokens_ = std::min<double>(static_cast<double>(capacity), tokens_ + refill);
        lastRefillUs_ = nowUs;
      }
    }
    const uint64_t lastUs = lastRequestUs_.load(std::memory_order_relaxed);
    if (lastUs > 0 && nowUs < lastUs + minIntervalUs) {
      result.throttleCause = "min_interval";
      result.throttledCount = throttledCount_.fetch_add(1, std::memory_order_relaxed) + 1;
      return result;
    }
    if (tokens_ < 1.0) {
      result.throttleCause = "token_bucket";
      result.throttledCount = throttledCount_.fetch_add(1, std::memory_order_relaxed) + 1;
      return result;
    }
    tokens_ -= 1.0;
    lastRequestUs_.store(nowUs, std::memory_order_relaxed);
  }

  pendingReason_.store(reason, std::memory_order_relaxed);
  pending_.store(true, std::memory_order_release);
  result.queued = true;
  return result;
}

bool KeyframeRequestState::ConsumePending(uint16_t* outReason) {
  if (!pending_.exchange(false, std::memory_order_acq_rel)) return false;
  if (outReason) *outReason = pendingReason_.load(std::memory_order_acquire);
  return true;
}

uint32_t KeyframeRequestState::NextSequence() {
  return nextRequestSeq_.fetch_add(1, std::memory_order_relaxed) + 1;
}

uint64_t KeyframeRequestState::min_interval_us() const {
  return minIntervalUs_.load(std::memory_order_relaxed);
}

uint64_t KeyframeRequestState::token_refill_us() const {
  return tokenRefillUs_.load(std::memory_order_relaxed);
}

uint32_t KeyframeRequestState::token_capacity() const {
  return tokenCapacity_.load(std::memory_order_relaxed);
}

void CaptureModeRequestState::Reset() {
  pending_.store(false, std::memory_order_relaxed);
  nextSeq_.store(0, std::memory_order_relaxed);
  mode_.store(0, std::memory_order_relaxed);
  xPermille_.store(5000, std::memory_order_relaxed);
  yPermille_.store(5000, std::memory_order_relaxed);
}

void CaptureModeRequestState::Request(uint16_t mode, uint32_t xPermille, uint32_t yPermille) {
  if (mode != 1 && mode != 2) return;
  mode_.store(mode, std::memory_order_release);
  xPermille_.store(std::min<uint32_t>(10000u, xPermille), std::memory_order_release);
  yPermille_.store(std::min<uint32_t>(10000u, yPermille), std::memory_order_release);
  pending_.store(true, std::memory_order_release);
}

bool CaptureModeRequestState::ConsumePending(PendingCaptureModeRequest* out) {
  if (!out) return false;
  if (!pending_.exchange(false, std::memory_order_acq_rel)) return false;
  out->seq = nextSeq_.fetch_add(1, std::memory_order_relaxed) + 1;
  out->mode = mode_.load(std::memory_order_acquire);
  out->xPermille = std::min<uint32_t>(10000u, xPermille_.load(std::memory_order_acquire));
  out->yPermille = std::min<uint32_t>(10000u, yPermille_.load(std::memory_order_acquire));
  return (out->mode == 1 || out->mode == 2);
}

void StreamStateControl::Reset() {
  pending_.store(false, std::memory_order_relaxed);
  nextSeq_.store(0, std::memory_order_relaxed);
  active_.store(false, std::memory_order_relaxed);
}

void StreamStateControl::Request(bool active) {
  active_.store(active, std::memory_order_release);
  pending_.store(true, std::memory_order_release);
}

bool StreamStateControl::ConsumePending(PendingStreamStateRequest* out) {
  if (!out) return false;
  if (!pending_.exchange(false, std::memory_order_acq_rel)) return false;
  out->seq = nextSeq_.fetch_add(1, std::memory_order_relaxed) + 1;
  out->active = active_.load(std::memory_order_acquire);
  return true;
}

void DesktopBackendControl::Reset() {
  pending_.store(false, std::memory_order_relaxed);
  nextSeq_.store(0, std::memory_order_relaxed);
  backend_.store(2, std::memory_order_relaxed);
}

void DesktopBackendControl::Request(uint16_t backend) {
  if (backend < 1 || backend > 2) return;
  backend_.store(backend, std::memory_order_release);
  pending_.store(true, std::memory_order_release);
}

bool DesktopBackendControl::ConsumePending(PendingDesktopBackendRequest* out) {
  if (!out) return false;
  if (!pending_.exchange(false, std::memory_order_acq_rel)) return false;
  out->seq = nextSeq_.fetch_add(1, std::memory_order_relaxed) + 1;
  out->backend = backend_.load(std::memory_order_acquire);
  return true;
}

RuntimeTuneState::RuntimeTuneState(uint32_t bitrateMin, uint32_t bitrateMax, uint32_t bitrateStep,
                                   uint32_t keyintMin, uint32_t keyintMax)
    : bitrateMin_(bitrateMin),
      bitrateMax_(std::max<uint32_t>(bitrateMin, bitrateMax)),
      bitrateStep_(std::max<uint32_t>(1, bitrateStep)),
      keyintMin_(std::max<uint32_t>(1, keyintMin)),
      keyintMax_(std::max<uint32_t>(std::max<uint32_t>(1, keyintMin), keyintMax)) {}

void RuntimeTuneState::Reset(uint32_t bitrate, uint32_t keyint, uint32_t fps) {
  enabled_.store(false, std::memory_order_relaxed);
  dirty_.store(false, std::memory_order_relaxed);
  nextSeq_.store(0, std::memory_order_relaxed);
  targetBitrate_.store(bitrate, std::memory_order_relaxed);
  targetKeyint_.store(keyint, std::memory_order_relaxed);
  targetFps_.store(fps, std::memory_order_relaxed);
  lastSentUs_.store(0, std::memory_order_relaxed);
}

void RuntimeTuneState::SetEnabled(bool enabled) {
  enabled_.store(enabled, std::memory_order_relaxed);
}

bool RuntimeTuneState::enabled() const {
  return enabled_.load(std::memory_order_relaxed);
}

void RuntimeTuneState::MarkDirty() {
  dirty_.store(true, std::memory_order_release);
}

void RuntimeTuneState::SetTargets(uint32_t bitrate, uint32_t keyint, uint32_t fps) {
  if (bitrate > 0) {
    targetBitrate_.store(std::clamp<uint32_t>(bitrate, bitrateMin_, bitrateMax_), std::memory_order_relaxed);
  }
  if (keyint > 0) {
    targetKeyint_.store(std::clamp<uint32_t>(keyint, keyintMin_, keyintMax_), std::memory_order_relaxed);
  }
  if (fps > 0) {
    targetFps_.store(std::clamp<uint32_t>(fps, 1u, 120u), std::memory_order_relaxed);
  }
  MarkDirty();
}

void RuntimeTuneState::EnsureDefaults(uint32_t observedRecvMbpsX1000) {
  uint32_t bitrate = targetBitrate_.load(std::memory_order_relaxed);
  if (bitrate == 0) {
    uint32_t guessed = 8000000;
    if (observedRecvMbpsX1000 > 0) {
      guessed = static_cast<uint32_t>(std::clamp<uint64_t>(
          static_cast<uint64_t>(observedRecvMbpsX1000) * 1000ULL, bitrateMin_, bitrateMax_));
    }
    targetBitrate_.store(guessed, std::memory_order_relaxed);
  }
  uint32_t keyint = targetKeyint_.load(std::memory_order_relaxed);
  if (keyint == 0) {
    targetKeyint_.store(std::clamp<uint32_t>(60, keyintMin_, keyintMax_), std::memory_order_relaxed);
  }
}

void RuntimeTuneState::ApplyDelta(int bitrateStepCount, int keyintStepCount, uint32_t observedRecvMbpsX1000) {
  if (!enabled()) return;
  EnsureDefaults(observedRecvMbpsX1000);
  if (bitrateStepCount != 0) {
    const uint32_t cur = targetBitrate_.load(std::memory_order_relaxed);
    const int64_t next =
        static_cast<int64_t>(cur) + static_cast<int64_t>(bitrateStepCount) * static_cast<int64_t>(bitrateStep_);
    targetBitrate_.store(
        static_cast<uint32_t>(std::clamp<int64_t>(next, bitrateMin_, bitrateMax_)),
        std::memory_order_relaxed);
  }
  if (keyintStepCount != 0) {
    const uint32_t cur = targetKeyint_.load(std::memory_order_relaxed);
    const int64_t next = static_cast<int64_t>(cur) + static_cast<int64_t>(keyintStepCount);
    targetKeyint_.store(
        static_cast<uint32_t>(std::clamp<int64_t>(next, keyintMin_, keyintMax_)),
        std::memory_order_relaxed);
  }
  MarkDirty();
}

bool RuntimeTuneState::ConsumePending(uint64_t nowUs, uint32_t observedRecvMbpsX1000, PendingRuntimeTuneRequest* out) {
  if (!out) return false;
  if (!enabled()) return false;
  if (!dirty_.exchange(false, std::memory_order_acq_rel)) return false;
  EnsureDefaults(observedRecvMbpsX1000);

  ControlRuntimeEncoderConfigMessage message{};
  message.header.magic = kMagic;
  message.header.type = static_cast<uint16_t>(MessageType::ControlRuntimeEncoderConfig);
  message.header.size = static_cast<uint16_t>(sizeof(message));
  message.seq = nextSeq_.fetch_add(1, std::memory_order_relaxed) + 1;
  message.bitrate = targetBitrate_.load(std::memory_order_relaxed);
  message.keyint = targetKeyint_.load(std::memory_order_relaxed);
  message.fps = targetFps_.load(std::memory_order_relaxed);
  if (message.bitrate > 0) message.flags |= 0x1u;
  if (message.keyint > 0) message.flags |= 0x2u;
  if (message.fps > 0) message.flags |= 0x4u;
  if (message.flags == 0) return false;
  message.clientSendQpcUs = nowUs;
  out->message = message;
  lastSentUs_.store(nowUs, std::memory_order_relaxed);
  return true;
}

void ClientControlScheduler::Reset(uint32_t controlIntervalMs, uint64_t nowUs) {
  nextPingSeq_ = 0;
  nextMetricsSeq_ = 0;
  nextWindowListSeq_ = 0;
  nextWindowSelectSeq_ = 0;
  nextPingUs_ = nowUs;
  lastMetricsSentUs_ = 0;
  inputAckCount_ = 0;
  controlIntervalMs_ = std::clamp<uint32_t>(controlIntervalMs, 20, 10000);
}

void ClientControlScheduler::OnPingCompleted(uint64_t doneUs) {
  nextPingUs_ = doneUs + static_cast<uint64_t>(controlIntervalMs_) * 1000ULL;
}

bool ClientControlScheduler::NextAction(uint64_t nowUs,
                                        const ClientControlMetricsSnapshot& metrics,
                                        WindowPanelStateModel* windowPanel,
                                        StreamStateControl* streamState,
                                        CaptureModeRequestState* captureMode,
                                        KeyframeRequestState* keyframeRequests,
                                        RuntimeTuneState* runtimeTune,
                                        ClientInputQueue* inputQueue,
                                        ControlOutboundAction* out,
                                        DesktopBackendControl* desktopBackend) {
  if (!windowPanel || !streamState || !captureMode || !keyframeRequests || !runtimeTune || !inputQueue || !out) {
    return false;
  }
  *out = ControlOutboundAction{};

  if (nowUs >= nextPingUs_) {
    out->kind = ControlOutboundActionKind::Ping;
    out->expectedResponseType = MessageType::ControlPong;
    out->expectedResponseSize = expected_message_size(MessageType::ControlPong);
    out->ping.header.magic = kMagic;
    out->ping.header.type = static_cast<uint16_t>(MessageType::ControlPing);
    out->ping.header.size = static_cast<uint16_t>(sizeof(out->ping));
    out->ping.seq = ++nextPingSeq_;
    out->ping.clientSendQpcUs = nowUs;
    return true;
  }

  PendingStreamStateRequest pendingStreamState{};
  if (streamState->ConsumePending(&pendingStreamState)) {
    out->kind = ControlOutboundActionKind::StreamState;
    out->streamState.header.magic = kMagic;
    out->streamState.header.type = static_cast<uint16_t>(MessageType::ControlStreamState);
    out->streamState.header.size = static_cast<uint16_t>(sizeof(out->streamState));
    out->streamState.seq = pendingStreamState.seq;
    out->streamState.flags = pendingStreamState.active ? 0x1u : 0u;
    out->streamState.clientSendQpcUs = nowUs;
    return true;
  }

  if (windowPanel->TakeListRequest()) {
    out->kind = ControlOutboundActionKind::WindowListRequest;
    out->expectedResponseType = MessageType::ControlWindowList;
    out->expectedResponseSize = expected_message_size(MessageType::ControlWindowList);
    out->windowListRequest.header.magic = kMagic;
    out->windowListRequest.header.type = static_cast<uint16_t>(MessageType::ControlWindowListRequest);
    out->windowListRequest.header.size = static_cast<uint16_t>(sizeof(out->windowListRequest));
    out->windowListRequest.seq = ++nextWindowListSeq_;
    out->windowListRequest.clientSendQpcUs = nowUs;
    return true;
  }

  if (windowPanel->TakeMonitorListRequest()) {
    out->kind = ControlOutboundActionKind::MonitorListRequest;
    out->expectedResponseType = MessageType::ControlMonitorList;
    out->expectedResponseSize = expected_message_size(MessageType::ControlMonitorList);
    out->monitorListRequest.header.magic = kMagic;
    out->monitorListRequest.header.type =
        static_cast<uint16_t>(MessageType::ControlMonitorListRequest);
    out->monitorListRequest.header.size = static_cast<uint16_t>(sizeof(out->monitorListRequest));
    out->monitorListRequest.seq = ++nextMonitorSeq_;
    out->monitorListRequest.clientSendQpcUs = nowUs;
    return true;
  }

  uint32_t pendingMonitorId = 0;
  if (windowPanel->TakeMonitorSelectRequest(&pendingMonitorId)) {
    out->kind = ControlOutboundActionKind::MonitorSelect;
    // Answered with the list, so the reply carries the selection that actually took effect.
    out->expectedResponseType = MessageType::ControlMonitorList;
    out->expectedResponseSize = expected_message_size(MessageType::ControlMonitorList);
    out->monitorSelect.header.magic = kMagic;
    out->monitorSelect.header.type = static_cast<uint16_t>(MessageType::ControlMonitorSelect);
    out->monitorSelect.header.size = static_cast<uint16_t>(sizeof(out->monitorSelect));
    out->monitorSelect.seq = ++nextMonitorSeq_;
    out->monitorSelect.monitorId = pendingMonitorId;
    out->monitorSelect.clientSendQpcUs = nowUs;
    return true;
  }

  uint64_t pendingWindowId = 0;
  if (windowPanel->TakeSelectRequest(&pendingWindowId)) {
    out->kind = ControlOutboundActionKind::WindowSelect;
    out->expectedResponseType = MessageType::ControlWindowSelected;
    out->expectedResponseSize = expected_message_size(MessageType::ControlWindowSelected);
    out->windowSelect.header.magic = kMagic;
    out->windowSelect.header.type = static_cast<uint16_t>(MessageType::ControlWindowSelect);
    out->windowSelect.header.size = static_cast<uint16_t>(sizeof(out->windowSelect));
    out->windowSelect.seq = ++nextWindowSelectSeq_;
    out->windowSelect.windowId = pendingWindowId;
    out->windowSelect.clientSendQpcUs = nowUs;
    return true;
  }

  PendingCaptureModeRequest pendingCaptureMode{};
  if (captureMode->ConsumePending(&pendingCaptureMode)) {
    out->kind = ControlOutboundActionKind::CaptureMode;
    out->captureMode.header.magic = kMagic;
    out->captureMode.header.type = static_cast<uint16_t>(MessageType::ControlCaptureModeRequest);
    out->captureMode.header.size = static_cast<uint16_t>(sizeof(out->captureMode));
    out->captureMode.seq = pendingCaptureMode.seq;
    out->captureMode.mode = pendingCaptureMode.mode;
    out->captureMode.xPermille = pendingCaptureMode.xPermille;
    out->captureMode.yPermille = pendingCaptureMode.yPermille;
    out->captureMode.clientSendQpcUs = nowUs;
    return true;
  }

  if (metrics.updatedQpcUs > 0 && metrics.updatedQpcUs != lastMetricsSentUs_) {
    out->kind = ControlOutboundActionKind::Metrics;
    out->metrics = metrics.message;
    out->metrics.header.magic = kMagic;
    out->metrics.header.type = static_cast<uint16_t>(MessageType::ControlClientMetrics);
    out->metrics.header.size = static_cast<uint16_t>(sizeof(out->metrics));
    out->metrics.seq = ++nextMetricsSeq_;
    out->metrics.clientSendQpcUs = nowUs;
    lastMetricsSentUs_ = metrics.updatedQpcUs;
    return true;
  }

  uint16_t pendingKeyframeReason = 0;
  if (keyframeRequests->ConsumePending(&pendingKeyframeReason)) {
    out->kind = ControlOutboundActionKind::KeyframeRequest;
    out->keyframe.header.magic = kMagic;
    out->keyframe.header.type = static_cast<uint16_t>(MessageType::ControlRequestKeyFrame);
    out->keyframe.header.size = static_cast<uint16_t>(sizeof(out->keyframe));
    out->keyframe.seq = keyframeRequests->NextSequence();
    out->keyframe.reason = pendingKeyframeReason;
    out->keyframe.clientSendQpcUs = nowUs;
    return true;
  }

  PendingRuntimeTuneRequest pendingTune{};
  if (runtimeTune->ConsumePending(nowUs, metrics.message.recvMbpsX1000, &pendingTune)) {
    out->kind = ControlOutboundActionKind::RuntimeTune;
    out->runtimeTune = pendingTune.message;
    return true;
  }

  PendingDesktopBackendRequest pendingDesktopBackend{};
  if (desktopBackend && desktopBackend->ConsumePending(&pendingDesktopBackend)) {
    out->kind = ControlOutboundActionKind::DesktopBackend;
    out->desktopBackend.header.magic = kMagic;
    out->desktopBackend.header.type =
        static_cast<uint16_t>(MessageType::ControlDesktopBackendRequest);
    out->desktopBackend.header.size = static_cast<uint16_t>(sizeof(out->desktopBackend));
    out->desktopBackend.seq = pendingDesktopBackend.seq;
    out->desktopBackend.backend = pendingDesktopBackend.backend;
    out->desktopBackend.clientSendQpcUs = nowUs;
    return true;
  }

  QueuedControlInputMessage outbound{};
  if (inputQueue->TryDequeue(&outbound)) {
    out->expectedResponseType = MessageType::ControlInputAck;
    out->expectedResponseSize = expected_message_size(MessageType::ControlInputAck);
    out->inputGeneratedUs = outbound.generatedUs;  // P0 (#351): carry local generation stamp for queue-age
    if (outbound.type == MessageType::ControlInputEvent) {
      out->kind = ControlOutboundActionKind::InputEvent;
      out->inputEvent = outbound.inputEvent;
      out->inputEvent.clientSendQpcUs = nowUs;
      return true;
    }
    if (outbound.type == MessageType::ControlInputText) {
      out->kind = ControlOutboundActionKind::InputText;
      out->inputText = outbound.inputText;
      out->inputText.clientSendQpcUs = nowUs;
      return true;
    }
    if (outbound.type == MessageType::ControlPhysicalKey) {
      out->kind = ControlOutboundActionKind::PhysicalKey;
      out->physicalKey = outbound.physicalKey;
      out->physicalKey.clientSendQpcUs = nowUs;
      return true;
    }
  }

  return false;
}

uint64_t ClientControlScheduler::RecordInputAck(uint32_t inputLogEvery) {
  ++inputAckCount_;
  if (inputLogEvery == 0) return 0;
  return (inputAckCount_ % inputLogEvery) == 0 ? inputAckCount_ : 0;
}

void UdpH264FrameAssembler::Reset() {
  assemblies_.clear();
  deliveredAny_ = false;
  lastDeliveredSeq_ = 0;
}

bool UdpH264FrameAssembler::OldestIncomplete(uint16_t* missingOut, uint16_t maxMissing,
                                             IncompleteAuInfo* info) const {
  for (const Assembly& a : assemblies_) {
    if (a.receivedCount >= a.chunkCount) continue;  // data complete; only awaiting delivery
    uint32_t highWater = 0;  // highest received index + 1 (0 = nothing received yet)
    for (uint32_t i = 0; i < a.chunkCount && i < a.received.size(); ++i) {
      if (a.received[i]) highWater = i + 1;
    }
    uint16_t total = 0;
    uint16_t filled = 0;
    for (uint32_t i = 0; i < a.chunkCount; ++i) {
      if (i < a.received.size() && a.received[i]) continue;  // already have this data chunk
      ++total;
      if (missingOut && filled < maxMissing) missingOut[filled++] = static_cast<uint16_t>(i);
    }
    if (total == 0) continue;
    if (info) {
      info->seq = a.seq;
      info->generation = a.header.streamGeneration;
      info->chunkCount = a.chunkCount;
      info->missingTotal = total;
      info->highWater = static_cast<uint16_t>(highWater);
      info->keyFrame = (a.header.flags & kEncodedFrameFlagKeyFrame) != 0;
    }
    return true;
  }
  return false;
}

UdpH264AssemblyStepResult UdpH264FrameAssembler::PushDatagram(const uint8_t* data, size_t len) {
  UdpH264AssemblyStepResult result{};
  if (!data || len < sizeof(UdpVideoChunkHeader)) return result;

  UdpVideoChunkHeader packet{};
  std::memcpy(&packet, data, sizeof(packet));
  result.packetSeq = packet.seq;
  result.expectedSeq = deliveredAny_ ? lastDeliveredSeq_ + 1u : packet.seq;
  result.packetChunkOffset = packet.chunkOffset;

  if (packet.magic != kMagic ||
      packet.kind != static_cast<uint16_t>(UdpPacketKind::VideoChunk) ||
      packet.size != sizeof(UdpVideoChunkHeader)) {
    return result;
  }
  if (packet.codec != static_cast<uint16_t>(UdpCodec::H264)) {
    return result;
  }
  const bool parityPacket = (packet.flags & 0x10u) != 0;
  const bool interleavedParity = parityPacket && (packet.flags & 0x20u) != 0;
  if (packet.payloadSize == 0 || packet.chunkSize == 0 || packet.chunkStride == 0 ||
      packet.chunkStride > 4096 || packet.chunkCount == 0 ||
      packet.chunkCount > kMaxUdpVideoChunks ||
      (sizeof(UdpVideoChunkHeader) + packet.chunkSize) > len) {
    result.disposition = UdpH264AssemblyDisposition::Malformed;
    return result;
  }
  if (packet.payloadSize > kMaxUdpAssembledPayloadBytes) {
    result.disposition = UdpH264AssemblyDisposition::Malformed;
    result.oversizePayload = true;
    result.rejectedPayloadSize = packet.payloadSize;
    return result;
  }
  const uint32_t expectedChunkCount =
      (packet.payloadSize + packet.chunkStride - 1u) / packet.chunkStride;
  if (expectedChunkCount != packet.chunkCount || packet.chunkIndex >= packet.chunkCount) {
    result.disposition = UdpH264AssemblyDisposition::Malformed;
    return result;
  }
  const uint32_t expectedOffset = static_cast<uint32_t>(packet.chunkIndex) * packet.chunkStride;
  if (packet.chunkOffset != expectedOffset) {
    result.disposition = UdpH264AssemblyDisposition::Malformed;
    return result;
  }
  if (parityPacket) {
    // Interleaved parity names its group directly; consecutive parity names the first chunk
    // of the run it covers. The layout travels with the packet, so a host that still sends
    // the old grouping keeps working without the two ends having to agree in advance.
    const uint32_t groupCount =
        (static_cast<uint32_t>(packet.chunkCount) + kUdpVideoFecGroupSize - 1u) /
        kUdpVideoFecGroupSize;
    const bool wellFormedGroup = interleavedParity
                                     ? (packet.chunkIndex < groupCount)
                                     : ((packet.chunkIndex % kUdpVideoFecGroupSize) == 0);
    if (!wellFormedGroup || packet.chunkSize != packet.chunkStride) {
      result.disposition = UdpH264AssemblyDisposition::Malformed;
      return result;
    }
  } else {
    const uint32_t expectedDataSize =
        std::min<uint32_t>(packet.chunkStride, packet.payloadSize - expectedOffset);
    if (packet.chunkSize != expectedDataSize) {
      result.disposition = UdpH264AssemblyDisposition::Malformed;
      return result;
    }
  }

  if (deliveredAny_ && !sequence_is_newer(packet.seq, lastDeliveredSeq_)) {
    // Parity packets intentionally follow all data packets. A no-loss frame can therefore
    // complete before its parity arrives; that harmless late repair packet must not reset the
    // decoder. Older completed-frame traffic is stale for the same reason and is ignored.
    result.disposition = UdpH264AssemblyDisposition::Ignored;
    result.reorderDetected = true;
    return result;
  }

  auto assemblyIt = std::find_if(assemblies_.begin(), assemblies_.end(),
                                 [&](const Assembly& item) { return item.seq == packet.seq; });
  if (assemblyIt == assemblies_.end()) {
    if (assemblies_.size() >= kMaxConcurrentVideoAssemblies) {
      assemblies_.pop_front();
      result.droppedPreviousIncomplete = true;
    }
    Assembly created{};
    created.seq = packet.seq;
    created.payloadSize = packet.payloadSize;
    created.chunkCount = packet.chunkCount;
    created.chunkStride = packet.chunkStride;
    created.payload.assign(packet.payloadSize, 0);
    created.received.assign(packet.chunkCount, 0);
    const size_t fecGroupCount =
        (static_cast<size_t>(packet.chunkCount) + kUdpVideoFecGroupSize - 1u) /
        kUdpVideoFecGroupSize;
    created.parity.resize(fecGroupCount);
    created.parityReceived.assign(fecGroupCount, 0);
    created.header.header.magic = kMagic;
    created.header.header.type = static_cast<uint16_t>(MessageType::EncodedFrameH264);
    created.header.header.size = static_cast<uint16_t>(sizeof(EncodedFrameHeader));
    created.header.seq = packet.seq;
    created.header.width = packet.width;
    created.header.height = packet.height;
    created.header.flags = ((packet.flags & 0x1u) ? kEncodedFrameFlagKeyFrame : 0u) |
                           ((packet.flags & kUdpVideoChunkFlagSynthetic) ? kEncodedFrameFlagSynthetic : 0u);
    created.header.streamGeneration = packet.streamGeneration;
    created.header.captureQpcUs = packet.captureQpcUs;
    created.header.encodeStartQpcUs = packet.encodeStartQpcUs;
    created.header.encodeEndQpcUs = packet.encodeEndQpcUs;
    created.header.sendQpcUs = packet.sendQpcUs;
    assemblies_.push_back(std::move(created));
    assemblyIt = std::prev(assemblies_.end());
    result.startedNewAssembly = true;
  }

  Assembly& assembly = *assemblyIt;
  if (assembly.payloadSize != packet.payloadSize ||
      assembly.chunkCount != packet.chunkCount ||
      assembly.chunkStride != packet.chunkStride ||
      assembly.header.width != packet.width || assembly.header.height != packet.height ||
      assembly.header.streamGeneration != packet.streamGeneration) {
    assemblies_.erase(assemblyIt);
    result.disposition = UdpH264AssemblyDisposition::Malformed;
    return result;
  }

  if (parityPacket) {
    const size_t groupIndex =
        interleavedParity ? packet.chunkIndex
                          : (packet.chunkIndex / kUdpVideoFecGroupSize);
    assembly.parityInterleaved = interleavedParity ? 1u : 0u;
    if (groupIndex >= assembly.parityReceived.size()) {
      result.disposition = UdpH264AssemblyDisposition::Malformed;
      return result;
    }
    if (!assembly.parityReceived[groupIndex]) {
      assembly.parity[groupIndex].assign(data + sizeof(UdpVideoChunkHeader),
                                         data + sizeof(UdpVideoChunkHeader) + packet.chunkSize);
      assembly.parityReceived[groupIndex] = 1;
    }
  } else if (!assembly.received[packet.chunkIndex]) {
    std::memcpy(assembly.payload.data() + packet.chunkOffset,
                data + sizeof(UdpVideoChunkHeader), packet.chunkSize);
    assembly.received[packet.chunkIndex] = 1;
    ++assembly.receivedCount;
  }

  const uint16_t groupCount = static_cast<uint16_t>(assembly.parity.size());
  for (size_t groupIndex = 0; groupIndex < assembly.parity.size(); ++groupIndex) {
    if (!assembly.parityReceived[groupIndex]) continue;
    // Interleaved: chunks groupIndex, groupIndex+G, groupIndex+2G ...
    // Consecutive:  chunks groupIndex*8 .. groupIndex*8+7
    const bool interleaved = assembly.parityInterleaved != 0;
    const uint16_t groupStart = static_cast<uint16_t>(
        interleaved ? groupIndex : (groupIndex * kUdpVideoFecGroupSize));
    const uint16_t groupStep = interleaved ? groupCount : uint16_t{1};
    const uint16_t groupEnd =
        interleaved ? assembly.chunkCount
                    : std::min<uint16_t>(
                          assembly.chunkCount,
                          static_cast<uint16_t>(groupStart + kUdpVideoFecGroupSize));
    uint16_t missingIndex = 0;
    uint16_t missingCount = 0;
    for (uint16_t index = groupStart; index < groupEnd; index = static_cast<uint16_t>(index + groupStep)) {
      if (!assembly.received[index]) {
        missingIndex = index;
        ++missingCount;
      }
    }
    if (missingCount != 1) continue;
    std::vector<uint8_t> recovered = assembly.parity[groupIndex];
    for (uint16_t index = groupStart; index < groupEnd; index = static_cast<uint16_t>(index + groupStep)) {
      if (index == missingIndex || !assembly.received[index]) continue;
      const uint32_t offset = static_cast<uint32_t>(index) * assembly.chunkStride;
      const uint32_t bytes = std::min<uint32_t>(assembly.chunkStride,
                                                assembly.payloadSize - offset);
      for (uint32_t i = 0; i < bytes; ++i) recovered[i] ^= assembly.payload[offset + i];
    }
    const uint32_t recoveredOffset = static_cast<uint32_t>(missingIndex) * assembly.chunkStride;
    const uint32_t recoveredSize = std::min<uint32_t>(assembly.chunkStride,
                                                      assembly.payloadSize - recoveredOffset);
    std::memcpy(assembly.payload.data() + recoveredOffset, recovered.data(), recoveredSize);
    assembly.received[missingIndex] = 1;
    ++assembly.receivedCount;
    result.fecRecovered = true;
    ++result.fecRecoveredChunks;
  }

  for (uint16_t index = 0; index < assembly.chunkCount; ++index) {
    if (!assembly.received[index]) {
      result.expectedNextOffset = static_cast<uint32_t>(index) * assembly.chunkStride;
      break;
    }
  }
  if (assembly.receivedCount != assembly.chunkCount) {
    result.disposition = UdpH264AssemblyDisposition::Partial;
    return result;
  }

  assembly.header.payloadSize = assembly.payloadSize;
  result.disposition = UdpH264AssemblyDisposition::Completed;
  result.frame.header = assembly.header;
  result.frame.payload = std::move(assembly.payload);
  if (deliveredAny_ && packet.seq != lastDeliveredSeq_ + 1u) {
    result.droppedPreviousIncomplete = true;
  }
  deliveredAny_ = true;
  lastDeliveredSeq_ = packet.seq;
  assemblies_.erase(std::remove_if(assemblies_.begin(), assemblies_.end(),
                                   [&](const Assembly& item) {
                                     return !sequence_is_newer(item.seq, lastDeliveredSeq_);
                                   }),
                    assemblies_.end());
  return result;
}

void WindowPanelStateModel::Reset() {
  std::lock_guard<std::mutex> lk(mu_);
  state_ = WindowPanelSnapshot{};
  listRequestPending_ = false;
  selectRequestPending_ = false;
  pendingSelectId_ = 0;
  monitorListRequestPending_ = false;
  monitorSelectRequestPending_ = false;
  pendingMonitorId_ = 0;
}

void WindowPanelStateModel::RequestList(const char* statusText) {
  std::lock_guard<std::mutex> lk(mu_);
  listRequestPending_ = true;
  if (statusText) state_.status = statusText;
}

bool WindowPanelStateModel::TakeListRequest() {
  std::lock_guard<std::mutex> lk(mu_);
  if (!listRequestPending_) return false;
  listRequestPending_ = false;
  return true;
}

bool WindowPanelStateModel::RequestSelect(uint64_t windowId, const char* statusText) {
  std::lock_guard<std::mutex> lk(mu_);
  if (state_.selectionLocked) return false;
  selectRequestPending_ = true;
  pendingSelectId_ = windowId;
  if (statusText) state_.status = statusText;
  return true;
}

bool WindowPanelStateModel::TakeSelectRequest(uint64_t* outWindowId) {
  std::lock_guard<std::mutex> lk(mu_);
  if (!selectRequestPending_) return false;
  selectRequestPending_ = false;
  if (outWindowId) *outWindowId = pendingSelectId_;
  return true;
}

void WindowPanelStateModel::RequestMonitorList() {
  std::lock_guard<std::mutex> lk(mu_);
  monitorListRequestPending_ = true;
}

bool WindowPanelStateModel::TakeMonitorListRequest() {
  std::lock_guard<std::mutex> lk(mu_);
  // Only if the host said it understands these. An older one drains the request without
  // answering, and the control loop is strictly request/response, so it would wait forever.
  if (!monitorListRequestPending_ || !state_.hostSupportsMonitors) {
    monitorListRequestPending_ = false;
    return false;
  }
  monitorListRequestPending_ = false;
  return true;
}

bool WindowPanelStateModel::SetHostSupportsMonitors(bool supported) {
  std::lock_guard<std::mutex> lk(mu_);
  const bool newlySupported = supported && !state_.hostSupportsMonitors;
  state_.hostSupportsMonitors = supported;
  if (!supported) {
    state_.monitors.clear();
    state_.selectedMonitorId = 0;
  }
  return newlySupported;
}

bool WindowPanelStateModel::RequestMonitorSelect(uint32_t monitorId) {
  std::lock_guard<std::mutex> lk(mu_);
  if (!state_.hostSupportsMonitors) return false;
  monitorSelectRequestPending_ = true;
  pendingMonitorId_ = monitorId;
  return true;
}

bool WindowPanelStateModel::TakeMonitorSelectRequest(uint32_t* outMonitorId) {
  std::lock_guard<std::mutex> lk(mu_);
  if (!monitorSelectRequestPending_) return false;
  monitorSelectRequestPending_ = false;
  if (outMonitorId) *outMonitorId = pendingMonitorId_;
  return true;
}

void WindowPanelStateModel::ApplyMonitorList(const ControlMonitorListMessage& msg) {
  std::lock_guard<std::mutex> lk(mu_);
  state_.monitors.clear();
  const uint32_t count = std::min<uint32_t>(msg.itemCount, kControlMonitorListMaxEntries);
  for (uint32_t i = 0; i < count; ++i) {
    const auto& src = msg.items[i];
    MonitorEntry e{};
    e.id = src.id;
    e.x = src.x;
    e.y = src.y;
    e.width = src.width;
    e.height = src.height;
    e.primary = (src.flags & kControlMonitorFlagPrimary) != 0;
    state_.monitors.push_back(e);
  }
  state_.selectedMonitorId = msg.selectedMonitorId;
}

void WindowPanelStateModel::SetStatus(const std::string& status) {
  std::lock_guard<std::mutex> lk(mu_);
  state_.status = status;
}

WindowListApplyResult WindowPanelStateModel::ApplyWindowList(const ControlWindowListMessage& msg, int visibleCount) {
  WindowListApplyResult result{};
  std::lock_guard<std::mutex> lk(mu_);
  state_.items.clear();
  const uint32_t count = std::min<uint32_t>(msg.itemCount, kControlWindowListMaxEntries);
  state_.items.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    const auto& src = msg.items[i];
    WindowTargetUiEntry item{};
    item.id = src.id;
    item.pid = src.pid;
    item.width = src.width;
    item.height = src.height;
    item.minimized = ((src.flags & 0x1u) != 0);
    item.title = fixed_cstr_to_string(src.title, sizeof(src.title));
    if (item.title.empty()) item.title = "(untitled)";
    state_.items.push_back(std::move(item));
  }
  state_.selectedId = msg.selectedWindowId;
  state_.selectionLocked = ((msg.flags & 0x1u) != 0);
  if (msg.selectedWindowId == 0) {
    state_.selectedTitle = "desktop";
  } else {
    const auto it = std::find_if(state_.items.begin(), state_.items.end(),
                                 [&](const WindowTargetUiEntry& entry) {
                                   return entry.id == msg.selectedWindowId;
                                 });
    state_.selectedTitle = (it != state_.items.end()) ? it->title : "window";
  }
  set_selected_target_dimensions(&state_);
  const int clampedVisibleCount = std::max(1, visibleCount);
  const int maxScroll = std::max<int>(0, static_cast<int>(state_.items.size()) - clampedVisibleCount);
  state_.scrollIndex = std::clamp(state_.scrollIndex, 0, maxScroll);
  state_.status = std::string("window_list_received count=") + std::to_string(count);

  std::ostringstream oss;
  oss << "[native-video-client][control] window-list seq=" << msg.seq
      << " count=" << count
      << " selectedId=" << msg.selectedWindowId
      << " locked=" << (((msg.flags & 0x1u) != 0) ? 1 : 0);
  if (!state_.items.empty()) {
    oss << " firstId=" << state_.items.front().id
        << " firstTitle=" << state_.items.front().title;
  }
  result.logLine = oss.str();
  return result;
}

WindowSelectApplyResult WindowPanelStateModel::ApplyWindowSelected(const ControlWindowSelectedMessage& msg) {
  WindowSelectApplyResult result{};
  std::lock_guard<std::mutex> lk(mu_);
  const bool ok = ((msg.flags & 0x1u) != 0);
  const bool locked = ((msg.flags & 0x2u) != 0);
  state_.selectionLocked = state_.selectionLocked || locked;
  state_.lastSelectSeq = msg.seq;
  state_.lastSelectOk = ok;
  state_.lastSelectWindowId = msg.windowId;
  state_.lastSelectStreamGeneration = msg.streamGeneration;
  state_.lastSelectHostSendQpcUs = msg.hostSendQpcUs;
  const std::string reason = fixed_cstr_to_string(msg.reason, sizeof(msg.reason));
  const std::string title = fixed_cstr_to_string(msg.title, sizeof(msg.title));
  if (ok) {
    state_.selectedId = msg.windowId;
    state_.selectedTitle = (msg.windowId == 0) ? "desktop" : (title.empty() ? "window" : title);
    set_selected_target_dimensions(&state_);
    state_.status = std::string("window_selected: ") + state_.selectedTitle;
  } else {
    state_.status = std::string("window_select_failed: ") + (reason.empty() ? "unknown" : reason);
  }

  std::ostringstream oss;
  oss << "[native-video-client][control] window-selected seq=" << msg.seq
      << " ok=" << (ok ? 1 : 0)
      << " windowId=" << msg.windowId
      << " streamGen=" << msg.streamGeneration
      << " reason=" << (reason.empty() ? "none" : reason)
      << " title=" << (title.empty() ? "<empty>" : title)
      << " locked=" << (locked ? 1 : 0)
      << " hostSendQpcUs=" << msg.hostSendQpcUs;
  result.logLine = oss.str();
  result.ok = ok;
  return result;
}

void WindowPanelStateModel::Scroll(int deltaSteps, int visibleCount) {
  std::lock_guard<std::mutex> lk(mu_);
  const int clampedVisibleCount = std::max(1, visibleCount);
  const int maxScroll = std::max<int>(0, static_cast<int>(state_.items.size()) - clampedVisibleCount);
  state_.scrollIndex = std::clamp(state_.scrollIndex + deltaSteps, 0, maxScroll);
}

bool WindowPanelStateModel::TryResolveWindowIdForVisibleRow(int row, int visibleCount, uint64_t* outWindowId) const {
  if (!outWindowId || row < 0) return false;
  std::lock_guard<std::mutex> lk(mu_);
  const int clampedVisibleCount = std::max(1, visibleCount);
  const int scrollIndex =
      std::clamp(state_.scrollIndex, 0, std::max<int>(0, static_cast<int>(state_.items.size()) - clampedVisibleCount));
  const int itemIndex = scrollIndex + row;
  if (itemIndex < 0 || itemIndex >= static_cast<int>(state_.items.size())) return false;
  *outWindowId = state_.items[static_cast<size_t>(itemIndex)].id;
  return true;
}

WindowPanelSnapshot WindowPanelStateModel::Snapshot() const {
  std::lock_guard<std::mutex> lk(mu_);
  return state_;
}

bool WindowPanelStateModel::IsDesktopSelected() const {
  std::lock_guard<std::mutex> lk(mu_);
  return state_.selectedId == 0;
}

}  // namespace remote60::native_poc
