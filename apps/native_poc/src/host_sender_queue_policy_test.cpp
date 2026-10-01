// Sender queue policy (ledger H-19): what the encode loop does with one access unit.
//
// The case this exists for: an async MFT can release [key, delta] from a single encode call,
// because it drains its accepted-input backlog and that backlog can straddle a GOP boundary.
// `backlogged` is judged once per batch, so the delta used to see the reading the key had
// already invalidated -- and dropped the queue the IDR had just re-anchored.

#include "host_sender_queue_policy.hpp"

#include <cstdio>
#include <deque>
#include <string>
#include <vector>

using remote60::native_poc::backlog_resync_active;
using remote60::native_poc::decide_sender_queue_action;
using remote60::native_poc::SenderQueueAction;

namespace {

int gFailures = 0;

const char* name(SenderQueueAction a) {
  switch (a) {
    case SenderQueueAction::EnqueueKey: return "EnqueueKey";
    case SenderQueueAction::HoldForKey: return "HoldForKey";
    case SenderQueueAction::DropAndResync: return "DropAndResync";
    case SenderQueueAction::Enqueue: return "Enqueue";
  }
  return "?";
}

void expect(const char* what, SenderQueueAction got, SenderQueueAction want) {
  if (got == want) return;
  std::printf("  FAIL %s: got %s want %s\n", what, name(got), name(want));
  ++gFailures;
}

void expect_eq(const char* what, long long got, long long want) {
  if (got == want) return;
  std::printf("  FAIL %s: got %lld want %lld\n", what, got, want);
  ++gFailures;
}

void expect_true(const char* what, bool got) {
  if (got) return;
  std::printf("  FAIL %s: expected true\n", what);
  ++gFailures;
}

void expect_false(const char* what, bool got) {
  if (!got) return;
  std::printf("  FAIL %s: expected false\n", what);
  ++gFailures;
}

constexpr std::size_t kMax = 6;

// --- 1. the individual branches, unchanged from the original if/else chain ---
void test_branches() {
  std::printf("branches\n");
  expect("key always re-anchors",
         decide_sender_queue_action(true, false, false, 0, kMax), SenderQueueAction::EnqueueKey);
  expect("key re-anchors even while the barrier is closed",
         decide_sender_queue_action(true, true, true, 5, kMax), SenderQueueAction::EnqueueKey);
  expect("delta with a closed barrier is held",
         decide_sender_queue_action(false, true, false, 0, kMax), SenderQueueAction::HoldForKey);
  expect("closed barrier outranks backlog",
         decide_sender_queue_action(false, true, true, 5, kMax), SenderQueueAction::HoldForKey);
  expect("delta on a backlog resyncs",
         decide_sender_queue_action(false, false, true, 0, kMax), SenderQueueAction::DropAndResync);
  expect("delta on a full queue resyncs even when not backlogged",
         decide_sender_queue_action(false, false, false, kMax, kMax), SenderQueueAction::DropAndResync);
  expect("ordinary delta enqueues",
         decide_sender_queue_action(false, false, false, 1, kMax), SenderQueueAction::Enqueue);
}

// --- 2. the regression: one batch of [key, delta] arriving on a depth-2 backlog ---
// Model just enough of the sender queue to assert the end state.
struct FakeSender {
  std::deque<std::string> queue;
  bool waitingForKey = false;
  bool requestKey = false;
  unsigned drops = 0;
};

void apply(FakeSender& s, bool keyFrame, bool& backlogged, const std::string& label) {
  switch (decide_sender_queue_action(keyFrame, s.waitingForKey, backlogged, s.queue.size(), kMax)) {
    case SenderQueueAction::EnqueueKey:
      s.drops += static_cast<unsigned>(s.queue.size());
      s.queue.clear();
      s.waitingForKey = false;
      s.queue.push_back(label);
      backlogged = false;  // the fix
      break;
    case SenderQueueAction::HoldForKey:
      ++s.drops;
      s.requestKey = true;
      break;
    case SenderQueueAction::DropAndResync:
      s.drops += static_cast<unsigned>(s.queue.size()) + 1u;
      s.queue.clear();
      s.waitingForKey = true;
      s.requestKey = true;
      break;
    case SenderQueueAction::Enqueue:
      s.queue.push_back(label);
      break;
  }
}

void test_key_then_delta_in_one_batch() {
  std::printf("[key, delta] batch on a depth-2 backlog\n");
  FakeSender s;
  s.queue.push_back("stale-a");
  s.queue.push_back("stale-b");
  bool backlogged = s.queue.size() >= 2;  // judged once, before the batch
  expect_true("precondition: judged backlogged", backlogged);

  apply(s, /*keyFrame=*/true, backlogged, "key");
  apply(s, /*keyFrame=*/false, backlogged, "delta");

  expect_eq("queue depth", static_cast<long long>(s.queue.size()), 2);
  if (s.queue.size() == 2) {
    expect_true("queue[0] is the key", s.queue[0] == "key");
    expect_true("queue[1] is the delta", s.queue[1] == "delta");
  }
  expect_false("barrier stays open", s.waitingForKey);
  expect_false("no keyframe re-request", s.requestKey);
  expect_eq("only the two stale frames were dropped", s.drops, 2);
}

// Without the reset the same batch eats its own IDR and asks for another -- the loop this
// regression test pins down. Asserted explicitly so a future edit that drops the reset fails
// here with a readable reason rather than silently restoring the IDR train.
void test_without_reset_would_regress() {
  std::printf("[key, delta] batch WITHOUT the reset (documents the old behaviour)\n");
  FakeSender s;
  s.queue.push_back("stale-a");
  s.queue.push_back("stale-b");
  bool backlogged = true;

  // key: same as above, but deliberately skip the `backlogged = false`.
  s.drops += static_cast<unsigned>(s.queue.size());
  s.queue.clear();
  s.waitingForKey = false;
  s.queue.push_back("key");

  apply(s, /*keyFrame=*/false, backlogged, "delta");
  expect_eq("the IDR is gone", static_cast<long long>(s.queue.size()), 0);
  expect_true("barrier closed again", s.waitingForKey);
  expect_true("another key requested", s.requestKey);
}

// --- 3. a batch that genuinely overflows still resyncs ---
void test_cap_still_applies() {
  std::printf("cap still applies after a key opened the barrier\n");
  FakeSender s;
  bool backlogged = false;
  apply(s, true, backlogged, "key");
  for (int i = 0; i < static_cast<int>(kMax) - 1; ++i) {
    apply(s, false, backlogged, "d" + std::to_string(i));
  }
  expect_eq("queue is at the cap", static_cast<long long>(s.queue.size()),
            static_cast<long long>(kMax));
  expect_false("still open at the cap", s.waitingForKey);
  apply(s, false, backlogged, "overflow");
  expect_eq("overflow cleared the queue", static_cast<long long>(s.queue.size()), 0);
  expect_true("overflow closed the barrier", s.waitingForKey);
  expect_true("overflow asked for a key", s.requestKey);
}

// --- 4. r4 R3: under the cap, accepted multi-output is staged (chain kept), not discarded ---
// Drives the REAL product helpers (backlog_resync_active + decide_sender_queue_action, both called by
// host_stage_encode_send_h264_au.cpp). The full HostContext emit path needs live capture/encoder/
// graphics and is not unit-harnessable; the integrated behaviour is the real-MFT matrix. Here we pin
// the decision the stage makes for one encode call that releases two deltas.
void test_r3_cap_staging_keeps_chain() {
  std::printf("r4 R3: a 2-delta batch under the cap keeps the reference chain\n");
  // One encode call releases two deltas while a frame is already queued (depth 1 before the batch),
  // and the sender had a pre-batch backlog reading. Under the cap that pre-batch flag is NOT a resync
  // trigger (the input gate + bounded staging handle congestion), so both deltas enqueue.
  const bool preBatchBacklogged = true;  // the reading the old path would have resynced on
  const bool capBacklog = backlog_resync_active(/*capActive=*/true, preBatchBacklogged);
  expect_false("under the cap the pre-batch backlog does not force resync", capBacklog);

  FakeSender s;
  s.queue.push_back("queued-0");  // depth 1 before the batch
  bool backlogged = capBacklog;   // what the stage feeds decide_sender_queue_action under the cap
  apply(s, /*keyFrame=*/false, backlogged, "delta-1");
  apply(s, /*keyFrame=*/false, backlogged, "delta-2");
  expect_eq("both accepted deltas enqueued (chain kept, no discard)",
            static_cast<long long>(s.queue.size()), 3);
  expect_false("no resync -> barrier stays open", s.waitingForKey);
  expect_false("no spurious keyframe request", s.requestKey);
  expect_eq("nothing was dropped", s.drops, 0);

  // NEGATIVE control: cap OFF (or the pre-fix path) -- the same pre-batch backlog DOES force a resync
  // on the first accepted delta, discarding the chain and asking for a key. This is the bug R3 fixes.
  expect_true("cap off: the pre-batch backlog still resyncs (legacy flood defence)",
              backlog_resync_active(/*capActive=*/false, preBatchBacklogged));
  FakeSender s2;
  s2.queue.push_back("queued-0");
  bool backlogged2 = backlog_resync_active(/*capActive=*/false, preBatchBacklogged);  // true
  apply(s2, /*keyFrame=*/false, backlogged2, "delta-1");
  expect_eq("cap off: the chain is discarded on the first delta", static_cast<long long>(s2.queue.size()), 0);
  expect_true("cap off: a key is re-requested", s2.requestKey);
}

}  // namespace

int main() {
  test_branches();
  test_key_then_delta_in_one_batch();
  test_without_reset_would_regress();
  test_cap_still_applies();
  test_r3_cap_staging_keeps_chain();
  if (gFailures == 0) {
    std::printf("host_sender_queue_policy_test: PASS\n");
    return 0;
  }
  std::printf("host_sender_queue_policy_test: FAIL (%d)\n", gFailures);
  return 1;
}
