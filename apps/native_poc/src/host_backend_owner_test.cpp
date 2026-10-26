// capture-lifecycle r2 (Codex N2): the one owner decision of a viewer's desktop-backend choice
// (host_backend_owner.hpp) and the mailbox take that uses it (host_main_loop_mailbox.hpp), on the boundary
// orders Codex listed. A tick is modelled as the backend stage runs it: ONE epoch read, the owner step, then the
// take under the owner that step settled.

#include "host_backend_owner.hpp"
#include "host_main_loop_mailbox.hpp"

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

constexpr uint32_t kDefault = 1;  // dxgi: the host's startup request
constexpr uint32_t kWgc = 2;

// What the backend stage keeps: the owner state and the backend requested.
struct Rig {
  BackendOwnerState owner;
  uint32_t requested = kDefault;
  uint64_t appliedKey = 0;
  MainLoopMailbox mailbox;
  // One tick. `postBetween` runs after the epoch read and before the take (a control thread posting then).
  template <typename F>
  void Tick(uint64_t epochRead, uint64_t endedEpoch, F postBetween) {
    const BackendOwnerStep step = StepBackendOwner(owner, epochRead, endedEpoch);
    if (step.ownerChanged || step.ownerEnded) requested = kDefault;
    postBetween();
    if (const auto req = mailbox.TakeBackendRequestForOwner(owner.ownerEpoch)) {
      requested = req->backend;
      appliedKey = BackendRequestKey(req->epoch, owner.ownerEpoch, req->seq);
    }
  }
  void Tick(uint64_t epochRead, uint64_t endedEpoch = 0) { Tick(epochRead, endedEpoch, [] {}); }
};

void TestNewOwnerRequestPostedMidTick() {
  // Codex N2: main settled owner A -> control adopts B and posts B's WGC -> the same tick's take. The request is
  // B's: left for the next tick, which makes B the owner first -- B's choice survives B's own reset.
  Rig r;
  r.Tick(5);
  r.Tick(5, 0, [&] { r.mailbox.PostBackendRequest({6, 1, kWgc}); });
  expect(r.requested == kDefault, "N2: B's request is not taken under owner A");
  r.Tick(6);
  expect(r.requested == kWgc, "N2: B's WGC applied after B became the owner (not wiped by B's reset)");
  expect(r.appliedKey == ((6ull << 32) | 1), "N2: marked under B's own epoch and seq");
  r.Tick(6);
  expect(r.requested == kWgc, "N2: the same owner keeps its choice on later ticks");
}

void TestOldOwnerRequestAfterNewOwner() {
  // A posted its request, then B became the owner before the take: A's request is dropped, never applied to B.
  Rig r;
  r.Tick(5);
  r.mailbox.PostBackendRequest({5, 3, kWgc});
  r.Tick(6);
  expect(r.requested == kDefault, "N2: A's late request does not touch B's backend");
  expect(r.appliedKey == 0, "N2: and is not marked under B's key");
  expect(r.mailbox.StaleDroppedCount() == 1, "N2: counted as dropped");
}

void TestTakeUnderOwnerThenNewOwner() {
  // A's request taken and applied under A, then B: B starts from the default, the mark stays A's.
  Rig r;
  r.Tick(5, 0, [&] { r.mailbox.PostBackendRequest({5, 2, kWgc}); });
  expect(r.requested == kWgc && r.appliedKey == ((5ull << 32) | 2), "N2: A's request handled under A");
  r.Tick(6);
  expect(r.requested == kDefault, "N2: the new owner B starts from the host default");
  expect(r.appliedKey == ((5ull << 32) | 2), "N2: A's mark is never re-labelled as B's");
}

void TestResumeKeepsRealEndResets() {
  Rig r;
  r.Tick(7, 0, [&] { r.mailbox.PostBackendRequest({7, 1, kWgc}); });
  r.Tick(7);  // a resume / pause / re-sent Hello: the same epoch
  expect(r.requested == kWgc, "N2: the same session (resume/pause) keeps its choice");
  r.Tick(7, 7);  // its Serve() really ended
  expect(r.requested == kDefault, "N2: the real end puts the choice back to the default (before any new session)");
  r.requested = kWgc;  // (as if set again by B below -- the end must be applied once only)
  r.Tick(7, 7);
  expect(r.requested == kWgc, "N2: an end is applied once");
}

void TestLateEndOfEarlierOwner() {
  Rig r;
  r.Tick(8);
  r.Tick(9, 0, [&] { r.mailbox.PostBackendRequest({9, 1, kWgc}); });
  r.Tick(9);
  r.Tick(9, 8);  // A's end arrives late, after B took over
  expect(r.requested == kWgc, "N2: a late end of the earlier owner leaves B's choice alone");
}

void TestClassify() {
  expect(ClassifyBackendRequest(0, 5) == BackendRequestFate::Handle, "classify: unstamped -> handle");
  expect(ClassifyBackendRequest(5, 5) == BackendRequestFate::Handle, "classify: own epoch -> handle");
  expect(ClassifyBackendRequest(4, 5) == BackendRequestFate::Drop, "classify: older -> drop");
  expect(ClassifyBackendRequest(6, 5) == BackendRequestFate::Leave, "classify: newer -> leave");
  expect(BackendRequestKey(0, 5, 3) == ((5ull << 32) | 3), "key: unstamped uses the owner");
}

}  // namespace

int main() {
  TestNewOwnerRequestPostedMidTick();
  TestOldOwnerRequestAfterNewOwner();
  TestTakeUnderOwnerThenNewOwner();
  TestResumeKeepsRealEndResets();
  TestLateEndOfEarlierOwner();
  TestClassify();
  std::printf("[host-backend-owner-test] %s failures=%d\n", gFailures == 0 ? "PASS" : "FAIL", gFailures);
  return gFailures == 0 ? 0 : 1;
}
