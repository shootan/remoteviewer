#pragma once

// What to bring back after a swap, decided separately from bringing it back.
//
// The completion condition (design 3.6) is "the same configuration that was running before is
// running again" -- not "everything the product has is running". Those differ, and the difference
// matters in both directions: starting something the user had closed is as wrong as failing to
// restart something they had open.
//
// The other thing this file exists to get right is that the product's processes are not peers.
// Some are started by a supervisor, and relaunching one of those directly produces two problems
// at once -- an instance nobody supervises, and a second instance when the supervisor starts its
// own. One of them is a Windows service, which comes back through the SCM rather than by creating
// a process. And one of them must come back WITHOUT the updater's elevated token, because a
// client launched from an elevated updater would run every later session as administrator.
//
// So the plan is a decision, and it is made here where it can be tested, rather than inside the
// code that calls CreateProcess.
//
// Design: docs/업데이트_기능_설계.md 3.5 (non-elevated relaunch of the client), 3.6 (Relaunch).

#include <string>
#include <vector>

#include "update_effects.hpp"

namespace remote60::native_poc::update {

enum class RelaunchKind {
  /**
   * Started as a child of the updater, inheriting its token.
   *
   * Correct for the host, which is `requireAdministrator` anyway -- it was elevated before the
   * update and it is elevated after, so nothing changes and no UAC prompt appears.
   */
  ElevatedProcess,
  /**
   * Started in the ordinary, non-elevated user context.
   *
   * The client is not an administrator program. Launching it as a child of an elevated updater
   * would hand it an administrator token it never had, and every window it opens afterwards --
   * every file dialog, every child process -- would run with it.
   */
  UserProcess,
  /** Brought back through the SCM. Creating the process directly would not make it a service. */
  Service,
  /**
   * Deliberately NOT relaunched: something else starts it.
   *
   * Kept in the plan rather than filtered out silently, because "we are not starting this" is a
   * decision worth being able to see and to test, and because the reason is not obvious from the
   * image name alone.
   */
  SupervisedByAnother,
};

const char* relaunch_kind_name(RelaunchKind kind);

struct RelaunchEntry {
  std::wstring imageName;  // file name only, as it appears in the install directory
  RelaunchKind kind = RelaunchKind::ElevatedProcess;
  /** Why, in one clause. Goes to the log so a plan can be read after the fact. */
  std::string reason;
};

/**
 * One image the product knows, and how it comes back.
 *
 * A table rather than a hard-coded chain so that it can be HANDED IN. That matters for one
 * reason: until it could be, the code that starts processes had no way to start anything but the
 * real product, so it was linked into nothing and had never executed. Passing a table of dummy
 * names lets the same code run for real against a temp directory.
 *
 * The allow-list property is unchanged by this. A name that is not in the table still cannot be
 * started. What moved is where the table comes from, not whether there is one.
 */
struct KnownImage {
  /** As spelled in the install directory. A launcher builds its path from this. */
  const wchar_t* name;
  /** The same, lowercased, for matching an observed image path. */
  const wchar_t* lower;
  RelaunchKind kind;
  const char* reason;
};

/** The product's own table. The default, and the only one production ever passes. */
const std::vector<KnownImage>& product_images();

/**
 * The plan for a set of processes that were stopped.
 *
 * Only what was actually running appears. Duplicates collapse: two instances of one image are one
 * entry, because the product runs one of each and starting two would be worse than starting none.
 * Order follows first appearance, so a caller that starts entries in order starts the supervisor
 * before anything that might depend on it.
 *
 * An image the product does not know is dropped rather than launched. A name reaching this from
 * a manifest is data, and data does not get to name an executable to run.
 */
std::vector<RelaunchEntry> relaunch_plan(const std::vector<ProcessTarget>& stopped);

/** The same, against a table the caller supplies. Tests pass their own dummies. */
std::vector<RelaunchEntry> relaunch_plan(const std::vector<ProcessTarget>& stopped,
                                         const std::vector<KnownImage>& table);

/**
 * The entries a caller is expected to actually start, in order.
 *
 * Just the plan without the SupervisedByAnother ones. Separate function so that "we decided not
 * to start this" and "there was nothing to start" stay distinguishable.
 */
std::vector<RelaunchEntry> entries_to_start(const std::vector<RelaunchEntry>& plan);

/** File name of a path, without directories. Lowercased for comparison. */
std::wstring image_leaf_lower(const std::wstring& path);

}  // namespace remote60::native_poc::update
