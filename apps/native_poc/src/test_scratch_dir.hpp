#pragma once

/**
 * Where tests are allowed to make a mess, and the rules for cleaning it up.
 *
 * TEST SUPPORT. Nothing in the product includes this.
 *
 * The tests that drive real processes and real file swaps used %TEMP% and removed their
 * directories with a recursive delete. Two things are wrong with that, and neither needs a
 * malicious actor to bite:
 *
 *   1. %TEMP% is shared. A path built by string concatenation that comes out slightly wrong --
 *      an empty suffix, a stray separator -- names somebody else's directory, and the recursion
 *      deletes it. There is no boundary to stop it because everything is under the same root.
 *   2. A recursive delete that does not look at FILE_ATTRIBUTE_REPARSE_POINT walks THROUGH a
 *      junction. A junction dropped into a scratch directory -- by anything at all -- turns a
 *      cleanup into a delete of whatever it points at.
 *
 * This is about to matter more than it did: item G's service fixture is meant to be run from an
 * elevated shell, where both of those stop being ugly and start being destructive.
 *
 * So: one root, inside the build directory (which is inside the repository), named at compile
 * time. Every removal is canonicalised and refused unless it is strictly beneath that root, and
 * a reparse point is removed as a LINK and never descended into.
 */

#include <string>

namespace remote60::native_poc::test_support {

/**
 * The root every scratch path must live under, created on first use.
 *
 * Fixed at compile time (REMOTE60_TEST_SCRATCH_ROOT) to the build directory, so it is inside the
 * repository rather than in a directory shared with the rest of the machine. Empty when the root
 * could not be created or is itself a reparse point, and callers must treat that as a failure
 * rather than falling back to somewhere else.
 */
std::wstring scratch_root();

/**
 * A unique directory under the root: <root>\<tag>-<pid>-<counter>, created.
 *
 * The pid and counter are what make it this run's, so two runs in parallel -- or a fixture child
 * of this run -- cannot collide and cannot clean up each other's work.
 */
std::wstring make_scratch_dir(const std::wstring& tag);

/**
 * Removes a directory and everything beneath it, refusing anything outside the root.
 *
 * Returns false when the path could not be canonicalised, is not strictly under the root, or when
 * something was left behind. A false is worth reporting: a leftover directory means a handle is
 * still open somewhere, which is usually a fixture that did not stop.
 *
 * Reparse points (junctions, symlinks, mount points) are removed as links. The recursion never
 * enters one.
 */
bool remove_scratch_tree(const std::wstring& dir);

/** True when `path` canonicalises to something strictly beneath `root`. Case-insensitive. */
bool is_strictly_under(const std::wstring& root, const std::wstring& path);

/** True when the path exists and carries FILE_ATTRIBUTE_REPARSE_POINT. */
bool is_reparse_point(const std::wstring& path);

/** Canonical, absolute form of `path`, or empty when it cannot be resolved. */
std::wstring canonical_path(const std::wstring& path);

}  // namespace remote60::native_poc::test_support
