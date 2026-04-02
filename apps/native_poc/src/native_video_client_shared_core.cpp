#include "native_video_client_shared_core.hpp"

#include <algorithm>
#include <cstring>
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
