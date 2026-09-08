#include "updater_effects.hpp"

#include <windows.h>

#include "product_version.hpp"
#include "update_http.hpp"
#include "update_process_targets.hpp"
#include "update_registration_wiring.hpp"

namespace remote60::native_poc::update {
namespace {

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

std::wstring widen(const std::string& text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                       nullptr, 0);
  std::wstring out(size <= 0 ? 0 : static_cast<size_t>(size), L'\0');
  if (size > 0) {
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size);
  }
  return out;
}

/**
 * Passes everything through, and does two things at one exact point.
 *
 * After VerifyDownload succeeds -- the first moment the version is both known and trustworthy --
 * it records that version and captures the process identities, and only then releases a caller
 * that is waiting to exit.
 *
 * The capture has to come first. The moment the caller is released it starts leaving, and a
 * process that has already exited cannot be enumerated: asking Quiesce to discover targets later
 * produces a list missing exactly the process this update told to go, and nothing would ever
 * bring it back.
 */
class ReadySignaller : public UpdateEffects {
 public:
  ReadySignaller(UpdateEffects& inner, std::wstring eventName,
                 std::shared_ptr<std::string> versionToInstall,
                 std::shared_ptr<std::string> expectedVersion, std::string installedVersion,
                 std::function<void()> captureTargets,
                 std::function<void(const std::wstring&)> signalReady)
      : inner_(inner),
        eventName_(std::move(eventName)),
        versionToInstall_(std::move(versionToInstall)),
        expectedVersion_(std::move(expectedVersion)),
        installedVersion_(std::move(installedVersion)),
        captureTargets_(std::move(captureTargets)),
        signalReady_(std::move(signalReady)) {}

  bool AcquireLock() override { return inner_.AcquireLock(); }
  void ReleaseLock() override { inner_.ReleaseLock(); }
  bool FetchManifest(std::string* d, std::string* s) override { return inner_.FetchManifest(d, s); }
  std::string InstalledVersion() override { return inner_.InstalledVersion(); }
  bool Download(const ManifestFields& f) override { return inner_.Download(f); }

  bool VerifyDownload(const ManifestFields& f) override {
    const bool ok = inner_.VerifyDownload(f);
    if (ok) {
      *versionToInstall_ = f.version;
      *expectedVersion_ = f.version;
      if (captureTargets_) captureTargets_();  // before anyone is told they may leave
      if (signalReady_) signalReady_(eventName_);
    }
    return ok;
  }

  void DiscardDownload() override { inner_.DiscardDownload(); }
  bool PrepareForSwap() override { return inner_.PrepareForSwap(); }
  bool Quiesce() override { return inner_.Quiesce(); }
  bool Swap() override { return inner_.Swap(); }
  bool RegisterInstall() override { return inner_.RegisterInstall(); }
  RelaunchVerdict Relaunch() override { return inner_.Relaunch(); }
  bool HealthCheck() override { return inner_.HealthCheck(); }

  bool Rollback() override {
    // The files are going back to what was installed before, so anything checking the product
    // afterwards has to look for THAT version. Without this the health check following a rollback
    // would wait for the version the update was carrying, which is no longer on disk, and report
    // a failure the rollback had just finished preventing.
    *expectedVersion_ = installedVersion_;
    return inner_.Rollback();
  }

  void Commit() override { inner_.Commit(); }

 private:
  UpdateEffects& inner_;
  std::wstring eventName_;
  std::shared_ptr<std::string> versionToInstall_;
  std::shared_ptr<std::string> expectedVersion_;
  std::string installedVersion_;
  std::function<void()> captureTargets_;
  std::function<void(const std::wstring&)> signalReady_;
};

}  // namespace

bool UpdaterDeps::validate(std::string* detail) const {
  const auto fail = [detail](const char* why) {
    if (detail) *detail = why;
    return false;
  };
  if (!fetchText) return fail("fetchText not set");
  if (!fetchFile) return fail("fetchFile not set");
  if (!enumerateTargets) return fail("enumerateTargets not set");
  if (!requestStop) return fail("requestStop not set");
  if (!registrationOps.runProcess) return fail("registrationOps.runProcess not set");
  if (!registrationOps.createShortcut) return fail("registrationOps.createShortcut not set");
  if (!makeRelaunch) return fail("makeRelaunch not set");
  if (!verifier) return fail("verifier not set");
  if (!log) return fail("log not set");
  if (selfImagePath.empty()) return fail("selfImagePath not set");
  if (payloadNames.empty()) return fail("payloadNames not set");
  if (relaunchTable.empty()) return fail("relaunchTable not set");
  // signalReady may be absent: an update nobody is waiting on is an ordinary case.
  return true;
}

UpdaterDeps production_updater_deps(std::function<void(const std::string&)> log) {
  UpdaterDeps deps;
  deps.log = log ? std::move(log) : [](const std::string&) {};

  deps.fetchText = [](const std::string& url, size_t maxBytes, std::string* body,
                      std::string* error) {
    return https_get_text(url, maxBytes, body, error) == FetchStatus::Ok;
  };
  deps.fetchFile = [](const std::string& url, const std::wstring& destPath, uint64_t maxBytes,
                      std::string* error) {
    return https_get_file(url, destPath, maxBytes, error) == FetchStatus::Ok;
  };
  // The two calls that can reach a real GNLinkHost.exe.
  deps.enumerateTargets = []() { return enumerate_product_processes(product_image_names()); };
  deps.requestStop = [](const ProcessTarget& target) { return request_process_stop(target); };
  deps.registrationOps = production_registration_ops();
  deps.makeRelaunch = [](const RelaunchConfig& config, const std::vector<ProcessTarget>& stopped) {
    return make_relaunch_effects(config, stopped, product_images());
  };
  // The compiled-in trust anchor. A test passes its own instead, which is how a signed fixture
  // can be exercised without this ever changing.
  deps.verifier = default_verifier();
  deps.signalReady = [](const std::wstring& eventName) {
    if (eventName.empty()) return;
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName.c_str());
    if (!event) return;
    SetEvent(event);
    CloseHandle(event);
  };
  wchar_t self[MAX_PATH]{};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  deps.selfImagePath = self;
  // The executables come from the same list the process enumerator uses, so the set that is
  // stopped and the set that is replaced cannot drift apart; the two data files are named
  // alongside them.
  deps.payloadNames = product_image_names();
  deps.payloadNames.push_back(L"ui\shell.html");
  deps.payloadNames.push_back(L"ui\macro.html");
  deps.relaunchTable = product_images();
  return deps;
}

UpdaterEffects::UpdaterEffects(UpdaterOptions options, UpdaterDeps deps)
    : options_(std::move(options)), deps_(std::move(deps)) {}

bool UpdaterEffects::build(std::string* detail) {
  if (!deps_.validate(detail)) return false;

  UpdateEffectsConfig config;
  config.installDir = options_.installDir;
  config.stagingDir = options_.stagingDir;
  config.lockName = L"Global\\GNLinkUpdate";
  // What a swap replaces. The executables come from the same list the process enumerator uses, so
  // the set that is stopped and the set that is replaced cannot drift apart.
  config.payloadNames = product_image_names();
  config.payloadNames.push_back(L"ui\\shell.html");
  config.payloadNames.push_back(L"ui\\macro.html");
  config.registryRoot = options_.registryRoot;
  config.serviceName = options_.serviceName;
  config.expectedVersion = {};  // filled once the manifest says which version this is
  config.updaterImagePath = deps_.selfImagePath;

  const std::string manifestUrl = options_.manifestUrl;
  UpdaterDeps deps = deps_;
  // The step whose absence made every later stage unreachable: FetchManifest had nothing to
  // return, because nothing ever put anything there. Fetched once and kept, so the rest of the
  // attempt works from one snapshot -- re-fetching per stage would let the server change what is
  // being installed part way through.
  config.fetchManifest = [manifestUrl, deps](std::string* document, std::string* signatureHex) {
    std::string error;
    if (!deps.fetchText(manifestUrl, 64 * 1024, document, &error)) {
      deps.log("manifest fetch failed: " + error);
      return false;
    }
    // The detached signature lives beside the document, fetched separately so the bytes verified
    // are exactly the bytes that arrived with nothing wrapping them.
    if (!deps.fetchText(manifestUrl + ".sig", 4 * 1024, signatureHex, &error)) {
      deps.log("signature fetch failed: " + error);
      return false;
    }
    while (!signatureHex->empty() &&
           (signatureHex->back() == '\n' || signatureHex->back() == '\r' ||
            signatureHex->back() == ' ')) {
      signatureHex->pop_back();
    }
    return true;
  };

  config.fetchArtifact = [deps](const ManifestArtifact& artifact, const std::wstring& destPath) {
    std::string error;
    // The manifest's own size is the cap, so a body larger than what was signed for is refused
    // while it arrives rather than after it has been written.
    if (!deps.fetchFile(artifact.url, destPath, artifact.size, &error)) {
      deps.log("fetch failed for " + artifact.name + ": " + error);
      return false;
    }
    return true;
  };

  config.enumerateTargets = deps_.enumerateTargets;
  config.requestStop = deps_.requestStop;

  install::RegistrationTarget target;
  target.installDir = options_.installDir;
  target.setupPath = options_.installDir + L"\\GNLinkSetup.exe";
  target.productName = L"GNLink";
  target.clientShortcutName = L"GNLink";
  target.publisher = L"Chrono Studio";
  target.serviceName = options_.serviceName;
  target.firewallRuleName = L"GNLink";
  // From the option, not hardcoded. It was hardcoded, which made --registry-root a required
  // argument that named something it did not control.
  {
    void* hive = nullptr;
    std::wstring subkey;
    if (!split_registry_root(options_.registryRoot, &hive, &subkey)) {
      if (detail) *detail = "--registry-root is not a hive this build can use";
      return false;
    }
    target.uninstallRoot = static_cast<HKEY>(hive);
    target.uninstallSubkey = subkey;
  }
  target.hostExeName = L"GNLinkHost.exe";
  target.clientExeName = L"GNLinkClient.exe";
  target.serviceExeName = L"GNLinkInputService.exe";
  target.streamExeName = L"GNLinkStream.exe";
  registrationTarget_ = target;

  config_ = config;
  return true;
}

UpdateOutcome UpdaterEffects::run(const std::string& platform) {
  // The version this attempt is installing, learned from the verified manifest. Read late on
  // purpose: registration and relaunch are configured before run_update starts, and a value
  // filled in then would carry THIS binary's compile-time version into DisplayVersion and into
  // the health check -- the reference-point poisoning that ruled out re-running the old
  // installer, arrived at from the other side.
  auto versionToInstall = std::make_shared<std::string>();
  // Starts as what is installed now, so a rollback -- which puts the OLD files back -- checks the
  // health of the version that is actually on disk afterwards.
  auto expectedVersion = std::make_shared<std::string>(options_.installedVersion);
  auto stopped = std::make_shared<std::vector<ProcessTarget>>();

  UpdateEffectsConfig config = config_;
  const auto enumerate = config.enumerateTargets;
  config.enumerateTargets = [enumerate, stopped]() {
    const auto targets = enumerate();
    if (stopped->empty()) *stopped = targets;
    return targets;
  };

  // Built when each callback is actually invoked, so they see the verified version rather than a
  // placeholder captured before the manifest was read.
  install::RegistrationTarget baseTarget = registrationTarget_;
  install::RegistrationOps ops = deps_.registrationOps;
  auto registration = std::make_shared<RegistrationEffects>();
  const auto ensureRegistration = [baseTarget, ops, registration, versionToInstall]() {
    if (registration->apply) return;
    install::RegistrationTarget target = baseTarget;
    target.version = widen(*versionToInstall);
    *registration = make_registration_effects(target, ops);
  };
  config.captureRegistration = [ensureRegistration, registration]() {
    ensureRegistration();
    return registration->capture();
  };
  config.registerInstall = [ensureRegistration, registration, versionToInstall]() {
    if (versionToInstall->empty()) return false;  // nothing verified: nothing to register as
    ensureRegistration();
    return registration->apply();
  };
  config.restoreRegistration = [ensureRegistration, registration]() {
    ensureRegistration();
    return registration->restore();
  };

  RelaunchConfig relaunchConfig;
  relaunchConfig.installDir = options_.installDir;
  relaunchConfig.serviceName = options_.serviceName;
  relaunchConfig.healthLogPath = options_.healthLogPath;
  // Only the host writes a health report. When it was not running before the update, there is
  // nothing to wait for.
  relaunchConfig.healthReporterImage = L"GNLinkHost.exe";

  auto relaunchEffects = std::make_shared<RelaunchEffects>();
  UpdaterDeps deps = deps_;
  std::string* notice = &userNotice_;
  config.relaunch = [deps, relaunchConfig, stopped, relaunchEffects, expectedVersion, notice]() {
    RelaunchConfig live = relaunchConfig;
    live.expectedVersion = *expectedVersion;
    *relaunchEffects = deps.makeRelaunch(live, *stopped);
    // No relaunch to run means nothing came back. Reported as the severe verdict rather than the
    // mild one: an assembly that produced no relaunch has not established that the machine is
    // reachable, and guessing in the reassuring direction is how this class of defect starts.
    if (!relaunchEffects->relaunch) return RelaunchVerdict::RequiredMissing;
    const RelaunchVerdict verdict = relaunchEffects->relaunch();
    if (relaunchEffects->lastOutcomes) {
      for (const RelaunchOutcome& outcome : relaunchEffects->lastOutcomes()) {
        deps.log("relaunch " + to_utf8(outcome.imageName) + ": " + outcome.detail);
      }
    }
    if (relaunchEffects->userNotice) *notice = relaunchEffects->userNotice();
    return verdict;
  };
  // Wired to the same relaunch effects, so what is stopped is exactly what they started.
  config.releaseBeforeRollback = [relaunchEffects]() {
    if (!relaunchEffects->stopStarted) return 0;
    return relaunchEffects->stopStarted();
  };
  config.healthCheck = [deps, relaunchEffects]() {
    if (!relaunchEffects->healthCheck) return false;
    const bool ok = relaunchEffects->healthCheck();
    if (relaunchEffects->lastHealthDetail) deps.log("health: " + relaunchEffects->lastHealthDetail());
    return ok;
  };

  WindowsUpdateEffects effects(config);
  effects.set_installed_version(options_.installedVersion);
  // Reads the enumerator once and remembers the result, so identities exist before anyone is told
  // they may leave.
  const auto captureNow = [enumerate, stopped]() {
    if (stopped->empty()) *stopped = enumerate();
  };
  ReadySignaller signaller(effects, options_.readyEventName, versionToInstall, expectedVersion,
                           options_.installedVersion, captureNow, deps_.signalReady);
  const UpdateOutcome outcome = run_update(signaller, deps_.verifier, platform);
  verifiedVersion_ = *versionToInstall;
  return outcome;
}

const char* working_copy_result_name(WorkingCopyResult result) {
  switch (result) {
    case WorkingCopyResult::Started: return "started";
    case WorkingCopyResult::RefusedDirectory: return "refused-directory";
    case WorkingCopyResult::CreateFailed: return "create-failed";
    case WorkingCopyResult::CopyFailed: return "copy-failed";
    case WorkingCopyResult::LaunchFailed: return "launch-failed";
  }
  return "unknown";
}

WorkingCopyResult prepare_working_copy(const WorkingCopySteps& steps) {
  // The directory is judged BEFORE it is created and before anything is written into it. A check
  // that ran afterwards would be reporting on a directory that had already received an
  // executable this process is about to run elevated.
  if (steps.checkDirectory && !steps.checkDirectory()) return WorkingCopyResult::RefusedDirectory;
  if (steps.createDirectory && !steps.createDirectory()) return WorkingCopyResult::CreateFailed;
  if (steps.copySelf && !steps.copySelf()) return WorkingCopyResult::CopyFailed;
  if (steps.launch && !steps.launch()) return WorkingCopyResult::LaunchFailed;
  return WorkingCopyResult::Started;
}

}  // namespace remote60::native_poc::update
