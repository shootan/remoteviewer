// GNLinkUpdater.exe -- the process that actually performs an update.
//
// Everything under src/update_*.cpp existed before this file did, and none of it was reachable:
// run_update had no caller anywhere in the product. This is that caller. It builds the effects
// from arguments, runs the state machine once, writes down what happened, and exits.
//
// Three things decide the shape of this file.
//
// It runs from a COPY of itself. The updater has to be replaceable by an update, or it becomes a
// second permanent maintenance binary frozen at its compile-time constants -- the trap that ruled
// out re-running the old installer for registration. So it is installed in the product directory
// like every other file, and on startup it copies itself into an administrator-only working
// directory and re-executes from there. The copy is what does the work, and the installed
// original is then just another file the swap can replace.
//
// It never decides anything for itself. Install directory, staging directory, manifest url,
// service name, registry root, log paths -- all arguments, none defaulted. A missing one is an
// error. This is the same discipline UpdateEffectsConfig::validate() enforces, applied one level
// earlier so a bad launch fails before anything is fetched.
//
// Its failure mode is "nothing happened". The host that started it keeps running until this
// process says it has the lock and a verified download; if that signal never comes, the host
// simply carries on. An update that does not happen is an ordinary outcome. An update that half
// happens is not.
//
// Design: docs/업데이트_기능_설계.md 3.1-3.6, docs/업데이트_배선_계획.md W1-W6.

#include <windows.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "install_registration.hpp"
#include "product_version.hpp"
#include "update_check.hpp"
#include "update_effects.hpp"
#include "update_http.hpp"
#include "update_process_targets.hpp"
#include "update_registration_wiring.hpp"
#include "update_relaunch.hpp"
#include "update_state_machine.hpp"
#include "updater_options.hpp"

namespace {

using namespace remote60::native_poc::update;
namespace install = remote60::native_poc::install;

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
 * only account of what it did. Written unbuffered for the same reason -- a crash must not take
 * the explanation with it.
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

bool ensure_directory(const std::wstring& path) {
  if (CreateDirectoryW(path.c_str(), nullptr)) return true;
  return GetLastError() == ERROR_ALREADY_EXISTS;
}

/**
 * Copies this executable into the working directory and runs the copy, then returns.
 *
 * The caller exits immediately afterwards: from here on the copy owns the update, and the
 * installed original is free to be replaced.
 *
 * The working directory must be one only administrators can write. This process runs elevated, so
 * a user-writable location would leave a window between the copy and the launch in which the file
 * could be swapped for something else -- an elevation-of-privilege bug rather than an untidiness.
 * %TEMP% and %ProgramData% are both out for that reason; the intended location is
 * %ProgramFiles%\GNLink.update, a sibling of the install directory, where the default ACL is
 * already administrators-only. A defence that has to be erected is a defence that can be
 * forgotten.
 *
 * It is still required on the command line rather than computed here, because this process
 * decides nothing for itself.
 */
bool run_from_copy(const UpdaterOptions& options, const std::vector<std::wstring>& originalArgs) {
  if (!ensure_directory(options.workDir)) {
    log_line("could not create the working directory " + to_utf8(options.workDir));
    return false;
  }

  const std::wstring copyPath = updater_copy_path(options.workDir, self_image_path());
  // A previous attempt's copy may still be there. Replacing it is correct: it is the same build
  // or an older one, and either way this run's image is the one that should execute.
  if (!CopyFileW(self_image_path().c_str(), copyPath.c_str(), FALSE)) {
    log_line("could not place the working copy at " + to_utf8(copyPath) + " (error " +
             std::to_string(GetLastError()) + ")");
    return false;
  }

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
  // CREATE_BREAKAWAY_FROM_JOB: the host that started this may belong to a job object, and an
  // updater inside the host's job dies when the host is asked to stop -- which this update is
  // about to do. Design 3.2 (2). Not fatal if the job forbids breakaway; the launch is retried
  // without it so a machine that does not need it still works.
  BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                                CREATE_BREAKAWAY_FROM_JOB | CREATE_NEW_PROCESS_GROUP, nullptr,
                                options.workDir.c_str(), &si, &pi);
  if (!started) {
    started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                             CREATE_NEW_PROCESS_GROUP, nullptr, options.workDir.c_str(), &si, &pi);
    if (started) log_line("the working copy could not break away from its job object");
  }
  if (!started) {
    log_line("could not start the working copy (error " + std::to_string(GetLastError()) + ")");
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  log_line("handed the update to the working copy at " + to_utf8(copyPath));
  return true;
}

/**
 * Lets the caller know it is safe to exit.
 *
 * Signalled once the lock is held and a verified release is staged -- not before. A host that
 * exited earlier and then saw the update fail would leave nothing running to put the product
 * back, and the user would see the program simply gone.
 */
void signal_ready(const std::wstring& eventName) {
  if (eventName.empty()) return;
  HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName.c_str());
  if (!event) {
    log_line("no one is waiting on " + to_utf8(eventName));
    return;
  }
  SetEvent(event);
  CloseHandle(event);
  log_line("told the caller it may exit");
}

/**
 * The effects, assembled from the production pieces.
 *
 * This function is the whole point of the file: every one of these was written, tested and then
 * never connected to anything.
 */
class UpdaterEffects {
 public:
  explicit UpdaterEffects(const UpdaterOptions& options) : options_(options) {}

  bool build(std::string* detail) {
    UpdateEffectsConfig config;
    config.installDir = options_.installDir;
    config.stagingDir = options_.stagingDir;
    config.lockName = L"Global\\GNLinkUpdate";
    // What a swap replaces. The executables come from the same list the process enumerator
    // uses, so the set that is stopped and the set that is replaced cannot drift apart, and the
    // two data files are named alongside them.
    config.payloadNames = product_image_names();
    config.payloadNames.push_back(L"ui\\shell.html");
    config.payloadNames.push_back(L"ui\\macro.html");
    config.registryRoot = options_.registryRoot;
    config.serviceName = options_.serviceName;
    config.expectedVersion = {};  // filled once the manifest says which version this is
    config.updaterImagePath = self_image_path();

    config.fetchArtifact = [](const ManifestArtifact& artifact, const std::wstring& destPath) {
      std::string error;
      // The manifest's own size is the cap. A body larger than what was signed for is refused
      // while it is arriving rather than after it has been written.
      const FetchStatus status = https_get_file(artifact.url, destPath, artifact.size, &error);
      if (status != FetchStatus::Ok) {
        log_line(std::string("fetch failed for ") + artifact.name + ": " +
                 fetch_status_name(status) + " " + error);
        return false;
      }
      return true;
    };

    // The two calls that can reach a real GNLinkHost.exe. They live in update_process_targets.cpp
    // and this is the first binary that links it.
    config.enumerateTargets = []() {
      return enumerate_product_processes(product_image_names());
    };
    config.requestStop = [](const ProcessTarget& target) { return request_process_stop(target); };

    install::RegistrationTarget target;
    target.installDir = options_.installDir;
    target.setupPath = options_.installDir + L"\\GNLinkSetup.exe";
    target.productName = L"GNLink";
    target.clientShortcutName = L"GNLink";
    target.publisher = L"Chrono Studio";
    target.serviceName = options_.serviceName;
    target.firewallRuleName = L"GNLink";
    target.uninstallRoot = HKEY_LOCAL_MACHINE;
    target.uninstallSubkey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GNLink";
    target.hostExeName = L"GNLinkHost.exe";
    target.clientExeName = L"GNLinkClient.exe";
    target.serviceExeName = L"GNLinkInputService.exe";
    target.streamExeName = L"GNLinkStream.exe";
    registrationTarget_ = target;

    config_ = config;
    if (detail) detail->clear();
    return true;
  }

  /**
   * Finishes the configuration once the manifest is known, and runs.
   *
   * Registration and relaunch cannot be built earlier than this: registration needs the version
   * the manifest claims (never a constant compiled into this binary), and relaunch needs the list
   * of processes that were actually stopped, which does not exist until Quiesce has run.
   */
  UpdateOutcome run(const std::string& platform) {
    // Captured here so the relaunch built afterwards describes the configuration that was
    // running, rather than everything the product could run.
    auto stopped = std::make_shared<std::vector<ProcessTarget>>();
    UpdateEffectsConfig config = config_;
    const auto enumerate = config.enumerateTargets;
    config.enumerateTargets = [enumerate, stopped]() {
      const auto targets = enumerate();
      if (stopped->empty()) *stopped = targets;
      return targets;
    };

    install::RegistrationTarget target = registrationTarget_;
    target.version = versionToInstall_.empty() ? std::wstring(remote60::native_poc::kProductVersion)
                                               : versionToInstall_;
    RegistrationEffects registration =
        make_registration_effects(target, production_registration_ops());
    config.captureRegistration = registration.capture;
    config.registerInstall = registration.apply;
    config.restoreRegistration = registration.restore;

    RelaunchConfig relaunchConfig;
    relaunchConfig.installDir = options_.installDir;
    relaunchConfig.serviceName = options_.serviceName;
    relaunchConfig.healthLogPath = options_.healthLogPath;
    relaunchConfig.expectedVersion = to_utf8(target.version);

    // Deferred: the plan needs what Quiesce stopped, and Quiesce has not run yet when this is
    // wired. The lambda reads `stopped` at the moment it is called, which is after.
    auto relaunchEffects = std::make_shared<RelaunchEffects>();
    config.relaunch = [this, relaunchConfig, stopped, relaunchEffects]() {
      *relaunchEffects = make_relaunch_effects(relaunchConfig, *stopped);
      const bool ok = relaunchEffects->relaunch();
      for (const RelaunchOutcome& outcome : relaunchEffects->lastOutcomes()) {
        log_line("relaunch " + to_utf8(outcome.imageName) + ": " + outcome.detail);
      }
      userNotice_ = relaunchEffects->userNotice();
      return ok;
    };
    config.healthCheck = [relaunchEffects]() {
      if (!relaunchEffects->healthCheck) return false;
      const bool ok = relaunchEffects->healthCheck();
      log_line("health: " + relaunchEffects->lastHealthDetail());
      return ok;
    };

    WindowsUpdateEffects effects(config);
    effects.set_installed_version(options_.installedVersion);
    ReadySignaller signaller(effects, options_.readyEventName);
    const UpdateOutcome outcome = run_update(signaller, default_verifier(), platform);
    return outcome;
  }

  const std::string& user_notice() const { return userNotice_; }

 private:
  /**
   * Passes everything through, and signals the waiting caller at exactly one point: after the
   * download has been verified.
   *
   * A decorator rather than a flag inside WindowsUpdateEffects, because "when may the host exit"
   * is a question about this particular launch, not about how effects work.
   */
  class ReadySignaller : public UpdateEffects {
   public:
    ReadySignaller(UpdateEffects& inner, std::wstring eventName)
        : inner_(inner), eventName_(std::move(eventName)) {}

    bool AcquireLock() override { return inner_.AcquireLock(); }
    void ReleaseLock() override { inner_.ReleaseLock(); }
    bool FetchManifest(std::string* d, std::string* s) override {
      return inner_.FetchManifest(d, s);
    }
    std::string InstalledVersion() override { return inner_.InstalledVersion(); }
    bool Download(const ManifestFields& f) override { return inner_.Download(f); }
    bool VerifyDownload(const ManifestFields& f) override {
      const bool ok = inner_.VerifyDownload(f);
      // Here and nowhere else. The lock is held and the bytes are verified, so the host exiting
      // now cannot leave the product in a state this process could not finish.
      if (ok) signal_ready(eventName_);
      return ok;
    }
    void DiscardDownload() override { inner_.DiscardDownload(); }
    bool PrepareForSwap() override { return inner_.PrepareForSwap(); }
    bool Quiesce() override { return inner_.Quiesce(); }
    bool Swap() override { return inner_.Swap(); }
    bool RegisterInstall() override { return inner_.RegisterInstall(); }
    bool Relaunch() override { return inner_.Relaunch(); }
    bool HealthCheck() override { return inner_.HealthCheck(); }
    bool Rollback() override { return inner_.Rollback(); }
    void Commit() override { inner_.Commit(); }

   private:
    UpdateEffects& inner_;
    std::wstring eventName_;
  };

  UpdaterOptions options_;
  UpdateEffectsConfig config_;
  install::RegistrationTarget registrationTarget_;
  std::wstring versionToInstall_;
  std::string userNotice_;
};

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
    std::vector<std::wstring> forward = arguments;
    return run_from_copy(options, forward) ? 0 : 3;
  }

  log_line("starting: install=" + to_utf8(options.installDir) +
           " staging=" + to_utf8(options.stagingDir) + " installed=" + options.installedVersion);

  UpdaterEffects effects(options);
  if (!effects.build(&why)) {
    log_line("could not assemble the effects: " + why);
    return 4;
  }

  const UpdateOutcome outcome = effects.run(options.platform);
  log_line(std::string("result: ") + result_name(outcome.result) + " -- " + outcome.detail);
  if (!effects.user_notice().empty()) log_line("for the user: " + effects.user_notice());

  switch (outcome.result) {
    case UpdateResult::Updated:
    case UpdateResult::NothingToDo:
      return 0;
    // Not a success and not a disaster: the new version is installed and consistent, but
    // something did not come back on its own. Its own exit code so a caller can tell the user.
    case UpdateResult::UpdatedButNotRelaunched:
      return 10;
    case UpdateResult::AbandonedBeforeSwap:
      return 11;
    case UpdateResult::RolledBack:
      return 12;
    case UpdateResult::RollbackFailed:
      // The only outcome where a human has to look.
      return 13;
  }
  return 1;
}
