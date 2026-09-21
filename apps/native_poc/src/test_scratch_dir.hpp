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
 * THE CORRECTIONS, since they are the whole substance of this file.
 *
 * r5: the boundary was a string comparison against a canonicalised path, and canonicalising with
 * GetFullPathName resolves "." and ".." and nothing else. root\junction\child passed -- it is
 * textually under the root -- and `child` looks like an ordinary directory because the filesystem
 * already followed the junction. Every component between the root and the target is examined now.
 *
 * r5: "inside the repository" was a property of CMAKE_BINARY_DIR, which can be anywhere. It is
 * checked at runtime against the repository root, and a root that fails is refused.
 *
 * r5: every run used the same names, so two runs shared and deleted each other's work.
 *
 * r6: the root was CREATED before its ancestors were checked. An existing ancestor that is a
 * junction meant directories were made outside the boundary and only then refused -- the refusal
 * came after the damage. Nothing is created until every component that already exists has been
 * checked, and the check is repeated afterwards.
 *
 * r6: the run directory accepted ERROR_ALREADY_EXISTS as success, so it could adopt a directory
 * it did not make -- another run's, or a junction somebody left there. Only a directory this
 * process creates is accepted.
 *
 * r6: the boundary was re-read from a cached root at delete time. The cache is still there for
 * the path, but the root's own link status is re-checked on every top-level removal rather than
 * trusted from start-up.
 * ---------------------------------------------------------------------------------------------
 *
 * The root and the run directory are fixed for the lifetime of the process. Nothing here supports
 * changing them mid-run, and a test must not try: paths handed out earlier would then point
 * outside the boundary that is being enforced.
 *
 * Both are returned BY REFERENCE, and that is not a micro-optimisation. Returning by value made
 * every call produce a separate temporary, so
 *
 *     std::string(scratch_run_dir().begin(), scratch_run_dir().end())
 *
 * took its two iterators from two DIFFERENT objects. The distance between them is whatever the
 * addresses happen to be; when it comes out large, std::string throws length_error, nothing
 * catches it, and the process aborts. That is the 0xC0000409 this suite was dying with in about
 * three runs in five. A reference makes that same expression correct by construction rather than
 * by everyone remembering not to write it.
 */

#include <string>
#include <vector>

namespace remote60::native_poc::test_support {

/** Why a candidate root was accepted or refused. */
enum class RootVerdict : uint8_t {
  Ok = 0,
  /** One of the paths could not be turned into an absolute path at all. */
  Unresolvable,
  /** The build directory is not inside the repository, so the boundary claim would be false. */
  NotInRepository,
  /** A component above the root is a reparse point. Nothing is created in this case. */
  AncestorIsLink,
  /** The root itself is a reparse point. */
  RootIsLink,
  /** The directory could not be created. */
  CannotCreate,
};

const char* root_verdict_name(RootVerdict v);

/**
 * Validates a candidate scratch root and creates it, in that order.
 *
 * Exposed with explicit arguments so the refusals can be produced in a test: the compile-time
 * pair is what scratch_root() passes. The ordering is the point -- every component of `root` that
 * ALREADY EXISTS is checked before a single directory is made, so a refusal never happens after
 * something has been created outside the boundary.
 *
 * `why` (optional) receives a sentence naming the paths involved.
 */
RootVerdict validate_and_create_root(const std::wstring& repoRoot, const std::wstring& root,
                                     std::string* why = nullptr);

/** Why the scratch root was refused. Empty when it was accepted. */
std::string scratch_root_problem();

/**
 * The root every scratch path must live under, created and validated on first use.
 *
 * Empty when it was refused. Callers must treat that as a hard failure and must not fall back to
 * anywhere else -- that fallback is the thing this prevents.
 */
const std::wstring& scratch_root();

/**
 * This process's own directory beneath the root, created on first use.
 *
 * Only a directory this process CREATES is accepted; a name that already exists is not adopted,
 * it is retried under a new name. Adopting one would mean sharing with whatever made it, and
 * "whatever made it" includes another run midway through its own cleanup.
 */
const std::wstring& scratch_run_dir();

/** A named path inside this run's directory. Nothing is created. */
std::wstring scratch_path(const std::wstring& name);

/** A unique directory inside this run's directory, created. */
std::wstring make_scratch_dir(const std::wstring& tag);

/**
 * Removes a directory and everything beneath it, refusing anything not provably inside the root.
 *
 * Refused when the root's own link status has changed since start-up, when the path is not
 * strictly beneath the root, or when any component between the two is a reparse point. Reparse
 * points at the target are removed as links; the recursion never enters one.
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
 * `root` itself and `path` itself are not examined: the root is checked separately, and a target
 * that is itself a link is removed as one.
 */
bool has_reparse_between(const std::wstring& root, const std::wstring& path);

/** True when the path exists and carries FILE_ATTRIBUTE_REPARSE_POINT. */
bool is_reparse_point(const std::wstring& path);

/** Canonical, absolute form of `path`, or empty when it cannot be resolved. Text only. */
std::wstring canonical_path(const std::wstring& path);

/** Every ancestor of `path` that is a reparse point, root-first. Empty when none are. */
std::vector<std::wstring> reparse_ancestors(const std::wstring& path);

}  // namespace remote60::native_poc::test_support
