#pragma once

/**
 * Where tests are allowed to make a mess, and the rules for cleaning it up.
 *
 * TEST SUPPORT. Nothing in the product includes this.
 *
 * The tests that drive real processes and real file swaps used %TEMP% and removed their
 * directories with a recursive delete. This exists because that is about to be run from an
 * ELEVATED shell (item G's service fixture), where the ways it can go wrong stop being untidy and
 * start being destructive.
 *
 * ---------------------------------------------------------------------------------------------
 * WHAT r5 GOT WRONG, since the corrections are the whole substance of this file.
 *
 *   * The boundary was a string comparison against a canonicalised path, and canonicalising with
 *     GetFullPathName resolves "." and ".." and nothing else. So root\junction\child passed the
 *     check -- it is textually under the root -- and `child` is an ordinary directory as far as
 *     its attributes are concerned, because the filesystem already followed the junction. The
 *     recursion then ran wherever the junction pointed. Refusing to call GetFinalPathName is not
 *     the same as being safe from links; every component between the root and the target has to
 *     be looked at, and now is.
 *
 *   * The root was described as "inside the repository because it is fixed at compile time". It
 *     was fixed to CMAKE_BINARY_DIR, and nothing stops a build directory being anywhere at all.
 *     The claim is now CHECKED at runtime against the repository root, and a root that fails the
 *     check is refused rather than used.
 *
 *   * Every run used the same directory names, so two runs at once -- or a run started while
 *     another was cleaning up -- shared and deleted each other's work. Each process now gets its
 *     own run directory.
 * ---------------------------------------------------------------------------------------------
 */

#include <string>
#include <vector>

namespace remote60::native_poc::test_support {

/** Why a scratch root was refused. Empty when it was accepted. */
std::string scratch_root_problem();

/**
 * The root every scratch path must live under, created and validated on first use.
 *
 * Empty when it could not be created, when it is not strictly inside the repository, or when any
 * component of its path is a reparse point. Callers must treat an empty root as a hard failure
 * and must not fall back to anywhere else -- that fallback is the thing this prevents.
 */
std::wstring scratch_root();

/**
 * This process's own directory beneath the root: <root>\run-<pid>-<tick>, created on first use.
 *
 * Two runs of the same test, or two different tests, no longer share a path. Before this they
 * did, which meant one run's cleanup could delete another run's fixture out from under it.
 */
std::wstring scratch_run_dir();

/** A named path inside this run's directory. Nothing is created. */
std::wstring scratch_path(const std::wstring& name);

/** A unique directory inside this run's directory: <run>\<tag>-<counter>, created. */
std::wstring make_scratch_dir(const std::wstring& tag);

/**
 * Removes a directory and everything beneath it, refusing anything that is not provably inside
 * the root.
 *
 * Refused when the path cannot be canonicalised, when it is not strictly beneath the root, or
 * when ANY component between the root and it is a reparse point. Reparse points at the target
 * itself are removed as links; the recursion never enters one.
 *
 * Returns false on refusal and on anything left behind. A false is worth reporting rather than
 * ignoring: a directory that will not go usually means a fixture that has not stopped.
 */
bool remove_scratch_tree(const std::wstring& dir);

/** Removes this run's directory. Returns false if anything is left. */
bool remove_scratch_run_dir();

/** True when `path` canonicalises to something strictly beneath `root`. Case-insensitive. */
bool is_strictly_under(const std::wstring& root, const std::wstring& path);

/**
 * True when a component strictly between `root` and `path` is a reparse point.
 *
 * `root` itself and `path` itself are not examined: the root is validated once when it is
 * accepted, and a target that is itself a link is removed as one.
 */
bool has_reparse_between(const std::wstring& root, const std::wstring& path);

/** True when the path exists and carries FILE_ATTRIBUTE_REPARSE_POINT. */
bool is_reparse_point(const std::wstring& path);

/** Canonical, absolute form of `path`, or empty when it cannot be resolved. Text only. */
std::wstring canonical_path(const std::wstring& path);

/** Every ancestor of `path` that is a reparse point, root-first. Empty when none are. */
std::vector<std::wstring> reparse_ancestors(const std::wstring& path);

}  // namespace remote60::native_poc::test_support
