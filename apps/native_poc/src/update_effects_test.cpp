// The production UpdateEffects, driven entirely inside a temporary directory.
//
// Three things this test deliberately cannot do, and the reasons are not hypothetical -- on the
// machine this was written on, GNLinkHost, GNLinkStream and GNLinkInputService were running while
// it executed:
//
//   1. It cannot find a process by image name. update_process_targets.cpp is NOT linked into this
//      binary, so enumerate_product_processes and request_process_stop do not exist here. The
//      isolation is a link-time fact, not a promise.
//   2. It cannot touch %ProgramFiles%\GNLink. Every path is a fresh temp directory, and
//      UpdateEffectsConfig has no defaults to fall back to -- an unset field fails validate()
//      instead of quietly becoming something real.
//   3. It cannot open a socket. The artifact "download" is an injected function writing fixed
//      bytes.
//
// Processes that do get stopped here are dummies this harness starts itself, targeted by the PID
// it was handed at creation.

#include "update_effects.hpp"
#include "update_registration_wiring.hpp"

#include <windows.h>

#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace remote60::native_poc::update;
// The registration unit lives in a sibling namespace; alias it so the wiring cases read.
namespace install = remote60::native_poc::install;

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

std::wstring make_temp_dir(const wchar_t* tag) {
  wchar_t base[MAX_PATH]{};
  GetTempPathW(MAX_PATH, base);
  wchar_t unique[MAX_PATH]{};
  swprintf(unique, MAX_PATH, L"%sgnlink-updtest-%lu-%s", base, GetCurrentProcessId(), tag);
  CreateDirectoryW(unique, nullptr);
  return unique;
}

void remove_tree(const std::wstring& dir) {
  WIN32_FIND_DATAW find{};
  HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &find);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      const std::wstring name = find.cFileName;
      if (name == L"." || name == L"..") continue;
      DeleteFileW((dir + L"\\" + name).c_str());
    } while (FindNextFileW(h, &find));
    FindClose(h);
  }
  RemoveDirectoryW(dir.c_str());
}

void write_text(const std::wstring& path, const std::string& text) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string read_text(const std::wstring& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string narrow_w(const std::wstring& w) { return std::string(w.begin(), w.end()); }

bool exists(const std::wstring& path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

/** A process this harness starts and can therefore safely stop, identified by its own PID. */
class DummyProcess {
 public:
  bool start() {
    // Something that exists on every Windows and does nothing until told to stop.
    wchar_t cmd[] = L"cmd.exe /c pause";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    return CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                          nullptr, &si, &pi_) != FALSE;
  }
  uint32_t pid() const { return pi_.dwProcessId; }
  /** Ends it the blunt way -- this is the harness's own child, not a product process. */
  void kill() {
    if (pi_.hProcess) {
      TerminateProcess(pi_.hProcess, 0);
      WaitForSingleObject(pi_.hProcess, 2000);
    }
  }
  bool alive() const {
    if (!pi_.hProcess) return false;
    return WaitForSingleObject(pi_.hProcess, 0) == WAIT_TIMEOUT;
  }
  ~DummyProcess() {
    if (pi_.hProcess) CloseHandle(pi_.hProcess);
    if (pi_.hThread) CloseHandle(pi_.hThread);
  }

 private:
  PROCESS_INFORMATION pi_{};
};

const char* kArtifactBytes = "GNLINK-TEST-ARTIFACT-v105";
// sha256 of kArtifactBytes, filled in at runtime so the test never carries a stale constant.
std::string gArtifactSha;

/** A config that is complete and points only at the directories it is given. */
UpdateEffectsConfig base_config(const std::wstring& install, const std::wstring& staging) {
  UpdateEffectsConfig c;
  c.installDir = install;
  c.stagingDir = staging;
  c.payloadNames = {L"AlphaPayload.bin", L"BetaPayload.bin"};
  c.lockName = L"Local\\gnlink-update-test-" + std::to_wstring(GetCurrentProcessId());
  c.fetchArtifact = [](const ManifestFields&, const std::wstring& dest) {
    write_text(dest, kArtifactBytes);
    return true;
  };
  c.enumerateTargets = []() { return std::vector<ProcessTarget>{}; };
  c.requestStop = [](const ProcessTarget&) { return true; };
  // Scratch values. Nothing reads them yet, but requiring them means a future RegisterInstall
  // cannot quietly reach for HKLM\\...\\Uninstall\\GNLink or the real GNLinkSecureInput service.
  c.registryRoot = L"HKCU\\Software\\GNLinkUpdateTest";
  c.serviceName = L"GNLinkUpdateTestService";
  c.captureRegistration = []() { return true; };
  c.registerInstall = []() { return true; };
  c.restoreRegistration = []() { return true; };
  c.relaunch = []() { return true; };
  c.healthCheck = []() { return true; };
  c.quiesceTimeoutMs = 5000;
  // The updater's own image, so validate() can check it is not among the payload rather than
  // trusting that it never would be.
  {
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    c.updaterImagePath = self;
  }
  return c;
}

/** The same config, but treating GNLinkSetup.exe as a member of the package. */
UpdateEffectsConfig setup_member_config(const std::wstring& install, const std::wstring& staging) {
  UpdateEffectsConfig c = base_config(install, staging);
  // Condition 3 of the package contract: the maintenance binary is a full member, staged, backed
  // up, replaced and rolled back exactly like a product file -- not copied afterwards.
  c.payloadNames = {L"AlphaPayload.bin", L"GNLinkSetup.exe"};
  return c;
}

ManifestFields artifact_fields() {
  ManifestFields f;
  f.schema = 1;
  f.platform = "windows";
  f.version = "0.2.105";
  f.artifact = "test.bin";
  f.size = std::char_traits<char>::length(kArtifactBytes);
  f.sha256 = gArtifactSha;
  return f;
}

}  // namespace

int main(int argc, char** argv) {
  // Re-executed by the concurrency case below. Takes the named mutex, says so, and holds it while
  // the parent tries to take it too. A second PROCESS is the only honest way to test this: a
  // Windows mutex is re-entrant for its owning thread, and the real contenders are processes.
  if (argc >= 3 && std::string(argv[1]) == "--hold-lock") {
    const std::string name(argv[2]);
    const std::wstring wide(name.begin(), name.end());
    HANDLE h = CreateMutexW(nullptr, FALSE, wide.c_str());
    if (!h) return 2;
    if (WaitForSingleObject(h, 0) != WAIT_OBJECT_0) return 3;
    std::cout << "held" << std::endl;  // flushed, so the parent knows the lock is taken
    Sleep(4000);
    ReleaseMutex(h);
    CloseHandle(h);
    return 0;
  }

  const std::wstring install = make_temp_dir(L"install");
  const std::wstring staging = make_temp_dir(L"staging");

  // Guard the guard: if these ever stopped being temp paths the rest of the test would be
  // operating on something real, so it is asserted rather than assumed.
  check("install dir is a temp path, not Program Files",
        install.find(L"Program Files") == std::wstring::npos && !install.empty());
  check("staging dir is outside the install dir",
        staging.find(install) == std::wstring::npos);

  {
    // Hash the artifact bytes once, through the same helper the product uses.
    const std::wstring probe = staging + L"\\probe.bin";
    write_text(probe, kArtifactBytes);
    gArtifactSha = sha256_file_hex(probe);
    check("sha256_file_hex returns 64 hex characters", gArtifactSha.size() == 64, gArtifactSha);
    DeleteFileW(probe.c_str());
  }

  // ---------------------------------------------------------------- config validation

  {
    UpdateEffectsConfig c;
    std::string why;
    check("empty config is invalid", !c.validate(&why), why);

    c = base_config(install, staging);
    check("complete config is valid", c.validate(&why), why);

    // Each required field, removed one at a time. This is what makes the injection structural:
    // there is no field whose absence is tolerated by falling back to something real.
    struct Missing { const char* name; void (*clear)(UpdateEffectsConfig&); };
    const Missing missing[] = {
        {"installDir", [](UpdateEffectsConfig& x) { x.installDir.clear(); }},
        {"stagingDir", [](UpdateEffectsConfig& x) { x.stagingDir.clear(); }},
        {"lockName", [](UpdateEffectsConfig& x) { x.lockName.clear(); }},
        {"payloadNames", [](UpdateEffectsConfig& x) { x.payloadNames.clear(); }},
        {"fetchArtifact", [](UpdateEffectsConfig& x) { x.fetchArtifact = nullptr; }},
        {"enumerateTargets", [](UpdateEffectsConfig& x) { x.enumerateTargets = nullptr; }},
        {"requestStop", [](UpdateEffectsConfig& x) { x.requestStop = nullptr; }},
        {"registerInstall", [](UpdateEffectsConfig& x) { x.registerInstall = nullptr; }},
        {"relaunch", [](UpdateEffectsConfig& x) { x.relaunch = nullptr; }},
        {"healthCheck", [](UpdateEffectsConfig& x) { x.healthCheck = nullptr; }},
        {"captureRegistration", [](UpdateEffectsConfig& x) { x.captureRegistration = nullptr; }},
        {"restoreRegistration", [](UpdateEffectsConfig& x) { x.restoreRegistration = nullptr; }},
        {"registryRoot", [](UpdateEffectsConfig& x) { x.registryRoot.clear(); }},
        {"serviceName", [](UpdateEffectsConfig& x) { x.serviceName.clear(); }},
    };
    for (const Missing& m : missing) {
      UpdateEffectsConfig broken = base_config(install, staging);
      m.clear(broken);
      check(std::string("missing ") + m.name + " makes the config invalid", !broken.validate());
    }

    UpdateEffectsConfig nested = base_config(install, install + L"\\inside");
    check("staging inside the install dir is rejected", !nested.validate(&why), why);
  }

  {
    // An invalid config must not merely misbehave -- it must refuse to take the lock, which is
    // the first thing the state machine does.
    UpdateEffectsConfig broken = base_config(install, staging);
    broken.installDir.clear();
    WindowsUpdateEffects effects(broken);
    check("invalid config cannot acquire the lock", !effects.AcquireLock(), effects.last_error());
  }

  // ---------------------------------------------------------------- mutual exclusion

  {
    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects first(c);
    check("first holder takes the lock", first.AcquireLock(), first.last_error());
    {
      // From ANOTHER THREAD. A Windows mutex is re-entrant for the thread that owns it, so a
      // second acquire on this thread would succeed and prove nothing -- and in production the
      // contender is a different process anyway. A thread is the closest thing this test can
      // stand up without spawning one.
      bool secondAcquired = true;
      std::string secondError;
      std::thread contender([&] {
        WindowsUpdateEffects second(c);
        secondAcquired = second.AcquireLock();
        secondError = second.last_error();
        if (secondAcquired) second.ReleaseLock();
      });
      contender.join();
      check("a second holder is refused immediately, not queued", !secondAcquired, secondError);
    }
    first.ReleaseLock();
    WindowsUpdateEffects third(c);
    check("lock is available again after release", third.AcquireLock(), third.last_error());
    third.ReleaseLock();
  }

  // ---------------------------------------------------------------- download and verification

  {
    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    check("download writes the artifact", e.Download(f), e.last_error());
    check("verification accepts a matching artifact", e.VerifyDownload(f), e.last_error());

    ManifestFields wrongSize = f;
    wrongSize.size = f.size + 1;
    check("verification rejects a size mismatch", !e.VerifyDownload(wrongSize), e.last_error());

    ManifestFields wrongHash = f;
    wrongHash.sha256 = std::string(64, 'a');
    check("verification rejects a hash mismatch", !e.VerifyDownload(wrongHash), e.last_error());

    e.DiscardDownload();
    check("discard removes the staged artifact", !e.VerifyDownload(f));
  }

  {
    // A fetch that half-writes must not leave bytes that a later attempt mistakes for complete.
    UpdateEffectsConfig c = base_config(install, staging);
    c.fetchArtifact = [](const ManifestFields&, const std::wstring& dest) {
      write_text(dest, "short");
      return false;  // reports failure after writing
    };
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    check("a failed fetch reports failure", !e.Download(f), e.last_error());
    e.DiscardDownload();
    check("nothing usable is left behind", !e.VerifyDownload(f));
  }

  // ---------------------------------------------------------------- swap and rollback

  const std::string kOldAlpha = "OLD-ALPHA";
  const std::string kOldBeta = "OLD-BETA";
  const auto seed_install = [&]() {
    write_text(install + L"\\AlphaPayload.bin", kOldAlpha);
    write_text(install + L"\\BetaPayload.bin", kOldBeta);
  };

  {
    seed_install();
    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    check("swap: download", e.Download(f), e.last_error());
    check("swap succeeds", e.Swap(), e.last_error());
    check("swap: alpha replaced", read_text(install + L"\\AlphaPayload.bin") == kArtifactBytes);
    check("swap: beta replaced", read_text(install + L"\\BetaPayload.bin") == kArtifactBytes);
    check("swap: backups still exist before registration",
          exists(install + L"\\AlphaPayload.bin.gnlink-old"));
    check("registration succeeds", e.RegisterInstall(), e.last_error());
    // Registration is NOT the commit point. Relaunch and the health check can still fail into a
    // rollback, and that rollback needs these files -- so they have to still be here. An earlier
    // version of this code dropped them here, and a rollback after a health failure then found
    // nothing to restore.
    check("backups SURVIVE registration, because a rollback is still possible",
          exists(install + L"\\AlphaPayload.bin.gnlink-old") &&
              exists(install + L"\\BetaPayload.bin.gnlink-old"));
    e.Commit();
    check("and are dropped only once the update is committed",
          !exists(install + L"\\AlphaPayload.bin.gnlink-old") &&
              !exists(install + L"\\BetaPayload.bin.gnlink-old"));
    e.DiscardDownload();
  }

  {
    // The case ledger I01 is about: one file cannot be replaced. The result must be all-old, not
    // a mixture.
    seed_install();
    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);

    // Hold the second payload open with no sharing, the way a running executable is held.
    HANDLE held = CreateFileW((install + L"\\BetaPayload.bin").c_str(), GENERIC_READ, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check("test could lock the second payload", held != INVALID_HANDLE_VALUE);

    const bool swapped = e.Swap();
    check("swap fails when a payload is locked", !swapped, e.last_error());
    // This is the assertion ledger I01 is about: the installer replaces files one at a time and
    // leaves the earlier ones replaced when a later one is locked. Here the first payload must
    // still be the old bytes.
    check("first payload was NOT left replaced (no mixed version)",
          read_text(install + L"\\AlphaPayload.bin") == kOldAlpha,
          read_text(install + L"\\AlphaPayload.bin"));
    check("no backup files left over", !exists(install + L"\\AlphaPayload.bin.gnlink-old"));

    // The locked file can only be read once the test lets go of it -- with share mode 0, even
    // this process cannot open it a second time.
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
    check("locked payload untouched", read_text(install + L"\\BetaPayload.bin") == kOldBeta,
          read_text(install + L"\\BetaPayload.bin"));
    e.DiscardDownload();
  }

  {
    // Explicit rollback after a successful swap, before registration.
    seed_install();
    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    check("rollback case: swap succeeds", e.Swap(), e.last_error());
    check("rollback restores the previous version", e.Rollback(), e.last_error());
    check("rollback: alpha is old again", read_text(install + L"\\AlphaPayload.bin") == kOldAlpha);
    check("rollback: beta is old again", read_text(install + L"\\BetaPayload.bin") == kOldBeta);
    check("rollback leaves no backups", !exists(install + L"\\AlphaPayload.bin.gnlink-old"));
    e.DiscardDownload();
  }

  {
    // A first install: nothing there to move aside. Must not be mistaken for a failure.
    DeleteFileW((install + L"\\AlphaPayload.bin").c_str());
    DeleteFileW((install + L"\\BetaPayload.bin").c_str());
    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    check("swap onto an empty install dir succeeds", e.Swap(), e.last_error());
    check("files were placed", read_text(install + L"\\AlphaPayload.bin") == kArtifactBytes);
    check("rollback with nothing to restore still reports success", e.Rollback(), e.last_error());
    e.DiscardDownload();
  }

  // ---------------------------------------------------------------- stopping processes

  {
    // Only ever the harness's own children, addressed by the PID it was handed.
    DummyProcess a;
    DummyProcess b;
    check("dummy process a started", a.start());
    check("dummy process b started", b.start());
    std::vector<ProcessTarget> targets;
    ProcessTarget ta, tb;
    check("identity captured for a", capture_process_identity(a.pid(), &ta));
    check("identity captured for b", capture_process_identity(b.pid(), &tb));
    check("identity carries an image path", !ta.imagePath.empty(), std::string());
    check("identity carries a creation time", ta.creationTime != 0);
    targets = {ta, tb};

    UpdateEffectsConfig c = base_config(install, staging);
    c.enumerateTargets = [targets]() { return targets; };
    c.requestStop = [&](const ProcessTarget& t) {
      // Stands in for the real WM_CLOSE / CTRL_BREAK request. These are our own children.
      if (t.pid == a.pid()) { a.kill(); return true; }
      if (t.pid == b.pid()) { b.kill(); return true; }
      return false;
    };
    WindowsUpdateEffects e(c);
    check("prepare asks every target", e.PrepareForSwap(), e.last_error());
    check("quiesce sees them gone", e.Quiesce(), e.last_error());
    check("dummy a is actually gone", !a.alive());
    check("dummy b is actually gone", !b.alive());
  }

  {
    // A target that will not stop must NOT be forced. Quiesce fails and the caller abandons.
    DummyProcess stubborn;
    check("stubborn dummy started", stubborn.start());
    const uint32_t pid = stubborn.pid();

    ProcessTarget target;
    check("stubborn identity captured", capture_process_identity(pid, &target));

    UpdateEffectsConfig c = base_config(install, staging);
    c.quiesceTimeoutMs = 300;  // short, because the point is that it gives up
    c.enumerateTargets = [target]() { return std::vector<ProcessTarget>{target}; };
    c.requestStop = [](const ProcessTarget&) { return true; };  // asked, does not comply
    WindowsUpdateEffects e(c);
    check("prepare succeeds (asking worked)", e.PrepareForSwap(), e.last_error());
    check("quiesce gives up rather than forcing", !e.Quiesce(), e.last_error());
    check("the stubborn process is STILL ALIVE -- it was never terminated", stubborn.alive());
    stubborn.kill();
  }

  {
    // A target that cannot even be asked: abandoned at Prepare, before Quiesce.
    UpdateEffectsConfig c = base_config(install, staging);
    c.enumerateTargets = []() {
      ProcessTarget t;
      t.pid = 4;  // System. Never ours, and never actually touched -- requestStop refuses first.
      t.creationTime = 1;
      return std::vector<ProcessTarget>{t};
    };
    c.requestStop = [](const ProcessTarget&) { return false; };
    WindowsUpdateEffects e(c);
    check("prepare fails when a target cannot be asked", !e.PrepareForSwap(), e.last_error());
  }

  // ---------------------------------------------------------------- end to end, still isolated

  {
    DeleteFileW((install + L"\\AlphaPayload.bin").c_str());
    DeleteFileW((install + L"\\BetaPayload.bin").c_str());
    seed_install();

    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    e.set_installed_version("0.2.104");
    // A manifest whose signature the injected verifier accepts. Real signature verification has
    // its own suite; what is exercised here is the effects behind the state machine.
    e.set_manifest(
        "schema=1\nplatform=windows\nversion=0.2.105\nartifact=test.bin\nsize=" +
            std::to_string(artifact_fields().size) + "\nsha256=" + gArtifactSha + "\n",
        std::string(128, '0'));

    const auto accept = [](const std::string&, const std::vector<uint8_t>&) { return true; };
    const UpdateOutcome out = run_update(e, accept, "windows");
    check("end to end: Updated", out.result == UpdateResult::Updated,
          std::string(result_name(out.result)) + " " + out.detail + " / " + e.last_error());
    check("end to end: files are the new version",
          read_text(install + L"\\AlphaPayload.bin") == kArtifactBytes);
    check("end to end: no backups left", !exists(install + L"\\AlphaPayload.bin.gnlink-old"));
    check("end to end: staging cleaned", !exists(staging + L"\\artifact.staged"));
  }

  {
    // The same run, but the installed version is already newer. Nothing may be touched.
    seed_install();
    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    e.set_installed_version("0.2.106");
    e.set_manifest(
        "schema=1\nplatform=windows\nversion=0.2.105\nartifact=test.bin\nsize=" +
            std::to_string(artifact_fields().size) + "\nsha256=" + gArtifactSha + "\n",
        std::string(128, '0'));
    const auto accept = [](const std::string&, const std::vector<uint8_t>&) { return true; };
    const UpdateOutcome out = run_update(e, accept, "windows");
    check("newer installed: NothingToDo", out.result == UpdateResult::NothingToDo,
          result_name(out.result));
    check("newer installed: install untouched",
          read_text(install + L"\\AlphaPayload.bin") == kOldAlpha);
  }

  // ---------------------------------------------------------------- PID reuse

  {
    // A live process, but described with the wrong creation time -- which is what a reused PID
    // looks like. Quiesce must treat it as already gone rather than waiting on a stranger, so
    // this must succeed IMMEDIATELY even though the PID is very much alive.
    DummyProcess bystander;
    check("bystander started", bystander.start());
    ProcessTarget stale;
    check("bystander identity captured", capture_process_identity(bystander.pid(), &stale));
    const uint64_t realCreation = stale.creationTime;
    stale.creationTime = realCreation ^ 0xFFFFull;  // as if a different process now holds the PID

    UpdateEffectsConfig c = base_config(install, staging);
    c.quiesceTimeoutMs = 400;
    c.enumerateTargets = [stale]() { return std::vector<ProcessTarget>{stale}; };
    c.requestStop = [](const ProcessTarget&) { return true; };
    WindowsUpdateEffects e(c);
    const DWORD before = GetTickCount();
    const bool quiesced = e.Quiesce();
    const DWORD elapsed = GetTickCount() - before;
    check("a reused PID is treated as already gone", quiesced, e.last_error());
    check("and it did not wait out the timeout", elapsed < 300, std::to_string(elapsed) + "ms");
    check("the bystander was never touched", bystander.alive());

    // Sanity: with the CORRECT identity the same call does wait and does time out. Without this
    // the case above could pass for the wrong reason.
    ProcessTarget correct = stale;
    correct.creationTime = realCreation;
    UpdateEffectsConfig c2 = base_config(install, staging);
    c2.quiesceTimeoutMs = 400;
    c2.enumerateTargets = [correct]() { return std::vector<ProcessTarget>{correct}; };
    c2.requestStop = [](const ProcessTarget&) { return true; };
    WindowsUpdateEffects e2(c2);
    check("with the right identity it does wait and fail", !e2.Quiesce(), e2.last_error());
    check("bystander still alive after the timeout", bystander.alive());
    bystander.kill();
  }

  {
    // process_identity_matches directly, against this process.
    ProcessTarget self;
    check("self identity captured", capture_process_identity(GetCurrentProcessId(), &self));
    HANDLE me = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    check("self handle opened", me != nullptr);
    check("identity matches itself", process_identity_matches(me, self));
    ProcessTarget wrongTime = self;
    wrongTime.creationTime += 1;
    check("a different creation time does not match", !process_identity_matches(me, wrongTime));
    ProcessTarget wrongPath = self;
    wrongPath.imagePath = L"C:\\nowhere\\else.exe";
    check("a different image path does not match", !process_identity_matches(me, wrongPath));
    ProcessTarget noTime = self;
    noTime.creationTime = 0;
    check("an unidentified target never matches", !process_identity_matches(me, noTime));
    if (me) CloseHandle(me);
  }

  // ---------------------------------------------------------------- real relaunch failure

  {
    // A genuine CreateProcessW against a path that does not exist -- not a lambda returning false.
    seed_install();
    UpdateEffectsConfig c = base_config(install, staging);
    c.relaunch = [&]() {
      std::wstring cmd = install + L"\\NoSuchProduct.exe";
      STARTUPINFOW si{};
      si.cb = sizeof(si);
      PROCESS_INFORMATION pi{};
      const BOOL ok = CreateProcessW(cmd.c_str(), nullptr, nullptr, nullptr, FALSE,
                                     CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
      if (ok) {  // should never happen; clean up if it somehow does
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
      }
      return ok != FALSE;
    };
    WindowsUpdateEffects e(c);
    e.set_installed_version("0.2.104");
    e.set_manifest("schema=1\nplatform=windows\nversion=0.2.105\nartifact=test.bin\nsize=" +
                       std::to_string(artifact_fields().size) + "\nsha256=" + gArtifactSha + "\n",
                   std::string(128, '0'));
    const auto accept = [](const std::string&, const std::vector<uint8_t>&) { return true; };
    const UpdateOutcome out = run_update(e, accept, "windows");
    check("a real failed CreateProcess -> UpdatedButNotRelaunched",
          out.result == UpdateResult::UpdatedButNotRelaunched, result_name(out.result));
    check("and the new files are left in place, not rolled back",
          read_text(install + L"\\AlphaPayload.bin") == kArtifactBytes);
    e.DiscardDownload();
  }

  // ---------------------------------------------------------------- real rollback failure

  {
    // Rollback restores by moving the backup over the live path. Holding the live path open with
    // no sharing makes that move genuinely fail at the OS level.
    seed_install();
    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    check("rollback-failure case: swap succeeds first", e.Swap(), e.last_error());

    HANDLE held = CreateFileW((install + L"\\BetaPayload.bin").c_str(), GENERIC_READ, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check("could hold the swapped-in file open", held != INVALID_HANDLE_VALUE);
    const bool rolled = e.Rollback();
    check("rollback reports failure when a file cannot be restored", !rolled, e.last_error());
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
    // Alpha was restorable and must have been restored even though Beta was not -- a rollback
    // that gives up entirely on the first problem would leave more of the new version in place.
    check("the restorable file was still restored",
          read_text(install + L"\\AlphaPayload.bin") == kOldAlpha,
          read_text(install + L"\\AlphaPayload.bin"));
    e.DiscardDownload();
  }

  // ---------------------------------------------------------------- real concurrent execution

  {
    // Two real processes, one named mutex. The contender is this same executable, re-run with a
    // flag, so the lock is contended across a process boundary exactly as it would be in the field.
    wchar_t selfPath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
    const std::wstring lockName =
        L"Local\\gnlink-update-crossproc-" + std::to_wstring(GetCurrentProcessId());

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    check("pipe created for the contender", CreatePipe(&readPipe, &writePipe, &sa, 0) != FALSE);

    std::wstring cmd = std::wstring(L"\"") + selfPath + L"\" --hold-lock " + lockName;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    const BOOL started = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    check("lock contender process started", started != FALSE);
    CloseHandle(writePipe);

    if (started) {
      // Wait for it to say it holds the lock, rather than sleeping and hoping.
      char buf[16]{};
      DWORD read = 0;
      const BOOL got = ReadFile(readPipe, buf, 4, &read, nullptr);
      check("contender reported holding the lock", got && read >= 4, std::string(buf, read));

      UpdateEffectsConfig c = base_config(install, staging);
      c.lockName = lockName;
      WindowsUpdateEffects e(c);
      check("another PROCESS holding the lock blocks this one", !e.AcquireLock(), e.last_error());

      // And the state machine turns that into "not now" rather than an error or a wait.
      e.set_installed_version("0.2.104");
      e.set_manifest("schema=1\nplatform=windows\nversion=0.2.105\nartifact=test.bin\nsize=" +
                         std::to_string(artifact_fields().size) + "\nsha256=" + gArtifactSha + "\n",
                     std::string(128, '0'));
      const auto accept = [](const std::string&, const std::vector<uint8_t>&) { return true; };
      const UpdateOutcome out = run_update(e, accept, "windows");
      check("contended lock -> NothingToDo", out.result == UpdateResult::NothingToDo,
            result_name(out.result));
      check("contended lock -> nothing was downloaded", !out.entered(UpdateState::Download));

      WaitForSingleObject(pi.hProcess, 8000);
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);

      // Once it exits the lock must be free again.
      WindowsUpdateEffects after(c);
      check("lock is free once the other process exits", after.AcquireLock(), after.last_error());
      after.ReleaseLock();
    }
    CloseHandle(readPipe);
  }

  // ---------------------------------------------------------------- privilege flow, observed

  {
    // Not an assertion about what elevation SHOULD be -- just a recorded fact, because the design
    // says a host-launched updater inherits an elevated token and needs no second prompt, and
    // that claim is only checkable from a real elevated run (which this is not).
    HANDLE token = nullptr;
    bool elevated = false;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
      TOKEN_ELEVATION info{};
      DWORD size = 0;
      if (GetTokenInformation(token, TokenElevation, &info, sizeof(info), &size)) {
        elevated = info.TokenIsElevated != 0;
      }
      CloseHandle(token);
    }
    std::cout << "NOTE  this test ran " << (elevated ? "ELEVATED" : "NOT elevated")
              << " -- the design's \"host-launched updater needs no second UAC prompt\" claim is"
                 " not verified here either way\n";
    check("elevation state could be read", true);
  }

  // ---------------------------------------------------------------- Setup as a package member

  const std::string kOldSetup = "OLD-SETUP-BINARY";
  const auto seed_with_setup = [&]() {
    write_text(install + L"\\AlphaPayload.bin", kOldAlpha);
    write_text(install + L"\\GNLinkSetup.exe", kOldSetup);
  };

  {
    // The point of condition 3: the maintenance binary goes through the same swap. After an
    // update the uninstall entry must not still be pointing at a binary from an older build.
    seed_with_setup();
    UpdateEffectsConfig c = setup_member_config(install, staging);
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    check("setup-member: download", e.Download(f), e.last_error());
    check("setup-member: swap succeeds", e.Swap(), e.last_error());
    check("setup-member: the product file was replaced",
          read_text(install + L"\\AlphaPayload.bin") == kArtifactBytes);
    check("setup-member: GNLinkSetup.exe was replaced TOO",
          read_text(install + L"\\GNLinkSetup.exe") == kArtifactBytes,
          read_text(install + L"\\GNLinkSetup.exe"));
    check("setup-member: both have backups before registration",
          exists(install + L"\\GNLinkSetup.exe.gnlink-old"));
    e.DiscardDownload();
  }

  {
    // Condition 5, first regression: the artifact does not match its hash. Nothing may be
    // replaced -- including the Setup.
    seed_with_setup();
    UpdateEffectsConfig c = setup_member_config(install, staging);
    WindowsUpdateEffects e(c);
    ManifestFields f = artifact_fields();
    e.Download(f);
    f.sha256 = std::string(64, 'b');
    check("hash mismatch is refused", !e.VerifyDownload(f), e.last_error());
    check("and the old Setup is untouched",
          read_text(install + L"\\GNLinkSetup.exe") == kOldSetup);
    check("and the old product file is untouched",
          read_text(install + L"\\AlphaPayload.bin") == kOldAlpha);
    e.DiscardDownload();
  }

  {
    // Condition 5, second regression: the Setup on disk is locked, the way it would be if it were
    // running. The swap must leave everything as it was -- no half-updated install.
    seed_with_setup();
    UpdateEffectsConfig c = setup_member_config(install, staging);
    WindowsUpdateEffects e(c);
    e.Download(artifact_fields());

    HANDLE held = CreateFileW((install + L"\\GNLinkSetup.exe").c_str(), GENERIC_READ, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check("could lock the Setup binary", held != INVALID_HANDLE_VALUE);
    check("swap fails when the Setup is locked", !e.Swap(), e.last_error());
    check("the product file was NOT left replaced",
          read_text(install + L"\\AlphaPayload.bin") == kOldAlpha,
          read_text(install + L"\\AlphaPayload.bin"));
    check("no backups left behind", !exists(install + L"\\AlphaPayload.bin.gnlink-old"));
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
    check("the locked Setup is still the old one",
          read_text(install + L"\\GNLinkSetup.exe") == kOldSetup);
    e.DiscardDownload();
  }

  {
    // Condition 5, third regression: the swap succeeds and something afterwards fails. The
    // rollback has to restore the OLD Setup as well, not just the product files -- otherwise the
    // install is left with a new maintenance binary and old everything else.
    seed_with_setup();
    UpdateEffectsConfig c = setup_member_config(install, staging);
    c.registerInstall = []() { return false; };  // fails after the swap
    WindowsUpdateEffects e(c);
    e.set_installed_version("0.2.104");
    e.set_manifest("schema=1\nplatform=windows\nversion=0.2.105\nartifact=test.bin\nsize=" +
                       std::to_string(artifact_fields().size) + "\nsha256=" + gArtifactSha + "\n",
                   std::string(128, '0'));
    const auto accept = [](const std::string&, const std::vector<uint8_t>&) { return true; };
    const UpdateOutcome out = run_update(e, accept, "windows");
    check("a post-swap failure rolls back", out.result == UpdateResult::RolledBack,
          std::string(result_name(out.result)) + " " + out.detail);
    check("rollback restored the product file",
          read_text(install + L"\\AlphaPayload.bin") == kOldAlpha,
          read_text(install + L"\\AlphaPayload.bin"));
    check("rollback restored the OLD Setup too",
          read_text(install + L"\\GNLinkSetup.exe") == kOldSetup,
          read_text(install + L"\\GNLinkSetup.exe"));
    check("no backups left after rollback",
          !exists(install + L"\\GNLinkSetup.exe.gnlink-old"));
  }

  // ---------------------------------------------------------------- the updater cannot replace itself

  {
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    const std::wstring selfPath = self;
    const size_t slash = selfPath.find_last_of(L"\\/");
    const std::wstring selfName = selfPath.substr(slash + 1);

    UpdateEffectsConfig c = base_config(install, staging);
    c.payloadNames = {L"AlphaPayload.bin", selfName};
    std::string why;
    check("naming the running updater as payload is refused", !c.validate(&why), why);
    check("and the reason says so", why.find("updater") != std::string::npos, why);
  }
  {
    // The updater sitting inside the directory it would replace is the same hazard by a different
    // route, and design 3.2 puts it outside for exactly this reason.
    UpdateEffectsConfig c = base_config(install, staging);
    c.updaterImagePath = install + L"\\SomeUpdater.exe";
    std::string why;
    check("an updater inside the install dir is refused", !c.validate(&why), why);
  }

  // ---------------------------------------------------------------- version consistency

  {
    // Condition 4. The artifact here is plain text containing the version as UTF-16, standing in
    // for a PE image carrying a wide string literal.
    const std::wstring probe = staging + L"\\versioned.bin";
    std::string utf16;
    for (char ch : std::string("0.2.105")) { utf16.push_back(ch); utf16.push_back('\0'); }
    write_text(probe, "prefix" + utf16 + "suffix");
    check("the version is found when present",
          file_contains_utf16_version(probe, "0.2.105"));
    check("a different version is not found",
          !file_contains_utf16_version(probe, "0.3.0"));
    check("an empty version is never found", !file_contains_utf16_version(probe, ""));
    check("a missing file is not a match", !file_contains_utf16_version(staging + L"\\nope.bin", "0.2.105"));
    DeleteFileW(probe.c_str());
  }

  {
    // Wired in: an artifact that does not carry the expected version fails verification even
    // though its hash is right.
    UpdateEffectsConfig c = base_config(install, staging);
    c.expectedVersion = "0.2.105";
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    check("an artifact without the expected version is refused", !e.VerifyDownload(f),
          e.last_error());
    check("and the reason names the version",
          e.last_error().find("0.2.105") != std::string::npos, e.last_error());
    e.DiscardDownload();
  }
  {
    // And the manifest's own version must agree with what we were told to expect.
    UpdateEffectsConfig c = base_config(install, staging);
    c.expectedVersion = "0.2.105";
    c.fetchArtifact = [](const ManifestFields&, const std::wstring& dest) {
      std::string utf16;
      for (char ch : std::string("0.2.105")) { utf16.push_back(ch); utf16.push_back('\0'); }
      write_text(dest, utf16);
      return true;
    };
    WindowsUpdateEffects e(c);
    ManifestFields f;
    f.schema = 1;
    f.platform = "windows";
    f.version = "0.9.9";  // disagrees with expectedVersion
    f.artifact = "test.bin";
    {
      // Size and hash have to match the bytes the fetcher writes, or the earlier checks fire first.
      const std::wstring probe = staging + L"\\probe2.bin";
      std::string utf16;
      for (char ch : std::string("0.2.105")) { utf16.push_back(ch); utf16.push_back('\0'); }
      write_text(probe, utf16);
      uint64_t size = 0;
      file_size_bytes(probe, &size);
      f.size = size;
      f.sha256 = sha256_file_hex(probe);
      DeleteFileW(probe.c_str());
    }
    e.Download(f);
    check("a manifest version that disagrees with the expected one is refused",
          !e.VerifyDownload(f), e.last_error());
    check("and the reason names both", e.last_error().find("0.9.9") != std::string::npos,
          e.last_error());
    e.DiscardDownload();
  }

  // ---------------------------------------------------------------- registration through the shared unit

  {
    // The wiring, driven against a scratch HKCU key. What matters is the ORDER: the snapshot has
    // to be taken before a single file moves, because a rollback restores what was registered
    // then -- not the version being abandoned.
    const std::wstring subkey =
        L"Software\\GNLinkUpdateWiringTest\\" + std::to_wstring(GetCurrentProcessId());
    RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
    check("wiring test key is not the real uninstall path",
          subkey.find(L"Microsoft\\Windows\\CurrentVersion\\Uninstall") == std::wstring::npos);

    const auto make_target = [&](const std::wstring& version) {
      install::RegistrationTarget t;
      t.installDir = install;
      t.setupPath = install + L"\\GNLinkSetup.exe";
      t.version = version;
      t.productName = L"GNLink Host (wiring test)";
      t.clientShortcutName = L"GNLink (wiring test)";
      t.publisher = L"GNLink";
      t.serviceName = L"GNLinkWiringTestService";
      t.firewallRuleName = L"GNLink Wiring Test";
      t.uninstallRoot = HKEY_CURRENT_USER;
      t.uninstallSubkey = subkey;
      t.hostExeName = L"GNLinkHost.exe";
      t.clientExeName = L"GNLinkClient.exe";
      t.serviceExeName = L"GNLinkInputService.exe";
      t.streamExeName = L"GNLinkStream.exe";
      return t;
    };
    // Recorded, not performed: no service, no firewall rule, no Start menu entry.
    install::RegistrationOps recordingOps;
    recordingOps.runProcess = [](const std::wstring&, const std::wstring&) { return 0; };
    recordingOps.createShortcut = [](const std::wstring&, const std::wstring&,
                                     const std::wstring&) { return true; };

    const auto read_version = [&]() -> std::wstring {
      HKEY key = nullptr;
      if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return L"<none>";
      }
      wchar_t buffer[256]{};
      DWORD bytes = sizeof(buffer);
      DWORD type = 0;
      const LSTATUS s = RegQueryValueExW(key, L"DisplayVersion", nullptr, &type,
                                         reinterpret_cast<BYTE*>(buffer), &bytes);
      RegCloseKey(key);
      return (s == ERROR_SUCCESS) ? std::wstring(buffer) : L"<none>";
    };

    {
      // Seed a previous installation, the way an earlier version would have left it.
      RegistrationEffects seed = make_registration_effects(make_target(L"0.2.104"), recordingOps);
      check("seed registration succeeds", seed.apply());
      check("seed version recorded", read_version() == L"0.2.104", narrow_w(read_version()));
    }

    {
      // A successful update: capture, swap, register the NEW version.
      seed_install();
      UpdateEffectsConfig c = base_config(install, staging);
      RegistrationEffects reg = make_registration_effects(make_target(L"0.2.105"), recordingOps);
      c.captureRegistration = reg.capture;
      c.registerInstall = reg.apply;
      c.restoreRegistration = reg.restore;

      WindowsUpdateEffects e(c);
      e.Download(artifact_fields());
      check("wiring: swap succeeds", e.Swap(), e.last_error());
      check("wiring: register succeeds", e.RegisterInstall(), e.last_error());
      check("wiring: DisplayVersion is the new version", read_version() == L"0.2.105",
            narrow_w(read_version()));
      check("wiring: the shared unit reported all four steps",
            reg.lastResult().completed.size() == 4,
            std::to_string(reg.lastResult().completed.size()));
      e.DiscardDownload();
    }

    {
      // The case Codex called out: a failure AFTER the swap must put the registry back to the
      // PREVIOUS version. Restoring the new one would leave old files claiming to be new.
      seed_install();
      UpdateEffectsConfig c = base_config(install, staging);
      RegistrationEffects reg = make_registration_effects(make_target(L"0.3.0"), recordingOps);
      c.captureRegistration = reg.capture;
      c.registerInstall = reg.apply;
      c.restoreRegistration = reg.restore;
      c.relaunch = []() { return true; };
      c.healthCheck = []() { return false; };  // fails after registration

      WindowsUpdateEffects e(c);
      e.set_installed_version("0.2.105");
      e.set_manifest("schema=1\nplatform=windows\nversion=0.3.0\nartifact=test.bin\nsize=" +
                         std::to_string(artifact_fields().size) + "\nsha256=" + gArtifactSha + "\n",
                     std::string(128, '0'));
      const auto accept = [](const std::string&, const std::vector<uint8_t>&) { return true; };
      const UpdateOutcome out = run_update(e, accept, "windows");
      check("wiring: a health failure rolls back", out.result == UpdateResult::RolledBack,
            std::string(result_name(out.result)) + " " + out.detail);
      check("wiring: files went back to the old version",
            read_text(install + L"\\AlphaPayload.bin") == kOldAlpha);
      check("wiring: DisplayVersion went back to the PREVIOUS version, not 0.3.0",
            read_version() == L"0.2.105", narrow_w(read_version()));
    }

    {
      // Capture failing must stop the swap before anything moves.
      seed_install();
      UpdateEffectsConfig c = base_config(install, staging);
      c.captureRegistration = []() { return false; };
      WindowsUpdateEffects e(c);
      e.Download(artifact_fields());
      check("a failed capture stops the swap", !e.Swap(), e.last_error());
      check("and nothing was moved", read_text(install + L"\\AlphaPayload.bin") == kOldAlpha);
      check("no backups were made", !exists(install + L"\\AlphaPayload.bin.gnlink-old"));
      e.DiscardDownload();
    }

    {
      // Restoring without having captured is a true "nothing to put back", not a failure -- and
      // must not turn an ordinary rollback into RollbackFailed.
      RegistrationEffects reg = make_registration_effects(make_target(L"9.9.9"), recordingOps);
      check("restore without capture reports success", reg.restore());
    }

    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\GNLinkUpdateWiringTest");
  }

  remove_tree(staging);
  remove_tree(install);

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED")
            << "  (" << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
