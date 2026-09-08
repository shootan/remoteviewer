#pragma once

// Deciding whether the new build actually came up, from what it says about itself.
//
// The completion condition (design 3.6) is not "the process exists". A process that starts and
// immediately fails to do its job is exactly the failure a health check is for, and its existence
// proves nothing. So the evidence is a line the product writes about ITSELF, after it is far
// enough along to know: which version it is, and whether it reached the directory.
//
// Two properties this is built around.
//
// The first is that evidence must come from the RUN BEING CHECKED. A log file already contains
// lines from every previous run, including ones from the version being replaced, and a checker
// that scans the whole file will happily accept a banner written last week. The reader here takes
// an offset and only sees what was appended after it -- the caller records that offset before the
// relaunch, so nothing older can be mistaken for evidence.
//
// The second is that "not yet" and "no" are different. A host that has not reported on the
// directory yet is not a host that failed to reach it, and treating them the same would roll back
// a good update for being slower than a timeout. Pending is its own state, and only running out
// of time turns it into a failure.
//
// Design: docs/업데이트_기능_설계.md 3.6 (Health).

#include <string>

namespace remote60::native_poc::update {

enum class DirectoryHealth {
  /** No health line has appeared yet. */
  NotReported,
  /** The product reported, but does not know about the directory yet. Not a failure. */
  Pending,
  /** A batch was accepted by the directory. */
  Ok,
  /**
   * There is nothing to reach: no account is signed in, so no directory contact is expected.
   *
   * Accepted rather than treated as a failure. A machine sitting at the sign-in screen is a
   * normal state, and rolling an update back because nobody was signed in would be inventing a
   * fault the update did not cause.
   */
  NotConfigured,
};

const char* directory_health_name(DirectoryHealth value);

struct HealthSignals {
  bool reported = false;
  /** The version the product says it is. Compared against what the update installed. */
  std::string reportedVersion;
  DirectoryHealth directory = DirectoryHealth::NotReported;
};

/**
 * The last health line in `text`, or an empty result when there is none.
 *
 * Last wins, because the product reports again when it learns something -- a run that starts
 * `directory=pending` and later says `directory=ok` has answered, and the first line is history.
 *
 * The format is `[host-app] health version=<v> directory=<state>`. A line that is not one of
 * those, or carries a state this build does not know, is ignored rather than guessed at: an
 * unrecognised state is not evidence of health.
 */
HealthSignals scan_health_report(const std::string& text);

enum class HealthVerdict {
  /** The version reported matches and the directory is in an acceptable state. */
  Healthy,
  /** Nothing has been reported yet, or the directory has not answered. Keep waiting. */
  Waiting,
  /** Reported, but as a different version than the one just installed. */
  WrongVersion,
};

const char* health_verdict_name(HealthVerdict verdict);

/**
 * Judges signals against the version the update installed.
 *
 * `WrongVersion` is separate from `Waiting` on purpose. A product reporting the OLD version after
 * a swap is not slow -- it is evidence that something is running the files the update was meant
 * to replace, and waiting longer will not change that.
 */
HealthVerdict judge_health(const HealthSignals& signals, const std::string& expectedVersion,
                           std::string* detail);

}  // namespace remote60::native_poc::update
