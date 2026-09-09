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

#include "update_handoff.hpp"
#include "update_job_guard.hpp"
#include "updater_effects.hpp"
#include "update_credential_channel.hpp"
#include "update_endpoint.hpp"
#include "updater_options.hpp"
#include "url_origin.hpp"

namespace {

using namespace remote60::native_poc::update;
using remote60::native_poc::url_origin_key;

/** How long either hop waits. Long enough for an elevation prompt, bounded so nothing hangs. */
constexpr uint32_t kCredentialDeadlineMs = 120000;

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
/**
 * Hands the credential on to the working copy, over a second pipe of the bootstrap's own.
 *
 * Two hops because the launch has two. The bootstrap cannot forward the pipe it read from -- that
 * one was proved against the bootstrap's own process id -- so it opens its own, names it in the
 * copy's arguments, and applies the same check to whoever connects.
 *
 * The handle from CreateProcessW is kept for the whole exchange. The bootstrap used to close it
 * immediately and leave; closing it here would leave the check comparing against a process id
 * that is no longer pinned to anything, which is exactly the case the check exists to refuse.
 */
bool run_from_copy(const UpdaterOptions& options, const std::vector<std::wstring>& originalArgs,
                   const std::string& credential);

bool run_from_copy(const UpdaterOptions& options, const std::vector<std::wstring>& originalArgs,
                   const std::string& credential) {
  const std::wstring copyPath = updater_copy_path(options.workDir, self_image_path());
  // The bootstrap's own channel. It cannot forward the one it read from: that pipe was proved
  // against the bootstrap's process id, and the working copy is a different process.
  const std::wstring childPipeName =
      credential.empty() ? std::wstring()
                         : make_credential_pipe_name(GetCurrentProcessId(), GetTickCount64());

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
  steps.launch = [&options, &originalArgs, &copyPath, &childPipeName, &credential]() {
    std::wstring commandLine = L"\"" + copyPath + L"\"";
    for (const std::wstring& arg : originalArgs) {
      commandLine += L" \"";
      commandLine += arg;
      commandLine += L"\"";
    }
    commandLine += L" --running-from-copy";
    // The name of the bootstrap's pipe replaces the parent's. Passing the parent's would send the
    // copy to a channel that has already been served and closed.
    if (!childPipeName.empty()) {
      commandLine += L" --credential-pipe \"";
      commandLine += childPipeName;
      commandLine += L"\"";
    }

    // Created before the copy is started, so the name never exists without an owner.
    CredentialServer credentialServer;
    if (!credential.empty()) {
      std::string error;
      if (!credentialServer.Create(childPipeName, &error)) {
        log_line("could not open the credential channel for the working copy: " + error);
        return false;
      }
    }

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

    bool handedOver = true;
    if (!credential.empty()) {
      // The handle is still open here, and stays open until the exchange is finished: that is
      // what keeps the process id from being recycled underneath the check.
      std::string payload = credential;
      std::string error;
      handedOver = credentialServer.Serve(pi.hProcess, payload, kCredentialDeadlineMs, &error);
      if (!handedOver) {
        log_line("the working copy did not receive the credential: " + error);
      }
    }
    CloseHandle(pi.hProcess);
    // A copy that never got its credential will fail its own fetch and stop. Reported as a
    // failure here so the exit code says so, rather than reporting success for a run that cannot
    // do the thing it was started for.
    return handedOver;
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

  // The credential, if one is coming, before anything else happens. Read once here and once in
  // the working copy; it is never written down in between.
  std::string credential;
  if (!options.credentialPipeName.empty()) {
    std::string error;
    if (!receive_credential(options.credentialPipeName, kCredentialDeadlineMs, &credential,
                            &error)) {
      // Not "carry on without it": the fetch would then get a 401 nobody can explain, and the
      // failure would look like the server's rather than ours.
      log_line("no credential arrived: " + error);
      return 5;
    }
  }

  if (!options.runningFromCopy) {
    // Hand over to a copy of ourselves so the installed original can be replaced by this update.
    const bool ok = run_from_copy(options, arguments, credential);
    SecureZeroMemory(credential.data(), credential.size());
    return ok ? 0 : 3;
  }

  // Held for as long as this process lives, and never by the bootstrap. Whoever is waiting can
  // then tell "the working copy is still going" from "it is gone" without either side having to
  // remember to say anything -- including in the death that gets to run no cleanup at all.
  HANDLE aliveMutex = nullptr;
  {
    const std::wstring aliveName = make_alive_mutex_name(options.readyEventName);
    if (!aliveName.empty()) {
      aliveMutex = CreateMutexW(nullptr, TRUE, aliveName.c_str());
      if (!aliveMutex) log_line("could not hold the liveness mutex; the caller will have to wait");
    }
  }

  log_line("starting: install=" + to_utf8(options.installDir) +
           " staging=" + to_utf8(options.stagingDir) + " installed=" + options.installedVersion);

  // What the worker may send, and where. Derived urls are on the directory's own origin by
  // construction, so that is the one origin this credential may reach; every artifact url is
  // judged against it separately.
  UpdateEndpoint endpoint;
  endpoint.url = options.manifestUrl;
  endpoint.derived = options.derivedEndpoint;
  if (options.derivedEndpoint && !credential.empty()) {
    endpoint.credentialHeader = credential;
    endpoint.origin = url_origin_key(options.manifestUrl);
  }

  UpdaterEffects effects(options, production_updater_deps(log_line, endpoint));
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
