#pragma once
// capture-lifecycle r3 (Codex N3-host): the desktop-backend status the control thread answers request 34 with
// (ControlDesktopBackendStatus, 90), as ONE snapshot. The main loop publishes the owner epoch, the packed status
// (requested | effective<<16 | active<<32 | flags<<48, wire codes), its change count and the key of the request
// it handled together, under one lock; the control thread reads all of them under the same lock. r2 read them
// as separate atomics (packed first, the handled key after): a publish in between made a "handled" answer
// carry the status from before it -- applied, with the old backend. Pure but for the lock, so the order is
// unit-tested (host_backend_status_board_test.cpp).

#include <cstdint>
#include <functional>
#include <mutex>

namespace remote60::native_poc {

struct BackendStatusSnapshot {
  uint64_t ownerEpoch = 0;  // the owner this status is of (0 = none published yet)
  uint64_t packed = 0;
  uint32_t changes = 0;     // how many times packed changed
  uint64_t handledKey = 0;  // (epoch << 32 | seq) of the last backend request the main loop handled
};

class BackendStatusBoard {
 public:
  // Main loop: the status as it is now, for this owner.
  void Publish(uint64_t ownerEpoch, uint64_t packed) {
    std::lock_guard<std::mutex> lk(mu_);
    PublishLocked(ownerEpoch, packed);
  }
  // Main loop: a request handled -- its key with the status it left, in one step.
  void PublishHandled(uint64_t ownerEpoch, uint64_t packed, uint64_t handledKey) {
    std::lock_guard<std::mutex> lk(mu_);
    PublishLocked(ownerEpoch, packed);
    s_.handledKey = handledKey;
  }
  // Control thread: everything above, of one publish.
  BackendStatusSnapshot Read() const {
    BackendStatusSnapshot out;
    {
      std::lock_guard<std::mutex> lk(mu_);
      out = s_;
    }
    if (testAfterRead) testAfterRead();
    return out;
  }
  // Unit test only: runs after a Read (a publish there is "between this read and the answer").
  std::function<void()> testAfterRead;

 private:
  void PublishLocked(uint64_t ownerEpoch, uint64_t packed) {
    if (s_.packed != packed) ++s_.changes;
    s_.packed = packed;
    s_.ownerEpoch = ownerEpoch;
  }
  mutable std::mutex mu_;
  BackendStatusSnapshot s_;
};

struct BackendStatusAnswer {
  bool applied = false;
  bool ownerKnown = false;  // the snapshot is this session's (else the fields are 0: not known for it)
  uint16_t requested = 0, effective = 0, active = 0, flags = 0;
  uint32_t changes = 0;
};

// What request 34 (servedEpoch, isChange, its own key) is answered with, from ONE snapshot. `postedKey`: the last
// change this session's control thread posted (a question is pending while that one is not handled). A
// snapshot of another owner never answers for this session: its fields are not sent as this session's.
inline BackendStatusAnswer DecideBackendStatusAnswer(const BackendStatusSnapshot& snap, uint64_t servedEpoch,
                                                     bool isChange, uint64_t requestKey, uint64_t postedKey) {
  BackendStatusAnswer a;
  a.ownerKnown = snap.ownerEpoch == servedEpoch;
  const bool changeDone = isChange ? snap.handledKey == requestKey
                                   : ((postedKey >> 32) != servedEpoch || snap.handledKey == postedKey);
  a.applied = a.ownerKnown && changeDone;
  if (a.ownerKnown) {
    a.requested = static_cast<uint16_t>(snap.packed & 0xffffu);
    a.effective = static_cast<uint16_t>((snap.packed >> 16) & 0xffffu);
    a.active = static_cast<uint16_t>((snap.packed >> 32) & 0xffffu);
    a.flags = static_cast<uint16_t>((snap.packed >> 48) & 0xffffu);
    a.changes = snap.changes;
  }
  return a;
}

}  // namespace remote60::native_poc
