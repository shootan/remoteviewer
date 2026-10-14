// motion-settle r1 M1: SettleRefiner (host_settle.hpp) on a hand-made clock -- when a still screen gets
// its refinement inputs, how many, and every bound that keeps it from becoming a stream of its own.

#include "host_settle.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

using namespace remote60::native_poc;

namespace {

int gFailures = 0;
int gChecks = 0;
void expect(bool ok, const std::string& what) {
  ++gChecks;
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) ++gFailures;
}

constexpr uint64_t kMs = 1000;
SettleIdentity Id(uint64_t gen = 1) {
  SettleIdentity id;
  id.mediaEpoch = 3;
  id.inputEpoch = 5;
  id.generation = gen;
  id.attachment = 77;
  return id;
}

struct Run {
  uint32_t served = 0;
  uint64_t firstServeUs = 0, lastServeUs = 0;
  SettleEnd ended = SettleEnd::None;
  uint64_t endedUs = 0;
  bool started = false;
};

// Polls every 1 ms from `from` to `to`; each serve is accepted (NoteSubmitted) unless `refuse`.
Run Drive(SettleRefiner& s, uint64_t from, uint64_t to, const SettleIdentity& id, bool queueBusy = false,
          bool realWaiting = false, bool keyOwed = false, bool refuse = false) {
  Run r;
  for (uint64_t t = from; t <= to; t += kMs) {
    const SettleAction a = s.Poll(t, id, queueBusy, realWaiting, keyOwed);
    if (a.started) r.started = true;
    if (a.serve) {
      ++r.served;
      if (r.firstServeUs == 0) r.firstServeUs = t;
      r.lastServeUs = t;
      if (!refuse) s.NoteSubmitted();
    }
    if (a.ended != SettleEnd::None && r.ended == SettleEnd::None) {
      r.ended = a.ended;
      r.endedUs = t;
    }
  }
  return r;
}

void TestBasicEpisode() {
  SettleRefiner s;
  s.Configure(SettleConfig{});
  s.NoteRealChange(1000 * kMs, Id(), 42);
  Run early = Drive(s, 1000 * kMs, 1169 * kMs, Id());
  expect(early.served == 0 && !early.started, "M1: nothing before 170 ms of quiet");
  Run r = Drive(s, 1170 * kMs, 3000 * kMs, Id());
  expect(r.started, "M1: the episode starts after the quiet");
  expect(r.served == 8, "M1: eight refinement inputs by default (got " + std::to_string(r.served) + ")");
  expect(r.firstServeUs == 1170 * kMs, "M1: the first one at once");
  expect(r.lastServeUs - r.firstServeUs >= 7 * 33 * kMs, "M1: spaced at least 33 ms apart");
  expect(r.ended == SettleEnd::Complete && r.endedUs < 1500 * kMs, "M1: ends complete well inside its deadline");
  Run after = Drive(s, 3001 * kMs, 20000 * kMs, Id());
  expect(after.served == 0, "M1: a finished episode does not re-arm itself -- one per real change");
  expect(s.content_version() == 42, "M1: the episode is bound to the content version that armed it");
}

void TestChangeWhileWaitingRearms() {
  SettleRefiner s;
  s.Configure(SettleConfig{});
  s.NoteRealChange(0, Id(), 1);
  Drive(s, 0, 100 * kMs, Id());
  s.NoteRealChange(100 * kMs, Id(), 2);  // still typing
  Run r = Drive(s, 101 * kMs, 269 * kMs, Id());
  expect(r.served == 0, "M1: a change while waiting restarts the quiet");
  Run r2 = Drive(s, 270 * kMs, 2000 * kMs, Id());
  expect(r2.served == 8 && r2.firstServeUs == 270 * kMs, "M1: ... and the episode follows the LAST change");
}

void TestChangeWhileActiveSupersedes() {
  SettleRefiner s;
  s.Configure(SettleConfig{});
  s.NoteRealChange(0, Id(), 1);
  Run r = Drive(s, 0, 175 * kMs, Id());
  expect(r.served == 1, "M1: one refinement out before the next change");
  const SettleEnd e = s.NoteRealChange(176 * kMs, Id(), 2);
  expect(e == SettleEnd::Superseded, "M1: a new change supersedes the running episode");
  Run r2 = Drive(s, 177 * kMs, 345 * kMs, Id());
  expect(r2.served == 0, "M1: ... nothing until the new quiet");
  Run r3 = Drive(s, 346 * kMs, 3000 * kMs, Id());
  expect(r3.served == 8, "M1: ... then a full episode for the new picture");
}

void TestIdentityChangeCancels() {
  SettleRefiner s;
  s.Configure(SettleConfig{});
  s.NoteRealChange(0, Id(1), 1);
  Drive(s, 0, 175 * kMs, Id(1));
  Run r = Drive(s, 176 * kMs, 3000 * kMs, Id(2));  // a new generation (selection / reconnect)
  expect(r.served == 0 && r.ended == SettleEnd::Cancelled, "M1: a new generation cancels the episode");
  // Negative control: going back to the old identity must not revive the old episode.
  Run back = Drive(s, 3001 * kMs, 6000 * kMs, Id(1));
  expect(back.served == 0, "M1: the old episode is not revived under its old identity");
  // Armed (not yet active) and the identity moves: nothing ever starts.
  SettleRefiner a;
  a.Configure(SettleConfig{});
  a.NoteRealChange(0, Id(1), 1);
  Run ra = Drive(a, 0, 100 * kMs, Id(1));
  Run rb = Drive(a, 101 * kMs, 3000 * kMs, Id(9));
  expect(ra.served == 0 && rb.served == 0 && !rb.started, "M1: an armed episode under a changed identity never starts");
}

void TestPriorityAndDeadline() {
  for (int which = 0; which < 3; ++which) {
    SettleRefiner s;
    s.Configure(SettleConfig{});
    s.NoteRealChange(0, Id(), 1);
    Run r = Drive(s, 0, 3000 * kMs, Id(), which == 0, which == 1, which == 2);
    const char* name = which == 0 ? "a busy sender queue" : which == 1 ? "a real capture waiting" : "a key owed";
    expect(r.served == 0, std::string("M1: no refinement while ") + name);
    expect(r.ended == SettleEnd::Expired && r.endedUs == 1170 * kMs,
           std::string("M1: ... and the episode expires at its 1 s deadline (") + name + ")");
  }
}

void TestBoundsWhenRefused() {
  // The cache is refused every time (lock screen, identity mismatch inside KickTryFill): attempts are
  // bounded, the episode ends by itself.
  SettleRefiner s;
  SettleConfig c;
  c.minSpacingUs = 0;
  s.Configure(c);
  s.NoteRealChange(0, Id(), 1);
  Run r = Drive(s, 0, 5000 * kMs, Id(), false, false, false, /*refuse=*/true);
  expect(r.served == 16, "M1: refused serves stop at twice the episode's inputs (8 -> 16 attempts; got " + std::to_string(r.served) + ")");
  expect(r.ended == SettleEnd::Expired, "M1: ... and the episode says expired, not complete");
  // A configuration past the ceiling is clamped.
  SettleRefiner big;
  SettleConfig b;
  b.inputs = 50;
  b.deadlineUs = 10000000;
  b.minSpacingUs = 0;
  big.Configure(b);
  big.NoteRealChange(0, Id(), 1);
  Run rb = Drive(big, 0, 5000 * kMs, Id());
  expect(big.config().inputs == 30 && rb.served == 30 && big.config().deadlineUs == 3000000,
         "r2 A: a configuration past the ceilings is clamped -- 30 accepted inputs, a 3 s deadline");
  SettleRefiner slow;
  SettleConfig sl;
  sl.inputs = 30;
  sl.deadlineUs = 3000000;
  sl.minSpacingUs = 200000;  // 200 ms apart: 30 inputs do not fit in 3 s
  slow.Configure(sl);
  slow.NoteRealChange(0, Id(), 1);
  Run rs = Drive(slow, 0, 10000 * kMs, Id());
  expect(rs.served <= 16 && rs.ended == SettleEnd::Expired && rs.endedUs == 3170 * kMs,
         "r2 A: the 3 s deadline ends the episode whatever its input budget (" + std::to_string(rs.served) + " served)");
}

void TestByteBudget() {
  SettleRefiner s;
  SettleConfig c;
  c.inputs = 30;
  c.deadlineUs = 3000000;
  c.minSpacingUs = 0;
  s.Configure(c);
  s.SetByteBudget(100000);
  s.NoteRealChange(0, Id(), 1);
  uint32_t served = 0;
  SettleEnd ended = SettleEnd::None;
  for (uint64_t t = 0; t < 5000 * kMs && ended == SettleEnd::None; t += kMs) {
    const SettleAction a = s.Poll(t, Id(), false, false, false);
    if (a.serve) {
      ++served;
      s.NoteSubmitted();
      s.NoteEmitted(30000);  // each refinement AU 30 KB
    }
    if (a.ended != SettleEnd::None) ended = a.ended;
  }
  expect(ended == SettleEnd::Budget && served == 4, "r3: the episode ends at its byte budget (4 x 30 KB > 100 KB), whatever its input count");
}

void TestDisabled() {
  SettleRefiner s;
  SettleConfig c;
  c.enabled = false;
  s.Configure(c);
  s.NoteRealChange(0, Id(), 1);
  Run r = Drive(s, 0, 5000 * kMs, Id());
  expect(r.served == 0 && !r.started, "M1: REMOTE60_NATIVE_SETTLE=0 -- never arms (the negative control)");
}

void TestTelemetry() {
  SettleRefiner s;
  s.Configure(SettleConfig{});
  s.NoteRealChange(0, Id(), 1);
  Drive(s, 0, 2000 * kMs, Id());
  s.NoteEmitted(1000);
  s.NoteEmitted(500);
  expect(s.episodes() == 1 && s.submitted_total() == 8 && s.emitted_total() == 2 && s.emitted_bytes_total() == 1500 &&
             s.emitted_this_episode() == 2 && s.ended_total(SettleEnd::Complete) == 1,
         "M1: requested / submitted / emitted are counted apart; nothing claims 'sharp'");
}

}  // namespace

// r6: a late AU of an earlier episode goes to the totals, never to the running episode's budget.
void TestLateAuNotCharged() {
  SettleRefiner s;
  SettleConfig c;
  c.inputs = 30;
  c.deadlineUs = 3000000;
  c.minSpacingUs = 0;
  s.Configure(c);
  s.SetByteBudget(100000);
  s.NoteRealChange(0, Id(), 1);
  (void)s.Poll(200 * kMs, Id(), false, false, false);
  s.NoteEmitted(60000);
  s.NoteEmittedLate(67137);  // an IDR of the previous episode surfacing late
  expect(s.emitted_bytes_this_episode() == 60000, "r6: a late AU is not charged to the running episode");
  expect(s.emitted_bytes_total() == 127137 && s.emitted_late_total() == 1, "r6: ...but it is in the totals");
  expect(s.byte_budget() == 100000, "r6: the budget is readable for the drain's affordability check");
}

int main() {
  TestLateAuNotCharged();
  TestBasicEpisode();
  TestChangeWhileWaitingRearms();
  TestChangeWhileActiveSupersedes();
  TestIdentityChangeCancels();
  TestPriorityAndDeadline();
  TestBoundsWhenRefused();
  TestDisabled();
  TestByteBudget();
  TestTelemetry();
  std::printf("host_settle_test: %s (%d checks, %d failed)\n", gFailures == 0 ? "PASS" : "FAIL", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
