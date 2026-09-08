// The updater's ordering and failure rules, driven against a fake seam.
//
// Every case here is a decision that becomes hard to reproduce once real process termination and
// file replacement are attached -- which is exactly why they are pinned before that happens. The
// ones that matter most:
//
//   * a server that is down cannot disturb a working installation
//   * Prepare or Quiesce failing abandons the attempt WITHOUT touching the disk, and without
//     escalating to a forced shutdown
//   * once the swap starts, everything through Health rolls back
//   * a failed Relaunch does NOT roll back a good install
//
// The fake records which effects ran and in what order, so "the swap was never attempted" is
// asserted as a fact about calls rather than inferred from a result code.

#include "update_state_machine.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace remote60::native_poc::update;

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

/** A manifest that parses and is newer than the fake installed version. */
const char* kDocument =
    "schema=2\n"
    "releaseId=r-0.2.105\n"
    "platform=windows\n"
    "arch=x64\n"
    "version=0.2.105\n"
    "artifact=GNLinkSetup.exe|3475968|0000000000000000000000000000000000000000000000000000000000000000|https://u.example/s.exe\n";

/** 128 hex characters, so it decodes to a 64-byte signature. Content is irrelevant to the fake. */
const char* kSignatureHex =
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000";

SignatureVerifier accepting() {
  return [](const std::string&, const std::vector<uint8_t>&) { return true; };
}
SignatureVerifier rejecting() {
  return [](const std::string&, const std::vector<uint8_t>&) { return false; };
}

/** Records every call, and fails whichever step the test asks it to. */
class FakeEffects : public UpdateEffects {
 public:
  // Which step should fail. Everything else succeeds.
  bool lockAvailable = true;
  bool manifestReachable = true;
  bool downloadOk = true;
  bool verifyOk = true;
  bool prepareOk = true;
  bool quiesceOk = true;
  bool swapOk = true;
  bool registerOk = true;
  bool relaunchOk = true;
  bool healthOk = true;
  bool rollbackOk = true;
  std::string installed = "0.2.104";
  std::string document = kDocument;

  std::vector<std::string> calls;
  int discardCount = 0;
  int commitCount = 0;
  bool lockReleased = false;

  /** How many times a step ran. "once, not in a loop" is a claim worth being able to make. */
  int count(const std::string& name) const {
    int n = 0;
    for (const std::string& c : calls) {
      if (c == name) ++n;
    }
    return n;
  }

  bool ran(const std::string& name) const {
    for (const auto& c : calls) if (c == name) return true;
    return false;
  }

  bool AcquireLock() override { calls.push_back("AcquireLock"); return lockAvailable; }
  void ReleaseLock() override { calls.push_back("ReleaseLock"); lockReleased = true; }
  bool FetchManifest(std::string* doc, std::string* sig) override {
    calls.push_back("FetchManifest");
    if (!manifestReachable) return false;
    *doc = document;
    *sig = kSignatureHex;
    return true;
  }
  std::string InstalledVersion() override { calls.push_back("InstalledVersion"); return installed; }
  bool Download(const ManifestFields&) override { calls.push_back("Download"); return downloadOk; }
  bool VerifyDownload(const ManifestFields&) override { calls.push_back("VerifyDownload"); return verifyOk; }
  void DiscardDownload() override { calls.push_back("DiscardDownload"); ++discardCount; }
  bool PrepareForSwap() override { calls.push_back("PrepareForSwap"); return prepareOk; }
  bool Quiesce() override { calls.push_back("Quiesce"); return quiesceOk; }
  bool Swap() override { calls.push_back("Swap"); return swapOk; }
  bool RegisterInstall() override { calls.push_back("RegisterInstall"); return registerOk; }
  bool Relaunch() override { calls.push_back("Relaunch"); return relaunchOk; }
  bool HealthCheck() override { calls.push_back("HealthCheck"); return healthOk; }
  bool Rollback() override { calls.push_back("Rollback"); return rollbackOk; }
  void Commit() override { calls.push_back("Commit"); ++commitCount; }
};

std::string joined(const std::vector<std::string>& v) {
  std::string s;
  for (const auto& c : v) { if (!s.empty()) s += ","; s += c; }
  return s;
}

/** Shorthand for "the destructive steps never ran". */
bool disk_untouched(const FakeEffects& f) {
  return !f.ran("Swap") && !f.ran("RegisterInstall") && !f.ran("Rollback");
}

}  // namespace

int main() {
  // ---------------------------------------------------------------- happy path

  {
    FakeEffects f;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("happy path -> Updated", o.result == UpdateResult::Updated, result_name(o.result));
    check("happy path visits every stage in order",
          o.visited == std::vector<UpdateState>{
              UpdateState::Idle, UpdateState::CheckRequested, UpdateState::Evaluate,
              UpdateState::Download, UpdateState::Verify, UpdateState::Prepare,
              UpdateState::Quiesce, UpdateState::Swap, UpdateState::Register,
              UpdateState::Relaunch, UpdateState::Health, UpdateState::Done},
          joined(f.calls));
    check("staging is cleaned up on success", f.discardCount == 1, std::to_string(f.discardCount));
    check("the update is committed exactly once", f.commitCount == 1, std::to_string(f.commitCount));
    check("lock is released", f.lockReleased);
  }

  // ---------------------------------------------------------------- nothing to do

  {
    FakeEffects f;
    f.lockAvailable = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("lock held -> NothingToDo", o.result == UpdateResult::NothingToDo, result_name(o.result));
    check("lock held -> nothing else is called at all",
          f.calls == std::vector<std::string>{"AcquireLock"}, joined(f.calls));
    check("lock held -> never waited on (no manifest fetch)", !f.ran("FetchManifest"));
  }

  {
    // The rule that matters most for a product people are using: an unreachable server is
    // allowed to do nothing at all.
    FakeEffects f;
    f.manifestReachable = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("server unreachable -> NothingToDo", o.result == UpdateResult::NothingToDo,
          result_name(o.result));
    check("server unreachable -> disk untouched", disk_untouched(f), joined(f.calls));
    check("server unreachable -> no rollback of a working install", !f.ran("Rollback"));
    check("server unreachable -> lock released", f.lockReleased);
  }

  {
    FakeEffects f;
    const UpdateOutcome o = run_update(f, rejecting(), "windows");
    check("bad signature -> NothingToDo", o.result == UpdateResult::NothingToDo, result_name(o.result));
    check("bad signature -> nothing downloaded", !f.ran("Download"), joined(f.calls));
    check("bad signature -> installed version never even read", !f.ran("InstalledVersion"));
    check("bad signature -> disk untouched", disk_untouched(f));
  }

  {
    FakeEffects f;
    f.installed = "0.2.105";
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("same version -> NothingToDo", o.result == UpdateResult::NothingToDo, result_name(o.result));
    check("same version -> nothing downloaded", !f.ran("Download"));
  }
  {
    FakeEffects f;
    f.installed = "0.2.106";
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("newer installed -> NothingToDo", o.result == UpdateResult::NothingToDo,
          result_name(o.result));
    check("newer installed -> nothing downloaded", !f.ran("Download"));
  }
  {
    // Numeric comparison reached through the real path, not a re-implementation.
    FakeEffects f;
    f.installed = "0.2.99";
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("0.2.105 over 0.2.99 -> Updated (numeric, not lexicographic)",
          o.result == UpdateResult::Updated, result_name(o.result));
  }
  {
    FakeEffects f;
    const UpdateOutcome o = run_update(f, accepting(), "android");
    check("platform mismatch -> NothingToDo", o.result == UpdateResult::NothingToDo,
          result_name(o.result));
    check("platform mismatch -> nothing downloaded", !f.ran("Download"));
  }

  // ---------------------------------------------------------------- abandoned before the swap

  {
    FakeEffects f;
    f.downloadOk = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("download fails -> AbandonedBeforeSwap", o.result == UpdateResult::AbandonedBeforeSwap,
          result_name(o.result));
    check("download fails -> partial bytes discarded", f.discardCount == 1,
          std::to_string(f.discardCount));
    check("download fails -> disk untouched", disk_untouched(f));
  }
  {
    FakeEffects f;
    f.verifyOk = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("artifact does not match manifest -> AbandonedBeforeSwap",
          o.result == UpdateResult::AbandonedBeforeSwap, result_name(o.result));
    check("mismatch -> artifact discarded", f.discardCount == 1, std::to_string(f.discardCount));
    check("mismatch -> disk untouched", disk_untouched(f));
  }
  {
    FakeEffects f;
    f.prepareOk = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("prepare fails -> AbandonedBeforeSwap", o.result == UpdateResult::AbandonedBeforeSwap,
          result_name(o.result));
    check("prepare fails -> quiesce is NOT attempted (no escalation to force)", !f.ran("Quiesce"),
          joined(f.calls));
    check("prepare fails -> disk untouched", disk_untouched(f));
    check("prepare fails -> verified download is KEPT for the next attempt", f.discardCount == 0,
          std::to_string(f.discardCount));
  }
  {
    FakeEffects f;
    f.quiesceOk = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("quiesce fails -> AbandonedBeforeSwap", o.result == UpdateResult::AbandonedBeforeSwap,
          result_name(o.result));
    check("quiesce fails -> swap is NOT attempted", !f.ran("Swap"), joined(f.calls));
    check("quiesce fails -> disk untouched", disk_untouched(f));
    check("quiesce fails -> download kept", f.discardCount == 0, std::to_string(f.discardCount));
  }

  // ---------------------------------------------------------------- rollback

  {
    FakeEffects f;
    f.swapOk = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("swap fails -> RolledBack", o.result == UpdateResult::RolledBack, result_name(o.result));
    check("swap fails -> rollback ran", f.ran("Rollback"), joined(f.calls));
    // The rule the Commit method exists for: nothing commits on a path that rolls back, so the
    // backups a rollback needs are still there when it runs.
    check("swap fails -> never committed", f.commitCount == 0, std::to_string(f.commitCount));
    check("swap fails -> registration never ran", !f.ran("RegisterInstall"));
  }
  {
    // And the outcome when the restore works but nothing comes back up. Worse than RolledBack for
    // a remote user: the files are right and the machine is unreachable.
    FakeEffects f;
    f.registerOk = false;
    f.relaunchOk = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("rolled back but not relaunched is its own outcome",
          o.result == UpdateResult::RolledBackNotRelaunched, result_name(o.result));
    check("...and it is not reported as an ordinary rollback",
          o.result != UpdateResult::RolledBack);
    // No retry. A second identical attempt is unlikely to differ, and looping here would sit
    // between the user and a machine that is already in its restored state.
    check("relaunch is attempted once, not repeated", f.count("Relaunch") == 1,
          std::to_string(f.count("Relaunch")));
  }
  {
    FakeEffects f;
    f.registerOk = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("registration fails -> RolledBack", o.result == UpdateResult::RolledBack,
          result_name(o.result));
    check("registration fails -> rollback ran", f.ran("Rollback"));
    // This assertion used to require the opposite, and it was protecting a real defect: putting
    // the files back and stopping there leaves a machine whose files are correct and whose host
    // is not running. The host is one of the processes stopped to do the swap, so for a remote
    // user that is an unreachable machine with no way in to fix it. Restoring is only half of a
    // rollback; the other half is that what was running is running again.
    check("registration fails -> the previous version is started again", f.ran("Relaunch"));
    check("registration fails -> and its health is checked", f.ran("HealthCheck"));
  }
  {
    FakeEffects f;
    f.healthOk = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("health fails -> RolledBack", o.result == UpdateResult::RolledBack, result_name(o.result));
    check("health fails -> rollback ran", f.ran("Rollback"));
    check("health fails -> never committed, so the backups survived for it",
          f.commitCount == 0, std::to_string(f.commitCount));
  }
  {
    FakeEffects f;
    f.swapOk = false;
    f.rollbackOk = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("swap AND rollback fail -> RollbackFailed, named distinctly",
          o.result == UpdateResult::RollbackFailed, result_name(o.result));
  }

  // ---------------------------------------------------------------- relaunch is not a rollback

  {
    FakeEffects f;
    f.relaunchOk = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("relaunch fails -> UpdatedButNotRelaunched",
          o.result == UpdateResult::UpdatedButNotRelaunched, result_name(o.result));
    check("relaunch fails -> does NOT roll a good install back", !f.ran("Rollback"),
          joined(f.calls));
    check("relaunch fails -> health is not claimed", !o.entered(UpdateState::Health));
    check("relaunch fails -> staging still cleaned up", f.discardCount == 1,
          std::to_string(f.discardCount));
    check("relaunch fails -> still committed (this outcome does not roll back)",
          f.commitCount == 1, std::to_string(f.commitCount));
  }

  // ---------------------------------------------------------------- the lock always comes back

  {
    // Whatever happens, the next attempt must not find the lock stuck. Checked across the paths
    // that leave at different depths.
    struct Variant { const char* name; void (*apply)(FakeEffects&); };
    const Variant variants[] = {
        {"lock unavailable", [](FakeEffects& f) { f.lockAvailable = false; }},
        {"server down", [](FakeEffects& f) { f.manifestReachable = false; }},
        {"download fails", [](FakeEffects& f) { f.downloadOk = false; }},
        {"quiesce fails", [](FakeEffects& f) { f.quiesceOk = false; }},
        {"swap fails", [](FakeEffects& f) { f.swapOk = false; }},
        {"rollback fails", [](FakeEffects& f) { f.swapOk = false; f.rollbackOk = false; }},
        {"relaunch fails", [](FakeEffects& f) { f.relaunchOk = false; }},
        {"success", [](FakeEffects&) {}},
    };
    for (const Variant& v : variants) {
      FakeEffects f;
      v.apply(f);
      (void)run_update(f, accepting(), "windows");
      // AcquireLock returning false means nothing was held, so nothing has to be released.
      const bool ok = f.lockAvailable ? f.lockReleased : !f.lockReleased;
      check(std::string("lock is not left held: ") + v.name, ok, joined(f.calls));
    }
  }

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED")
            << "  (" << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
