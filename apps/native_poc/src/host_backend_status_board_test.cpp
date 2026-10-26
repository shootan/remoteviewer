// capture-lifecycle r3 (Codex N3-host): request 34's answer comes from ONE status snapshot
// (host_backend_status_board.hpp). The counterexample Codex gave -- the reader takes the old status, the main
// loop publishes the new status with the handled key, the reader takes the new key -- must give either the new
// status applied, or NotYetApplied; never "applied" with the old backend. And another owner's status is never
// sent as this session's.

#include "host_backend_status_board.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

using namespace remote60::native_poc;

namespace {

int gFailures = 0;

void expect(bool condition, const std::string& what) {
  std::printf("%s %s\n", condition ? "PASS" : "FAIL", what.c_str());
  if (!condition) ++gFailures;
}

uint64_t Packed(uint16_t requested, uint16_t active) {
  return static_cast<uint64_t>(requested) | (static_cast<uint64_t>(requested) << 16) |
         (static_cast<uint64_t>(active) << 32) | (1ull << 48);
}

constexpr uint16_t kDxgi = 1, kWgc = 2;

// The control thread's loop, as host_control_session.cpp runs it (one Read per look), bounded to `looks`.
BackendStatusAnswer Answer(BackendStatusBoard& b, uint64_t servedEpoch, bool isChange, uint64_t key, uint64_t posted,
                           int looks) {
  BackendStatusAnswer a;
  for (int i = 0; i < looks; ++i) {
    a = DecideBackendStatusAnswer(b.Read(), servedEpoch, isChange, key, posted);
    if (a.applied) break;
  }
  return a;
}

void TestPublishBetweenReadAndAnswer() {
  BackendStatusBoard b;
  b.Publish(7, Packed(kDxgi, kDxgi));
  const uint64_t key = (7ull << 32) | 3;
  bool once = false;
  // The main loop handles the WGC change right after the reader's first look.
  b.testAfterRead = [&] {
    if (once) return;
    once = true;
    b.PublishHandled(7, Packed(kWgc, kWgc), key);
  };
  const BackendStatusAnswer first = DecideBackendStatusAnswer(b.Read(), 7, true, key, key);
  expect(!first.applied, "N3: the look taken before the publish is not applied (NotYetApplied)");
  const BackendStatusAnswer next = DecideBackendStatusAnswer(b.Read(), 7, true, key, key);
  expect(next.applied && next.active == kWgc && next.requested == kWgc,
         "N3: the next look is applied WITH the new status (wgc), never the old one");
  b.testAfterRead = nullptr;
  const BackendStatusAnswer looped = Answer(b, 7, true, key, key, 3);
  expect(!looped.applied || looped.active == kWgc, "N3: applied always comes with the status it was handled with");
}

void TestQuestionWhileChangePending() {
  BackendStatusBoard b;
  b.Publish(7, Packed(kDxgi, kDxgi));
  const uint64_t posted = (7ull << 32) | 4;  // a change posted, not handled yet
  const BackendStatusAnswer q = DecideBackendStatusAnswer(b.Read(), 7, false, (7ull << 32) | 5, posted);
  expect(!q.applied && q.ownerKnown, "N3: a question while this session's change waits -> NotYetApplied");
  b.PublishHandled(7, Packed(kWgc, kWgc), posted);
  const BackendStatusAnswer q2 = DecideBackendStatusAnswer(b.Read(), 7, false, (7ull << 32) | 6, posted);
  expect(q2.applied && q2.active == kWgc, "N3: ...and applied, with the new status, once it is handled");
}

void TestOtherOwnerNeverAnswers() {
  BackendStatusBoard b;
  b.PublishHandled(6, Packed(kWgc, kWgc), (6ull << 32) | 9);  // the previous session's WGC
  const BackendStatusAnswer a = DecideBackendStatusAnswer(b.Read(), 7, false, (7ull << 32) | 1, 0);
  expect(!a.applied && !a.ownerKnown && a.requested == 0 && a.active == 0,
         "N3: a new session's first question before its own publish: not known (0), NotYetApplied -- not the old WGC");
  b.Publish(7, Packed(kDxgi, kDxgi));
  const BackendStatusAnswer a2 = DecideBackendStatusAnswer(b.Read(), 7, false, (7ull << 32) | 2, 0);
  expect(a2.applied && a2.active == kDxgi, "N3: once published for it, its own status (dxgi), applied");
}

void TestChangeCount() {
  BackendStatusBoard b;
  b.Publish(7, Packed(kDxgi, kDxgi));
  b.Publish(7, Packed(kDxgi, kDxgi));
  b.PublishHandled(7, Packed(kWgc, kWgc), 1);
  expect(b.Read().changes == 2, "N3: the change count moves only when the status changes");
}

}  // namespace

int main() {
  TestPublishBetweenReadAndAnswer();
  TestQuestionWhileChangePending();
  TestOtherOwnerNeverAnswers();
  TestChangeCount();
  std::printf("[host-backend-status-board-test] %s failures=%d\n", gFailures == 0 ? "PASS" : "FAIL", gFailures);
  return gFailures == 0 ? 0 : 1;
}
