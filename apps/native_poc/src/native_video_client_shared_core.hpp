#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "poc_protocol.hpp"

namespace remote60::native_poc {

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

struct WindowTargetUiEntry {
  uint64_t id = 0;
  uint32_t pid = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  bool minimized = false;
  std::string title;
};

struct WindowPanelSnapshot {
  std::vector<WindowTargetUiEntry> items;
  uint64_t selectedId = 0;
  std::string selectedTitle = "desktop";
  bool selectionLocked = false;
  std::string status = "waiting_control";
  int scrollIndex = 0;
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
};

}  // namespace remote60::native_poc
