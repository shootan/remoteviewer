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

#include <windows.h>

#include <fstream>
#include <iostream>
#include <string>
#include <thread>
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
  c.enumerateTargets = []() { return std::vector<uint32_t>{}; };
  c.requestStop = [](uint32_t) { return true; };
  c.registerInstall = []() { return true; };
  c.relaunch = []() { return true; };
  c.healthCheck = []() { return true; };
  c.quiesceTimeoutMs = 5000;
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

int main() {
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
    check("backups are dropped only after registration",
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
    const std::vector<uint32_t> pids = {a.pid(), b.pid()};

    UpdateEffectsConfig c = base_config(install, staging);
    c.enumerateTargets = [pids]() { return pids; };
    c.requestStop = [&](uint32_t pid) {
      // Stands in for the real WM_CLOSE / CTRL_BREAK request. These are our own children.
      if (pid == a.pid()) { a.kill(); return true; }
      if (pid == b.pid()) { b.kill(); return true; }
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

    UpdateEffectsConfig c = base_config(install, staging);
    c.quiesceTimeoutMs = 300;  // short, because the point is that it gives up
    c.enumerateTargets = [pid]() { return std::vector<uint32_t>{pid}; };
    c.requestStop = [](uint32_t) { return true; };  // asked, but it does not comply
    WindowsUpdateEffects e(c);
    check("prepare succeeds (asking worked)", e.PrepareForSwap(), e.last_error());
    check("quiesce gives up rather than forcing", !e.Quiesce(), e.last_error());
    check("the stubborn process is STILL ALIVE -- it was never terminated", stubborn.alive());
    stubborn.kill();
  }

  {
    // A target that cannot even be asked: abandoned at Prepare, before Quiesce.
    UpdateEffectsConfig c = base_config(install, staging);
    c.enumerateTargets = []() { return std::vector<uint32_t>{4}; };  // System, never ours
    c.requestStop = [](uint32_t) { return false; };
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

  remove_tree(staging);
  remove_tree(install);

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED")
            << "  (" << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
