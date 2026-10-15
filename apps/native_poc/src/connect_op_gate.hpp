#pragma once

// keepalive r3 R2-a / r4 S1·S2: who may still use, adopt and clean up a native connection.
//
// One gate per process (native_bridge.cpp). The app's connect operations are numbered HERE (Next): a number
// is never handed out twice in a process, so a new Activity cannot make an older Activity's operation current
// again (r4 S2-a). A sign-out, an exit, a stop, a disconnect or another connect takes a new number; every
// operation before it is stale from that instant.
//   - Network steps (observe / punch on the one shared rendezvous socket) run one at a time (EnterStep holds
//     the step lock) and only for the current operation. A step's stop flag is registered under the same
//     lock as the currency check and is set by Next() under it too, so a stale step can never clear its own
//     cancellation (r4 S2: the r3 code stored stop=false after IsCurrent, over a cancel in between).
//   - The rendezvous socket belongs to the operation that observed with it (ClaimRendezvous); a later step
//     of another operation does not punch or hand over that socket (r4 S2).
//   - The session's Connect runs inside Adopt: serialised, not started for a stale operation, and an
//     operation that went stale while Connect ran is disconnected before Adopt returns 0. The adopted
//     operation and generation are recorded.
//   - A late cleanup of a stale operation is CloseIfOwn: under the same lock as Adopt, it closes the session
//     only if that operation's adoption is still the one running -- the compare and the close are one step
//     (r4 S1: the r3 app read the generation, compared it, then called an unconditional Disconnect).
//   - The user's explicit disconnect is CloseExplicit: unconditional, but the session and the rendezvous
//     socket are each closed on their own boundary above, once (r5 T1).
// Taking a new number never waits for a running Connect or network step.

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace remote60::native_poc {

class ConnectOpGate {
 public:
  // A new current operation (process-unique). Every step of an older one is stopped now.
  int64_t Next() {
    std::lock_guard<std::mutex> lk(mu_);
    const int64_t op = ++counter_;
    current_.store(op, std::memory_order_release);
    for (auto* s : steps_) {
      if (s->op != op) s->stop.store(true, std::memory_order_release);
    }
    return op;
  }
  bool IsCurrent(int64_t op) const { return op != 0 && current_.load(std::memory_order_acquire) == op; }

  // One observe / punch of `op`. Constructed by EnterStep; its `stop` goes true the moment `op` is stale.
  struct Step {
    int64_t op = 0;
    std::atomic<bool> stop{false};
  };
  class StepGuard {
   public:
    StepGuard(ConnectOpGate& g, int64_t op) : g_(g), net_(g.netMu_) {
      g_.Hook("step_lock_taken");
      std::lock_guard<std::mutex> lk(g_.mu_);
      step_.op = op;
      entered_ = g_.IsCurrent(op);
      if (entered_) g_.steps_.push_back(&step_);
    }
    ~StepGuard() {
      std::lock_guard<std::mutex> lk(g_.mu_);
      for (size_t i = 0; i < g_.steps_.size(); ++i) {
        if (g_.steps_[i] == &step_) {
          g_.steps_.erase(g_.steps_.begin() + static_cast<long>(i));
          break;
        }
      }
    }
    bool entered() const { return entered_; }
    const std::atomic<bool>* stop() const { return &step_.stop; }
    bool stopped() const { return step_.stop.load(std::memory_order_acquire) || !g_.IsCurrent(step_.op); }

   private:
    ConnectOpGate& g_;
    std::unique_lock<std::mutex> net_;  // one network step at a time on the shared rendezvous
    Step step_;
    bool entered_ = false;
  };

  // Inside a StepGuard: the rendezvous socket is now `op`'s (it observed with it).
  bool ClaimRendezvous(int64_t op) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!IsCurrent(op)) return false;
    rendezvousOwner_ = op;
    return true;
  }
  bool OwnsRendezvous(int64_t op) const {
    std::lock_guard<std::mutex> lk(mu_);
    return IsCurrent(op) && rendezvousOwner_ == op;
  }

  struct Steps {
    std::function<bool()> connect;          // the session's Connect
    std::function<uint64_t()> generation;   // the session's connection generation now
    std::function<void()> disconnect;       // the session's Disconnect
    std::function<void()> closePrepared;    // drop the prepared socket of a connect that does not start
  };

  // 0: not adopted (stale before or during, or Connect failed). Otherwise the generation Connect made.
  uint64_t Adopt(int64_t op, const Steps& s) {
    std::lock_guard<std::mutex> lk(adoptMu_);
    Hook("before_adopt");
    if (!IsCurrent(op)) {
      if (s.closePrepared) s.closePrepared();
      staleRefused_.fetch_add(1, std::memory_order_relaxed);
      return 0;
    }
    const bool ok = s.connect && s.connect();
    Hook("after_connect");
    const uint64_t gen = ok && s.generation ? s.generation() : 0;
    if (!IsCurrent(op)) {
      if (ok && s.disconnect) s.disconnect();  // made by a stale operation: never handed over
      staleRefused_.fetch_add(1, std::memory_order_relaxed);
      return 0;
    }
    if (gen != 0) {
      adoptedOp_ = op;
      adoptedGen_ = gen;
    }
    return gen;
  }

  // A stale operation's late cleanup: closes the session only if `op` adopted generation `gen` and that is
  // still the session running -- compared and closed in one step with every Adopt. true = it closed.
  bool CloseIfOwn(int64_t op, uint64_t gen, const std::function<uint64_t()>& generationNow,
                  const std::function<void()>& disconnect) {
    std::lock_guard<std::mutex> lk(adoptMu_);
    Hook("close_if_own_locked");
    if (op == 0 || gen == 0 || adoptedOp_ != op || adoptedGen_ != gen) return false;
    if (!generationNow || generationNow() != gen) return false;
    if (disconnect) disconnect();
    adoptedOp_ = 0;
    adoptedGen_ = 0;
    staleClosed_.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  // r5 T1: the user's explicit end of everything (disconnect, exit, the park's end) -- unconditional, unlike
  // CloseIfOwn, but on the same boundaries: first a new number (every operation before it is stale; never
  // waits), then the session is closed under adoptMu_ (one at a time with every Adopt / CloseIfOwn -- the
  // r4 nativeDisconnect called it from outside, so two threads could join the same worker), unless an
  // operation newer than this close has already been adopted. The rendezvous socket is closed under the
  // step lock (after any running step has seen its stop and left), handed to `runAsync` so the caller does
  // not wait for a step's network wait; null runs it here. Returns the close's own number.
  int64_t CloseExplicit(const std::function<void()>& disconnect, const std::function<void()>& closeRendezvous,
                        const std::function<void(const std::function<void()>&)>& runAsync) {
    const int64_t k = Next();
    Hook("close_explicit_cancelled");
    {
      std::lock_guard<std::mutex> lk(adoptMu_);
      Hook("close_explicit_locked");
      if (adoptedOp_ <= k) {
        if (disconnect) disconnect();
        adoptedOp_ = 0;
        adoptedGen_ = 0;
      }
    }
    const std::function<void()> rendezvous = [this, k, closeRendezvous] { CloseRendezvousThrough(k, closeRendezvous); };
    if (runAsync) {
      runAsync(rendezvous);
    } else {
      rendezvous();
    }
    return k;
  }
  // The rendezvous half of CloseExplicit(k): under the step lock; not a socket a newer operation observed with.
  void CloseRendezvousThrough(int64_t k, const std::function<void()>& closeRendezvous) {
    std::lock_guard<std::mutex> net(netMu_);
    Hook("close_rendezvous_locked");
    {
      std::lock_guard<std::mutex> lk(mu_);
      if (rendezvousOwner_ > k) return;
      rendezvousOwner_ = 0;
    }
    if (closeRendezvous) closeRendezvous();
  }

  uint32_t StaleRefused() const { return staleRefused_.load(std::memory_order_relaxed); }
  uint32_t StaleClosed() const { return staleClosed_.load(std::memory_order_relaxed); }

#ifdef REMOTE60_CONNECT_GATE_TEST_HOOKS
  // TEST BUILD ONLY: called at the named boundaries (a test stops or interleaves there). Never compiled
  // into the APK's library (build gate: no "close_if_own_locked" / "step_lock_taken" string in it).
  std::function<void(const char*)> testHook;
#endif

 private:
  void Hook(const char* at) {
#ifdef REMOTE60_CONNECT_GATE_TEST_HOOKS
    if (testHook) testHook(at);
#else
    (void)at;
#endif
  }
  mutable std::mutex mu_;   // currency, steps, rendezvous owner
  std::mutex netMu_;        // one network step at a time
  std::mutex adoptMu_;      // Adopt, CloseIfOwn and CloseExplicit's session close
  std::atomic<int64_t> current_{0};
  int64_t counter_ = 0;
  std::vector<Step*> steps_;
  int64_t rendezvousOwner_ = 0;
  int64_t adoptedOp_ = 0;
  uint64_t adoptedGen_ = 0;
  std::atomic<uint32_t> staleRefused_{0};
  std::atomic<uint32_t> staleClosed_{0};
};

}  // namespace remote60::native_poc
