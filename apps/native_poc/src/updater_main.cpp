// GNLinkUpdater.exe -- the process that performs an update.
//
// The assembly used to live here, which meant it was linked into this binary and nothing else,
// which meant nobody could drive it. Five defects lived in it undetected -- the manifest was never
// fetched, the verified version never reached registration, process identities were captured after
// the processes had already gone -- while every part being assembled was correct and separately
// tested. It is in updater_effects.cpp now, where a test links it.
//
// What is left here is what only an executable does: read a command line, hand over to a copy of
// itself, and turn an outcome into an exit code.
//
// It runs from a COPY of itself. The updater has to be replaceable by an update, or it becomes a
// second permanent maintenance binary frozen at its compile-time constants -- the trap that ruled
// out re-running the old installer for registration. So it is installed in the product directory
// like every other file, and on startup it copies itself into an administrator-only working
// directory and re-executes from there.
//
// It decides nothing for itself. Install directory, staging directory, manifest url, service name,
// registry root and log paths are all arguments and none is defaulted; a missing one is an error.
//
// Its failure mode is "nothing happened". Design: docs/업데이트_기능_설계.md 3.1-3.6.

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "update_job_guard.hpp"
#include "updater_effects.hpp"
#include "updater_options.hpp"

namespace {

using namespace remote60::native_poc::update;

std::wstring gLogPath;

std::string to_utf8(const std::wstring& text) {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                       nullptr, 0, nullptr, nullptr);
  std::string out(size <= 0 ? 0 : static_cast<size_t>(size), '\0');
  if (size > 0) {
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size,
                        nullptr, nullptr);
  }
  return out;
}

/**
 * One stamped line into the updater's own log.
 *
 * This process replaces files while nothing else is running, so if it goes wrong the log is the
 * only account of what it did. Closed after every line for the same reason -- a crash must not
 * take the explanation with it.
 */
void log_line(const std::string& text) {
  std::fprintf(stdout, "%s\n", text.c_str());
  if (gLogPath.empty()) return;
  std::FILE* file = nullptr;
  if (_wfopen_s(&file, gLogPath.c_str(), L"a") != 0 || !file) return;
  SYSTEMTIME now{};
  GetLocalTime(&now);
  std::fprintf(file, "%02d-%02d %02d:%02d:%02d.%03d [updater] %s\n", now.wMonth, now.wDay,
               now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, text.c_str());
  std::fclose(file);
}

std::wstring self_image_path() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return path;
}

/**
 * Copies this executable into the working directory and runs the copy.
 *
 * The ORDER of the steps is the safety property, and it is expressed through
 * prepare_working_copy so that something can check it: the directory is judged before it is
 * created, created before anything is written into it, and the copy exists before it is launched.
 * A check that ran after the copy would be reporting on a directory that had already received an
 * executable this process is about to run elevated.
 *
 * The working directory must be one only administrators can write, for the same reason. %TEMP%
 * and %ProgramData% are out; %ProgramFiles%\\GNLink.update is admin-only by its default ACL, so
 * there is no ACL to set and therefore none to forget.
 */
bool run_from_copy(const UpdaterOptions& options, const std::vector<std::wstring>& originalArgs) {
  const std::wstring copyPath = updater_copy_path(options.workDir, self_image_path());

  WorkingCopySteps steps;
  steps.checkDirectory = [&options]() {
    std::string why;
    const LaunchGuardVerdict verdict = check_work_directory(options.workDir, &why);
    if (verdict == LaunchGuardVerdict::Ok) return true;
    log_line(std::string("refusing to use the working directory: ") +
             launch_guard_verdict_name(verdict) + " -- " + why);
    return false;
  };
  steps.createDirectory = [&options]() {
    if (CreateDirectoryW(options.workDir.c_str(), nullptr)) return true;
    if (GetLastError() == ERROR_ALREADY_EXISTS) return true;
    log_line("could not create the working directory");
    return false;
  };
  steps.copySelf = [&copyPath]() {
    // A previous attempt's copy may still be there; replacing it is correct, since this run's
    // image is the one that should execute.
    if (CopyFileW(self_image_path().c_str(), copyPath.c_str(), FALSE)) return true;
    log_line("could not place the working copy (error " + std::to_string(GetLastError()) + ")");
    return false;
  };
  steps.launch = [&options, &originalArgs, &copyPath]() {
    std::wstring commandLine = L"\"" + copyPath + L"\"";
    for (const std::wstring& arg : originalArgs) {
      commandLine += L" \"";
      commandLine += arg;
      commandLine += L"\"";
    }
    commandLine += L" --running-from-copy";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                                  CREATE_BREAKAWAY_FROM_JOB | CREATE_NEW_PROCESS_GROUP, nullptr,
                                  options.workDir.c_str(), &si, &pi);
    if (!started) {
      // Breakaway was refused. Whether that matters depends on the job we are in, and it is asked
      // rather than assumed: refusing on every job would fail on machines where nothing was at
      // risk, and that is how a safety check ends up switched off. Unknown is not a pass.
      const JobKind kind = job_kind_of_current_process();
      const LaunchGuardVerdict verdict = judge_launch(kind, false);
      if (verdict != LaunchGuardVerdict::Ok) {
        log_line(std::string("refusing to start the working copy: ") +
                 launch_guard_verdict_name(verdict));
        return false;
      }
      started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                               CREATE_NEW_PROCESS_GROUP, nullptr, options.workDir.c_str(), &si,
                               &pi);
    }
    if (!started) {
      log_line("could not start the working copy (error " + std::to_string(GetLastError()) + ")");
      return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
  };

  const WorkingCopyResult result = prepare_working_copy(steps);
  if (result != WorkingCopyResult::Started) {
    log_line(std::string("working copy not started: ") + working_copy_result_name(result));
    return false;
  }
  log_line("handed the update to the working copy at " + to_utf8(copyPath));
  return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::vector<std::wstring> arguments;
  for (int i = 1; i < argc; ++i) arguments.push_back(argv[i]);

  const ParseResult parsed = parse_updater_options(arguments);
  if (!parsed.ok()) {
    std::fprintf(stderr, "%s: %s\n", parse_status_name(parsed.status), parsed.detail.c_str());
    return 2;
  }
  const UpdaterOptions& options = parsed.options;

  std::string why;
  if (!options.validate(&why)) {
    std::fprintf(stderr, "%s\n", why.c_str());
    return 2;
  }
  gLogPath = options.logPath;

  if (!options.runningFromCopy) {
    // Hand over to a copy of ourselves so the installed original can be replaced by this update.
    return run_from_copy(options, arguments) ? 0 : 3;
  }

  log_line("starting: install=" + to_utf8(options.installDir) +
           " staging=" + to_utf8(options.stagingDir) + " installed=" + options.installedVersion);

  UpdaterEffects effects(options, production_updater_deps(log_line));
  if (!effects.build(&why)) {
    log_line("could not assemble the effects: " + why);
    return 4;
  }

  const UpdateOutcome outcome = effects.run(options.platform);
  log_line(std::string("result: ") + result_name(outcome.result) + " -- " + outcome.detail);
  // Which step failed is in the detail; what it actually hit is here. A log with only the first
  // tells an operator that the swap failed and not which file would not move.
  if (!effects.last_effects_error().empty()) {
    log_line("effects: " + effects.last_effects_error());
  }
  // Logged because it is the thing that used to be silently wrong: an empty value here would mean
  // nothing was ever verified, and the old code would have registered a version anyway.
  log_line("version verified: " + (effects.verified_version().empty()
                                       ? std::string("(none)")
                                       : effects.verified_version()));
  if (!effects.user_notice().empty()) log_line("for the user: " + effects.user_notice());

  switch (outcome.result) {
    case UpdateResult::Updated:
    case UpdateResult::NothingToDo:
      return 0;
    // Not a success and not a disaster: the new version is installed and consistent, but something
    // did not come back on its own.
    case UpdateResult::UpdatedButNotRelaunched:
      return 10;
    case UpdateResult::AbandonedBeforeSwap:
      return 11;
    // Nothing was changed and something is not running. No damage to find, and possibly no way in
    // to look -- its own code so an operator is not left reading "abandoned" and assuming all is
    // well.
    case UpdateResult::AbandonedNotRelaunched:
      return 15;
    case UpdateResult::RolledBack:
      return 12;
    // Worse than 12 and better than 13: the files are right, but nothing is running. A remote user
    // cannot reach this machine until someone starts it at the keyboard.
    case UpdateResult::RolledBackNotRelaunched:
      return 14;
    case UpdateResult::RollbackFailed:
      // The only outcome where a human has to look. Deliberately does not relaunch: the files may
      // be a mixture, and starting something out of a mixed installation is worse than starting
      // nothing.
      return 13;
  }
  return 1;
}
