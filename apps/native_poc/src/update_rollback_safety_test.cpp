// Whether a rollback may start. (updater-health-gate D2)
//
// Pure: no file, no process. The probe is what touches those, and it is a seam.
//
// The incident this pins: a rollback restored four files, failed on two with ACCESS_DENIED, and
// left an installation that was half one build and half the other. Two things had to be true for
// that -- it could not tell a running image from a permission failure, and it started before it
// knew whether it could finish.

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "update_rollback_safety.hpp"

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

RemovalProbe probe(bool exists, bool openOk, uint32_t err) {
  RemovalProbe p;
  p.exists = exists;
  p.openForDeleteOk = openOk;
  p.removeError = err;
  return p;
}

const char* obstacle(const RemovalProbe& p) { return removal_obstacle_name(classify_removal(p)); }

QuiesceDecision decide(std::vector<RemovalObstacle> obstacles, uint64_t waited, uint64_t budget) {
  QuiesceInputs in;
  in.obstacles = std::move(obstacles);
  in.waitedMs = waited;
  in.budgetMs = budget;
  return quiesce_decide(in);
}

constexpr uint32_t kAccessDenied = 5;
constexpr uint32_t kSharingViolation = 32;

}  // namespace

int main() {
  std::cout << "update_rollback_safety_test\n";

  // ------------------------------------------------- five means two different things
  {
    // Measured, not assumed: a running image GRANTS the DELETE-access open and refuses only the
    // unlink; an ACL refuses the open. Before this they were one error with one response.
    check("access-denied with the open granted is a running image",
          classify_removal(probe(true, true, kAccessDenied)) == RemovalObstacle::RunningImage,
          obstacle(probe(true, true, kAccessDenied)));
    check("access-denied with the open refused is a permission problem",
          classify_removal(probe(true, false, kAccessDenied)) == RemovalObstacle::Permission,
          obstacle(probe(true, false, kAccessDenied)));
    check("...and those are different words in the log",
          std::string(removal_obstacle_name(RemovalObstacle::RunningImage)) !=
              removal_obstacle_name(RemovalObstacle::Permission));

    check("a sharing violation is something to wait for",
          classify_removal(probe(true, false, kSharingViolation)) == RemovalObstacle::RunningImage,
          obstacle(probe(true, false, kSharingViolation)));
    check("an unexpected error is reported as itself, not guessed at",
          classify_removal(probe(true, false, 1234)) == RemovalObstacle::Other,
          obstacle(probe(true, false, 1234)));

    check("a file that is not there is not in the way",
          classify_removal(probe(false, false, kAccessDenied)) == RemovalObstacle::None,
          obstacle(probe(false, false, kAccessDenied)));
    check("a file that can be removed is not in the way",
          classify_removal(probe(true, true, 0)) == RemovalObstacle::None,
          obstacle(probe(true, true, 0)));
  }

  // ------------------------------------------------- one decision for the whole attempt
  {
    check("everything free means the restore may start",
          decide({RemovalObstacle::None, RemovalObstacle::None}, 0, 10000) ==
              QuiesceDecision::Proceed);
    check("nothing to check at all also proceeds", decide({}, 0, 10000) == QuiesceDecision::Proceed);

    check("one running file holds the whole attempt back",
          decide({RemovalObstacle::None, RemovalObstacle::RunningImage}, 0, 10000) ==
              QuiesceDecision::WaitMore,
          quiesce_decision_name(decide({RemovalObstacle::None, RemovalObstacle::RunningImage}, 0, 10000)));
    check("...and when the budget is spent it stops rather than starting",
          decide({RemovalObstacle::RunningImage}, 10000, 10000) == QuiesceDecision::StopRunning,
          quiesce_decision_name(decide({RemovalObstacle::RunningImage}, 10000, 10000)));

    // The reason permission is separated from running at all: waiting cannot help, and every
    // second of it is a second the product is down.
    check("a permission failure is not waited on",
          decide({RemovalObstacle::Permission}, 0, 10000) == QuiesceDecision::StopPermission,
          quiesce_decision_name(decide({RemovalObstacle::Permission}, 0, 10000)));
    check("...even when something else is merely running",
          decide({RemovalObstacle::RunningImage, RemovalObstacle::Permission}, 0, 10000) ==
              QuiesceDecision::StopPermission,
          quiesce_decision_name(
              decide({RemovalObstacle::RunningImage, RemovalObstacle::Permission}, 0, 10000)));
    check("an unexpected error stops too",
          decide({RemovalObstacle::Other}, 0, 10000) == QuiesceDecision::StopOther);

    // A budget of zero is "do not wait", not "wait forever".
    check("a zero budget stops at once rather than looping",
          decide({RemovalObstacle::RunningImage}, 0, 0) == QuiesceDecision::StopRunning,
          quiesce_decision_name(decide({RemovalObstacle::RunningImage}, 0, 0)));

    // Every decision has to be distinguishable in a log, or the next investigation starts where
    // the last one did.
    const QuiesceDecision all[] = {QuiesceDecision::Proceed,        QuiesceDecision::WaitMore,
                                   QuiesceDecision::StopRunning,    QuiesceDecision::StopPermission,
                                   QuiesceDecision::StopOther};
    bool distinct = true;
    for (size_t i = 0; i < 5; ++i) {
      for (size_t j = i + 1; j < 5; ++j) {
        if (std::string(quiesce_decision_name(all[i])) == quiesce_decision_name(all[j])) {
          distinct = false;
        }
      }
    }
    check("every decision has its own word", distinct);
  }

  // ------------------------------------------------- the shape the incident took
  {
    // Four files free, two held by processes the updater never started. The old code restored the
    // four and failed on the two. The decision now is one: do not start.
    const std::vector<RemovalObstacle> incident = {
        RemovalObstacle::None,         RemovalObstacle::None, RemovalObstacle::None,
        RemovalObstacle::None,         RemovalObstacle::RunningImage,
        RemovalObstacle::RunningImage};
    check("the 2026-09-21 shape waits rather than half restoring",
          decide(incident, 0, 10000) == QuiesceDecision::WaitMore,
          quiesce_decision_name(decide(incident, 0, 10000)));
    check("...and refuses to start when they never go quiet",
          decide(incident, 10001, 10000) == QuiesceDecision::StopRunning,
          quiesce_decision_name(decide(incident, 10001, 10000)));
  }

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED")
            << "  (" << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
