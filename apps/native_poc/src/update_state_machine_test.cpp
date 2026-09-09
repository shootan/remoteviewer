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
  /**
   * What the relaunch reports. A verdict, not a bool, because the machine differs on whether the
   * thing that did not come back was one it needs.
   */
  RelaunchVerdict requiredVerdict = RelaunchVerdict::AllBack;
  /**
   * What the OPTIONAL phase reports. Its own knob, because the two phases now happen at different
   * points in the sequence and a case usually means one or the other -- "the client did not come
   * back" and "the host did not come back" were the same field, which made it impossible to say
   * that the client was started after the commit and the host before it.
   */
  RelaunchVerdict optionalVerdict = RelaunchVerdict::AllBack;
  /**
   * What the relaunch of the RESTORED build reports, when it differs. Absent means the same
   * verdict -- fine for most cases, but a test that wants to see a clean RolledBack after a new
   * build failed needs the second attempt to succeed where the first did not.
   */
  const RelaunchVerdict* requiredAfterRollback = nullptr;
  bool healthOk = true;
  /**
   * What health says about the RESTORED build, when that differs from what it said about the new
   * one. -1 means "the same answer", which is what every case wanted until a case needed the new
   * build to be unhealthy and the old one fine -- the two are different questions asked of
   * different installations, and one bool could not tell them apart.
   */
  int healthAfterRollback = -1;
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
  RelaunchVerdict RelaunchRequired() override {
    const bool restored = ran("Rollback");
    calls.push_back("RelaunchRequired");
    if (restored && requiredAfterRollback) return *requiredAfterRollback;
    return requiredVerdict;
  }
  RelaunchVerdict RelaunchOptional() override {
    calls.push_back("RelaunchOptional");
    return optionalVerdict;
  }
  bool HealthCheck() override {
    const bool restored = ran("Rollback");
    calls.push_back("HealthCheck");
    if (restored && healthAfterRollback >= 0) return healthAfterRollback != 0;
    return healthOk;
  }
  bool Rollback() override { calls.push_back("Rollback"); return rollbackOk; }
  void Commit() override { calls.push_back("Commit"); ++commitCount; }
};

std::string joined(const std::vector<std::string>& v) {
  std::string s;
  for (const auto& c : v) { if (!s.empty()) s += ","; s += c; }
  return s;
}

/** Whether `first` ran before `second`. Both must have run. */
bool ran_before(const FakeEffects& f, const std::string& first, const std::string& second) {
  int a = -1, b = -1;
  for (size_t i = 0; i < f.calls.size(); ++i) {
    if (a < 0 && f.calls[i] == first) a = static_cast<int>(i);
    if (b < 0 && f.calls[i] == second) b = static_cast<int>(i);
  }
  return a >= 0 && b >= 0 && a < b;
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
    // A rollback whose restored build DOES come up healthy. The distinction the case above cannot
    // make on its own: without this, "RestoredButUnhealthy" could be what every rollback reports.
    FakeEffects f;
    f.healthOk = false;
    const UpdateOutcome first = run_update(f, accepting(), "windows");
    check("an unhealthy restore is not called a clean rollback",
          first.result == UpdateResult::RestoredButUnhealthy, result_name(first.result));

    FakeEffects g;
    g.registerOk = false;  // forces a rollback with health still answering yes
    const UpdateOutcome second = run_update(g, accepting(), "windows");
    check("a rollback whose restore is healthy is a clean rollback",
          second.result == UpdateResult::RolledBack, result_name(second.result));
  }
  {
    // The contract that a single bool could not express. A client that did not come back is not
    // a reason to undo a good install; a host that did not is, because a machine nobody can reach
    // is worth less than an older one somebody can.
    FakeEffects f;
    f.optionalVerdict = RelaunchVerdict::OptionalMissing;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("only an optional image missing -> the update stands",
          o.result == UpdateResult::UpdatedButNotRelaunched, result_name(o.result));
    check("...and it is committed, because it is not going back", f.commitCount == 1,
          std::to_string(f.commitCount));
    check("...and no rollback happened", !f.ran("Rollback"));
    // The order, not just the outcome. This branch used to commit without health having been
    // asked at all, so "committed" was true here for the wrong reason and the case below could
    // not exist.
    check("...and health was asked BEFORE the backups were dropped",
          ran_before(f, "HealthCheck", "Commit"), joined(f.calls));
  }
  {
    // Host launched-but-unhealthy + client launch fails.
    //
    // The two halves are individually harmless and together were a trap. The relaunch reports
    // OptionalMissing because the client did not come back -- which on its own is not worth
    // undoing an install for. But the host DID launch, so the verdict says nothing is missing
    // that the machine needs, and the host then failed to come up. The old order committed on the
    // optional verdict alone, deleted the backups, and reported a partial success for a machine
    // that was not reachable and had nothing left to go back to.
    //
    // "Required started" is not "required healthy": CreateProcess returning tells you a process
    // exists, and health is the only thing that asks whether the product is answering.
    FakeEffects f;
    f.optionalVerdict = RelaunchVerdict::OptionalMissing;
    f.healthOk = false;             // the host came up and is not answering
    f.healthAfterRollback = 1;      // the version it goes back to is fine
    const RelaunchVerdict backUp = RelaunchVerdict::AllBack;
    f.requiredAfterRollback = &backUp;  // and it comes back complete
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("unhealthy host + missing client -> rolled back, not a partial success",
          o.result == UpdateResult::RolledBack, result_name(o.result));
    check("...and NOT reported as UpdatedButNotRelaunched",
          o.result != UpdateResult::UpdatedButNotRelaunched, result_name(o.result));
    check("...and the backups were never dropped", f.commitCount == 0,
          std::to_string(f.commitCount));
    check("...and the rollback actually ran", f.ran("Rollback"), joined(f.calls));
    check("...and health was asked before anything was decided",
          ran_before(f, "HealthCheck", "Rollback"), joined(f.calls));
  }
  {
    // The counter-control for the case above: same missing client, host healthy. If the new order
    // simply rolled back whenever something was missing, this would fail -- so the two together
    // say health is what decides, not the verdict.
    FakeEffects f;
    f.optionalVerdict = RelaunchVerdict::OptionalMissing;
    f.healthOk = true;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("healthy host + missing client -> the install still stands",
          o.result == UpdateResult::UpdatedButNotRelaunched, result_name(o.result));
    check("...and it did not roll back", !f.ran("Rollback"), joined(f.calls));
  }
  {
    FakeEffects f;
    f.requiredVerdict = RelaunchVerdict::RequiredMissing;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("something the machine needs missing -> rollback", f.ran("Rollback"));
    check("...and NOT committed, because the backups are the way back", f.commitCount == 0,
          std::to_string(f.commitCount));
    check("...and the result is a rollback, not a partial success",
          o.result == UpdateResult::RolledBack ||
              o.result == UpdateResult::RolledBackNotRelaunched ||
              o.result == UpdateResult::RestoredButUnhealthy,
          result_name(o.result));
    check("...and it is never reported as UpdatedButNotRelaunched",
          o.result != UpdateResult::UpdatedButNotRelaunched, result_name(o.result));
  }
  {
    // Giving up after a caller has already been released on this update's behalf.
    //
    // Nothing on disk was touched -- no file replaced, no registration changed -- and yet the
    // product may be gone, because it was asked to make way and then nobody asked it back. For a
    // remote user that is an unreachable machine reached without a single thing going wrong,
    // which is harder to diagnose than a failed update precisely because there is no damage.
    FakeEffects f;
    f.prepareOk = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("prepare fails -> what left is started again",
          f.ran("RelaunchRequired") && f.ran("RelaunchOptional"), joined(f.calls));
    check("prepare fails -> AbandonedBeforeSwap", o.result == UpdateResult::AbandonedBeforeSwap,
          result_name(o.result));
    check("prepare fails -> nothing was swapped or registered",
          !f.ran("Swap") && !f.ran("RegisterInstall"));
  }
  {
    FakeEffects f;
    f.prepareOk = false;
    f.optionalVerdict = RelaunchVerdict::OptionalMissing;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    // Distinguished, because "we changed nothing" and "the machine is reachable" are separate
    // claims and only one of them is true here.
    check("abandoned and not relaunched is its own outcome",
          o.result == UpdateResult::AbandonedNotRelaunched, result_name(o.result));
    check("...and not reported as an ordinary abandon",
          o.result != UpdateResult::AbandonedBeforeSwap);
    check("relaunch is attempted once per phase",
          f.count("RelaunchRequired") == 1 && f.count("RelaunchOptional") == 1,
          joined(f.calls));
  }
  {
    FakeEffects f;
    f.quiesceOk = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("quiesce fails -> what stopped is started again",
          f.ran("RelaunchRequired") && f.ran("RelaunchOptional"), joined(f.calls));
    check("quiesce fails -> still an abandon", o.result == UpdateResult::AbandonedBeforeSwap,
          result_name(o.result));
  }
  {
    // Before anyone could have left: the download never completed, so no caller was released and
    // no targets were captured. Relaunch is still called and has nothing to do -- which is why it
    // is safe to call it on every abandon path rather than guessing which ones need it.
    FakeEffects f;
    f.downloadOk = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("download fails -> abandoned", o.result == UpdateResult::AbandonedBeforeSwap,
          result_name(o.result));
    check("download fails -> nothing was swapped", !f.ran("Swap"));
  }
  {
    // And the outcome when the restore works but nothing comes back up. Worse than RolledBack for
    // a remote user: the files are right and the machine is unreachable.
    // "Rolled back and the machine is unreachable" now means the REQUIRED images did not come
    // back after the restore. It used to mean any relaunch failure, which lumped a client window
    // that did not reopen in with a host nobody can reach -- and those are not the same event for
    // the person on the other end.
    FakeEffects f;
    f.registerOk = false;
    const RelaunchVerdict stillMissing = RelaunchVerdict::RequiredMissing;
    f.requiredAfterRollback = &stillMissing;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("rolled back but not relaunched is its own outcome",
          o.result == UpdateResult::RolledBackNotRelaunched, result_name(o.result));
    check("...and it is not reported as an ordinary rollback",
          o.result != UpdateResult::RolledBack);
    // No retry. A second identical attempt is unlikely to differ, and looping here would sit
    // between the user and a machine that is already in its restored state.
    check("relaunch is attempted once, not repeated",
          f.count("RelaunchRequired") == 1, joined(f.calls));
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
    check("registration fails -> the previous version is started again",
          f.ran("RelaunchRequired"), joined(f.calls));
    check("registration fails -> and its health is checked", f.ran("HealthCheck"));
  }
  {
    FakeEffects f;
    f.healthOk = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    // The fake's health check answers the same way after the rollback as before it, so the
    // restored build is unhealthy too -- and that is now reported instead of discarded. It used
    // to say RolledBack regardless, which claimed the machine was fine on the strength of a
    // result nobody had looked at.
    check("health fails, and the restore is unhealthy too -> RestoredButUnhealthy",
          o.result == UpdateResult::RestoredButUnhealthy, result_name(o.result));
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

  // ------------------------------------------- the order: required, health, commit, then optional
  //
  // The optional images start AFTER the commit, and the reason is not tidiness. The client is
  // started as the logged-on user, and when the route that hands back a handle is unavailable the
  // shell starts it and returns nothing -- no handle, no way to know which process is ours, no way
  // to stop it. A rollback has to move the files such a process holds. Starting it before the
  // commit therefore risks closing the way back at the exact moment the way back is needed.
  //
  // Reclassifying afterwards does not help: by the time the fall-through is discovered the process
  // exists. The order is the fix, so the order is what these assert.

  {
    // 1. The token launch fails and the shell would take over -- and the new host is unhealthy.
    // The rollback must succeed, and NOTHING optional may have been started before it.
    FakeEffects f;
    f.healthOk = false;
    f.healthAfterRollback = 1;
    f.optionalVerdict = RelaunchVerdict::OptionalMissing;  // the shell route, unownable
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("unhealthy host -> the rollback runs and succeeds", o.result == UpdateResult::RolledBack,
          result_name(o.result));
    // The assertion this whole reordering exists for.
    check("...and no optional image was started before the rollback",
          !ran_before(f, "RelaunchOptional", "Rollback"), joined(f.calls));
    check("...and none was committed either", f.commitCount == 0, std::to_string(f.commitCount));
  }
  {
    // 2. Required missing beats optional missing. Both fail; the severe one decides.
    FakeEffects f;
    f.requiredVerdict = RelaunchVerdict::RequiredMissing;
    f.optionalVerdict = RelaunchVerdict::OptionalMissing;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("required missing decides, whatever the optional one says", f.ran("Rollback"),
          joined(f.calls));
    check("...and it is never a partial success",
          o.result != UpdateResult::UpdatedButNotRelaunched, result_name(o.result));
    // And the optional phase never ran on the way there -- there was nothing to run it for.
    check("...and the optional phase was not reached before the rollback",
          !ran_before(f, "RelaunchOptional", "Rollback"), joined(f.calls));
  }
  {
    // 3. The ordinary success: zero optional launches before the commit, exactly one after.
    FakeEffects f;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("happy path -> Updated", o.result == UpdateResult::Updated, result_name(o.result));
    check("nothing optional starts before the commit", ran_before(f, "Commit", "RelaunchOptional"),
          joined(f.calls));
    check("...and exactly one optional phase runs after it",
          f.count("RelaunchOptional") == 1, joined(f.calls));
    check("...while the required one ran before health", ran_before(f, "RelaunchRequired", "HealthCheck"),
          joined(f.calls));
  }
  {
    // 4. After a rollback the OLD optional comes back too. Restoring the files and leaving the
    // user without their window is half a restore.
    FakeEffects f;
    f.registerOk = false;  // forces a rollback
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("a rollback brings the old optional images back as well",
          ran_before(f, "Rollback", "RelaunchOptional"), joined(f.calls));
    check("...after the required ones and their health check",
          ran_before(f, "RelaunchRequired", "RelaunchOptional") &&
              ran_before(f, "HealthCheck", "RelaunchOptional"),
          joined(f.calls));
    check("...and the result is a clean rollback", o.result == UpdateResult::RolledBack,
          result_name(o.result));
  }
  {
    // 5. Client-only: the host was never running, so there is no required plan. Nothing waits on
    // a health report that nobody was ever going to write.
    FakeEffects f;
    f.requiredVerdict = RelaunchVerdict::AllBack;  // an empty required plan reports AllBack
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("client-only -> the update completes", o.result == UpdateResult::Updated,
          result_name(o.result));
    check("client-only -> the client still comes back, after the commit",
          ran_before(f, "Commit", "RelaunchOptional"), joined(f.calls));
    check("client-only -> health is still asked once, not skipped and not repeated",
          f.count("HealthCheck") == 1, joined(f.calls));
  }
  {
    // 6. Optional failing after the commit: the backups are already gone and that is correct,
    // and the outcome is NOT reported as a plain success.
    FakeEffects f;
    f.optionalVerdict = RelaunchVerdict::OptionalMissing;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("optional failing after the commit -> UpdatedButNotRelaunched",
          o.result == UpdateResult::UpdatedButNotRelaunched, result_name(o.result));
    check("...not hidden as Updated", o.result != UpdateResult::Updated, result_name(o.result));
    check("...the commit already happened and is not undone",
          f.commitCount == 1 && !f.ran("Rollback"), joined(f.calls));
    check("...and the detail says what happened",
          o.detail.find("optional") != std::string::npos, o.detail);
  }
  {
    // 7. A partial quiesce: something never left. It must not be started a second time, and the
    // abandon path brings back both kinds because nothing on disk was touched.
    FakeEffects f;
    f.quiesceOk = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("partial quiesce -> abandoned without touching the disk", disk_untouched(f),
          joined(f.calls));
    check("...and both phases are attempted exactly once",
          f.count("RelaunchRequired") == 1 && f.count("RelaunchOptional") == 1, joined(f.calls));
    check("...and it is an abandon, not an update", o.result == UpdateResult::AbandonedBeforeSwap,
          result_name(o.result));
  }

  // ---------------------------------------------------------------- relaunch is not a rollback

  {
    FakeEffects f;
    f.optionalVerdict = RelaunchVerdict::OptionalMissing;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("relaunch fails -> UpdatedButNotRelaunched",
          o.result == UpdateResult::UpdatedButNotRelaunched, result_name(o.result));
    check("relaunch fails -> does NOT roll a good install back", !f.ran("Rollback"),
          joined(f.calls));
    // This required the OPPOSITE, and it was protecting the defect above: it said that when
    // something optional did not come back, health was never asked. That was true, and it was the
    // bug -- the machine's own health went unexamined and the backups were dropped anyway.
    check("relaunch fails -> health IS asked, because the install is being kept",
          o.entered(UpdateState::Health), joined(f.calls));
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
        {"relaunch fails", [](FakeEffects& f) { f.optionalVerdict = RelaunchVerdict::OptionalMissing; }},
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
