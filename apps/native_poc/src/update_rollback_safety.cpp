#include "update_rollback_safety.hpp"

#include <windows.h>

namespace remote60::native_poc::update {

const char* removal_obstacle_name(RemovalObstacle o) {
  switch (o) {
    case RemovalObstacle::None: return "none";
    case RemovalObstacle::RunningImage: return "running";
    case RemovalObstacle::Permission: return "permission";
    case RemovalObstacle::Other: return "other";
  }
  return "unknown";
}

const char* quiesce_decision_name(QuiesceDecision d) {
  switch (d) {
    case QuiesceDecision::Proceed: return "proceed";
    case QuiesceDecision::WaitMore: return "wait";
    case QuiesceDecision::StopRunning: return "stop-still-running";
    case QuiesceDecision::StopPermission: return "stop-permission";
    case QuiesceDecision::StopOther: return "stop-error";
  }
  return "unknown";
}

RemovalObstacle classify_removal(const RemovalProbe& probe) {
  // A file that is not there is not in the way. The rollback only restores what it moved aside,
  // so an absent live file is the ordinary case after the swap has been undone.
  if (!probe.exists) return RemovalObstacle::None;
  if (probe.removeError == 0) return RemovalObstacle::None;

  if (probe.removeError == ERROR_ACCESS_DENIED) {
    // The measured distinction. A running image grants the open and refuses the unlink; an ACL
    // refuses the open. Without this the two are one error and one response, and the response is
    // wrong for whichever of them it was not written for.
    return probe.openForDeleteOk ? RemovalObstacle::RunningImage : RemovalObstacle::Permission;
  }
  // Somebody holds it with no sharing -- not an image section, but equally not ours to remove yet.
  if (probe.removeError == ERROR_SHARING_VIOLATION ||
      probe.removeError == ERROR_LOCK_VIOLATION) {
    return RemovalObstacle::RunningImage;
  }
  return RemovalObstacle::Other;
}

QuiesceDecision quiesce_decide(const QuiesceInputs& in) {
  bool running = false;
  for (const RemovalObstacle o : in.obstacles) {
    // Checked in this order on purpose: a permission failure ends the attempt immediately, and
    // reporting it as "still running" would spend the whole budget waiting for something that was
    // never going to move.
    if (o == RemovalObstacle::Permission) return QuiesceDecision::StopPermission;
    if (o == RemovalObstacle::Other) return QuiesceDecision::StopOther;
    if (o == RemovalObstacle::RunningImage) running = true;
  }
  if (!running) return QuiesceDecision::Proceed;
  if (in.waitedMs < in.budgetMs) return QuiesceDecision::WaitMore;
  return QuiesceDecision::StopRunning;
}

}  // namespace remote60::native_poc::update
