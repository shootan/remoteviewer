#include "updater_effects.hpp"

#include <windows.h>

#include "product_version.hpp"
#include "update_handoff.hpp"
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
                 std::function<bool(const std::wstring&)> signalReady,
                 std::function<bool(const std::wstring&, uint32_t)> awaitAck,
                 WindowsUpdateEffects* concrete, std::function<void(const std::string&)> log)
      : inner_(inner),
        eventName_(std::move(eventName)),
        versionToInstall_(std::move(versionToInstall)),
        expectedVersion_(std::move(expectedVersion)),
        installedVersion_(std::move(installedVersion)),
        captureTargets_(std::move(captureTargets)),
        signalReady_(std::move(signalReady)),
        awaitAck_(std::move(awaitAck)),
        concrete_(concrete),
        log_(std::move(log)) {}

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
      // Recorded, not discarded. Whether anyone received this decides whether the product may be
      // stopped, and that decision is made at PrepareForSwap -- the last point before anything
      // is touched.
      signalDelivered_ = signalReady_ ? signalReady_(eventName_) : false;
      if (!signalDelivered_ && log_ && !eventName_.empty()) {
        log_("the ready signal could not be delivered -- nothing is waiting for this update");
      }
    }
    return ok;
  }

  void DiscardDownload() override { inner_.DiscardDownload(); }
  bool PrepareForSwap() override {
    // The gate, and it sits here because this is the last step before anything is stopped or
    // moved. Failing here abandons the attempt with the disk untouched, which is the correct
    // outcome of a handover nobody completed.
    if (!eventName_.empty()) {
      const bool acked = awaitAck_ ? awaitAck_(eventName_, kAckTimeoutMs) : false;
      std::string why;
      if (!may_stop_the_product(signalDelivered_, acked, &why)) {
        if (log_) log_("not going ahead: " + why);
        return false;
      }
      if (log_) log_(why);
    }
    return inner_.PrepareForSwap();
  }
  bool Quiesce() override { return inner_.Quiesce(); }
  // Logged AT the failure, not afterwards. Read later it is gone: the rollback that follows runs
  // its own steps and each one overwrites the message, so an operator was left with "swap failed"
  // and no idea which file would not move.
  bool Swap() override { return note("swap", inner_.Swap()); }
  bool RegisterInstall() override { return note("register", inner_.RegisterInstall()); }
  RelaunchVerdict RelaunchRequired() override { return inner_.RelaunchRequired(); }
  RelaunchVerdict RelaunchOptional() override { return inner_.RelaunchOptional(); }
  bool HealthCheck() override { return inner_.HealthCheck(); }

  /**
   * How long to wait for the acknowledgement.
   *
   * Generous, because the answer comes from a UI thread that may be busy, and short enough that
   * an update does not sit indefinitely against a caller that has gone. Timing out here costs
   * nothing: nothing has been touched.
   */
  static constexpr uint32_t kAckTimeoutMs = 30000;

  bool note(const char* step, bool ok) {
    if (!ok && log_ && concrete_) {
      log_(std::string(step) + " failed: " + concrete_->last_error());
    }
    return ok;
  }

  bool Rollback() override {
    // The files are going back to what was installed before, so anything checking the product
    // afterwards has to look for THAT version. Without this the health check following a rollback
    // would wait for the version the update was carrying, which is no longer on disk, and report
    // a failure the rollback had just finished preventing.
    *expectedVersion_ = installedVersion_;
    return note("rollback", inner_.Rollback());
  }

  void Commit() override { inner_.Commit(); }

 private:
  UpdateEffects& inner_;
  std::wstring eventName_;
  std::shared_ptr<std::string> versionToInstall_;
  std::shared_ptr<std::string> expectedVersion_;
  std::string installedVersion_;
  std::function<void()> captureTargets_;
  std::function<bool(const std::wstring&)> signalReady_;
  std::function<bool(const std::wstring&, uint32_t)> awaitAck_;
  bool signalDelivered_ = false;
  WindowsUpdateEffects* concrete_ = nullptr;
  std::function<void(const std::string&)> log_;
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
  if (lockName.empty()) return fail("lockName not set");
  if (payloadNames.empty()) return fail("payloadNames not set");
  if (relaunchTable.empty()) return fail("relaunchTable not set");
  // signalReady and awaitAck may be absent: an update nobody is waiting on is an ordinary case,
  // and then there is no handshake to complete. What must not happen is one without the other --
  // signalling with no way to hear the answer is exactly the shape of the defect they exist for.
  if (static_cast<bool>(signalReady) != static_cast<bool>(awaitAck)) {
    return fail("signalReady and awaitAck must be supplied together");
  }
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
    if (eventName.empty()) return false;
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName.c_str());
    // Could not be opened, so nobody holds it. That is not "signalled anyway" -- it is the
    // absence of anyone to signal, and the caller has to be able to tell the difference.
    if (!event) return false;
    const bool set = SetEvent(event) != FALSE;
    CloseHandle(event);
    return set;
  };
  deps.awaitAck = [](const std::wstring& readyEventName, uint32_t timeoutMs) {
    const std::wstring ackName = make_ack_event_name(readyEventName);
    if (ackName.empty()) return false;
    HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, ackName.c_str());
    if (!event) return false;
    const bool acked = WaitForSingleObject(event, timeoutMs) == WAIT_OBJECT_0;
    CloseHandle(event);
    return acked;
  };
  wchar_t self[MAX_PATH]{};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  deps.selfImagePath = self;
  // The executables come from the same list the process enumerator uses, so the set that is
  // stopped and the set that is replaced cannot drift apart; the two data files are named
  // alongside them.
  // The machine-wide name, because one machine has one installation and two updaters replacing
  // the same directory is the thing this exists to prevent.
  deps.lockName = L"Global\\GNLinkUpdate";
  deps.payloadNames = product_image_names();
  deps.payloadNames.push_back(L"ui\\shell.html");
  deps.payloadNames.push_back(L"ui\\macro.html");
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
  config.lockName = deps_.lockName;
  // What a swap replaces. The executables come from the same list the process enumerator uses, so
  // the set that is stopped and the set that is replaced cannot drift apart.
  // What a swap replaces, as supplied. It used to be built here from the product's own
  // lists, which meant the assembly could only ever be run against the real product -- and
  // a test that handed it anything else was quietly ignored, so its scenarios passed while
  // every swap failed for want of a file nobody had staged.
  config.payloadNames = deps_.payloadNames;
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
  // Built ONCE, on whichever phase runs first, and reused by the other.
  //
  // The two phases share one set of relaunch effects on purpose: the handles the required phase
  // takes are the ones a rollback stops, and the log mark the health check reads is taken before
  // anything starts. Building a second set for the optional phase would take a second mark, after
  // the host had already written its banner, and the health check would then be reading against a
  // mark that came after the evidence it is looking for.
  auto ensure = [deps, relaunchConfig, stopped, relaunchEffects, expectedVersion]() {
    if (relaunchEffects->relaunchRequired || relaunchEffects->relaunchOptional) return;
    RelaunchConfig live = relaunchConfig;
    live.expectedVersion = *expectedVersion;
    *relaunchEffects = deps.makeRelaunch(live, *stopped);
  };
  // Outcomes accumulate across the two phases, so this logs only what the phase just added --
  // otherwise the optional phase would repeat every line the required one already wrote.
  auto reported = std::make_shared<size_t>(0);
  const auto report = [deps, relaunchEffects, notice, reported](const char* phase) {
    if (relaunchEffects->lastOutcomes) {
      const std::vector<RelaunchOutcome> all = relaunchEffects->lastOutcomes();
      if (*reported > all.size()) *reported = 0;  // the list was reset for a new attempt
      for (size_t i = *reported; i < all.size(); ++i) {
        deps.log(std::string("relaunch (") + phase + ") " + to_utf8(all[i].imageName) + ": " +
                 all[i].detail);
      }
      *reported = all.size();
    }
    // Read after both phases have contributed, so a client that did not come back is still named
    // even though the phase that failed was not the last one to run.
    if (relaunchEffects->userNotice) {
      const std::string current = relaunchEffects->userNotice();
      if (!current.empty()) *notice = current;
    }
  };
  config.relaunchRequired = [ensure, report, relaunchEffects]() {
    ensure();
    // Nothing to run means nothing came back. Reported as the severe verdict rather than the mild
    // one: an assembly that produced no relaunch has not established that the machine is
    // reachable, and guessing in the reassuring direction is how this class of defect starts.
    if (!relaunchEffects->relaunchRequired) return RelaunchVerdict::RequiredMissing;
    const RelaunchVerdict verdict = relaunchEffects->relaunchRequired();
    report("required");
    return verdict;
  };
  config.relaunchOptional = [ensure, report, relaunchEffects]() {
    ensure();
    // The mild verdict when there is nothing to run. This phase happens after the commit, so
    // there is no longer a decision it could change -- reporting it as severe would turn a
    // finished, healthy update into an alarm.
    if (!relaunchEffects->relaunchOptional) return RelaunchVerdict::OptionalMissing;
    const RelaunchVerdict verdict = relaunchEffects->relaunchOptional();
    report("optional");
    return verdict;
  };
  // Wired to the same relaunch effects, so what is stopped is exactly what they started.
  config.releaseBeforeRollback = [deps, relaunchEffects]() {
    if (!relaunchEffects->stopStarted) return true;  // nothing was started, nothing to stop
    const RelaunchEffects::StopReport report = relaunchEffects->stopStarted();
    for (const std::wstring& name : report.unstoppable) {
      deps.log("could not stop " + to_utf8(name) + " -- it was started by this attempt but there "
               "is no proof of ownership, or it did not exit");
    }
    // False stops the rollback before it touches a file. Restoring while something may still hold
    // the files produces a half-restored installation, and the reason looks like the rollback.
    return report.complete();
  };
  config.healthCheck = [deps, relaunchEffects]() {
    if (!relaunchEffects->healthCheck) return false;
    const bool ok = relaunchEffects->healthCheck();
    if (relaunchEffects->lastHealthDetail) deps.log("health: " + relaunchEffects->lastHealthDetail());
    return ok;
  };

  // Nothing runs until every seam is filled. This is the check that would have caught the manifest
  // fetch that was never wired and the version that never reached registration: both were
  // assembled, both were exercised through a config a TEST had filled in, and production shipped
  // with the field empty. An unwired seam is not a failure -- it is a step that quietly does not
  // happen, which is far harder to see.
  {
    const std::vector<std::string> missing = config.unwired();
    unwiredSeams_ = missing;
    if (!missing.empty()) {
      std::string names;
      for (const std::string& name : missing) {
        if (!names.empty()) names += ", ";
        names += name;
      }
      deps_.log("refusing to run: these are not wired -- " + names);
      UpdateOutcome broken;
      broken.result = UpdateResult::AbandonedBeforeSwap;
      broken.detail = "the updater was not fully assembled: " + names;
      return broken;
    }
  }

  WindowsUpdateEffects effects(config);
  effects.set_installed_version(options_.installedVersion);
  // Reads the enumerator once and remembers the result, so identities exist before anyone is told
  // they may leave.
  const auto captureNow = [enumerate, stopped]() {
    if (stopped->empty()) *stopped = enumerate();
  };
  ReadySignaller signaller(effects, options_.readyEventName, versionToInstall, expectedVersion,
                           options_.installedVersion, captureNow, deps_.signalReady,
                           deps_.awaitAck, &effects, deps_.log);
  const UpdateOutcome outcome = run_update(signaller, deps_.verifier, platform);
  verifiedVersion_ = *versionToInstall;
  lastEffectsError_ = effects.last_error();
  orphanedBackups_ = effects.orphaned_backups();
  for (const std::wstring& name : orphanedBackups_) {
    deps_.log("a backup could not be removed and is still beside the installation: " +
              to_utf8(name) + ".gnlink-old");
  }
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
