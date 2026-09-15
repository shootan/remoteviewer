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
  c.fetchArtifact = [](const ManifestArtifact&, const std::wstring& dest) {
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
  c.relaunchRequired = []() { return RelaunchVerdict::AllBack; };
  c.relaunchOptional = []() { return RelaunchVerdict::AllBack; };
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

/**
 * A signed-shaped document listing exactly the files a swap will replace.
 *
 * The staging path checks that every payload name is present in the staged release before it
 * moves anything, so a document naming other files is not merely inconvenient -- it is refused,
 * which is the behaviour worth having.
 */
std::string manifest_for(const std::string& version, const std::vector<std::wstring>& names) {
  std::string doc = "schema=2\nreleaseId=r-" + version +
                    "\nplatform=windows\narch=x64\nversion=" + version + "\n";
  for (const std::wstring& n : names) {
    const std::string narrow(n.begin(), n.end());
    doc += "artifact=" + narrow + "|" +
           std::to_string(std::char_traits<char>::length(kArtifactBytes)) + "|" + gArtifactSha +
           "|https://u.example/" + narrow + "\n";
  }
  return doc;
}

ManifestFields artifact_fields(const std::vector<std::string>& names = {"AlphaPayload.bin",
                                                                          "BetaPayload.bin"}) {
  ManifestFields f;
  f.schema = 2;
  f.releaseId = "r-0.2.105";
  f.arch = "x64";
  for (const std::string& n : names) {
    ManifestArtifact a;
    a.name = n;
    a.size = std::char_traits<char>::length(kArtifactBytes);
    a.sha256 = gArtifactSha;
    a.url = "https://u.example/" + n;
    f.artifacts.push_back(a);
  }
  f.platform = "windows";
  f.version = "0.2.105";
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
        {"relaunchRequired", [](UpdateEffectsConfig& x) { x.relaunchRequired = nullptr; }},
        {"relaunchOptional", [](UpdateEffectsConfig& x) { x.relaunchOptional = nullptr; }},
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
    wrongSize.artifacts[0].size = f.artifacts[0].size + 1;
    check("verification rejects a size mismatch", !e.VerifyDownload(wrongSize), e.last_error());

    ManifestFields wrongHash = f;
    wrongHash.artifacts[0].sha256 = std::string(64, 'a');
    check("verification rejects a hash mismatch", !e.VerifyDownload(wrongHash), e.last_error());

    e.DiscardDownload();
    check("discard removes the staged artifact", !e.VerifyDownload(f));
  }

  {
    // The 0.2.109 failure, as a unit.
    //
    // A manifest that does not name a file the swap replaces used to pass everything here --
    // signature, sizes, hashes -- and die at the swap, by which time the product had been asked
    // to stop and the user was looking at a closed application. The answer was available before
    // any of that: the manifest is in hand and so is the list of what will be replaced.
    UpdateEffectsConfig c = base_config(install, staging);
    c.payloadNames = {L"AlphaPayload.bin", L"BetaPayload.bin", L"GNLinkSetup.exe"};
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();  // Alpha and Beta only -- no Setup
    check("download still writes what the manifest does name", e.Download(f), e.last_error());
    check("a manifest missing a replaced file is refused before the swap", !e.VerifyDownload(f),
          e.last_error());
    check("...and the reason names the missing file",
          e.last_error().find("GNLinkSetup.exe") != std::string::npos, e.last_error());

    // The same release with the file named passes, so the refusal above is about the omission and
    // not about the case existing at all.
    UpdateEffectsConfig ok = base_config(install, staging);
    ok.payloadNames = {L"AlphaPayload.bin", L"BetaPayload.bin", L"GNLinkSetup.exe"};
    WindowsUpdateEffects e2(ok);
    const ManifestFields complete =
        artifact_fields({"AlphaPayload.bin", "BetaPayload.bin", "GNLinkSetup.exe"});
    check("naming it makes the same release acceptable",
          e2.Download(complete) && e2.VerifyDownload(complete), e2.last_error());
    e2.DiscardDownload();
    e.DiscardDownload();
  }

  {
    // A fetch that half-writes must not leave bytes that a later attempt mistakes for complete.
    UpdateEffectsConfig c = base_config(install, staging);
    c.fetchArtifact = [](const ManifestArtifact&, const std::wstring& dest) {
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
    // A rollback that is REFUSED because something this attempt started could not be stopped.
    //
    // Restoring files while a process may still be holding them does not undo the update -- it
    // produces an installation that is part old and part new, and the failure afterwards reads as
    // the rollback's fault rather than as the reason it should never have started. So the gate is
    // asked first, and a no means nothing moves at all.
    seed_install();
    UpdateEffectsConfig c = base_config(install, staging);
    c.releaseBeforeRollback = []() { return false; };  // "I could not prove it stopped"
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    check("refused rollback: the swap itself succeeds", e.Swap(), e.last_error());
    check("a rollback that cannot stop what it started does not report success", !e.Rollback(),
          e.last_error());
    // The point of the whole case: not one file moved. Refusing has to leave the installation
    // exactly as the swap left it, so that a later attempt still has both halves to work from.
    check("refused rollback: the new bytes are still in place",
          read_text(install + L"\\AlphaPayload.bin") == kArtifactBytes,
          read_text(install + L"\\AlphaPayload.bin"));
    check("refused rollback: the backups are still there, so a way back still exists",
          exists(install + L"\\AlphaPayload.bin.gnlink-old"));
    check("refused rollback: the reason says so", e.last_error().find("not rolling back") == 0,
          e.last_error());
    e.DiscardDownload();
  }
  {
    // The counter-control. Same swap, same rollback, and the gate says yes -- so the restore runs.
    // Without this the case above would also pass if Rollback() simply never worked.
    seed_install();
    UpdateEffectsConfig c = base_config(install, staging);
    bool asked = false;
    c.releaseBeforeRollback = [&asked]() { asked = true; return true; };
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    check("permitted rollback: swap succeeds", e.Swap(), e.last_error());
    check("a rollback whose stops all succeeded goes ahead", e.Rollback(), e.last_error());
    check("permitted rollback: the gate was consulted", asked);
    check("permitted rollback: the old bytes are back",
          read_text(install + L"\\AlphaPayload.bin") == kOldAlpha,
          read_text(install + L"\\AlphaPayload.bin"));
    e.DiscardDownload();
  }
  {
    // Commit reading what DeleteFileW told it.
    //
    // A backup held open cannot be deleted. That used to be discarded -- the result was ignored
    // and the list cleared -- so the file stayed beside the installation with nothing anywhere
    // recording that it had. It is the mechanism behind a stale .gnlink-old that no reading of the
    // code explained, and it is reproduced here rather than deferred to a machine.
    seed_install();
    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    check("orphan case: swap succeeds", e.Swap(), e.last_error());
    const std::wstring backup = install + L"\\AlphaPayload.bin.gnlink-old";
    check("orphan case: the backup exists to be held", exists(backup));
    HANDLE hold = CreateFileW(backup.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    check("orphan case: the test could hold the backup open", hold != INVALID_HANDLE_VALUE);
    e.Commit();
    check("a backup that could not be deleted is recorded, not forgotten",
          e.orphaned_backups().size() == 1,
          std::to_string(e.orphaned_backups().size()));
    check("...and it names which one", !e.orphaned_backups().empty() &&
          e.orphaned_backups()[0] == L"AlphaPayload.bin");
    check("...and it is visible in the error text",
          e.last_error().find("backups left behind") != std::string::npos, e.last_error());
    // And the file really did survive -- the recording describes something true.
    check("...and the backup really is still on disk", exists(backup));
    if (hold != INVALID_HANDLE_VALUE) CloseHandle(hold);
    DeleteFileW(backup.c_str());
    e.DiscardDownload();
  }
  {
    // Counter-control: an ordinary commit leaves nothing behind and records nothing. Without it,
    // "orphans are recorded" would also pass if every commit reported every backup.
    seed_install();
    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    check("clean commit case: swap succeeds", e.Swap(), e.last_error());
    e.Commit();
    check("a commit that deletes everything records no orphans", e.orphaned_backups().empty(),
          std::to_string(e.orphaned_backups().size()));
    check("...and the backups really are gone",
          !exists(install + L"\\AlphaPayload.bin.gnlink-old"));
    e.DiscardDownload();
  }

  {
    // The NEXT update, with a backup nobody can delete still sitting on the name.
    //
    // This is what makes the recording worth having. A stuck `.gnlink-old` used to block the
    // move-aside outright, so one undeletable file meant no further update could be installed --
    // the litter was never the damage; being unable to ship a fix was.
    //
    // Stuck the way it is really stuck: the backup is a RUNNING IMAGE. Windows refuses to delete
    // one and permits renaming it, and that asymmetry is the whole reason this works. A copy of
    // the command interpreter under the payload's name is a valid PE and runs regardless of the
    // extension.
    seed_install();
    const std::wstring backup = install + L"\\AlphaPayload.bin.gnlink-old";
    wchar_t comspec[MAX_PATH]{};
    const bool haveShell = GetEnvironmentVariableW(L"COMSPEC", comspec, MAX_PATH) > 0;
    const bool copied = haveShell && CopyFileW(comspec, backup.c_str(), FALSE) != FALSE;
    check("next-attempt case: a stale backup exists", copied);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + backup + L"\"";
    const BOOL running = copied && CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                                                  CREATE_NO_WINDOW, nullptr, install.c_str(), &si,
                                                  &pi);
    check("next-attempt case: and something is running it, so it cannot be deleted",
          running != FALSE);
    check("next-attempt case: deleting it really does fail",
          DeleteFileW(backup.c_str()) == FALSE && GetLastError() == ERROR_ACCESS_DENIED,
          std::to_string(GetLastError()));

    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    check("a later update is not blocked by a backup that could not be deleted", e.Swap(),
          e.last_error());
    check("...and it says where the stuck one went",
          e.last_error().find("was moved to") != std::string::npos, e.last_error());
    check("...and it was recorded, not just moved", !e.orphaned_backups().empty());
    check("...and the name it needed is free again, holding this attempt's backup",
          exists(backup));
    check("...and the stuck one is still there under its new name, not deleted",
          exists(backup + L".1"));
    // The rollback that follows must produce the ORIGINAL bytes -- not the ones it happened to
    // find lying around under a similar name.
    check("...and a rollback after it restores the original bytes exactly", e.Rollback(),
          e.last_error());
    check("...bytes match the seeded original",
          read_text(install + L"\\AlphaPayload.bin") == kOldAlpha,
          read_text(install + L"\\AlphaPayload.bin"));

    if (running) {
      TerminateProcess(pi.hProcess, 0);
      WaitForSingleObject(pi.hProcess, 3000);
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
    for (int attempt = 0; attempt < 40; ++attempt) {
      if (DeleteFileW((backup + L".1").c_str())) break;
      Sleep(50);
    }
    DeleteFileW(backup.c_str());
    e.DiscardDownload();
  }
  {
    // The limit. Fifty set aside is a repeating failure, not a directory to keep growing, so the
    // swap stops -- and names which of the two problems it hit, because "could not move aside" on
    // its own sends an operator to look at the wrong file.
    seed_install();
    const std::wstring backup = install + L"\\AlphaPayload.bin.gnlink-old";
    wchar_t comspec[MAX_PATH]{};
    const bool haveShell = GetEnvironmentVariableW(L"COMSPEC", comspec, MAX_PATH) > 0;
    const bool copied = haveShell && CopyFileW(comspec, backup.c_str(), FALSE) != FALSE;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + backup + L"\"";
    const BOOL running = copied && CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                                                  CREATE_NO_WINDOW, nullptr, install.c_str(), &si,
                                                  &pi);
    check("cap case: an undeletable backup is in the way", running != FALSE);
    for (int n = 1; n <= 50; ++n) write_text(backup + L"." + std::to_wstring(n), "older");

    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    check("a swap with nowhere left to put the stuck backup fails", !e.Swap(), e.last_error());
    // The cause travels with the symptom. "Could not move aside" on its own sends an operator to
    // look at the payload; the file that is actually stuck is the backup behind it.
    check("...and names the cause, not just the file that would not move",
          e.last_error().find("nowhere left to put it") != std::string::npos, e.last_error());
    check("...and nothing was left replaced",
          read_text(install + L"\\BetaPayload.bin") == kOldBeta,
          read_text(install + L"\\BetaPayload.bin"));

    if (running) {
      TerminateProcess(pi.hProcess, 0);
      WaitForSingleObject(pi.hProcess, 3000);
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
    for (int n = 1; n <= 50; ++n) DeleteFileW((backup + L"." + std::to_wstring(n)).c_str());
    for (int attempt = 0; attempt < 40; ++attempt) {
      if (DeleteFileW(backup.c_str())) break;
      Sleep(50);
    }
    e.DiscardDownload();
  }
  {
    // The OTHER kind of stuck, and the limit of the rename trick -- stated rather than left to be
    // discovered. A backup held open with no sharing cannot be deleted AND cannot be renamed, so
    // there is no way past it; the swap fails and nothing is half-done. Renaming rescues a
    // running image, which is the common case, and it does not rescue this one.
    seed_install();
    const std::wstring backup = install + L"\\AlphaPayload.bin.gnlink-old";
    write_text(backup, "stuck");
    HANDLE hold = CreateFileW(backup.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    check("exclusive-hold case: the backup is held with no sharing",
          hold != INVALID_HANDLE_VALUE);
    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    check("a backup that cannot even be renamed stops the swap", !e.Swap(), e.last_error());
    check("...and nothing was left replaced",
          read_text(install + L"\\BetaPayload.bin") == kOldBeta,
          read_text(install + L"\\BetaPayload.bin"));
    check("...and the payload it could not move is unchanged",
          read_text(install + L"\\AlphaPayload.bin") == kOldAlpha,
          read_text(install + L"\\AlphaPayload.bin"));
    if (hold != INVALID_HANDLE_VALUE) CloseHandle(hold);
    DeleteFileW(backup.c_str());
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

  // ------------------------------------------------- ownership follows the chain (B1)
  //
  // The product is three tiers deep: GNLinkHost owns a window and starts GNLinkStream, which has
  // none and starts GNLinkCapture, which has none either. Ownership used to be a single link with
  // the parent required to have a window, so the third tier failed the check, was not an orphan
  // either (its parent is alive), and fell through to a direct request -- which for a windowless
  // process cannot succeed, and abandons the update.
  //
  // Every case below asks the same question: was this pid asked directly? PrepareForSwap only
  // asks the ones it has NOT left to a supervisor.
  {
    DummyProcess host, stream, capture, stranger;
    check("three-tier fixture: host started", host.start());
    check("three-tier fixture: stream started", stream.start());
    check("three-tier fixture: capture started", capture.start());
    check("three-tier fixture: unrelated process started", stranger.start());

    ProcessTarget tHost, tStream, tCapture, tStranger;
    check("three-tier identities captured",
          capture_process_identity(host.pid(), &tHost) &&
              capture_process_identity(stream.pid(), &tStream) &&
              capture_process_identity(capture.pid(), &tCapture) &&
              capture_process_identity(stranger.pid(), &tStranger));

    // The topology is described rather than inherited: these are four siblings started by this
    // test, and what is under test is how the fields are interpreted.
    tHost.hasWindow = true;
    tStream.hasWindow = false;
    tCapture.hasWindow = false;
    tStranger.hasWindow = false;
    tStream.parentPid = tHost.pid;
    tCapture.parentPid = tStream.pid;
    // Creation times have to be ordered parent-before-child for the walk to accept an edge, and
    // the real ones are whatever the OS handed out.
    tHost.creationTime = 1000;
    tStream.creationTime = 2000;
    tCapture.creationTime = 3000;
    tStranger.creationTime = 2000;

    // Returns the set of pids PrepareForSwap decided to ask directly.
    const auto asked_pids = [&](std::vector<ProcessTarget> targets) {
      auto shared = std::make_shared<std::vector<uint32_t>>();
      UpdateEffectsConfig c = base_config(install, staging);
      c.enumerateTargets = [targets]() { return targets; };
      c.requestStop = [shared](const ProcessTarget& t) {
        shared->push_back(t.pid);
        return true;  // recorded, not performed: these processes stay alive for the next case
      };
      WindowsUpdateEffects e(c);
      e.PrepareForSwap();
      return *shared;
    };
    const auto was_asked = [](const std::vector<uint32_t>& asked, uint32_t pid) {
      return std::find(asked.begin(), asked.end(), pid) != asked.end();
    };

    {
      const std::vector<uint32_t> asked = asked_pids({tHost, tStream, tCapture});
      check("a three-tier tree asks only the one with a window", asked.size() == 1,
            std::to_string(asked.size()) + " asked");
      check("...and that one is the host", was_asked(asked, tHost.pid));
      check("...the middle tier is left to it", !was_asked(asked, tStream.pid));
      check("...and so is the third tier -- this is the B1 fix",
            !was_asked(asked, tCapture.pid));
    }

    {
      // Enumeration order is not guaranteed, and a walk that depended on seeing the parent first
      // would pass above and fail in the field.
      const std::vector<uint32_t> asked = asked_pids({tCapture, tStream, tHost});
      check("the answer does not depend on enumeration order",
            asked.size() == 1 && was_asked(asked, tHost.pid),
            std::to_string(asked.size()) + " asked");
    }

    {
      // The two-tier shape has to keep working exactly as it did.
      ProcessTarget directChild = tStream;
      directChild.parentPid = tHost.pid;
      const std::vector<uint32_t> asked = asked_pids({tHost, directChild});
      check("a two-tier tree still asks only the parent",
            asked.size() == 1 && was_asked(asked, tHost.pid),
            std::to_string(asked.size()) + " asked");
    }

    {
      // A live parent that is not one of our targets is not a supervisor we are stopping, so
      // there is nobody to leave the child to. It gets asked, as before.
      ProcessTarget adopted = tCapture;
      adopted.parentPid = tStranger.pid;
      const std::vector<uint32_t> asked = asked_pids({tHost, tStream, adopted});
      check("a windowless process whose live parent is not a target is still asked directly",
            was_asked(asked, adopted.pid));
    }

    {
      // A chain that runs out before reaching a window is not ownership.
      ProcessTarget orphanedMiddle = tStream;
      orphanedMiddle.parentPid = tStranger.pid;  // alive, but not in the list
      const std::vector<uint32_t> asked = asked_pids({orphanedMiddle, tCapture});
      check("a chain that never reaches a window leaves both ends asked directly",
            was_asked(asked, orphanedMiddle.pid) && was_asked(asked, tCapture.pid),
            std::to_string(asked.size()) + " asked");
    }

    {
      // A parent that started after its child is a reused pid, not a parent -- at any depth.
      ProcessTarget youngerHost = tHost;
      youngerHost.creationTime = 9000;  // after the stream it supposedly started
      const std::vector<uint32_t> asked = asked_pids({youngerHost, tStream, tCapture});
      check("a parent younger than its child breaks the chain",
            was_asked(asked, tStream.pid) && was_asked(asked, tCapture.pid),
            std::to_string(asked.size()) + " asked");
    }

    {
      // An unknown creation time is not evidence of anything and must not be read as ownership.
      ProcessTarget timelessMiddle = tStream;
      timelessMiddle.creationTime = 0;
      const std::vector<uint32_t> asked = asked_pids({tHost, timelessMiddle, tCapture});
      check("an unknown creation time in the middle breaks the chain",
            was_asked(asked, tCapture.pid));
    }

    {
      // A cycle is a process table that is lying. The walk has to end, and end conservatively.
      ProcessTarget loopA = tStream;
      ProcessTarget loopB = tCapture;
      loopA.parentPid = loopB.pid;
      loopB.parentPid = loopA.pid;
      loopA.creationTime = 5000;
      loopB.creationTime = 4000;  // each is older than the other by its own account
      const std::vector<uint32_t> asked = asked_pids({loopA, loopB});
      check("a cycle terminates and is not treated as ownership",
            was_asked(asked, loopA.pid) || was_asked(asked, loopB.pid),
            std::to_string(asked.size()) + " asked");
    }

    host.kill();
    stream.kill();
    capture.kill();
    stranger.kill();
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
    // The field failure of 2026-09-11, as a pair: asking fails because the target is already
    // leaving, and asking fails because the target is there and refusing. Only the first is
    // forgiven, and the difference has to be the process, not the message.
    //
    // Three attempts in a row ended "AbandonedBeforeSwap -- could not ask pid 7332 to stop"
    // about a host that had acknowledged the handoff 70ms earlier and was standing down. The
    // enumeration and the request are two moments; between them the window was destroyed and
    // the request had nothing to reach.
    DummyProcess leaving;
    check("leaving dummy started", leaving.start());
    ProcessTarget gone;
    check("leaving identity captured", capture_process_identity(leaving.pid(), &gone));
    gone.hasWindow = true;  // it had one when it was enumerated
    leaving.kill();
    for (int i = 0; i < 100 && leaving.alive(); ++i) Sleep(10);
    check("the target really is gone before we ask", !leaving.alive());

    UpdateEffectsConfig c = base_config(install, staging);
    c.enumerateTargets = [gone]() { return std::vector<ProcessTarget>{gone}; };
    c.requestStop = [](const ProcessTarget&) { return false; };  // nothing left to ask
    WindowsUpdateEffects e(c);
    check("asking a process that already left is not a failure", e.PrepareForSwap(),
          e.last_error());
    check("and quiesce agrees it is gone", e.Quiesce(), e.last_error());
  }

  {
    // The other half of the same branch: a LIVE target whose stop request fails is still a
    // failure. Without this the forgiveness above would excuse every refusal.
    DummyProcess present;
    check("present dummy started", present.start());
    ProcessTarget alive;
    check("present identity captured", capture_process_identity(present.pid(), &alive));
    alive.hasWindow = true;

    UpdateEffectsConfig c = base_config(install, staging);
    c.enumerateTargets = [alive]() { return std::vector<ProcessTarget>{alive}; };
    c.requestStop = [](const ProcessTarget&) { return false; };
    WindowsUpdateEffects e(c);
    check("a live target that cannot be asked still fails", !e.PrepareForSwap());
    check("and says which pid", e.last_error().find(std::to_string(alive.pid)) !=
                                    std::string::npos, e.last_error());
    present.kill();
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
        manifest_for("0.2.105", {L"AlphaPayload.bin", L"BetaPayload.bin"}),
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
        manifest_for("0.2.105", {L"AlphaPayload.bin", L"BetaPayload.bin"}),
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
    // The OPTIONAL phase, because that is what this block is about: an image the machine does not
    // need failing to come back. It runs after the commit now, which is the point -- the install
    // is already final by the time this fails, and it stays final.
    c.relaunchOptional = [&]() {
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
      // A real launch that really failed, reported as an OPTIONAL image failing -- which is the
      // case this block is about: the install is good and stays. The severe verdict has its own
      // case in the state machine suite, where it rolls back.
      return ok ? RelaunchVerdict::AllBack : RelaunchVerdict::OptionalMissing;
    };
    WindowsUpdateEffects e(c);
    e.set_installed_version("0.2.104");
    e.set_manifest(manifest_for("0.2.105", {L"AlphaPayload.bin", L"BetaPayload.bin"}),
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
    check("rollback failure names the locked file and OS error instead of only health failure",
          e.last_error().find("remove-live file=BetaPayload.bin win32=32") != std::string::npos,
          e.last_error());
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
      e.set_manifest(manifest_for("0.2.105", {L"AlphaPayload.bin", L"BetaPayload.bin"}),
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
    const ManifestFields f = artifact_fields({"AlphaPayload.bin", "GNLinkSetup.exe"});
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

  // ---------------------------------------------------------------- the updater replaces itself

  {
    // The list said GNLinkUpdater.exe was a member. Nothing had ever put a new one in staging and
    // watched the installed one change, so "the updater can now be updated" rested on a name
    // being in a vector -- which is the same shape as the mistake that left it out for every
    // release. This replaces it for real.
    //
    // The reason it is safe is that the running updater is a copy OUTSIDE installDir, so the
    // destination of this swap is not the image executing it. updaterImagePath is set to that
    // working-copy shape here, exactly as updater_effects builds it in production.
    const std::string kOldUpdater = "OLD-UPDATER-BINARY";
    write_text(install + L"\\AlphaPayload.bin", kOldAlpha);
    write_text(install + L"\\GNLinkUpdater.exe", kOldUpdater);

    const std::wstring workingCopy = staging + L"\\..\\gnlink-updater-workingcopy";
    CreateDirectoryW(workingCopy.c_str(), nullptr);

    UpdateEffectsConfig c = base_config(install, staging);
    c.payloadNames = {L"AlphaPayload.bin", L"GNLinkUpdater.exe"};
    c.updaterImagePath = workingCopy + L"\\GNLinkUpdater.exe";
    std::string why;
    check("updater-member: the config is accepted", c.validate(&why), why);

    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields({"AlphaPayload.bin", "GNLinkUpdater.exe"});
    check("updater-member: download", e.Download(f), e.last_error());
    check("updater-member: swap succeeds", e.Swap(), e.last_error());
    check("updater-member: the installed GNLinkUpdater.exe really changed",
          read_text(install + L"\\GNLinkUpdater.exe") == kArtifactBytes,
          read_text(install + L"\\GNLinkUpdater.exe"));
    check("updater-member: ...and the old one was kept as a backup",
          exists(install + L"\\GNLinkUpdater.exe.gnlink-old"));
    e.DiscardDownload();

    // The guard that makes the above safe, from the other side: an updater running from INSIDE
    // the directory being replaced must be refused. Without this the first check would pass for
    // the wrong reason -- because nothing was checking, rather than because the layout is right.
    UpdateEffectsConfig bad = base_config(install, staging);
    bad.payloadNames = {L"AlphaPayload.bin", L"GNLinkUpdater.exe"};
    bad.updaterImagePath = install + L"\\GNLinkUpdater.exe";
    std::string badWhy;
    check("updater-member: an updater running from the install directory is refused",
          !bad.validate(&badWhy), badWhy);
    check("updater-member: ...and the reason says so",
          badWhy.find("running updater") != std::string::npos ||
              badWhy.find("inside the directory") != std::string::npos,
          badWhy);

    RemoveDirectoryW(workingCopy.c_str());
  }

  {
    // Condition 5, first regression: the artifact does not match its hash. Nothing may be
    // replaced -- including the Setup.
    seed_with_setup();
    UpdateEffectsConfig c = setup_member_config(install, staging);
    WindowsUpdateEffects e(c);
    ManifestFields f = artifact_fields({"AlphaPayload.bin", "GNLinkSetup.exe"});
    e.Download(f);
    f.artifacts[1].sha256 = std::string(64, 'b');
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
    e.Download(artifact_fields({"AlphaPayload.bin", "GNLinkSetup.exe"}));

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
    // GNLinkSetup.exe is named here because this config REPLACES it. The fixture used to leave it
    // out, and nothing minded -- which is the same shape as the release that was published
    // without it: a document describing a release that could not complete. VerifyDownload now
    // refuses that combination, so a case about what happens AFTER the swap has to describe a
    // release that can reach the swap.
    e.set_manifest(
        manifest_for("0.2.105", {L"AlphaPayload.bin", L"BetaPayload.bin", L"GNLinkSetup.exe"}),
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
    const std::wstring selfDir = selfPath.substr(0, slash);
    const std::wstring selfName = selfPath.substr(slash + 1);

    {
      // A payload whose DESTINATION is the running image. This is the thing that must not happen:
      // the swap would move the running executable aside and have nothing to put back.
      UpdateEffectsConfig c = base_config(selfDir, staging);
      c.payloadNames = {L"AlphaPayload.bin", selfName};
      std::string why;
      check("a payload destination that is the running updater is refused", !c.validate(&why),
            why);
      check("and the reason says so", why.find("updater") != std::string::npos, why);
    }
    {
      // The same NAME, a different directory -- and this must be ALLOWED. It is how the updater
      // stays updatable: it runs from a copy of itself outside installDir while the installed
      // copy is replaced like any other file. A name comparison refuses this, and refusing it
      // would leave a second permanent maintenance binary frozen at its compile-time constants
      // (the trap that ruled out option (C) for registration, history #440).
      UpdateEffectsConfig c = base_config(install, staging);
      c.payloadNames = {L"AlphaPayload.bin", selfName};
      c.updaterImagePath = selfPath;  // outside `install`, which is a temp directory
      std::string why;
      check("replacing an installed copy while running from elsewhere is allowed", c.validate(&why),
            why);
    }
    {
      // Defence in depth: even with a different name, a destination equal to the running image
      // is refused. Contrived, but it is the property being asserted rather than the spelling.
      UpdateEffectsConfig c = base_config(selfDir, staging);
      c.payloadNames = {selfName};
      c.updaterImagePath = selfDir + L"\\" + selfName;
      std::string why;
      check("the comparison is on the whole path, not the name alone", !c.validate(&why), why);
    }
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
    // The version string is looked for in the Setup, which is the artifact that carries one --
    // the rest are data. Here it does not, even though its hash is right.
    UpdateEffectsConfig c = base_config(install, staging);
    c.expectedVersion = "0.2.105";
    c.payloadNames = {L"GNLinkSetup.exe"};
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields({"GNLinkSetup.exe"});
    check("download for the version case", e.Download(f), e.last_error());
    check("a Setup that does not carry the expected version is refused", !e.VerifyDownload(f),
          e.last_error());
    check("and the reason names the version",
          e.last_error().find("0.2.105") != std::string::npos, e.last_error());
    e.DiscardDownload();
  }
  {
    // And an artifact that DOES carry it passes, so the case above is not passing for the trivial
    // reason that the check always fails.
    UpdateEffectsConfig c = base_config(install, staging);
    c.expectedVersion = "0.2.105";
    c.payloadNames = {L"GNLinkSetup.exe"};
    std::string utf16;
    for (char ch : std::string("0.2.105")) { utf16.push_back(ch); utf16.push_back('\0'); }
    c.fetchArtifact = [utf16](const ManifestArtifact&, const std::wstring& dest) {
      write_text(dest, utf16);
      return true;
    };
    WindowsUpdateEffects e(c);
    ManifestFields f = artifact_fields({"GNLinkSetup.exe"});
    {
      const std::wstring probe = staging + L"\\probe_v.bin";
      write_text(probe, utf16);
      uint64_t size = 0;
      file_size_bytes(probe, &size);
      f.artifacts[0].size = size;
      f.artifacts[0].sha256 = sha256_file_hex(probe);
      DeleteFileW(probe.c_str());
    }
    check("download for the positive version case", e.Download(f), e.last_error());
    check("a Setup that carries the expected version passes", e.VerifyDownload(f), e.last_error());
    e.DiscardDownload();
  }
  {
    // And the manifest's own version must agree with what we were told to expect.
    UpdateEffectsConfig c = base_config(install, staging);
    c.expectedVersion = "0.2.105";
    c.fetchArtifact = [](const ManifestArtifact&, const std::wstring& dest) {
      std::string utf16;
      for (char ch : std::string("0.2.105")) { utf16.push_back(ch); utf16.push_back('\0'); }
      write_text(dest, utf16);
      return true;
    };
    c.payloadNames = {L"GNLinkSetup.exe"};
    WindowsUpdateEffects e(c);
    ManifestFields f = artifact_fields({"GNLinkSetup.exe"});
    f.version = "0.9.9";  // disagrees with expectedVersion
    {
      // Size and hash have to match the bytes the fetcher writes, or the earlier checks fire first.
      const std::wstring probe = staging + L"\\probe2.bin";
      std::string utf16;
      for (char ch : std::string("0.2.105")) { utf16.push_back(ch); utf16.push_back('\0'); }
      write_text(probe, utf16);
      uint64_t size = 0;
      file_size_bytes(probe, &size);
      f.artifacts[0].size = size;
      f.artifacts[0].sha256 = sha256_file_hex(probe);
      DeleteFileW(probe.c_str());
    }
    check("download for the disagreement case", e.Download(f), e.last_error());
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
                                     const std::wstring&, std::string*) { return true; };

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
      c.relaunchRequired = []() { return RelaunchVerdict::AllBack; };
  c.relaunchOptional = []() { return RelaunchVerdict::AllBack; };
      c.healthCheck = []() { return false; };  // fails after registration

      WindowsUpdateEffects e(c);
      e.set_installed_version("0.2.105");
      e.set_manifest(manifest_for("0.3.0", {L"AlphaPayload.bin", L"BetaPayload.bin"}),
                     std::string(128, '0'));
      const auto accept = [](const std::string&, const std::vector<uint8_t>&) { return true; };
      const UpdateOutcome out = run_update(e, accept, "windows");
      // The restored build is unhealthy too here -- the injected health check answers the same way
    // either side of the rollback -- so this is the honest outcome rather than a clean rollback.
    check("wiring: a health failure rolls back and the restore is not healthy either",
          out.result == UpdateResult::RestoredButUnhealthy,
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
  // ---------------------------------------------------------------- the manifest has to arrive
  //
  // This was the gap that made everything downstream unreachable in production: FetchManifest
  // returned whatever set_manifest had been given, set_manifest had no production caller, and so
  // every real run ended at the first step with "no manifest available". Every later stage was
  // correct and never executed. What was missing was not a check but a WIRE, and nothing tested
  // that the wire existed.

  {
    // No document injected and no fetcher: the honest failure, unchanged.
    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    std::string doc;
    std::string sig;
    check("with neither a document nor a fetcher, FetchManifest fails",
          !e.FetchManifest(&doc, &sig));
    check("and says why", e.last_error().find("no manifest") != std::string::npos, e.last_error());
  }

  {
    // A fetcher, and nothing injected. This is the production shape.
    UpdateEffectsConfig c = base_config(install, staging);
    int fetches = 0;
    c.fetchManifest = [&fetches](std::string* document, std::string* signatureHex) {
      ++fetches;
      *document = "schema=2\nreleaseId=r-1\nplatform=windows\narch=x64\nversion=9.9.9\n";
      *signatureHex = std::string(128, 'a');
      return true;
    };
    WindowsUpdateEffects e(c);
    std::string doc;
    std::string sig;
    check("a configured fetcher supplies the manifest", e.FetchManifest(&doc, &sig),
          e.last_error());
    check("and the document is the one it fetched",
          doc.find("version=9.9.9") != std::string::npos, doc);
    check("and the signature too", sig == std::string(128, 'a'));

    // Fetched once per attempt, not once per stage. Re-fetching would let the server change what
    // is being installed part way through -- the hazard releaseId exists for, one level up.
    std::string again;
    std::string againSig;
    check("a second call does not go back to the server", e.FetchManifest(&again, &againSig));
    check("and returns the same bytes", again == doc && againSig == sig);
    check("the fetcher ran exactly once", fetches == 1, std::to_string(fetches));
  }

  {
    // A fetcher that fails is a failed attempt, not a silent one.
    UpdateEffectsConfig c = base_config(install, staging);
    c.fetchManifest = [](std::string*, std::string*) { return false; };
    WindowsUpdateEffects e(c);
    std::string doc;
    std::string sig;
    check("a fetcher that fails fails the step", !e.FetchManifest(&doc, &sig));
    check("and the reason names the fetch",
          e.last_error().find("fetch") != std::string::npos, e.last_error());
  }

  {
    // An injected document wins, so a test that supplies one never reaches the network even when
    // a fetcher is also configured.
    UpdateEffectsConfig c = base_config(install, staging);
    bool fetcherRan = false;
    c.fetchManifest = [&fetcherRan](std::string*, std::string*) {
      fetcherRan = true;
      return true;
    };
    WindowsUpdateEffects e(c);
    e.set_manifest("schema=2\nversion=0.0.1\n", std::string(128, 'b'));
    std::string doc;
    std::string sig;
    check("an injected document is used", e.FetchManifest(&doc, &sig));
    check("and the fetcher is not called", !fetcherRan);
  }

  remove_tree(install);

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED")
            << "  (" << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
