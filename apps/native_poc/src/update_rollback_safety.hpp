#pragma once

// Whether a rollback may touch the installation yet. (updater-health-gate D2)
//
// Role:    name why a file cannot be removed, and decide whether to restore, wait, or stop.
// Thread:  none of its own; both functions are pure.
// Input:   what a probe found for each file, and how long the wait has run.
// Output:  an obstacle per file, and one decision for the attempt.
// Callers: WindowsUpdateEffects::Rollback.
//
// On 2026-09-21 a rollback deleted and restored four files, failed on two with ACCESS_DENIED, and
// reported RollbackFailed -- leaving an installation that was half one build and half the other.
// Neither half was wrong about its own file; the mistake was starting at all.
//
// The two failures it could not tell apart both report five. Measured
// (remote60_rollback_denial_probe): a running image GRANTS an open for DELETE, even with no
// sharing, and refuses only the unlink; an ACL refuses the open itself. So the open is the
// discriminator, and the two want opposite responses -- one goes away when the process does, the
// other never will.
//
// The updater stops only what it started, by the handle it was given at creation. A Host's child
// and a service it installed are not its to stop, and widening that to killing whatever holds a
// file is a different decision about trust than this one. So when something has not gone quiet,
// this stops before the first destructive step and keeps the backups, rather than producing a
// mixture and calling it a recovery.

#include <cstdint>
#include <vector>

namespace remote60::native_poc::update {

/** What a probe found about one file it may need to remove. */
struct RemovalProbe {
  bool exists = false;
  bool openForDeleteOk = false;  // CreateFile(path, DELETE, ...) was granted
  uint32_t removeError = 0;      // 0 when a removal would succeed; otherwise the win32 error
};

enum class RemovalObstacle : uint8_t {
  None = 0,      // it is gone, or it can be removed right now
  RunningImage,  // something is running it; it will go when that stops
  Permission,    // we are not allowed to remove it, and waiting will not change that
  Other,         // some other error -- reported rather than guessed at
};

const char* removal_obstacle_name(RemovalObstacle o);

RemovalObstacle classify_removal(const RemovalProbe& probe);

enum class QuiesceDecision : uint8_t {
  Proceed = 0,      // everything can be removed; the restore may start
  WaitMore,         // something is still running and there is budget left
  StopRunning,      // still running and the budget is spent
  StopPermission,   // a file we will never be allowed to remove; waiting is pointless
  StopOther,        // an error we did not expect; the same rule applies
};

const char* quiesce_decision_name(QuiesceDecision d);

struct QuiesceInputs {
  std::vector<RemovalObstacle> obstacles;
  uint64_t waitedMs = 0;
  uint64_t budgetMs = 0;
};

/**
 * One decision for the whole attempt, not one per file.
 *
 * A rollback is all or nothing: restoring the files that happen to be free while others are held
 * is exactly the mixture that made the incident worse than the false positive that caused it.
 * Permission outranks running, because no amount of waiting fixes it and the sooner the attempt
 * stops the more of the installation is left intact.
 */
QuiesceDecision quiesce_decide(const QuiesceInputs& in);

}  // namespace remote60::native_poc::update
