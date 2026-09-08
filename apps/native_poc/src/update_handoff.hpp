#pragma once

// Handing an update over to the updater, and deciding when it is safe to get out of the way.
//
// The host is one of the processes the update replaces, so it has to exit for the swap to happen.
// The question is when, and the wrong answer is "as soon as the updater starts". If the host
// exits first and the update then fails -- server unreachable, hash mismatch, a file that will
// not move -- there is nothing left running to put the product back, and what the user sees is
// that their program is simply gone.
//
// So the order is fixed: the updater takes the lock, downloads, and verifies; only then does it
// signal; only then does the host exit. Everything before the signal is recoverable by doing
// nothing, because nothing on disk has changed yet.
//
// The other half of that rule is what happens when the signal never comes. A failure to hand over
// is not a failure of the product -- the host simply keeps running and the update does not happen
// this time. "Nothing happened" is the correct outcome of every failure on this path.
//
// Design: docs/업데이트_기능_설계.md 3.5-3.6, docs/업데이트_배선_계획.md W3.

#include <cstdint>
#include <string>
#include <vector>

namespace remote60::native_poc::update {

/** Everything the updater needs to be told, from the caller that is handing over. */
struct UpdaterLaunchSpec {
  std::wstring installDir;
  std::wstring stagingDir;
  std::wstring workDir;
  std::string manifestUrl;
  std::string platform;
  std::string installedVersion;
  std::wstring healthLogPath;
  std::wstring logPath;
  std::wstring serviceName;
  std::wstring registryRoot;
  std::wstring readyEventName;
  uint32_t parentPid = 0;
};

/**
 * The argument list for the updater, in order.
 *
 * Built here rather than by whoever is launching, so the host and the client cannot drift into
 * handing the same process different sets of arguments -- and so this is checkable without
 * starting anything.
 */
std::vector<std::wstring> updater_arguments(const UpdaterLaunchSpec& spec);

/**
 * A name for this launch's ready event, unique to it.
 *
 * Unique because a leftover event from an earlier attempt, already signalled, would tell the host
 * to exit before this updater had done anything at all -- the exact failure the handshake exists
 * to prevent, arrived at by reusing a name.
 *
 * `Local\` rather than `Global\`: the host and the updater it starts are in the same session, and
 * a name in the global namespace would be visible to every session on the machine.
 */
std::wstring make_ready_event_name(uint32_t pid, uint64_t tick);

/** What the waiting caller should do. */
enum class HandoffVerdict {
  /** The updater has the lock and a verified download. Getting out of the way is now safe. */
  ExitNow,
  /**
   * It did not get that far. Keep running.
   *
   * Covers every failure the same way on purpose: the updater died, it never signalled, it was
   * still going when the wait ran out. None of them has changed anything on disk, so the right
   * response to all of them is to carry on as though no update had been attempted.
   */
  KeepRunning,
};

const char* handoff_verdict_name(HandoffVerdict verdict);

/**
 * Judges the outcome of the wait.
 *
 * `readySignalled` wins over `updaterExited`: an updater that signalled and then exited did its
 * job in the case where the update needed no swap at all, and treating the exit as a failure
 * would keep the host running when it had already agreed to leave.
 */
HandoffVerdict handoff_verdict(bool readySignalled, bool updaterExited, bool timedOut,
                               std::string* detail);

/**
 * What a request to run something elevated came back as.
 *
 * Cancelled is separated from Failed because they are different events with different correct
 * responses. A user who declines the prompt has made a choice, and the right answer is to carry
 * on exactly as before -- not to show an error, not to retry, and certainly not to look for
 * another way to get elevated. Collapsing it into "failed" is how an application ends up telling
 * someone that the thing they just declined did not work.
 */
enum class ElevationOutcome {
  Launched,
  /** The user said no. An ordinary answer. */
  Cancelled,
  /** It could not be asked at all, or the launch itself failed. */
  Failed,
};

const char* elevation_outcome_name(ElevationOutcome outcome);

/**
 * Reads the result of an elevated launch.
 *
 * `lastError` is the Win32 error when `succeeded` is false. ERROR_CANCELLED is the one the shell
 * reports when a user dismisses the consent prompt.
 */
ElevationOutcome elevation_outcome(bool succeeded, uint32_t lastError);

}  // namespace remote60::native_poc::update
