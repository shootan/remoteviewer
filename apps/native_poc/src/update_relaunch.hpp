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

struct RelaunchConfig {
  /** Where the product lives. Entries are started from here, never from a path in a manifest. */
  std::wstring installDir;
  /** The SCM name of the input service. Empty means a Service entry cannot be started. */
  std::wstring serviceName;
  /** The log the product writes its health report into. */
  std::wstring healthLogPath;
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
  /** What the last relaunch did, one line per entry, for the log. */
  std::function<std::vector<std::string>()> lastActions;
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

/**
 * Starts `exePath` in the ordinary user context by asking the shell to do it.
 *
 * Returns false when the shell cannot be reached -- no Explorer, no interactive desktop, or the
 * request refused. False means the program was NOT started; it never means "started, elevated".
 */
bool launch_via_shell(const std::wstring& exePath, const std::wstring& arguments);

/** Starts a stopped service and waits for it to report running. */
bool start_service_and_wait(const std::wstring& serviceName, uint32_t timeoutMs,
                            std::string* detail);

}  // namespace remote60::native_poc::update
