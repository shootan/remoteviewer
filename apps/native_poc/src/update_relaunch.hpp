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
  /** True when the plan deliberately did not start it (a supervised child). Not a failure. */
  bool skipped = false;
  /** Why, for the log and for what the user is told. */
  std::string detail;

  /** A failure that matters: something that should have come back did not. */
  bool failed() const { return !started && !skipped; }
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
};

/**
 * The `relaunch` and `healthCheck` callbacks for UpdateEffectsConfig, built together.
 *
 * Together because they share one piece of state: the size the log had before anything was
 * started. Building them separately would make it possible to wire a health check that reads a
 * previous run's lines, which is the failure mode that makes a health check worthless.
 */
struct RelaunchEffects {
  std::function<bool()> relaunch;
  std::function<bool()> healthCheck;
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
