// MainLoopMailbox (Phase 4): coalescing semantics and the keyframe reason mask.
//
// The properties that matter are the ones the old `*Pending` atomics got wrong:
//  - a request's payload travels WITH the request, so a second post cannot half-overwrite the
//    first between the loop's flag exchange and its payload reads;
//  - latest-wins per kind, which is what the overwriting atomics meant;
//  - keyframe reasons ACCUMULATE, because several can arrive between two ticks and each has a
//    different follow-up. This is the one place coalescing must not simply overwrite.

#include "host_main_loop_mailbox.hpp"

#include <cstdio>
#include <thread>
#include <vector>

using namespace remote60::native_poc;

namespace {

int gFailures = 0;

void expect(bool ok, const char* what) {
  if (ok) return;
  std::printf("  FAIL %s\n", what);
  ++gFailures;
}

void test_empty() {
  std::printf("empty mailbox\n");
  MainLoopMailbox mb;
  expect(!mb.TakeSelectMonitor().has_value(), "no monitor request");
  expect(!mb.TakeCaptureMode().has_value(), "no capture-mode request");
  expect(!mb.TakeTuneEncoder().has_value(), "no tune request");
  expect(!mb.TakeBackendRequest().has_value(), "no backend request");
  expect(mb.TakeKeyframeReasons() == kKeyframeReasonNone, "no keyframe reasons");
  expect(!mb.KeyframePending(), "nothing pending");
}

void test_take_is_consume() {
  std::printf("take consumes\n");
  MainLoopMailbox mb;
  mb.PostSelectMonitor({/*epoch=*/0, /*monitorId=*/2});
  const auto first = mb.TakeSelectMonitor();
  expect(first.has_value() && first->monitorId == 2, "first take returns the request");
  expect(!mb.TakeSelectMonitor().has_value(), "second take is empty");
}

void test_payload_travels_with_request() {
  std::printf("payload travels with the request\n");
  MainLoopMailbox mb;
  mb.PostCaptureMode({/*epoch=*/0, /*seq=*/7, /*mode=*/1, 1000, 2000});
  mb.PostCaptureMode({/*epoch=*/0, /*seq=*/8, /*mode=*/2, 3000, 4000});
  const auto got = mb.TakeCaptureMode();
  expect(got.has_value(), "a request is pending");
  if (!got) return;
  // The whole second request, never a mix of the two. The old code stored seq/mode/x/y in four
  // separate atomics and then set a flag, so the loop could read seq=8 with mode=1.
  expect(got->seq == 8 && got->mode == 2 && got->xPermille == 3000 && got->yPermille == 4000,
         "latest request arrives whole");
}

void test_latest_wins_per_kind() {
  std::printf("latest-wins per kind, kinds independent\n");
  MainLoopMailbox mb;
  mb.PostSelectMonitor({/*epoch=*/0, /*monitorId=*/1});
  mb.PostBackendRequest({/*epoch=*/0, /*seq=*/10, /*backend=*/3});
  mb.PostSelectMonitor({/*epoch=*/0, /*monitorId=*/5});
  const auto mon = mb.TakeSelectMonitor();
  const auto be = mb.TakeBackendRequest();
  expect(mon.has_value() && mon->monitorId == 5, "monitor coalesced to the latest");
  expect(be.has_value() && be->backend == 3 && be->seq == 10, "backend request untouched by it");
}

void test_tune_merges_fields() {
  std::printf("tune merges fields\n");
  MainLoopMailbox mb;
  mb.PostTuneEncoder({/*epoch=*/0, /*seq=*/1, /*bitrate=*/5000000, 0, 0});  // bitrate only
  mb.PostTuneEncoder({/*epoch=*/0, /*seq=*/2, 0, /*keyint=*/120, 0});      // keyint only
  const auto got = mb.TakeTuneEncoder();
  expect(got.has_value(), "a tune request is pending");
  if (!got) return;
  // Whole-record coalescing would have dropped the bitrate: the viewer can legitimately send the
  // two settings in separate messages.
  expect(got->bitrate == 5000000, "earlier bitrate survives");
  expect(got->keyint == 120, "later keyint applied");
  expect(got->fps == 0, "untouched field stays zero");
  expect(got->seq == 2, "seq is the latest");
}

void test_keyframe_reasons_accumulate() {
  std::printf("keyframe reasons accumulate\n");
  MainLoopMailbox mb;
  mb.PostRequestKeyframe(kKeyframeReasonViewer, 42);
  mb.PostRequestKeyframe(kKeyframeReasonSenderBarrier);
  expect(mb.KeyframePending(), "pending before the take");
  uint16_t viewerReason = 0;
  const uint32_t reasons = mb.TakeKeyframeReasons(&viewerReason);
  expect((reasons & kKeyframeReasonViewer) != 0, "viewer reason kept");
  expect((reasons & kKeyframeReasonSenderBarrier) != 0, "barrier reason kept");
  expect((reasons & kKeyframeReasonSenderBacklog) == 0, "backlog reason not invented");
  expect(viewerReason == 42, "viewer wire reason carried through");
  expect(mb.TakeKeyframeReasons() == kKeyframeReasonNone, "cleared after the take");
  expect(!mb.KeyframePending(), "not pending after the take");
}

void test_clear() {
  std::printf("clear drops a departed client's requests\n");
  MainLoopMailbox mb;
  mb.PostSelectMonitor({/*epoch=*/0, /*monitorId=*/3});
  mb.PostTuneEncoder({1, 1, 1, 1});
  mb.PostRequestKeyframe(kKeyframeReasonViewer);
  mb.Clear();
  expect(!mb.TakeSelectMonitor().has_value(), "monitor cleared");
  expect(!mb.TakeTuneEncoder().has_value(), "tune cleared");
  expect(mb.TakeKeyframeReasons() == kKeyframeReasonNone, "keyframe reasons cleared");
}

// Producers really are concurrent (control thread, sender thread, UDP reader), so post from
// several at once and assert the consumer never observes a torn request.
void test_concurrent_posts() {
  std::printf("concurrent producers\n");
  MainLoopMailbox mb;
  constexpr int kIters = 2000;
  std::vector<std::thread> producers;
  producers.emplace_back([&] {
    for (int i = 0; i < kIters; ++i)
      mb.PostCaptureMode({/*epoch=*/0, static_cast<uint32_t>(i), /*mode=*/1, 1000, 2000});
  });
  producers.emplace_back([&] {
    for (int i = 0; i < kIters; ++i) mb.PostRequestKeyframe(kKeyframeReasonSenderBarrier);
  });

  int torn = 0;
  int seen = 0;
  int keyframeTakes = 0;
  for (int i = 0; i < kIters * 4; ++i) {
    if (const auto m = mb.TakeCaptureMode()) {
      ++seen;
      // Every posted record has this exact shape; a mix of two would not.
      if (m->mode != 1 || m->xPermille != 1000 || m->yPermille != 2000) ++torn;
    }
    if (mb.TakeKeyframeReasons() != kKeyframeReasonNone) ++keyframeTakes;
  }
  for (auto& t : producers) t.join();
  // Drain whatever the loop above raced past.
  if (const auto m = mb.TakeCaptureMode()) {
    ++seen;
    if (m->mode != 1 || m->xPermille != 1000 || m->yPermille != 2000) ++torn;
  }
  expect(torn == 0, "no torn capture-mode request observed");
  expect(seen > 0, "consumer saw at least one request");
  (void)keyframeTakes;
}

}  // namespace


// H-27: a request posted by a client that has since gone away must not be applied to its
// successor. Clear() covers what is still queued at rollover; this covers the request that was
// posted just before one and taken just after.
void TestStaleEpochRequestIsDropped() {
  std::printf("stale-epoch requests are dropped\n");
  MainLoopMailbox mb;
  mb.PostSelectMonitor({/*epoch=*/7, /*monitorId=*/2});
  expect(!mb.TakeSelectMonitor(/*currentEpoch=*/8).has_value(),
         "older session's request is not applied");
  expect(mb.StaleDroppedCount() == 1, "and it is counted");
  expect(!mb.TakeSelectMonitor(/*currentEpoch=*/8).has_value(),
         "nor does it linger for the next take");

  mb.PostSelectMonitor({/*epoch=*/8, /*monitorId=*/3});
  const auto same = mb.TakeSelectMonitor(/*currentEpoch=*/8);
  expect(same.has_value() && same->monitorId == 3, "same-session request is applied");

  // 0 means "unstamped": producers that predate the stamp, and the tests above, still work.
  mb.PostCaptureMode({/*epoch=*/0, /*seq=*/1, /*mode=*/1, 5000, 5000});
  expect(mb.TakeCaptureMode(/*currentEpoch=*/9).has_value(), "unstamped request is accepted");
  expect(mb.StaleDroppedCount() == 1, "no extra stale drops");
}

int main() {
  TestStaleEpochRequestIsDropped();
  test_empty();
  test_take_is_consume();
  test_payload_travels_with_request();
  test_latest_wins_per_kind();
  test_tune_merges_fields();
  test_keyframe_reasons_accumulate();
  test_clear();
  test_concurrent_posts();
  if (gFailures == 0) {
    std::printf("host_main_loop_mailbox_test: PASS\n");
    return 0;
  }
  std::printf("host_main_loop_mailbox_test: FAIL (%d)\n", gFailures);
  return 1;
}
