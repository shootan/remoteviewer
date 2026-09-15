#include "native_video_client_shared_core.hpp"

#include <algorithm>
#include <cstring>
#include <chrono>
#include <limits>
#include <sstream>

namespace remote60::native_poc {

namespace {

constexpr size_t kMaxInputQueueSize = 256;
uint64_t queue_now_ms() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
uint32_t release_identity(const QueuedControlInputMessage& msg) {
  if (msg.type == MessageType::ControlInputEvent && msg.inputEvent.keyCode <= 255) {
    if (msg.inputEvent.kind == 6) return 1 + msg.inputEvent.keyCode;
    if (msg.inputEvent.kind == 3 && (msg.inputEvent.keyCode == 1 || msg.inputEvent.keyCode == 2 ||
                                   msg.inputEvent.keyCode == 4)) return 257 + msg.inputEvent.keyCode;
  }
  if (msg.type == MessageType::ControlPhysicalKey && !msg.physicalKey.down &&
      msg.physicalKey.vk <= 255 && msg.physicalKey.scanCode <= 255) {
    return msg.physicalKey.scanCode
        ? 1024 + (msg.physicalKey.scanCode << 1) + (msg.physicalKey.flags & 1)
        : 512 + msg.physicalKey.vk;
  }
  return 0;
}
constexpr uint32_t kMaxUdpAssembledPayloadBytes = 16u * 1024u * 1024u;
constexpr uint16_t kMaxUdpVideoChunks = 16384;
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
  const auto isMove = [](const QueuedControlInputMessage& value) {
    return value.type == MessageType::ControlInputEvent && value.inputEvent.kind == 1;
  };
  if (!backpressured_ && isMove(msg) && !queue_.empty() && isMove(queue_.back())) {
    queue_.back() = msg; queue_.back().queuedAtMs = queue_now_ms(); ++coalescedMoves_; return;
  }
  if (queue_.size() >= kMaxInputQueueSize && !backpressured_) {
    // Cancel unsent actions, never release edges. New presses/text are refused until releases
    // drain. Since all intervening downs are now gone, equal release identities can coalesce.
    backpressured_ = true;
    for (auto it = queue_.begin(); it != queue_.end();) {
      if (!release_identity(*it)) { it = queue_.erase(it); ++dropped_; }
      else ++it;
    }
  }
  if (backpressured_) {
    const uint32_t identity = release_identity(msg);
    if (!identity) { ++dropped_; return; }
    for (const auto& pending : queue_) if (release_identity(pending) == identity) return;
    // The finite set of valid release identities plus the original 256 entries bounds this
    // reserve below 1536 items. Arbitrary key/scan values cannot enlarge it.
  }
  queue_.push_back(msg);
  queue_.back().queuedAtMs = queue_now_ms();
  ready_.notify_one();
}

void ClientInputQueue::WaitForInput(uint32_t timeoutMs) {
  std::unique_lock<std::mutex> lock(mu_);
  ready_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return !queue_.empty(); });
}

bool ClientInputQueue::TryDequeue(QueuedControlInputMessage* out) {
  if (!out) return false;
  std::lock_guard<std::mutex> lk(mu_);
  const uint64_t now = queue_now_ms();
  while (!queue_.empty()) {
    const auto& next = queue_.front();
    if (!release_identity(next) && next.queuedAtMs && now - next.queuedAtMs > 2000) {
      queue_.pop_front(); ++dropped_; continue;
    }
    *out = next; queue_.pop_front();
    if (queue_.empty()) backpressured_ = false;
    return true;
  }
  backpressured_ = false;
  return false;
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
  backpressured_ = false;
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
  inputBurstCount_ = 0;
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

  // Input precedes background work, with a bounded burst so ABR feedback cannot starve.
  QueuedControlInputMessage outbound{};
  if (inputBurstCount_ < 8 && inputQueue->TryDequeue(&outbound)) {
    ++inputBurstCount_;
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

  inputBurstCount_ = 0;
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


  return false;
}

uint64_t ClientControlScheduler::RecordInputAck(uint32_t inputLogEvery) {
  ++inputAckCount_;
  if (inputLogEvery == 0) return 0;
  return (inputAckCount_ % inputLogEvery) == 0 ? inputAckCount_ : 0;
}

void UdpH264FrameAssembler::Reset() {
  assemblies_.clear();
  abandoned_.clear();
  deliveredKeyCandidates_.clear();
  saturated_ = false;
  saturationFloorSet_ = false;
  saturationFloorSeq_ = 0;
  deliveredAny_ = false;
  lastDeliveredSeq_ = 0;
}

void UdpH264FrameAssembler::ConfigureInOrderHold(uint64_t maxHoldUs, size_t maxConcurrent,
                                                 size_t maxHeldBytes) {
  holdMaxUs_ = maxHoldUs;
  maxConcurrent_ = std::max<size_t>(1, maxConcurrent);
  maxHeldBytes_ = maxHeldBytes;
}

size_t UdpH264FrameAssembler::HeldBytes() const {
  size_t total = 0;
  for (const Assembly& a : assemblies_) total += a.payloadSize;
  return total;
}

bool UdpH264FrameAssembler::AnyComplete() const {
  return std::any_of(assemblies_.begin(), assemblies_.end(), [](const Assembly& a) { return a.complete; });
}

void UdpH264FrameAssembler::RaiseSaturationFloor(uint32_t seq) {
  if (!saturationFloorSet_ || sequence_is_newer(seq, saturationFloorSeq_)) {
    saturationFloorSet_ = true;
    saturationFloorSeq_ = seq;
  }
}

size_t UdpH264FrameAssembler::saturation_candidates() const {
  return static_cast<size_t>(std::count_if(assemblies_.begin(), assemblies_.end(), [](const Assembly& a) {
    return (a.header.flags & kEncodedFrameFlagKeyFrame) != 0;
  }));
}

void UdpH264FrameAssembler::NoteKeyAccepted(uint64_t generation, uint32_t seq) {
  if (!saturated_) return;
  const bool wasCandidate =
      std::any_of(deliveredKeyCandidates_.begin(), deliveredKeyCandidates_.end(),
                  [&](const AbandonedAu& c) { return c.generation == generation && c.seq == seq; });
  if (!wasCandidate) return;  // not one of this episode's candidates: nothing is released
  // That key is the recovery point at the assembly/gate admission boundary -- the receiver
  // admitted it for decoding, which is not a claim that it decoded (a decode failure afterwards
  // is the existing reason 4/5/7 recovery's job, not this list's). From that boundary on, every
  // identity at or before its seq is behind the point the stream resumed from, whatever
  // generation it was recorded under -- the host's sequence is monotone across the session --
  // and the ordinary stale guard takes over for their late chunks.
  abandoned_.erase(std::remove_if(abandoned_.begin(), abandoned_.end(),
                                  [&](const AbandonedAu& a) { return !sequence_is_newer(a.seq, seq); }),
                   abandoned_.end());
  deliveredKeyCandidates_.clear();
  // Newer records may still hold the list at the cap: then the episode simply continues -- and
  // so does its floor, which is monotone for as long as the episode lasts.
  saturated_ = abandoned_.size() >= kAbandonedSaturationCap;
  if (!saturated_) {
    saturationFloorSet_ = false;
    saturationFloorSeq_ = 0;
  }
}

bool UdpH264FrameAssembler::IsAbandoned(uint64_t generation, uint32_t seq) const {
  for (const AbandonedAu& a : abandoned_) {
    if (a.seq == seq && a.generation == generation) return true;
  }
  return false;
}

bool UdpH264FrameAssembler::GiveUpIncomplete(uint64_t generation, uint32_t seq, uint64_t nowUs) {
  auto it = std::find_if(assemblies_.begin(), assemblies_.end(), [&](const Assembly& a) {
    return a.seq == seq && a.header.streamGeneration == generation;
  });
  if (it == assemblies_.end() || it->complete) return false;  // gone, or completed meanwhile: cancelled
  const bool wasKeyFrame = (it->header.flags & kEncodedFrameFlagKeyFrame) != 0;
  assemblies_.erase(it);
  if (saturated_) {
    // Inside an episode a candidate that runs out of time is ANOTHER ATTEMPT at the same
    // recovery, not a new identity to remember: recording it would let a host that keeps
    // failing grow the list without end, which is what the episode exists to bound. The floor
    // is what keeps it from coming back.
    if (wasKeyFrame) RaiseSaturationFloor(seq);
    return true;
  }
  // Remember it, so the chunks still on their way do not re-create it (see the header note).
  // Nothing is dropped to make room: forgetting an identity would let it be accepted again.
  abandoned_.push_back(AbandonedAu{generation, seq, nowUs});
  if (abandoned_.size() >= kAbandonedSaturationCap) {
    // The cap starts a recovery episode instead of evicting anything. The non-key assemblies
    // still held go with it: while the episode lasts only keyframes may start a new assembly, so
    // they cannot be re-created and need no record of their own.
    saturated_ = true;
    saturationFloorSet_ = false;
    saturationFloorSeq_ = 0;
    deliveredKeyCandidates_.clear();
    assemblies_.erase(std::remove_if(assemblies_.begin(), assemblies_.end(),
                                     [](const Assembly& a) {
                                       return (a.header.flags & kEncodedFrameFlagKeyFrame) == 0;
                                     }),
                      assemblies_.end());
    // ... and the key assemblies are trimmed to the episode's slots straight away, oldest first,
    // so the K=2 bound holds from the moment it starts rather than only for new candidates.
    while (assemblies_.size() > kSaturationKeyCandidates) {
      auto oldest = std::min_element(assemblies_.begin(), assemblies_.end(),
                                     [](const Assembly& a, const Assembly& b) {
                                       return sequence_is_newer(b.seq, a.seq);
                                     });
      RaiseSaturationFloor(oldest->seq);
      assemblies_.erase(oldest);
    }
  }
  return true;
}

// Hand one assembled AU out: the completion block PushDatagram used to run inline, so the legacy
// immediate path and the in-order hold path deliver byte-identical results.
UdpH264AssemblyStepResult UdpH264FrameAssembler::DeliverAssembly(Assembly& assembly) {
  UdpH264AssemblyStepResult result{};
  result.packetSeq = assembly.seq;
  result.expectedSeq = deliveredAny_ ? lastDeliveredSeq_ + 1u : assembly.seq;
  assembly.header.payloadSize = assembly.payloadSize;
  result.disposition = UdpH264AssemblyDisposition::Completed;
  result.frame.header = assembly.header;
  result.frame.payload = std::move(assembly.payload);
  result.fecRecovered = assembly.fecRecoveredChunks > 0;
  result.fecRecoveredChunks = assembly.fecRecoveredChunks;
  if (deliveredAny_ && assembly.seq != lastDeliveredSeq_ + 1u) {
    result.droppedPreviousIncomplete = true;
  }
  deliveredAny_ = true;
  lastDeliveredSeq_ = assembly.seq;
  if (saturated_ && (assembly.header.flags & kEncodedFrameFlagKeyFrame) != 0) {
    // A key candidate reached the caller. Only one it then ACCEPTS ends the episode
    // (NoteKeyAccepted); delivery alone is not enough. Bounded ring (see the header note): the
    // oldest entry gives way, and with it the right to end the episode.
    while (deliveredKeyCandidates_.size() >= kDeliveredKeyCandidateRing) deliveredKeyCandidates_.pop_front();
    deliveredKeyCandidates_.push_back(
        AbandonedAu{assembly.header.streamGeneration, assembly.seq, 0});
  }
  // Retire the tombstones this delivery makes redundant (see the header note): everything the
  // ordinary stale guard covers from now on. The sequence is session-wide, so a record made
  // under an older generation is retired by this delivery just the same.
  {
    const uint32_t deliveredSeq = assembly.seq;
    abandoned_.erase(std::remove_if(abandoned_.begin(), abandoned_.end(),
                                    [&](const AbandonedAu& a) { return !sequence_is_newer(a.seq, deliveredSeq); }),
                     abandoned_.end());
  }
  assemblies_.erase(std::remove_if(assemblies_.begin(), assemblies_.end(),
                                   [&](const Assembly& item) {
                                     return !sequence_is_newer(item.seq, lastDeliveredSeq_);
                                   }),
                    assemblies_.end());
  return result;
}

bool UdpH264FrameAssembler::PopDelivery(uint64_t nowUs, bool repairNonKey,
                                        UdpH264AssemblyStepResult* out) {
  if (!out) return false;
  while (!assemblies_.empty()) {
    // Sequence order, not arrival order: a reordered network can start a newer AU first.
    auto oldest = std::min_element(assemblies_.begin(), assemblies_.end(),
                                   [](const Assembly& a, const Assembly& b) {
                                     return sequence_is_newer(b.seq, a.seq);
                                   });
    if (oldest->complete) {
      *out = DeliverAssembly(*oldest);
      return true;
    }
    const bool anyComplete = std::any_of(assemblies_.begin(), assemblies_.end(),
                                         [](const Assembly& a) { return a.complete; });
    if (!anyComplete) return false;  // nothing behind it to release anyway
    // A complete keyframe behind the broken chain is the recovery point: nothing older than it
    // can be decoded without the missing reference anyway, and the IDR does not need it. Give up
    // everything ahead of the newest complete IDR at once, so the IDR goes out now and the gap it
    // carries is closed by the IDR itself (no request). (Codex condition 1.)
    auto newestKey = assemblies_.end();
    for (auto it = assemblies_.begin(); it != assemblies_.end(); ++it) {
      if (!it->complete || (it->header.flags & kEncodedFrameFlagKeyFrame) == 0) continue;
      if (newestKey == assemblies_.end() || sequence_is_newer(it->seq, newestKey->seq)) newestKey = it;
    }
    if (newestKey != assemblies_.end()) {
      const uint32_t keySeq = newestKey->seq;
      assemblies_.erase(std::remove_if(assemblies_.begin(), assemblies_.end(),
                                       [&](const Assembly& a) { return sequence_is_newer(keySeq, a.seq); }),
                        assemblies_.end());
      continue;
    }
    const bool keyFrame = (oldest->header.flags & kEncodedFrameFlagKeyFrame) != 0;
    const bool worthHolding = (repairNonKey || keyFrame) &&
                              holdMaxUs_ > 0 && nowUs < oldest->firstPacketUs + holdMaxUs_;
    if (worthHolding) return false;  // its retransmit may still land
    // Hold expired (or not worth it): give the AU up. The next delivery then carries the seq gap
    // as droppedPreviousIncomplete, exactly as the legacy path reported it.
    assemblies_.erase(oldest);
  }
  return false;
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
      info->firstPacketUs = a.firstPacketUs;
      info->lastProgressUs = a.lastProgressUs;
    }
    return true;
  }
  return false;
}

UdpH264AssemblyStepResult UdpH264FrameAssembler::PushDatagram(const uint8_t* data, size_t len) {
  return PushDatagram(data, len, 0);
}

UdpH264AssemblyStepResult UdpH264FrameAssembler::PushDatagram(const uint8_t* data, size_t len,
                                                              uint64_t nowUs) {
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

  if (IsAbandoned(packet.streamGeneration, packet.seq)) {
    // A04: this AU's repair was given up (its rounds and its grace were spent). Its late data or
    // FEC chunks are stale traffic now -- re-creating the assembly would block the head again and
    // make the caller abandon the same AU a second and third time, while the good AU behind it
    // waits. Only this exact (generation, seq) is affected.
    result.disposition = UdpH264AssemblyDisposition::Ignored;
    result.reorderDetected = true;
    return result;
  }
  if (deliveredAny_ && !sequence_is_newer(packet.seq, lastDeliveredSeq_)) {
    // Parity packets intentionally follow all data packets. A no-loss frame can therefore
    // complete before its parity arrives; that harmless late repair packet must not reset the
    // decoder. Older completed-frame traffic is stale for the same reason and is ignored.
    result.disposition = UdpH264AssemblyDisposition::Ignored;
    result.reorderDetected = true;
    return result;
  }

  // Episode admission runs BEFORE the assembly lookup: it can erase a candidate, and this is a
  // deque, so an iterator taken earlier would not survive it. A live assembly is answered
  // whatever the episode says -- a repair in progress is never interrupted by the floor.
  const bool haveLiveAssembly =
      std::any_of(assemblies_.begin(), assemblies_.end(),
                  [&](const Assembly& item) { return item.seq == packet.seq; });
  if (!haveLiveAssembly && saturated_) {
    const bool keyFrame = (packet.flags & 0x1u) != 0;
    if (!keyFrame) {
      // Only keyframes may start an assembly during the episode: a new P would just become the
      // next stuck head, and the recovery this episode is waiting for is a key.
      result.disposition = UdpH264AssemblyDisposition::Ignored;
      return result;
    }
    if (saturationFloorSet_ && !sequence_is_newer(packet.seq, saturationFloorSeq_)) {
      // At or below the floor: the episode has moved past this sequence, whatever generation it
      // carries (the session's seq is monotone). Its chunks may not re-create it, and must not
      // touch any timer or progress stamp.
      result.disposition = UdpH264AssemblyDisposition::Ignored;
      result.reorderDetected = true;
      return result;
    }
    if (saturationAdmit_ && !saturationAdmit_(packet.streamGeneration)) {
      // The caller's own generation gate would refuse this frame anyway. Nothing changes: it may
      // not evict a candidate the viewer is waiting for, nor raise the floor past it.
      result.disposition = UdpH264AssemblyDisposition::Ignored;
      return result;
    }
    if (assemblies_.size() >= kSaturationKeyCandidates) {
      // Slots are reused, not added to -- but only by a NEWER key. A key that is past the floor
      // yet older than both candidates (reordering) must not push a candidate that is already
      // being repaired out of its slot.
      auto oldest = std::min_element(assemblies_.begin(), assemblies_.end(),
                                     [](const Assembly& a, const Assembly& b) {
                                       return sequence_is_newer(b.seq, a.seq);
                                     });
      if (oldest == assemblies_.end() || !sequence_is_newer(packet.seq, oldest->seq)) {
        result.disposition = UdpH264AssemblyDisposition::Ignored;
        return result;
      }
      RaiseSaturationFloor(oldest->seq);
      assemblies_.erase(oldest);
    }
  }
  auto assemblyIt = std::find_if(assemblies_.begin(), assemblies_.end(),
                                 [&](const Assembly& item) { return item.seq == packet.seq; });
  if (assemblyIt == assemblies_.end()) {
    const auto evict_oldest = [&]() {
      if (holdMaxUs_ > 0) {
        // The oldest by sequence is the stuck head (complete ones were already popped).
        assemblies_.erase(std::min_element(assemblies_.begin(), assemblies_.end(),
                                           [](const Assembly& a, const Assembly& b) {
                                             return sequence_is_newer(b.seq, a.seq);
                                           }));
      } else {
        assemblies_.pop_front();
      }
      result.droppedPreviousIncomplete = true;
    };
    if (assemblies_.size() >= maxConcurrent_) evict_oldest();
    // Hold mode: the payload held across assemblies is bounded too (a burst of large IDRs must
    // not pile up behind one stuck AU). (Codex condition 1.)
    while (holdMaxUs_ > 0 && maxHeldBytes_ > 0 && !assemblies_.empty() &&
           HeldBytes() + packet.payloadSize > maxHeldBytes_) {
      evict_oldest();
    }
    Assembly created{};
    created.seq = packet.seq;
    created.firstPacketUs = nowUs;
    created.lastProgressUs = nowUs;
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
    assembly.lastProgressUs = nowUs;  // a new data chunk is progress; a duplicate (above) is not
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
    assembly.lastProgressUs = nowUs;  // an FEC-recovered chunk is progress too
    result.fecRecovered = true;
    ++result.fecRecoveredChunks;
    ++assembly.fecRecoveredChunks;
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

  if (holdMaxUs_ > 0) {
    // In-order hold: complete, but PopDelivery decides when it goes out.
    assembly.complete = true;
    result.disposition = UdpH264AssemblyDisposition::Queued;
    return result;
  }
  const bool droppedByEviction = result.droppedPreviousIncomplete;
  const bool fecRecovered = result.fecRecovered;
  const uint32_t fecRecoveredChunks = result.fecRecoveredChunks;
  result = DeliverAssembly(assembly);
  result.packetChunkOffset = packet.chunkOffset;
  result.droppedPreviousIncomplete = result.droppedPreviousIncomplete || droppedByEviction;
  result.fecRecovered = fecRecovered;
  result.fecRecoveredChunks = fecRecoveredChunks;
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
  // The sentence is composed where it is drawn, not here. This file is compiled into two dozen
  // targets and most of them do not pass /utf-8, so a Korean literal in it is read in code page
  // 949 and the lexer loses the closing quote -- the same misreading that mangled the Host's
  // message boxes, except here it will not even build. `displayStatus` stays empty and the
  // picker builds the line from the item count.

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
