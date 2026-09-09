#pragma once

// What the updater is told to do, and nothing it decides for itself.
//
// This is the entry point to a process that runs as administrator and replaces files in
// %ProgramFiles%. The thing that keeps it safe is the same thing that keeps UpdateEffectsConfig
// safe: there are no defaults. An updater that filled in a missing install directory with a
// sensible guess would be one bad launch away from replacing the wrong thing, so a missing
// argument is an error and the process exits without touching anything.
//
// Two consequences worth stating.
//
// An unknown argument is an ERROR, not something ignored. A typo in a flag handed to an elevated
// process is exactly the case where "carry on with the rest" is wrong -- the caller asked for
// something this build does not understand, and doing most of what they asked is worse than
// doing none of it.
//
// Nothing here reads the environment or the registry. Everything arrives on the command line, so
// what a given run will do is visible in the line that started it.
//
// Design: docs/업데이트_기능_설계.md 3.2-3.5, docs/업데이트_배선_계획.md W1.

#include <cstdint>
#include <string>
#include <vector>

namespace remote60::native_poc::update {

struct UpdaterOptions {
  /** The product directory to replace. Absolute. */
  std::wstring installDir;
  /** Where downloads are staged. Absolute, and must not be inside installDir. */
  std::wstring stagingDir;
  /**
   * Administrator-only directory the updater copies itself into and runs from.
   *
   * It runs elevated, so a user-writable location would leave a window between the copy and the
   * launch in which the binary could be swapped. That is an elevation-of-privilege bug, not a
   * tidiness question, which is why the location is required rather than defaulted to %TEMP%.
   */
  std::wstring workDir;
  /** Where the manifest is fetched from. https only. */
  std::string manifestUrl;
  /**
   * Whether that url is our directory's route (one body carrying document and signature) or an
   * operator's own (a document with a detached `url.sig`).
   *
   * Passed in rather than guessed, because both shapes are real and a truncated answer of one
   * looks like a valid answer of the other. The worker used to assume the detached shape always,
   * which meant a derived url made it ask for `...?platform=windows.sig` -- an address the server
   * has never served. Nobody saw it because the url only ever came from an environment variable,
   * so that line never ran against the real server.
   */
  bool derivedEndpoint = false;
  /** Matched against the manifest's platform field. */
  std::string platform;
  /** The version currently installed, for the comparison. */
  std::string installedVersion;
  /** The log the product writes its health report into. */
  std::wstring healthLogPath;
  /** Where this process writes what it did. */
  std::wstring logPath;
  /** The service to restart, and the registry root to register under. */
  std::wstring serviceName;
  std::wstring registryRoot;

  /**
   * The caller that will exit once this process signals readiness. 0 when nobody is waiting.
   *
   * Held as a PID rather than a handle because it arrives on a command line, and checked for
   * identity before anything is done with it -- a PID alone is not an identity (see ProcessTarget).
   */
  uint32_t parentPid = 0;
  /**
   * Named event signalled once the download is verified and the lock is held.
   *
   * The host must not exit before this. If it did and the update then failed, there would be
   * nothing left running to put the product back, and the user would see the program simply gone.
   */
  std::wstring readyEventName;

  /** Set by the re-executed copy so it does not copy itself again. */
  bool runningFromCopy = false;

  /** False when anything required is missing or malformed. Reason goes to `detail`. */
  bool validate(std::string* detail = nullptr) const;
};

enum class ParseStatus {
  Ok,
  /** An argument this build does not understand. Never ignored. */
  UnknownArgument,
  /** A flag that needs a value did not get one. */
  MissingValue,
  /** A value that could not be read as what the flag needs. */
  BadValue,
};

struct ParseResult {
  ParseStatus status = ParseStatus::Ok;
  UpdaterOptions options;
  /** What went wrong, for the log. Names the offending argument. */
  std::string detail;

  bool ok() const { return status == ParseStatus::Ok; }
};

/** Parses argv, excluding argv[0]. */
ParseResult parse_updater_options(const std::vector<std::wstring>& arguments);

const char* parse_status_name(ParseStatus status);

/**
 * Splits `HKLM\\Some\\Sub\\Key` into the hive and the rest.
 *
 * Exists because `--registry-root` was required, documented, and ignored: the assembly wrote
 * HKEY_LOCAL_MACHINE and a fixed subkey directly, so the option named something it did not
 * control. A required argument that changes nothing is worse than an absent one -- it reads like
 * a guarantee.
 *
 * `hive` receives HKEY_LOCAL_MACHINE or HKEY_CURRENT_USER as a void* (the header does not want
 * windows.h). Returns false for anything else, rather than guessing at a hive.
 */
bool split_registry_root(const std::wstring& text, void** hive, std::wstring* subkey);

/**
 * The path the copy runs from, given the work directory and this process's image.
 *
 * Deterministic so a caller can find and clean up a previous run's copy, rather than accumulating
 * one directory per attempt in a place only administrators can write.
 */
std::wstring updater_copy_path(const std::wstring& workDir, const std::wstring& selfImagePath);

}  // namespace remote60::native_poc::update
