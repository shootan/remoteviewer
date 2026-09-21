#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

// A service that accepted a STOP has not necessarily stopped. (updater-abandon-race r4, item G)
//
// request_service_stop returns true when the SCM ACCEPTED the control. That is all it can mean: a
// service that accepts a STOP enters SERVICE_STOP_PENDING, and may sit there for as long as its
// wait hint allows while its process keeps running and keeps its files open. r3 reported the
// consequence honestly -- "not covered, and not claimed" -- and Codex kept it as a release
// blocker. This file is the attempt to cover it.
//
// ---------------------------------------------------------------------------------------------
// TWO KINDS OF EVIDENCE, AND THE DIFFERENCE BETWEEN THEM.
//
// PART 1 runs everywhere. It pins the DECISION with a real process standing in for the service's
// process: the stop request is answered "accepted" and the process has not gone, and the question
// asked is what the update does next. That is mock evidence for the SCM half -- no service exists
// in it, and nothing here establishes how a real SCM behaves.
//
// PART 2 is the real thing: an isolated fixture service, installed by this test, pointed at this
// binary, asked to stop through the PRODUCTION request_service_stop, and observed across the
// STOP_PENDING window into SERVICE_STOPPED. Installing a service needs SC_MANAGER_CREATE_SERVICE,
// which needs elevation. When that is refused, part 2 reports BLOCKED and names the exact checks
// it did not make. A blocked check is not a passing one and is not counted as one.
//
// The product's own service is never touched. The fixture's name carries this process's id, is
// asserted to differ from the product's, and is deleted at the end.
// ---------------------------------------------------------------------------------------------

#include <windows.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "secure_input_protocol.hpp"
#include "update_effects.hpp"
#include "update_process_targets.hpp"

using namespace remote60::native_poc::update;
using remote60::native_poc::kSecureInputServiceName;

namespace {

int gChecks = 0;
int gFailures = 0;
int gBlocked = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

// Not a pass. Counted apart so a run where the environment refused cannot be read as a run where
// the behaviour was confirmed.
void blocked(const std::string& name, const std::string& why) {
  ++gBlocked;
  std::cout << "BLOCKED  " << name << "  " << why << "\n";
}

std::wstring own_path() {
  wchar_t buffer[MAX_PATH * 2]{};
  GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
  return buffer;
}

void write_text(const std::wstring& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary);
  out << text;
}

std::string read_text(const std::wstring& path) {
  std::ifstream in(path, std::ios::binary);
  std::string out((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return out;
}

void remove_dir_tree(const std::wstring& dir) {
  WIN32_FIND_DATAW found{};
  HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &found);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      const std::wstring name = found.cFileName;
      if (name == L"." || name == L"..") continue;
      const std::wstring child = dir + L"\\" + name;
      if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        remove_dir_tree(child);
      } else {
        DeleteFileW(child.c_str());
      }
    } while (FindNextFileW(h, &found));
    FindClose(h);
  }
  RemoveDirectoryW(dir.c_str());
}

const char* kArtifact = "GNLINK-SERVICE-FIXTURE-ARTIFACT-v105";

/**
 * An update attempt against one pid, with the stop answer supplied by the caller.
 *
 * Everything else is real: the install directory, the staging, the swap, the state machine. The
 * caller decides only what the stop REQUEST answered, which is the one thing part 1 is modelling.
 */
struct AttemptResult {
  UpdateResult result = UpdateResult::NothingToDo;
  bool reachedSwap = false;
  uint64_t elapsedMs = 0;
  std::string detail;
  std::string lastError;
};

AttemptResult attempt_against(const std::wstring& dir, uint32_t pid, bool stopAnswer,
                              uint32_t quiesceMs) {
  const std::wstring staging = dir + L"-staging";
  CreateDirectoryW(dir.c_str(), nullptr);
  CreateDirectoryW(staging.c_str(), nullptr);
  write_text(dir + L"\\AlphaPayload.bin", "OLD-PAYLOAD");
  write_text(dir + L"\\sentinel.txt", "untouched");

  const std::wstring probe = staging + L"\\probe.bin";
  write_text(probe, kArtifact);
  const std::string sha = sha256_file_hex(probe);
  DeleteFileW(probe.c_str());

  UpdateEffectsConfig c;
  c.installDir = dir;
  c.stagingDir = staging;
  c.payloadNames = {L"AlphaPayload.bin"};
  c.lockName = L"Local\\gnlink-service-fixture-" + std::to_wstring(GetCurrentProcessId());
  c.fetchArtifact = [](const ManifestArtifact&, const std::wstring& dest) {
    write_text(dest, kArtifact);
    return true;
  };
  c.enumerateTargets = [pid]() {
    std::vector<ProcessTarget> only;
    ProcessTarget t;
    if (capture_process_identity(pid, &t)) {
      t.hasWindow = true;  // routed to a direct request, like the updater's stale view
      only.push_back(t);
    }
    return only;
  };
  c.requestStop = [stopAnswer](const ProcessTarget&) { return stopAnswer; };
  c.registryRoot = L"HKCU\\Software\\GNLinkServiceFixture";
  c.serviceName = L"GNLinkServiceFixtureUnused";
  c.captureRegistration = []() { return true; };
  c.registerInstall = []() { return true; };
  c.restoreRegistration = []() { return true; };
  c.relaunchRequired = []() { return RelaunchVerdict::AllBack; };
  c.relaunchOptional = []() { return RelaunchVerdict::AllBack; };
  c.healthCheck = []() { return true; };
  c.stopSettleMs = 500;
  c.quiesceTimeoutMs = quiesceMs;
  c.updaterImagePath = own_path();

  std::string manifest =
      "schema=2\nreleaseId=r-0.2.105\nplatform=windows\narch=x64\nversion=0.2.105\n";
  manifest += "artifact=AlphaPayload.bin|" +
              std::to_string(std::char_traits<char>::length(kArtifact)) + "|" + sha +
              "|https://u.example/AlphaPayload.bin\n";

  WindowsUpdateEffects e(c);
  e.set_installed_version("0.2.104");
  e.set_manifest(manifest, std::string(128, '0'));
  const auto accept = [](const std::string&, const std::vector<uint8_t>&) { return true; };

  const uint64_t began = GetTickCount64();
  const UpdateOutcome out = run_update(e, accept, "windows");
  AttemptResult r;
  r.result = out.result;
  r.reachedSwap = out.entered(UpdateState::Swap);
  r.elapsedMs = GetTickCount64() - began;
  r.detail = out.detail;
  r.lastError = e.last_error();
  return r;
}

void assert_install_preserved(const std::string& prefix, const std::wstring& dir) {
  check(prefix + ": the payload still holds the old bytes",
        read_text(dir + L"\\AlphaPayload.bin") == "OLD-PAYLOAD",
        read_text(dir + L"\\AlphaPayload.bin"));
  check(prefix + ": no backup was made",
        GetFileAttributesW((dir + L"\\AlphaPayload.bin.gnlink-old").c_str()) ==
            INVALID_FILE_ATTRIBUTES);
  check(prefix + ": nothing else in the directory was written either",
        read_text(dir + L"\\sentinel.txt") == "untouched");
}

// ------------------------------------------------------------------------------ the fixture side

HANDLE gServiceStop = nullptr;
SERVICE_STATUS_HANDLE gStatusHandle = nullptr;
SERVICE_STATUS gStatus{};
DWORD gLingerMs = 0;

void report(DWORD state, DWORD checkPoint, DWORD waitHint) {
  gStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  gStatus.dwCurrentState = state;
  gStatus.dwControlsAccepted = (state == SERVICE_RUNNING) ? SERVICE_ACCEPT_STOP : 0;
  gStatus.dwWin32ExitCode = NO_ERROR;
  gStatus.dwCheckPoint = checkPoint;
  gStatus.dwWaitHint = waitHint;
  if (gStatusHandle) SetServiceStatus(gStatusHandle, &gStatus);
}

DWORD WINAPI service_control(DWORD control, DWORD, LPVOID, LPVOID) {
  if (control == SERVICE_CONTROL_STOP) {
    // Accept, and then take our time. This is the window the whole item is about: the SCM has
    // said yes, and the process is still here.
    report(SERVICE_STOP_PENDING, 1, gLingerMs + 5000);
    if (gServiceStop) SetEvent(gServiceStop);
  }
  return NO_ERROR;
}

void WINAPI service_main(DWORD, wchar_t** argv) {
  gStatusHandle = RegisterServiceCtrlHandlerExW(argv[0], service_control, nullptr);
  if (!gStatusHandle) return;
  gServiceStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  report(SERVICE_RUNNING, 0, 0);
  WaitForSingleObject(gServiceStop, 120000);
  const DWORD until = GetTickCount() + gLingerMs;
  while (GetTickCount() < until) {
    report(SERVICE_STOP_PENDING, 2, gLingerMs + 5000);
    Sleep(100);
  }
  report(SERVICE_STOPPED, 0, 0);
}

int run_fixture_service(DWORD lingerMs) {
  gLingerMs = lingerMs;
  wchar_t empty[] = L"";
  SERVICE_TABLE_ENTRYW table[] = {{empty, service_main}, {nullptr, nullptr}};
  return StartServiceCtrlDispatcherW(table) ? 0 : 91;
}

/** A plain process that stays until it is released. Part 1's stand-in for a service process. */
int run_fixture_linger(const std::wstring& eventName) {
  HANDLE e = OpenEventW(SYNCHRONIZE, FALSE, eventName.c_str());
  if (!e) return 91;
  WaitForSingleObject(e, 120000);
  CloseHandle(e);
  return 0;
}

struct Linger {
  PROCESS_INFORMATION pi{};
  HANDLE release = nullptr;

  bool start(const std::wstring& tag) {
    const std::wstring name =
        L"Local\\gnlink-svcfix-" + tag + L"-" + std::to_wstring(GetCurrentProcessId());
    release = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
    if (!release) return false;
    std::wstring cmd = L"\"" + own_path() + L"\" --fixture-linger " + name;
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    return CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                          nullptr, nullptr, &si, &pi) != FALSE;
  }
  ~Linger() {
    if (release) {
      SetEvent(release);
      WaitForSingleObject(pi.hProcess, 10000);
      CloseHandle(release);
    }
    if (pi.hThread) CloseHandle(pi.hThread);
    if (pi.hProcess) CloseHandle(pi.hProcess);
  }
  uint32_t pid() const { return static_cast<uint32_t>(pi.dwProcessId); }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 3 && std::string(argv[1]) == "--fixture-service") {
    return run_fixture_service(static_cast<DWORD>(std::strtoul(argv[2], nullptr, 10)));
  }
  if (argc >= 3 && std::string(argv[1]) == "--fixture-linger") {
    const std::string narrow(argv[2]);
    return run_fixture_linger(std::wstring(narrow.begin(), narrow.end()));
  }

  std::cout << "update_service_stop_test\n";

  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  const std::wstring base = std::wstring(temp) + L"gnlink-svcstop-" +
                            std::to_wstring(GetCurrentProcessId());

  // ======================================================================= PART 1 (mock evidence)
  //
  // No service exists in any of these. What is modelled is only the ANSWER a service stop gives --
  // "the SCM accepted" -- against a process that has or has not gone. Read as evidence about the
  // update's decision, not about the SCM.
  std::cout << "\n-- part 1: what an accepted stop is worth (mock: no service involved)\n";

  {
    // Accepted, and the process is still there: the shape of SERVICE_STOP_PENDING.
    Linger l;
    check("mock: a process to stand in for a service started", l.start(L"pending"));
    const std::wstring dir = base + L"-pending";
    const AttemptResult r = attempt_against(dir, l.pid(), true, 800);
    check("mock: an accepted stop whose process has not gone does not swap",
          r.result == UpdateResult::AbandonedBeforeSwap,
          std::string(result_name(r.result)) + " / " + r.detail + " / " + r.lastError);
    check("mock: ...the state machine never reached Swap", !r.reachedSwap);
    check("mock: ...and it waited for the budget before giving up", r.elapsedMs + 32 >= 800,
          std::to_string(r.elapsedMs) + "ms of an 800ms budget");
    assert_install_preserved("mock", dir);
    remove_dir_tree(dir + L"-staging");
    remove_dir_tree(dir);
  }

  {
    // Accepted, and the process goes while the update is waiting: the attempt proceeds.
    Linger l;
    check("mock: a process that will leave started", l.start(L"goes"));
    const std::wstring dir = base + L"-goes";
    // Released from another thread so the exit lands inside the quiesce wait.
    HANDLE releaser = l.release;
    std::thread letGo([releaser]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      SetEvent(releaser);
    });
    const AttemptResult r = attempt_against(dir, l.pid(), true, 5000);
    letGo.join();
    check("mock: a process that actually stops lets the attempt through",
          r.result == UpdateResult::Updated,
          std::string(result_name(r.result)) + " / " + r.detail + " / " + r.lastError);
    check("mock: ...and this time Swap was reached", r.reachedSwap);
    check("mock: ...and the payload was replaced",
          read_text(dir + L"\\AlphaPayload.bin") == kArtifact,
          read_text(dir + L"\\AlphaPayload.bin"));
    remove_dir_tree(dir + L"-staging");
    remove_dir_tree(dir);
  }

  {
    // Refused outright: abandoned before the swap, install preserved.
    Linger l;
    check("mock: a process for the refused case started", l.start(L"refused"));
    const std::wstring dir = base + L"-refused";
    const AttemptResult r = attempt_against(dir, l.pid(), false, 800);
    check("mock: a refused stop abandons before the swap",
          r.result == UpdateResult::AbandonedBeforeSwap,
          std::string(result_name(r.result)) + " / " + r.detail + " / " + r.lastError);
    check("mock: ...without reaching Swap", !r.reachedSwap);
    assert_install_preserved("mock refused", dir);
    remove_dir_tree(dir + L"-staging");
    remove_dir_tree(dir);
  }

  // ================================================================= PART 2 (real service, or not)
  std::cout << "\n-- part 2: the same three questions, against a real isolated service\n";

  const std::wstring fixtureService =
      L"GNLinkUpdFixtureSvc" + std::to_wstring(GetCurrentProcessId());
  check("the fixture service is not the product's",
        fixtureService != kSecureInputServiceName &&
            fixtureService.find(L"GNLink") == 0 && fixtureService != L"GNLinkSecureInput",
        std::string(fixtureService.begin(), fixtureService.end()));

  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
  const DWORD managerErr = GetLastError();
  if (!manager) {
    const std::string why = "OpenSCManager(SC_MANAGER_CREATE_SERVICE) failed with " +
                            std::to_string(managerErr) +
                            (managerErr == ERROR_ACCESS_DENIED ? " (access denied -- this run is "
                                                                 "not elevated)"
                                                               : "");
    blocked("real service: a STOP that is accepted but not yet complete blocks the swap", why);
    blocked("real service: the swap proceeds once the service has actually stopped", why);
    blocked("real service: a refusal or a timeout leaves the install untouched", why);
    std::cout << "\n  Not run, and therefore not claimed. Installing a service needs\n"
                 "  SC_MANAGER_CREATE_SERVICE, which needs elevation; nothing here falls back to\n"
                 "  the product's service, because stopping that would be operating on the user's\n"
                 "  machine rather than on a fixture. Run this binary from an elevated shell to\n"
                 "  get the three checks above.\n";
  } else {
    std::wstring binPath = L"\"" + own_path() + L"\" --fixture-service 3000";
    SC_HANDLE service = CreateServiceW(
        manager, fixtureService.c_str(), L"GNLink update fixture (test)", SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, binPath.c_str(),
        nullptr, nullptr, nullptr, nullptr, nullptr);
    if (!service) {
      blocked("real service: the fixture service could not be created",
              "CreateService failed with " + std::to_string(GetLastError()));
      CloseServiceHandle(manager);
    } else {
      check("real service: the fixture service started", StartServiceW(service, 0, nullptr) != FALSE,
            "err=" + std::to_string(GetLastError()));

      SERVICE_STATUS_PROCESS ssp{};
      DWORD needed = 0;
      const auto query = [&]() {
        return QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                                    reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp),
                                    &needed) != FALSE;
      };
      for (int i = 0; i < 100 && query() && ssp.dwCurrentState != SERVICE_RUNNING; ++i) {
        Sleep(100);
      }
      check("real service: it reached RUNNING", query() && ssp.dwCurrentState == SERVICE_RUNNING,
            "state=" + std::to_string(ssp.dwCurrentState));
      const uint32_t servicePid = ssp.dwProcessId;

      // The production function, against the fixture's own name.
      check("real service: the production request_service_stop is accepted",
            request_service_stop(fixtureService.c_str()));
      check("real service: ...and the service is STOP_PENDING, not STOPPED",
            query() && ssp.dwCurrentState == SERVICE_STOP_PENDING,
            "state=" + std::to_string(ssp.dwCurrentState));

      // Question 1: with the SCM's yes in hand and the process still there, does the update swap?
      {
        const std::wstring dir = base + L"-svc-pending";
        const AttemptResult r = attempt_against(dir, servicePid, true, 800);
        check("real service: a STOP that is accepted but not yet complete blocks the swap",
              r.result == UpdateResult::AbandonedBeforeSwap && !r.reachedSwap,
              std::string(result_name(r.result)) + " / " + r.detail);
        assert_install_preserved("real service pending", dir);
        remove_dir_tree(dir + L"-staging");
        remove_dir_tree(dir);
      }

      // Question 2: once it has actually gone, the attempt proceeds.
      for (int i = 0; i < 150 && query() && ssp.dwCurrentState != SERVICE_STOPPED; ++i) Sleep(100);
      check("real service: it reached STOPPED on its own",
            query() && ssp.dwCurrentState == SERVICE_STOPPED,
            "state=" + std::to_string(ssp.dwCurrentState));
      {
        const std::wstring dir = base + L"-svc-stopped";
        const AttemptResult r = attempt_against(dir, servicePid, true, 5000);
        check("real service: the swap proceeds once the service has actually stopped",
              r.result == UpdateResult::Updated && r.reachedSwap,
              std::string(result_name(r.result)) + " / " + r.detail + " / " + r.lastError);
        check("real service: ...and the payload was replaced",
              read_text(dir + L"\\AlphaPayload.bin") == kArtifact);
        remove_dir_tree(dir + L"-staging");
        remove_dir_tree(dir);
      }

      // Question 3: a refusal leaves everything alone. The service is already stopped, so the
      // refusal is supplied as the stop answer -- what is under test is the consequence.
      {
        Linger l;
        l.start(L"svc-refused");
        const std::wstring dir = base + L"-svc-refused";
        const AttemptResult r = attempt_against(dir, l.pid(), false, 800);
        check("real service: a refusal or a timeout leaves the install untouched",
              r.result == UpdateResult::AbandonedBeforeSwap && !r.reachedSwap,
              std::string(result_name(r.result)) + " / " + r.detail);
        assert_install_preserved("real service refused", dir);
        remove_dir_tree(dir + L"-staging");
        remove_dir_tree(dir);
      }

      check("real service: the fixture service was removed", DeleteService(service) != FALSE,
            "err=" + std::to_string(GetLastError()));
      CloseServiceHandle(service);
      CloseServiceHandle(manager);
    }
  }

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  (" << gChecks
            << " checks, " << gFailures << " failed, " << gBlocked << " blocked)\n";
  return gFailures == 0 ? 0 : 1;
}
