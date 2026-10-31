#pragma once

// Two things that must be true before an updater is allowed to start, and were only assumed.
//
// The first is that it will outlive the process that started it. The host is asked to stop as part
// of the update, and a job object with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE takes its children with
// it -- so an updater that failed to break away and carried on anyway would die part way through
// replacing files, with nothing left running to put them back. That is the exact failure ledger
// item (b) describes. Warning and continuing is not a mitigation; it is permission.
//
// The second is that the directory it copies itself into is one only administrators can write.
// "%ProgramFiles% is admin-only by default" is true and not sufficient: a directory that already
// exists was made by someone, and CreateDirectory succeeding-or-already-existing accepts whatever
// is there. If an attacker created it first with a permissive ACL, an elevated process is copying
// an executable into a place they control and then running it.
//
// Both checks answer "may we proceed", and both answer no by default.
//
// Design: docs/업데이트_기능_설계.md 3.2, docs/업데이트_배선_계획.md W1.

#include <string>

namespace remote60::native_poc::update {

/** Why a launch may not proceed, or that it may. */
enum class LaunchGuardVerdict {
  Ok,
  /** In a job that kills its children, and breakaway was refused. */
  WouldDieWithParent,
  /** The working directory is writable by someone who is not an administrator. */
  WorkDirNotProtected,
  /** The working directory is a reparse point, so its name does not decide where writes land. */
  WorkDirIsReparsePoint,
  /** The check itself could not be completed, which is not a pass. */
  Undetermined,
};

const char* launch_guard_verdict_name(LaunchGuardVerdict verdict);

/**
 * Whether this process belongs to a job object that would kill it along with its parent.
 *
 * Three states, not two: in no job, in a job that does not kill on close, or in one that does.
 * Only the last forbids proceeding without breakaway -- refusing on every job would fail on
 * machines where nothing was ever at risk.
 */
enum class JobKind { NotInJob, InHarmlessJob, InKillingJob, Unknown };

JobKind job_kind_of_current_process();

/**
 * Judges whether a child may be started, given whether breakaway succeeded.
 *
 * Pure, so the combinations can be exercised without a job object. `breakawaySucceeded` false
 * while in a killing job is the case that must refuse.
 */
LaunchGuardVerdict judge_launch(JobKind kind, bool breakawaySucceeded);

/**
 * Whether `path` is safe to copy an executable into and run it from.
 *
 * Checked whether or not this process created it: a directory that was already there was made by
 * somebody, and accepting it because CreateDirectory said ERROR_ALREADY_EXISTS is accepting
 * whatever ACL they gave it.
 */
LaunchGuardVerdict check_work_directory(const std::wstring& path, std::string* detail);

}  // namespace remote60::native_poc::update
