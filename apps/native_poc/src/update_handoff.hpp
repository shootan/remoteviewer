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
#include <functional>
#include <string>
#include <vector>

namespace remote60::native_poc::update {

/** Everything the updater needs to be told, from the caller that is handing over. */
struct UpdaterLaunchSpec {
  std::wstring installDir;
  std::wstring stagingDir;
  std::wstring workDir;
  std::string manifestUrl;
  /** See UpdaterOptions::derivedEndpoint -- the wire shape travels with the url. */
  bool derivedEndpoint = false;
  /** The name of the pipe the credential will be served on. A name, never the credential. */
  std::wstring credentialPipeName;
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

/**
 * The acknowledgement event that belongs to a ready event.
 *
 * Derived from the ready name rather than passed separately, so the two cannot be given to
 * different attempts. One attempt, one stem, two events.
 *
 * The acknowledgement is what makes the handshake two-sided. Signalling ready tells the host the
 * download is verified; it does not tell the UPDATER that anyone heard. Without an answer the
 * updater goes on to stop the product on the assumption that someone is leaving, and if nobody
 * was listening -- the host gave up, the host was never there, the event had already been closed
 * -- it stops a product that nothing is going to bring back for a swap nobody agreed to.
 */
std::wstring make_ack_event_name(const std::wstring& readyEventName);

/**
 * The name of the mutex the working copy holds for as long as it is alive.
 *
 * A mutex rather than an event, because the property wanted is "this process still exists" and
 * that is exactly what a mutex handle gives for free: when the holder dies, the wait is released
 * -- abandoned, but released. Nothing has to remember to signal anything on the way out, which
 * matters because the interesting death is the one that does not get to run any cleanup.
 *
 * The BOOTSTRAP must not hold it. It exits immediately by design, and a name held by the process
 * whose exit means nothing would say the worker had died every single time.
 *
 * WHAT IT DOES NOT COVER, stated because the measured number invites a wider claim than it earns:
 *
 *   * A death BEFORE the mutex is taken. There is no object yet, so there is nothing to be
 *     released, and the wait falls through to the timeout exactly as it did before. The window is
 *     from the working copy starting to its first few instructions -- small, and not zero.
 *   * It follows the THREAD that holds it, not the process. That is the same thing only because
 *     the working copy takes it on its main thread and holds it to exit. If that ever stops being
 *     true -- taken on a worker thread, released early -- this reports a death that has not
 *     happened, which is worse than missing one.
 *
 * So it is a contract, not a property of the operating system: the holder must be the process
 * whose life is being reported, and must take it as early as it can.
 */
std::wstring make_alive_mutex_name(const std::wstring& readyEventName);

/** What the waiting caller should do NEXT, which is not always a final answer. */
enum class HandoffStep {
  /**
   * Nothing has been decided. Keep waiting.
   *
   * This is the case that did not exist, and its absence was the defect. The process the host
   * starts is a BOOTSTRAP: it copies the updater into a working directory, launches that copy and
   * exits immediately, because it cannot hold open the file the update has to replace. Its exit is
   * the normal, successful path -- and the host was treating it as the updater dying.
   *
   * So the host told the user "the installed version is unchanged and you can carry on", closed
   * the ready event, and stopped listening. The copy, meanwhile, was downloading perfectly well;
   * it then signalled into an event nobody held and stopped the very host that had just promised
   * nothing would happen. For a remote user that is being told the update was cancelled and then
   * losing the machine.
   */
  KeepWaiting,
  /** The updater has a verified download. Getting out of the way is now safe. */
  ExitNow,
  /** It is not going to happen this time. Carry on as though nothing had been attempted. */
  KeepRunning,
};

const char* handoff_step_name(HandoffStep step);

/**
 * What to do when the wait wakes up.
 *
 * `bootstrapExitCode` is only read when `bootstrapExited` is true. Zero means it handed over and
 * left on purpose; anything else means it failed before there was a worker to wait for, and there
 * is nothing more to wait for either.
 */
HandoffStep handoff_step(bool readySignalled, bool bootstrapExited, uint32_t bootstrapExitCode,
                         bool timedOut, std::string* detail);

/**
 * Whether the updater may go on to stop the product.
 *
 * Both halves are required. The signal has to have been DELIVERED -- an event that could not be
 * opened means nobody is waiting -- and it has to have been ANSWERED, because a host that has
 * already given up and told the user so must not then be shut down by a late arrival.
 *
 * Returning false here is not a failure of the update. Nothing has been touched at this point, so
 * the correct outcome is that no update happens and everything keeps running.
 */
bool may_stop_the_product(bool signalDelivered, bool acknowledged, std::string* detail);

/**
 * Runs the wait to a decision, and acknowledges when the decision is to stand down.
 *
 * Handles are `void*` so this header stays free of windows.h; they are HANDLEs. `bootstrap` may be
 * null when there is nothing to watch besides the event.
 *
 * Lives here, and not inside the caller, because a wait that can only be run by starting the
 * product is a wait nobody runs. This one was wrong for exactly as long as that was true of it.
 */
/**
 * Milliseconds left of `timeoutMs`, given a monotonic clock.
 *
 * Its own function so the arithmetic can be tested at the values that break it. The wait used to
 * compute `deadline = GetTickCount() + timeoutMs` and compare -- both 32-bit, so on a machine up
 * for 49.7 days the sum wraps past zero, `deadline > now` is false immediately, and the wait ends
 * at once reporting that the updater ran out of time. It would have been reported as a flaky
 * update on long-lived machines and nothing else.
 *
 * `startedAt` and `now` come from a 64-bit monotonic source. Zero means the time is up.
 */
uint32_t remaining_ms(uint64_t startedAt, uint64_t now, uint32_t timeoutMs);

/**
 * Runs the wait to a decision, and acknowledges when the decision is to stand down.
 *
 * Handles are `void*` so this header stays free of windows.h; they are HANDLEs. `bootstrap` may be
 * null when there is nothing to watch besides the event.
 *
 * `now` supplies the monotonic clock, so a test can run this at values a real machine would take
 * weeks to reach. Empty uses the real one.
 *
 * ExitNow is returned ONLY when the acknowledgement was actually delivered. A caller that cannot
 * answer must not leave: the updater is waiting to be told it may go ahead, and a caller that
 * departs without answering leaves it with a product it is not allowed to stop and nobody to
 * stop it for.
 */
/**
 * `ownerStillValid` is asked once, immediately before the acknowledgement is sent, and never
 * after. Returning false withholds the acknowledgement, so the updater stands down without
 * stopping anything.
 *
 * It exists because "the owner was right when we launched" is a different statement from "the
 * owner is right now". A download takes minutes; a user can sign out, sign in as somebody else,
 * or repoint the client at another server while it runs. The acknowledgement is the last moment
 * anything can be withheld -- after it, the updater stops the product and swaps files.
 *
 * Empty means no owner condition, which is the behaviour this had before.
 */
HandoffStep await_handoff(void* readyEvent, void* ackEvent, void* bootstrap,
                          const std::wstring& readyName, uint32_t timeoutMs,
                          const std::function<uint64_t()>& now, std::string* detail,
                          const std::function<bool()>& ownerStillValid = {});

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
