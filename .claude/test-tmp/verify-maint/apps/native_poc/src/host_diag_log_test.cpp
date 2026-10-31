// The decisions inside the host's connect diagnostics (host_diag_log.hpp).
//
// Pure: no socket, no clock, no server. What these decide is what a reader of the log will
// conclude, which is why they are worth pinning -- a diagnostic that names the wrong cause is read
// as evidence and sends the next investigation the wrong way.
//
// What is NOT covered here, said plainly: the cycle lines in HostAgent::Run and the media-socket
// line at bind are unconditional single-line emissions with nothing to decide. They are verified by
// reading and by the build. Everything with a branch is below.

#include <cstdint>
#include <iostream>
#include <string>

#include "host_diag_log.hpp"

using namespace remote60::native_poc;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

}  // namespace

int main() {
  std::cout << "host_diag_log_test\n";

  // ---------------------------------------------------------------- why a hello was refused
  {
    // The case the 2026-09-21 failure was, every time: the host had nothing, because the capability
    // had been minted and not yet collected. Before this the log said "invalid directory
    // capability", which reads as though the capability were wrong.
    RejectFacts none;
    check("a host holding nothing says so", std::string(reject_reason(none)) == "none-held",
          reject_reason(none));

    RejectFacts expired;
    expired.expiredNow = 2;
    check("...and distinguishes having just thrown some away",
          std::string(reject_reason(expired)) == "expired", reject_reason(expired));

    RejectFacts mismatch;
    mismatch.held = 2;
    check("a host holding others names a mismatch, not an absence",
          std::string(reject_reason(mismatch)) == "no-match", reject_reason(mismatch));

    RejectFacts consumed;
    consumed.matched = true;
    consumed.held = 1;
    check("a capability that matched is never called missing",
          std::string(reject_reason(consumed)) == "consumed", reject_reason(consumed));

    RejectFacts moved;
    moved.matched = true;
    moved.endpointMoved = true;
    check("...and a match from a different tuple says which",
          std::string(reject_reason(moved)) == "peer-moved", reject_reason(moved));

    // The four names have to stay distinct, or the distinction is decorative.
    RejectFacts a, b, c2, d;
    b.expiredNow = 1;
    c2.held = 1;
    d.matched = true;
    const std::string names[] = {reject_reason(a), reject_reason(b), reject_reason(c2),
                                 reject_reason(d)};
    bool distinct = true;
    for (size_t i = 0; i < 4; ++i)
      for (size_t j = i + 1; j < 4; ++j)
        if (names[i] == names[j]) distinct = false;
    check("the reasons are four different words", distinct,
          names[0] + "," + names[1] + "," + names[2] + "," + names[3]);
  }

  // ---------------------------------------------------------------- a run of refusals, thinned
  {
    check("the first of a run is printed", reject_should_emit(1));
    check("the second is not", !reject_should_emit(2));
    check("...nor the thirty-first", !reject_should_emit(31));
    check("the thirty-second is", reject_should_emit(32));
    check("...and so is the sixty-fourth", reject_should_emit(64));
    check("a run of zero prints nothing", !reject_should_emit(0));

    // Over a long run the rate is bounded, which is the point: a client repeating its hello for a
    // minute must not be able to set the size of the log.
    int printed = 0;
    for (uint64_t n = 1; n <= 1000; ++n) if (reject_should_emit(n)) ++printed;
    check("a thousand refusals produce about thirty lines", printed > 20 && printed < 40,
          std::to_string(printed) + " lines");
  }

  // ---------------------------------------------------------------- punch arrivals, bounded
  {
    PunchLogWindow w;
    const uint64_t t0 = 1000000000ULL;

    // The first ten in a second go out.
    int emitted = 0;
    for (int i = 0; i < 10; ++i) if (punch_log_decide(&w, t0 + i * 1000).emit) ++emitted;
    check("ten arrivals in a second are all printed", emitted == 10, std::to_string(emitted));

    // The eleventh is not, and is counted.
    PunchLogDecision over = punch_log_decide(&w, t0 + 11000);
    check("the eleventh is held back", !over.emit);
    check("...but still counted", over.total == 11, std::to_string(over.total));

    for (int i = 0; i < 5; ++i) punch_log_decide(&w, t0 + 12000 + i * 1000);

    // The next window opens and the first line through it reports what was missed.
    PunchLogDecision next = punch_log_decide(&w, t0 + 1000000);
    check("a new second prints again", next.emit);
    check("...and says how many were skipped", next.skippedSince == 6,
          std::to_string(next.skippedSince));
    check("...and the total counts every arrival, printed or not", next.total == 17,
          std::to_string(next.total));

    // Cleared once reported, so the same skips are not claimed twice.
    PunchLogDecision after = punch_log_decide(&w, t0 + 1001000);
    check("a reported skip count is not repeated", after.skippedSince == 0,
          std::to_string(after.skippedSince));

    // A flood cannot raise the line rate above the ceiling.
    PunchLogWindow flood;
    int lines = 0;
    for (int i = 0; i < 5000; ++i) if (punch_log_decide(&flood, t0 + i).emit) ++lines;
    check("five thousand arrivals inside one second print ten lines", lines == 10,
          std::to_string(lines));
    check("...and all five thousand are still counted", flood.total == 5000,
          std::to_string(flood.total));

    // A null window is a no-op rather than a crash: this runs on a UDP path.
    check("a missing window emits nothing", !punch_log_decide(nullptr, t0).emit);
  }

  // ---------------------------------------------------------------- why the cycle woke
  {
    check("a punch wakes it as a refresh",
          std::string(cycle_wake_cause(true, true)) == "refresh");
    check("a full sleep wakes it as elapsed",
          std::string(cycle_wake_cause(false, true)) == "elapsed");
    check("shutdown outranks both", std::string(cycle_wake_cause(true, false)) == "stop");
    check("...either way", std::string(cycle_wake_cause(false, false)) == "stop");
  }

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED")
            << "  (" << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
