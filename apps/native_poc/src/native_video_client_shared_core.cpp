#include "native_video_client_shared_core.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>

namespace remote60::native_poc {

namespace {

constexpr size_t kMaxInputQueueSize = 256;

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
  if (msg.type == MessageType::ControlInputEvent &&
      msg.inputEvent.kind == 1 &&
      !queue_.empty() &&
      queue_.back().type == MessageType::ControlInputEvent &&
      queue_.back().inputEvent.kind == 1) {
    queue_.back() = msg;
    return;
  }
  if (queue_.size() >= kMaxInputQueueSize) {
    queue_.pop_front();
    dropped_.fetch_add(1, std::memory_order_relaxed);
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

void ClientInputQueue::Reset() {
  std::lock_guard<std::mutex> lk(mu_);
  queue_.clear();
  dropped_.store(0, std::memory_order_relaxed);
  nextSeq_.store(0, std::memory_order_relaxed);
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

RuntimeTuneState::RuntimeTuneState(uint32_t bitrateMin, uint32_t bitrateMax, uint32_t bitrateStep,
                                   uint32_t keyintMin, uint32_t keyintMax)
    : bitrateMin_(bitrateMin),
      bitrateMax_(std::max<uint32_t>(bitrateMin, bitrateMax)),
      bitrateStep_(std::max<uint32_t>(1, bitrateStep)),
      keyintMin_(std::max<uint32_t>(1, keyintMin)),
      keyintMax_(std::max<uint32_t>(std::max<uint32_t>(1, keyintMin), keyintMax)) {}

void RuntimeTuneState::Reset(uint32_t bitrate, uint32_t keyint) {
  enabled_.store(false, std::memory_order_relaxed);
  dirty_.store(false, std::memory_order_relaxed);
  nextSeq_.store(0, std::memory_order_relaxed);
  targetBitrate_.store(bitrate, std::memory_order_relaxed);
  targetKeyint_.store(keyint, std::memory_order_relaxed);
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
  if (message.bitrate > 0) message.flags |= 0x1u;
  if (message.keyint > 0) message.flags |= 0x2u;
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
                                        CaptureModeRequestState* captureMode,
                                        KeyframeRequestState* keyframeRequests,
                                        RuntimeTuneState* runtimeTune,
                                        ClientInputQueue* inputQueue,
                                        ControlOutboundAction* out) {
  if (!windowPanel || !captureMode || !keyframeRequests || !runtimeTune || !inputQueue || !out) {
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

  QueuedControlInputMessage outbound{};
  if (inputQueue->TryDequeue(&outbound)) {
    out->expectedResponseType = MessageType::ControlInputAck;
    out->expectedResponseSize = expected_message_size(MessageType::ControlInputAck);
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
  }

  return false;
}

uint64_t ClientControlScheduler::RecordInputAck(uint32_t inputLogEvery) {
  ++inputAckCount_;
  if (inputLogEvery == 0) return 0;
  return (inputAckCount_ % inputLogEvery) == 0 ? inputAckCount_ : 0;
}

void UdpH264FrameAssembler::Reset() {
  assembling_ = false;
  assemblingSeq_ = 0;
  assemblingExpected_ = 0;
  assemblingNextOffset_ = 0;
  assemblingHeader_ = EncodedFrameHeader{};
  assemblingPayload_.clear();
}

UdpH264AssemblyStepResult UdpH264FrameAssembler::PushDatagram(const uint8_t* data, size_t len) {
  UdpH264AssemblyStepResult result{};
  if (!data || len < sizeof(UdpVideoChunkHeader)) return result;

  UdpVideoChunkHeader packet{};
  std::memcpy(&packet, data, sizeof(packet));
  result.packetSeq = packet.seq;
  result.expectedSeq = assemblingSeq_;
  result.packetChunkOffset = packet.chunkOffset;
  result.expectedNextOffset = assemblingNextOffset_;

  if (packet.magic != kMagic ||
      packet.kind != static_cast<uint16_t>(UdpPacketKind::VideoChunk) ||
      packet.size != sizeof(UdpVideoChunkHeader)) {
    return result;
  }
  if (packet.codec != static_cast<uint16_t>(UdpCodec::H264)) {
    return result;
  }
  if (packet.payloadSize == 0 || packet.chunkSize == 0 ||
      packet.chunkOffset > packet.payloadSize ||
      (packet.chunkOffset + packet.chunkSize) > packet.payloadSize ||
      (sizeof(UdpVideoChunkHeader) + packet.chunkSize) > len) {
    assembling_ = false;
    result.disposition = UdpH264AssemblyDisposition::Malformed;
    return result;
  }

  const bool firstChunk = ((packet.flags & 0x2u) != 0);
  const bool lastChunk = ((packet.flags & 0x4u) != 0);
  if (firstChunk) {
    if (assembling_ && assemblingNextOffset_ < assemblingExpected_) {
      result.droppedPreviousIncomplete = true;
    }
    assembling_ = true;
    assemblingSeq_ = packet.seq;
    assemblingExpected_ = packet.payloadSize;
    assemblingNextOffset_ = 0;
    assemblingPayload_.assign(assemblingExpected_, 0);
    assemblingHeader_ = {};
    assemblingHeader_.header.magic = kMagic;
    assemblingHeader_.header.type = static_cast<uint16_t>(MessageType::EncodedFrameH264);
    assemblingHeader_.header.size = static_cast<uint16_t>(sizeof(assemblingHeader_));
    assemblingHeader_.seq = packet.seq;
    assemblingHeader_.width = packet.width;
    assemblingHeader_.height = packet.height;
    assemblingHeader_.flags = (packet.flags & 0x1u) ? 1u : 0u;
    assemblingHeader_.captureQpcUs = packet.captureQpcUs;
    assemblingHeader_.encodeStartQpcUs = packet.encodeStartQpcUs;
    assemblingHeader_.encodeEndQpcUs = packet.encodeEndQpcUs;
    assemblingHeader_.sendQpcUs = packet.sendQpcUs;
    result.startedNewAssembly = true;
    result.expectedSeq = assemblingSeq_;
    result.expectedNextOffset = 0;
  }

  if (!assembling_ || packet.seq != assemblingSeq_ || packet.chunkOffset != assemblingNextOffset_) {
    result.disposition = UdpH264AssemblyDisposition::Dropped;
    result.reorderDetected = true;
    result.expectedNextOffset = assemblingNextOffset_;
    return result;
  }

  std::memcpy(assemblingPayload_.data() + packet.chunkOffset,
              data + sizeof(UdpVideoChunkHeader), packet.chunkSize);
  assemblingNextOffset_ += packet.chunkSize;
  result.expectedNextOffset = assemblingNextOffset_;

  if (!lastChunk) {
    result.disposition = UdpH264AssemblyDisposition::Partial;
    return result;
  }

  if (assemblingNextOffset_ != assemblingExpected_) {
    assembling_ = false;
    result.disposition = UdpH264AssemblyDisposition::Malformed;
    return result;
  }

  assemblingHeader_.payloadSize = assemblingExpected_;
  result.disposition = UdpH264AssemblyDisposition::Completed;
  result.frame.header = assemblingHeader_;
  result.frame.payload = std::move(assemblingPayload_);
  assembling_ = false;
  return result;
}

void WindowPanelStateModel::Reset() {
  std::lock_guard<std::mutex> lk(mu_);
  state_ = WindowPanelSnapshot{};
  listRequestPending_ = false;
  selectRequestPending_ = false;
  pendingSelectId_ = 0;
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
  const std::string reason = fixed_cstr_to_string(msg.reason, sizeof(msg.reason));
  const std::string title = fixed_cstr_to_string(msg.title, sizeof(msg.title));
  if (ok) {
    state_.selectedId = msg.windowId;
    state_.selectedTitle = (msg.windowId == 0) ? "desktop" : (title.empty() ? "window" : title);
    state_.status = std::string("window_selected: ") + state_.selectedTitle;
  } else {
    state_.status = std::string("window_select_failed: ") + (reason.empty() ? "unknown" : reason);
  }

  std::ostringstream oss;
  oss << "[native-video-client][control] window-selected seq=" << msg.seq
      << " ok=" << (ok ? 1 : 0)
      << " windowId=" << msg.windowId
      << " reason=" << (reason.empty() ? "none" : reason)
      << " title=" << (title.empty() ? "<empty>" : title)
      << " locked=" << (locked ? 1 : 0);
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
