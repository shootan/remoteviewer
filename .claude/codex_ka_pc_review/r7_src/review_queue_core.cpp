#include "native_video_client_shared_core.hpp"
#include <algorithm>
#include <chrono>
namespace remote60::native_poc {
namespace {
constexpr size_t kMaxInputQueueSize=256;
uint64_t queue_now_ms() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
uint32_t release_identity(const QueuedControlInputMessage& msg) {
  if (msg.type == MessageType::ControlInputEvent && msg.inputEvent.keyCode <= 255) {
    if (msg.inputEvent.kind == 6) return 1 + msg.inputEvent.keyCode;
    // ka-pc-impl r7 (Codex N1): every mouse button's up, the X pair (VK_XBUTTON1=5, VK_XBUTTON2=6) included --
    // enqueue_release_for_pressed_mouse_buttons makes those ups, and a release-only drain must keep them. (Whether an
    // X edge may go to a host at all is the viewer's own fence, enqueue_input_event; it is not decided here.)
    if (msg.inputEvent.kind == 3 && (msg.inputEvent.keyCode == 1 || msg.inputEvent.keyCode == 2 ||
                                   msg.inputEvent.keyCode == 4 || msg.inputEvent.keyCode == 5 ||
                                   msg.inputEvent.keyCode == 6)) return 257 + msg.inputEvent.keyCode;
  }
  if (msg.type == MessageType::ControlPhysicalKey && !msg.physicalKey.down &&
      msg.physicalKey.vk <= 255 && msg.physicalKey.scanCode <= 255) {
    return msg.physicalKey.scanCode
        ? 1024 + (msg.physicalKey.scanCode << 1) + (msg.physicalKey.flags & 1)
        : 512 + msg.physicalKey.vk;
  }
  return 0;
}
}
uint32_t control_input_release_identity(const QueuedControlInputMessage& msg) { return release_identity(msg); }
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
bool ClientInputQueue::Empty() const {
  std::lock_guard<std::mutex> lk(mu_);
  return queue_.empty();
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
void ClientInputQueue::DropAllButReleases(uint32_t* dropped, uint32_t* kept) {
  std::lock_guard<std::mutex> lk(mu_);
  uint32_t gone = 0;
  for (auto it = queue_.begin(); it != queue_.end();) {
    if (!release_identity(*it)) { it = queue_.erase(it); ++gone; }
    else ++it;
  }
  // Backpressure ends where it always does: once the releases it is holding have drained.
  if (queue_.empty()) backpressured_ = false;
  if (dropped) *dropped = gone;
  if (kept) *kept = static_cast<uint32_t>(queue_.size());
  if (!queue_.empty()) ready_.notify_one();
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
  msg.inputEvent.buttons = static_cast<uint16_t>(buttons & kMouseWireMask);
  msg.inputEvent.x = x;
  msg.inputEvent.y = y;
  msg.inputEvent.wheelDelta = wheelDelta;
  msg.inputEvent.keyCode = keyCode;
  msg.inputEvent.clientSendQpcUs = nowUs;
  msg.generatedUs = nowUs;  // P0 (#351): local-only generation stamp; wire clientSendQpcUs is reset at send
  return msg;
}
}
