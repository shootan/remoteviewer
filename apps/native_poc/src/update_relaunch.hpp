#pragma once

// Actually bringing the product back, and actually reading whether it came up.
//
// This is the second translation unit in the updater that can reach the real installation, after
// update_process_targets.cpp, and it is kept separate for the same reason: it creates processes,
// starts a service, and reads the live host log. No test binary links it. update_relaunch_plan.cpp
// and update_health.cpp hold the decisions, are linked into tests, and cannot do any of this.
//
// The two things worth stating before the declarations.
//
// A client started from an elevated updater would inherit an administrator token and keep it for
// the rest of its life -- every dialog, every child process. So it is not started as a child at
// all; it goes through the shell, which holds the ordinary user token. When that route is not
// available the client is reported as not relaunched. There is no fallback to starting it as a
// child, because that fallback is exactly the outcome being avoided.
//
// A health check that scans the whole log will accept a banner written by a previous run,
// including one written by the version being replaced. So the log offset is recorded BEFORE the
// relaunch and only what is appended after it is ever read.
//
// Design: docs/업데이트_기능_설계.md 3.5 (non-elevated client), 3.6 (Relaunch, Health).

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "update_effects.hpp"
#include "update_health_log.hpp"
#include "update_relaunch_plan.hpp"
#include "update_state_machine.hpp"

namespace remote60::native_poc::update {

/**
 * What happened to one entry of the plan.
 *
 * Structured rather than a line of text, and structured rather than folded into one bool for the
 * whole relaunch, because the failures are not equivalent. A client that did not come back is an
 * inconvenience -- the user starts it from the Start menu. A HOST that did not come back locks a
 * remote user out of the machine entirely: there is no way in to fix it. Reporting both as
 * `false` loses exactly the distinction that decides how bad the situation is.
 */
struct RelaunchOutcome {
  std::wstring imageName;
  RelaunchKind kind = RelaunchKind::ElevatedProcess;
  /** True only when the thing is actually running now. Skipped entries are not "started". */
  bool started = false;
  /**
   * True when the check could not answer.
   *
   * Not started, and not silently counted as fine: something that might be down is not something
   * to report as back.
   */
  bool livenessUnknown = false;
  /**
   * True when it never left, so there was nothing to bring back.
   *
   * Distinct from `started`: relaunching something that is still running produces a second
   * instance. Abandoning after a caller was released is exactly the case -- some of the product
   * may have gone and some may not, and capturing an identity is not the same as that process
   * having exited.
   */
  bool alreadyRunning = false;
  /** The process this attempt started, when it started one AND can prove which. 0 otherwise. */
  uint32_t startedPid = 0;
  /**
   * True when something was started but this attempt cannot say which process it is.
   *
   * Started and owned are different facts, and they were being conflated. A launch routed through
   * the shell is performed by the shell, so nothing comes back: no handle, no PID. Comparing
   * process snapshots taken either side of the call looks like it recovers the PID, and it does
   * not -- a user starting the same program at that moment produces exactly the same difference,
   * and the handle opened on that basis pins a stranger's identity rather than establishing ours.
   *
   * So it is recorded as unknown. Nothing unknown is terminated, and a rollback does not begin
   * while one exists, because a rollback moves files this process may still be holding.
   */
  bool ownershipUnknown = false;
  /** True when the plan deliberately did not start it (a supervised child). Not a failure. */
  bool skipped = false;
  /** Why, for the log and for what the user is told. */
  std::string detail;

  /** A failure that matters: something that should have come back did not. */
  bool failed() const { return !started && !skipped && !alreadyRunning; }
  /** Whether the machine needs this one. A host or a service, not a client. */
  bool required() const {
    return kind == RelaunchKind::ElevatedProcess || kind == RelaunchKind::Service;
  }
};

struct RelaunchConfig {
  /** Where the product lives. Entries are started from here, never from a path in a manifest. */
  std::wstring installDir;
  /** The SCM name of the input service. Empty means a Service entry cannot be started. */
  std::wstring serviceName;
  /** The log the product writes its health report into. */
  std::wstring healthLogPath;
  /**
   * The image whose report counts as evidence of health.
   *
   * Named rather than assumed, because it decides what happens when that image was not running
   * before the update. An update started from the client on a machine where the host is not
   * running relaunches no host, so no host will ever write a report -- and a health check that
   * waited for one anyway would time out and roll back a perfectly good update. Demanding
   * evidence from a process that was never there is inventing a failure, in the same way that
   * treating "no account signed in" as a directory failure would.
   *
   * Empty means nothing reports, and health is then satisfied by the swap having succeeded.
   */
  std::wstring healthReporterImage;
  /** The version the update installed. A report naming a different one is a failure. */
  std::string expectedVersion;
  /** How long HealthCheck waits for the report before giving up. */
  uint32_t healthTimeoutMs = 30000;
  /** How often it looks. */
  uint32_t healthPollMs = 500;

  /**
   * Whether a captured target is still running under the same identity.
   *
   * Three answers, not two, and the third is the point: a check that could not be completed must
   * not be read as "it exited". Assuming departure relaunches something that is still there and
   * leaves two of it; assuming presence leaves a machine down while reporting success. Neither is
   * safe to guess, so Unknown is carried through and refused.
   *
   * Injected so a test can answer without real processes. When it is empty the real check is
   * used -- it used to default to "everything left", which meant PRODUCTION never performed this
   * check at all while the tests that exercised it passed.
   */
  enum class Liveness { Running, Exited, Unknown };
  std::function<Liveness(const ProcessTarget&)> isStillRunning;

  /**
   * Starts an image in the interactive user's context and hands back a handle to the process.
   *
   * The handle is the whole point. The client must not be started with the updater's
   * administrator token -- it would run with rights it has never had -- so it has to be launched
   * as the logged-on user; and the route that does that through the shell performs the launch in
   * another process and returns nothing. Without a handle there is no way to say which process
   * was the one asked for, and everything downstream that stops or waits on it is guesswork.
   *
   * The real implementation duplicates the shell's own token and calls CreateProcessWithTokenW,
   * which returns a PROCESS_INFORMATION like any other launch. It needs SeImpersonatePrivilege,
   * which the updater has because it runs elevated, and which is exactly why the updater is the
   * right place to do this.
   *
   * Empty means the real one -- not "skip it". An optional seam here would be the same defect as
   * the liveness check that production never filled in: switched off in the only build that
   * matters, while every test passed.
   *
   * `processHandleOut` receives a handle the caller owns. `pidOut` receives its PID.
   */
  std::function<bool(const std::wstring& exePath, const std::wstring& workDir,
                     void** processHandleOut, uint32_t* pidOut)>
      launchInUserContext;
};

/**
 * The `relaunch` and `healthCheck` callbacks for UpdateEffectsConfig, built together.
 *
 * Together because they share one piece of state: the size the log had before anything was
 * started. Building them separately would make it possible to wire a health check that reads a
 * previous run's lines, which is the failure mode that makes a health check worthless.
 */
struct RelaunchEffects {
  /**
   * Brings back what was stopped, and says what it managed.
   *
   * Returns a verdict rather than a bool because the caller has to treat a missing host
   * differently from a missing client -- see RelaunchVerdict.
   */
  std::function<RelaunchVerdict()> relaunch;
  std::function<bool()> healthCheck;
  /** What stopping the processes this attempt started achieved. */
  struct StopReport {
    int stopped = 0;
    /**
     * Things this attempt started that it could not prove it owns, or could not stop.
     *
     * Non-empty means a rollback must not begin. Restoring files while something may still be
     * holding them produces a half-restored installation, and the reason will look like the
     * rollback's fault rather than like this.
     */
    std::vector<std::wstring> unstoppable;
    bool complete() const { return unstoppable.empty(); }
  };

  /**
   * Stops the processes THIS attempt started.
   *
   * Ownership is proved by the handle received when the process was created, not by matching a
   * pid to an image name. A name match is not ownership: a pid can be reused, and another process
   * of the same name may be someone else's. Anything started through the shell has no such handle
   * -- the shell created it, not us -- so it is reported as unstoppable rather than killed on a
   * guess.
   */
  std::function<StopReport()> stopStarted;

  /** What the last relaunch did, per entry. Empty until relaunch() has run. */
  std::function<std::vector<RelaunchOutcome>()> lastOutcomes;
  /**
   * One sentence for the user when something did not come back, empty when everything did.
   *
   * Exists because the worst version of this failure is the silent one: the update finishes, a
   * program the user had open never reappears, and nothing says why. They conclude the update
   * deleted it.
   */
  std::function<std::string()> userNotice;
  /** Why the last health check ended as it did. */
  std::function<std::string()> lastHealthDetail;
};

/**
 * Builds them for a set of processes that were stopped.
 *
 * `stopped` is what Quiesce actually stopped, so the plan describes the configuration that was
 * running -- not everything the product could run.
 */
RelaunchEffects make_relaunch_effects(RelaunchConfig config,
                                      const std::vector<ProcessTarget>& stopped);

/** The same, against a table the caller supplies. Tests pass their own dummies. */
RelaunchEffects make_relaunch_effects(RelaunchConfig config,
                                      const std::vector<ProcessTarget>& stopped,
                                      const std::vector<KnownImage>& table);

/**
 * What to tell the user about a relaunch, or empty when there is nothing to say.
 *
 * Separated from the effects so it can be tested without starting anything, and so the wording
 * lives in one place rather than being reinvented by the host and the client separately.
 */
std::string relaunch_user_notice(const std::vector<RelaunchOutcome>& outcomes);

/**
 * Starts `exePath` in the ordinary user context by asking the shell to do it.
 *
 * Returns false when the shell cannot be reached -- no Explorer, no interactive desktop, or the
 * request refused. False means the program was NOT started; it never means "started, elevated".
 */
bool launch_via_shell(const std::wstring& exePath, const std::wstring& arguments);

/**
 * Makes launch_via_shell fail without a shell being involved, for the one case that has to be
 * tested and cannot be arranged otherwise: what happens when there is no route to the user's
 * context. The answer must be "nothing is started", never "started elevated instead".
 *
 * Production never calls this. It exists because the absence of a fallback is a property worth
 * asserting, and an absence cannot be asserted without reaching the branch.
 */
void set_shell_launch_disabled_for_test(bool disabled);

/** Starts a stopped service and waits for it to report running. */
bool start_service_and_wait(const std::wstring& serviceName, uint32_t timeoutMs,
                            std::string* detail);

}  // namespace remote60::native_poc::update
