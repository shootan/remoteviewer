#pragma once

// Per-window preview budget: how many times a thumbnail capture may be retried before the host
// stops asking, and for how long.
//
// Role:    decide whether a window's preview may be attempted now (Allow), and record what came
//          back (Record). Three consecutive failures put the window in a cooldown during which it
//          is skipped; one success clears everything.
// Thread:  not synchronised. Owned by the control thread, which is the only caller -- the control
//          dispatcher is one per process and handles thumbnail requests synchronously.
// Input:   a target key, an outcome, and the caller's clock in microseconds.
// Output:  a yes/no per attempt, plus the state needed to explain the no.
// Callers: the host control session's thumbnail path.
//
// Why this exists at all: a window that never answers WM_PRINT used to wedge the control session
// behind it -- 1h50m in the 2026-09-15 incident, which killed every session on the host because
// the dispatcher could not get back to reading. Deadlines alone are not enough. Without a budget
// the host would pay the full deadline for that window on every single request, forever.
//
// Deliberately free of Win32: the identity is passed in as plain integers, and time is a
// parameter. That is what makes the policy testable without a window, and what lets the capture
// mechanism (helper process or otherwise) be decided separately from the retry policy.

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <unordered_map>
#include <vector>

namespace remote60::native_poc {

// What one attempt turned into. The distinction that matters is Canceled: it is the only one that
// costs nothing, because it says the attempt never got to ask the window anything.
enum class ThumbnailOutcome {
  Ok,        // pixels came back
  Failed,    // the capture said no (not a window, iconic, hung-flagged, helper died)
  TimedOut,  // the deadline expired with no answer -- the case this whole file is about
  Canceled,  // the session went away, or we stopped caring before asking
};

// A window, identified well enough that a recycled HWND value does not inherit another window's
// punishment.
//
// The creation time is what makes the pid trustworthy: pids are reused, and without it a new
// process landing on the old pid would look like the same owner. Together the three distinguish
// HWND reuse ACROSS processes.
//
// They do NOT distinguish HWND reuse WITHIN one process -- a program that closes a window and
// opens another can land on the same HWND value with the same pid and the same creation time, and
// nothing in this key can tell those apart. That case is handled from the outside instead: the
// host drops entries for windows that leave the list (RetainOnly), and entries expire on their own
// (entryTtlUs). When it is missed anyway, the cost is one window skipped for at most one cooldown
// before a success clears it -- the error lands on "no preview for a minute", not "session stuck".
struct ThumbnailTargetKey {
  uint64_t windowId = 0;         // hwnd_to_id(hwnd)
  uint32_t ownerPid = 0;
  uint64_t processCreatedUs = 0;

  bool operator==(const ThumbnailTargetKey& other) const {
    return windowId == other.windowId && ownerPid == other.ownerPid &&
           processCreatedUs == other.processCreatedUs;
  }
};

struct ThumbnailTargetKeyHash {
  size_t operator()(const ThumbnailTargetKey& key) const {
    // Mixed rather than xored. An HWND value and a FILETIME are both dense in their low bits and
    // nearly constant in their high ones, so a plain xor of the three would throw away most of
    // what distinguishes two keys. boost::hash_combine's mixer, which is what this is, does not.
    uint64_t h = key.windowId * 0x9E3779B97F4A7C15ull;
    h ^= (static_cast<uint64_t>(key.ownerPid) + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2));
    h ^= (key.processCreatedUs + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2));
    return static_cast<size_t>(h);
  }
};

struct ThumbnailBudgetConfig {
  // Three tries, then stop asking for a minute. The retry interval keeps a burst from spending
  // three deadlines back to back on the same unresponsive window within one picker refresh.
  uint32_t maxAttempts = 3;
  uint64_t retryIntervalUs = 2ull * 1000 * 1000;
  uint64_t cooldownUs = 60ull * 1000 * 1000;
  // Long enough that a window sitting quietly in the list keeps its state across several picker
  // sessions, short enough that state cannot outlive the window it describes indefinitely.
  uint64_t entryTtlUs = 10ull * 60 * 1000 * 1000;
  size_t maxEntries = 256;
};

class ThumbnailBudget {
 public:
  explicit ThumbnailBudget(ThumbnailBudgetConfig config = {}) : config_(config) {
    if (config_.maxAttempts == 0) config_.maxAttempts = 1;
    if (config_.maxEntries == 0) config_.maxEntries = 1;
  }

  /**
   * Whether a capture may be attempted for this window right now.
   *
   * Touches the entry's recency, so a window being asked about regularly is not the one evicted
   * when the map is full.
   */
  bool Allow(const ThumbnailTargetKey& key, uint64_t nowUs) {
    Expire(nowUs);
    auto it = entries_.find(key);
    if (it == entries_.end()) return true;  // never tried: nothing to hold against it
    Entry& entry = it->second;
    entry.lastTouchedUs = nowUs;
    if (nowUs < entry.cooldownUntilUs) return false;
    if (entry.consecutiveFailures == 0) return true;
    // Inside a burst: space the attempts out rather than firing them as fast as the client asks.
    return nowUs - entry.lastAttemptUs >= config_.retryIntervalUs;
  }

  /** Record what an attempt produced. */
  void Record(const ThumbnailTargetKey& key, ThumbnailOutcome outcome, uint64_t nowUs) {
    if (outcome == ThumbnailOutcome::Canceled) {
      // Nothing was asked of the window, so nothing is held against it. Recency still moves: the
      // caller cared about this window just now.
      auto it = entries_.find(key);
      if (it != entries_.end()) it->second.lastTouchedUs = nowUs;
      return;
    }

    Entry& entry = entries_[key];
    entry.lastAttemptUs = nowUs;
    entry.lastTouchedUs = nowUs;

    if (outcome == ThumbnailOutcome::Ok) {
      // One success clears the whole history, including a cooldown still running. A window that
      // answers is a window that works, whatever it did a minute ago.
      entry.consecutiveFailures = 0;
      entry.cooldownUntilUs = 0;
      Evict(nowUs);
      return;
    }

    ++entry.consecutiveFailures;
    if (entry.consecutiveFailures >= config_.maxAttempts) {
      entry.cooldownUntilUs = nowUs + config_.cooldownUs;
      // Reset the counter with the cooldown, not after it: when the cooldown expires the window
      // gets a fresh burst of attempts rather than being permanently one failure from silence.
      entry.consecutiveFailures = 0;
    }
    Evict(nowUs);
  }

  /**
   * Drop state for windows that are no longer in the host's list.
   *
   * This is the main defence against the identity limit described on ThumbnailTargetKey: a window
   * that closes leaves the list, and its entry goes with it, so a later HWND reuse starts clean.
   */
  void RetainOnly(const std::vector<uint64_t>& liveWindowIds, uint64_t nowUs) {
    Expire(nowUs);
    for (auto it = entries_.begin(); it != entries_.end();) {
      bool live = false;
      for (uint64_t id : liveWindowIds) {
        if (id == it->first.windowId) {
          live = true;
          break;
        }
      }
      it = live ? std::next(it) : entries_.erase(it);
    }
  }

  void Forget(const ThumbnailTargetKey& key) { entries_.erase(key); }

  bool InCooldown(const ThumbnailTargetKey& key, uint64_t nowUs) const {
    const auto it = entries_.find(key);
    return it != entries_.end() && nowUs < it->second.cooldownUntilUs;
  }

  uint32_t ConsecutiveFailures(const ThumbnailTargetKey& key) const {
    const auto it = entries_.find(key);
    return it == entries_.end() ? 0u : it->second.consecutiveFailures;
  }

  size_t TrackedCount() const { return entries_.size(); }
  const ThumbnailBudgetConfig& config() const { return config_; }

 private:
  struct Entry {
    uint32_t consecutiveFailures = 0;
    uint64_t lastAttemptUs = 0;
    uint64_t cooldownUntilUs = 0;
    uint64_t lastTouchedUs = 0;
  };

  /** Entries nobody has asked about for a long time cannot still be describing anything useful. */
  void Expire(uint64_t nowUs) {
    if (config_.entryTtlUs == 0) return;
    for (auto it = entries_.begin(); it != entries_.end();) {
      const uint64_t idle = nowUs - it->second.lastTouchedUs;
      it = (it->second.lastTouchedUs != 0 && idle > config_.entryTtlUs) ? entries_.erase(it)
                                                                       : std::next(it);
    }
  }

  /** Keep the map bounded by dropping whatever has been idle longest. */
  void Evict(uint64_t nowUs) {
    (void)nowUs;
    while (entries_.size() > config_.maxEntries) {
      auto oldest = entries_.begin();
      for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->second.lastTouchedUs < oldest->second.lastTouchedUs) oldest = it;
      }
      entries_.erase(oldest);
    }
  }

  ThumbnailBudgetConfig config_;
  std::unordered_map<ThumbnailTargetKey, Entry, ThumbnailTargetKeyHash> entries_;
};

}  // namespace remote60::native_poc
