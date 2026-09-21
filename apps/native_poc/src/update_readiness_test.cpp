// What the updater will and will not believe about a relaunched Host. (updater-health-gate D1, item 2)
//
// Pure: no file, no process, no clock. Every refusal below is a way the old gate could be fooled or
// starved, so each one is checked on its own -- a judge that accepted everything would pass a test
// that only ever handed it good claims.

#include <cstdint>
#include <iostream>
#include <string>

#include "update_readiness.hpp"

using namespace remote60::native_poc::update;

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

ReadinessClaim good_claim() {
  ReadinessClaim c;
  c.nonce = "a1b2c3d4e5f60718";
  c.version = "0.2.132";
  c.pid = 20508;
  c.createTimeQw = 133700000000000000ULL;
  c.writtenAtMs = 1000;
  c.valid = true;
  return c;
}

ReadinessExpectation matching() {
  ReadinessExpectation e;
  e.nonce = "a1b2c3d4e5f60718";
  e.version = "0.2.132";
  e.pid = 20508;
  e.createTimeQw = 133700000000000000ULL;
  e.nowMs = 3000;
  e.maxAgeMs = 30000;
  e.processAlive = true;
  return e;
}

const char* name_of(ReadinessVerdict v) { return readiness_verdict_name(v); }

}  // namespace

int main() {
  std::cout << "update_readiness_test\n";

  // ------------------------------------------------------------------ the case it exists for
  check("this attempt's own fresh claim is accepted",
        readiness_judge(good_claim(), matching()) == ReadinessVerdict::Accept,
        name_of(readiness_judge(good_claim(), matching())));

  // ------------------------------------------------------------------ each refusal, separately
  {
    // A file left by a previous attempt. This is the one a log-line gate had no way to notice at
    // all, because a line in a log has nothing that says which attempt it belonged to.
    ReadinessClaim c = good_claim();
    c.nonce = "0000000000000000";
    check("a claim from another attempt is refused",
          readiness_judge(c, matching()) == ReadinessVerdict::WrongNonce,
          name_of(readiness_judge(c, matching())));

    // And the updater must never be able to ask for "any nonce will do".
    ReadinessExpectation e = matching();
    e.nonce.clear();
    check("an empty expectation matches nothing rather than everything",
          readiness_judge(good_claim(), e) == ReadinessVerdict::WrongNonce,
          name_of(readiness_judge(good_claim(), e)));
  }
  {
    ReadinessClaim c = good_claim();
    c.version = "0.2.131";
    check("the right attempt reporting the wrong build is refused",
          readiness_judge(c, matching()) == ReadinessVerdict::WrongVersion,
          name_of(readiness_judge(c, matching())));
  }
  {
    // A reused pid carrying the right nonce is exactly what a stale file looks like after a
    // restart, which is why the creation time is part of the identity.
    ReadinessClaim c = good_claim();
    c.createTimeQw = 133700000000000001ULL;
    check("the same pid with a different creation time is a different process",
          readiness_judge(c, matching()) == ReadinessVerdict::WrongProcess,
          name_of(readiness_judge(c, matching())));

    ReadinessClaim other = good_claim();
    other.pid = 20512;
    check("a different pid is refused too",
          readiness_judge(other, matching()) == ReadinessVerdict::WrongProcess,
          name_of(readiness_judge(other, matching())));
  }
  {
    ReadinessClaim c = good_claim();
    c.writtenAtMs = 1000;
    ReadinessExpectation e = matching();
    e.nowMs = 1000 + e.maxAgeMs;
    check("a claim exactly at the budget is still this attempt's",
          readiness_judge(c, e) == ReadinessVerdict::Accept, name_of(readiness_judge(c, e)));
    e.nowMs = 1000 + e.maxAgeMs + 1;
    check("...and one past it is stale", readiness_judge(c, e) == ReadinessVerdict::Stale,
          name_of(readiness_judge(c, e)));

    // Forwards as well: a claim from the future is not this attempt's either.
    //
    // With an ordinary budget the age subtraction wraps around and reports Stale by itself, so a
    // test using one cannot tell whether the forward check is there. A large budget makes the
    // forward check the deciding branch: the wrapped value is no longer bigger than the budget,
    // and without an explicit refusal the claim would be accepted.
    ReadinessClaim ahead = good_claim();
    ahead.writtenAtMs = matching().nowMs + 1;
    check("a claim written in the future is refused",
          readiness_judge(ahead, matching()) == ReadinessVerdict::Stale,
          name_of(readiness_judge(ahead, matching())));
    ReadinessExpectation generous = matching();
    generous.maxAgeMs = UINT64_MAX;
    check("...even when the budget is large enough to hide the wraparound",
          readiness_judge(ahead, generous) == ReadinessVerdict::Stale,
          name_of(readiness_judge(ahead, generous)));
  }
  {
    // Ready and then dead. A file cannot retract itself, so liveness is asked at judging time.
    ReadinessExpectation e = matching();
    e.processAlive = false;
    check("a host that reported readiness and then exited is refused",
          readiness_judge(good_claim(), e) == ReadinessVerdict::NotAlive,
          name_of(readiness_judge(good_claim(), e)));
  }
  {
    ReadinessClaim c = good_claim();
    c.valid = false;
    check("nothing readable is malformed, not accepted",
          readiness_judge(c, matching()) == ReadinessVerdict::Malformed,
          name_of(readiness_judge(c, matching())));
  }

  // Identity is reported before freshness: a leftover file and a slow host are different problems
  // and the log has to say which.
  {
    ReadinessClaim c = good_claim();
    c.nonce = "ffffffffffffffff";
    c.writtenAtMs = 0;
    ReadinessExpectation e = matching();
    e.nowMs = 999999;
    check("a stale claim from another attempt is reported as the other attempt",
          readiness_judge(c, e) == ReadinessVerdict::WrongNonce, name_of(readiness_judge(c, e)));
  }

  // ------------------------------------------------------------------ the on-disk form
  {
    const ReadinessClaim original = good_claim();
    const std::string text = serialize_readiness(original);
    const ReadinessClaim back = parse_readiness(text);
    check("a claim survives the round trip", back.valid);
    check("...with every field intact",
          back.nonce == original.nonce && back.version == original.version &&
              back.pid == original.pid && back.createTimeQw == original.createTimeQw &&
              back.writtenAtMs == original.writtenAtMs);
    check("...and the round trip is accepted",
          readiness_judge(back, matching()) == ReadinessVerdict::Accept);

    // A torn write. The marker is written last, so a file cut short has no "end" and must not
    // parse as a claim carrying defaults.
    for (size_t cut = 1; cut < text.size(); ++cut) {
      if (parse_readiness(text.substr(0, cut)).valid) {
        check("a truncated claim must not parse", false, "cut at " + std::to_string(cut));
        break;
      }
    }
    check("no prefix of a claim parses as a whole one", true);

    check("an empty file is malformed", !parse_readiness("").valid);
    check("noise is malformed", !parse_readiness("hello\nworld\nend\n").valid);

    // A missing field must not become a default that makes the comparison vacuous.
    check("a claim with no pid is malformed",
          !parse_readiness("nonce=a\nversion=b\ncreated=1\nwrittenMs=2\nend\n").valid);
    check("a claim with no nonce is malformed",
          !parse_readiness("version=b\npid=1\ncreated=1\nwrittenMs=2\nend\n").valid);
  }

  // ------------------------------------------------------------------ the names reach the log
  {
    // Six refusals with six different words: a log that called them all "refused" would leave the
    // next investigation exactly where the last one started.
    const ReadinessVerdict all[] = {ReadinessVerdict::Accept,      ReadinessVerdict::Malformed,
                                    ReadinessVerdict::WrongNonce,  ReadinessVerdict::WrongVersion,
                                    ReadinessVerdict::WrongProcess, ReadinessVerdict::Stale,
                                    ReadinessVerdict::NotAlive};
    bool distinct = true;
    for (size_t i = 0; i < 7; ++i) {
      for (size_t j = i + 1; j < 7; ++j) {
        if (std::string(readiness_verdict_name(all[i])) == readiness_verdict_name(all[j])) {
          distinct = false;
        }
      }
    }
    check("every verdict has its own word", distinct);
  }

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED")
            << "  (" << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
