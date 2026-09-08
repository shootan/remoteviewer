#pragma once

// Deciding whether a name from a manifest may be used as a path under the install directory.
//
// The signature check is the first lock: a manifest that did not verify never gets this far. This
// is the second. A payload name arrives as data and is turned into a filesystem path, so if it is
// used as given it becomes a primitive for writing anywhere on the machine -- with administrator
// rights, since that is what the updater runs with. Defence in depth means not relying on the
// signature having been checked correctly, or on the signing key never leaking, or on the
// publishing side never being tricked into signing something odd.
//
// The rules are a whitelist rather than a filter. Nothing here tries to sanitise a bad name into
// a good one: names are either acceptable or refused, because "make this safe" is a much harder
// problem than "is this already safe", and the set of names the product actually ships is small
// and boring.
//
// Design condition from the package-contract approval (2026-09-08): allowed list, normalisation,
// duplicate detection, traversal detection -- including `..`, absolute paths, drive letters,
// alternate data streams, and a re-check after backslash normalisation.

#include <string>
#include <vector>

namespace remote60::native_poc::update {

/** Why a payload name was refused. Ok means it may be joined to the install directory. */
enum class PayloadNameVerdict {
  Ok,
  Empty,
  Absolute,        // begins with a separator, or names a drive
  DriveRelative,   // "C:file" -- resolves against a per-drive current directory
  Traversal,       // a ".." component
  CurrentDir,      // a "." component; harmless but not something we ship, so refused
  AlternateStream, // a ':' anywhere else -- NTFS alternate data stream
  Wildcard,        // '*' or '?' -- never a real file name we publish
  ControlCharacter,
  TrailingDotOrSpace,  // Windows silently strips these, so two names could collide
  ReservedDeviceName,  // CON, NUL, COM1 ... -- opening these does not create a file
  TooLong,
  EmptyComponent,  // a doubled separator -- ambiguous, and a way to smuggle an empty segment
  Duplicate,       // the same file named twice in one list
};

const char* payload_name_verdict_name(PayloadNameVerdict verdict);

/**
 * Checks one name. Forward slashes are normalised to backslashes first and the result is checked
 * again, so a mixed-separator name cannot pass by looking different in each form.
 */
PayloadNameVerdict check_payload_name(const std::wstring& name);

/** Convenience: Ok or not. */
inline bool payload_name_is_safe(const std::wstring& name) {
  return check_payload_name(name) == PayloadNameVerdict::Ok;
}

/**
 * Checks a whole list, including for duplicates.
 *
 * Duplicates matter beyond tidiness: the swap moves each named file aside before writing any of
 * them, so the same name twice would have the second move-aside overwrite the first file's
 * backup, and a rollback would then restore the wrong bytes. Comparison is case-insensitive
 * because the filesystem is.
 *
 * On failure `badIndex` and `verdict` say which entry and why.
 */
bool check_payload_names(const std::vector<std::wstring>& names, size_t* badIndex,
                         PayloadNameVerdict* verdict);

/** Normalised form used for comparison: forward slashes folded, lowercased. */
std::wstring normalise_payload_name(const std::wstring& name);

}  // namespace remote60::native_poc::update
