#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

// A service that accepted a STOP has not necessarily stopped. (updater-abandon-race r4/r5, item G)
//
// request_service_stop returns true when the SCM ACCEPTED the control. That is all it can mean: a
// service that accepts a STOP enters SERVICE_STOP_PENDING and may sit there for as long as its
// wait hint allows, while its process keeps running and keeps its files open. r3 reported the
// consequence honestly -- "not covered, and not claimed" -- and it has been a release blocker
// since.
//
// ---------------------------------------------------------------------------------------------
// TWO KINDS OF EVIDENCE, AND THE DIFFERENCE BETWEEN THEM.
//
// PART 1 runs everywhere. A real process stands in for a service's process and the stop answer is
// supplied by the test. It pins the DECISION -- an accepted stop whose process has not gone must
// not swap -- and nothing more. No service exists in it, so it says nothing about the SCM.
//
// PART 2 is the real thing, and r5 rebuilt it after the r4 version was found to be theatre:
//
//   * the attempt's requestStop callback IS the production request_service_stop, against the
//     fixture service, so the SCM actually receives the control during the attempt;
//   * the identity and the watch handle are taken INSIDE that attempt while the service is still
//     RUNNING, and the same attempt then crosses STOP_PENDING, the real process exit, and the
//     swap. The r4 version stopped the service first and ran a fresh attempt afterwards, which
//     would have "succeeded" just as well with an empty target list;
//   * the refusal case is a REAL refusal: a fixture service whose DACL does not grant
//     SERVICE_STOP, so OpenService inside the production function fails with access denied.
//
// Part 2 needs SC_MANAGER_CREATE_SERVICE, which needs elevation. Without it every part-2 check is
// reported BLOCKED, counted apart from passes and failures, and --require-real makes a blocked run
// exit non-zero so it cannot be mistaken for a green one.
//
// The product's own service is never touched. The fixture names carry this process's id, are
// compared against the product's before anything is opened, and are deleted at the end. The
// scratch directories are under the build tree and are removed through the bounded remover -- this
// binary is meant to be run elevated, where a recursive delete that wandered would do real damage.
//
// Usage:
//   (no flags)       part 1 and, if it can, part 2. Blocked part-2 checks are reported.
//   --mock-only      part 1 only. Part 2 is not attempted and not reported as blocked.
//   --require-real   part 2 must run. Any blocked check makes the exit code non-zero.
// ---------------------------------------------------------------------------------------------

#include <windows.h>
#include <sddl.h>

#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "secure_input_protocol.hpp"
#include "test_scratch_dir.hpp"
#include "update_effects.hpp"
#include "update_process_targets.hpp"

using namespace remote60::native_poc::update;
using remote60::native_poc::kSecureInputServiceName;
using remote60::native_poc::test_support::remove_scratch_tree;
using remote60::native_poc::test_support::scratch_root;

namespace {

int gChecks = 0;
int gFailures = 0;
int gBlocked = 0;
int gAttempts = 0;
int gCleanupFailures = 0;
std::vector<std::string> gCleanupLeft;

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

std::string narrow(const std::wstring& w) { return std::string(w.begin(), w.end()); }

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

/**
 * A scratch path for this test, under the build tree rather than %TEMP%.
 *
 * This one matters more than most: part 2 is meant to be run from an ELEVATED shell, where a
 * recursive delete that wandered would do real damage. remove_scratch_tree refuses any path that
 * does not canonicalise to somewhere strictly beneath the compile-time root, and removes reparse
 * points as links rather than descending into them. See test_scratch_dir.hpp.
 */
std::wstring scratch(const std::wstring& name) {
  const std::wstring root = scratch_root();
  return root.empty() ? std::wstring() : root + L"\\" + name;
}

/** Cleans one attempt's directories and records, rather than ignores, anything left behind. */
void clean_attempt_dirs(const std::wstring& dir) {
  for (const std::wstring& d : {dir + L"-staging", dir}) {
    if (!remove_scratch_tree(d)) {
      ++gCleanupFailures;
      gCleanupLeft.push_back(narrow(d));
    }
  }
}

const char* kArtifact = "GNLINK-SERVICE-FIXTURE-ARTIFACT-v105";

const char* service_state_name(DWORD state) {
  switch (state) {
    case SERVICE_STOPPED: return "STOPPED";
    case SERVICE_START_PENDING: return "START_PENDING";
    case SERVICE_STOP_PENDING: return "STOP_PENDING";
    case SERVICE_RUNNING: return "RUNNING";
    case SERVICE_CONTINUE_PENDING: return "CONTINUE_PENDING";
    case SERVICE_PAUSE_PENDING: return "PAUSE_PENDING";
    case SERVICE_PAUSED: return "PAUSED";
    case 0: return "(not queried)";
    default: return "(other)";
  }
}

/** Current state and pid of a service, or {0,0} when it cannot be queried. */
struct ServiceSnapshot {
  DWORD state = 0;
  DWORD pid = 0;
};

ServiceSnapshot snapshot_service(SC_HANDLE service) {
  SERVICE_STATUS_PROCESS ssp{};
  DWORD needed = 0;
  if (!service) return {};
  if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp),
                            sizeof(ssp), &needed)) {
    return {};
  }
  return {ssp.dwCurrentState, ssp.dwProcessId};
}

/**
 * One update attempt against one pid, with the stop request supplied by the caller.
 *
 * Everything else is real: the install directory, the staging, the swap, the state machine, the
 * quiesce wait on a handle held from before the request. In part 2 the supplied request IS the
 * production request_service_stop, so the only thing this parameterises is WHICH stop is asked
 * for -- not whether one happens.
 */
struct AttemptResult {
  UpdateResult result = UpdateResult::NothingToDo;
  bool reachedSwap = false;
  int targets = -1;
  uint64_t elapsedMs = 0;
  std::string detail;
  std::string lastError;
  // Filled by the hooks below, so the ORDER can be asserted rather than assumed.
  DWORD stateAtEnumerate = 0;
  DWORD stateAfterStop = 0;
  bool stopAnswer = false;
  int stopCalls = 0;
};

using StopRequest = std::function<bool(const ProcessTarget&)>;
using StateProbe = std::function<DWORD()>;

AttemptResult attempt_against(const std::wstring& dir, uint32_t pid, const StopRequest& stop,
                              uint32_t settleMs, uint32_t quiesceMs,
                              const StateProbe& probe = nullptr) {
  ++gAttempts;
  const std::wstring staging = dir + L"-staging";
  CreateDirectoryW(dir.c_str(), nullptr);
  CreateDirectoryW(staging.c_str(), nullptr);
  write_text(dir + L"\\AlphaPayload.bin", "OLD-PAYLOAD");
  write_text(dir + L"\\sentinel.txt", "untouched");

  const std::wstring probeFile = staging + L"\\probe.bin";
  write_text(probeFile, kArtifact);
  const std::string sha = sha256_file_hex(probeFile);
  DeleteFileW(probeFile.c_str());

  auto out = std::make_shared<AttemptResult>();

  UpdateEffectsConfig c;
  c.installDir = dir;
  c.stagingDir = staging;
  c.payloadNames = {L"AlphaPayload.bin"};
  c.lockName = L"Local\\gnlink-service-fixture-" + std::to_wstring(GetCurrentProcessId());
  c.fetchArtifact = [](const ManifestArtifact&, const std::wstring& dest) {
    write_text(dest, kArtifact);
    return true;
  };
  c.enumerateTargets = [pid, out, probe]() {
    // The service's state AT THE MOMENT the target is captured. This is what makes the ordering
    // claim checkable: the identity, and the handle taken from it, were obtained while it ran.
    if (probe) out->stateAtEnumerate = probe();
    std::vector<ProcessTarget> only;
    ProcessTarget t;
    if (capture_process_identity(pid, &t)) {
      t.hasWindow = true;  // routed to a direct request, like the updater's stale view
      only.push_back(t);
    }
    out->targets = static_cast<int>(only.size());
    return only;
  };
  c.requestStop = [stop, out, probe](const ProcessTarget& t) {
    ++out->stopCalls;
    const bool answer = stop(t);
    out->stopAnswer = answer;
    // And the state immediately AFTER the control was accepted. STOP_PENDING is the window this
    // whole item is about, and it has to be observed, not inferred from a wait that came later.
    if (probe) out->stateAfterStop = probe();
    return answer;
  };
  c.registryRoot = L"HKCU\\Software\\GNLinkServiceFixture";
  c.serviceName = L"GNLinkServiceFixtureUnused";
  c.captureRegistration = []() { return true; };
  c.registerInstall = []() { return true; };
  c.restoreRegistration = []() { return true; };
  c.relaunchRequired = []() { return RelaunchVerdict::AllBack; };
  c.relaunchOptional = []() { return RelaunchVerdict::AllBack; };
  c.healthCheck = []() { return true; };
  c.stopSettleMs = settleMs;
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
  const UpdateOutcome outcome = run_update(e, accept, "windows");
  out->result = outcome.result;
  out->reachedSwap = outcome.entered(UpdateState::Swap);
  out->elapsedMs = GetTickCount64() - began;
  out->detail = outcome.detail;
  out->lastError = e.last_error();
  return *out;
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
    // said yes, and the process is still here holding whatever it holds.
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
  WaitForSingleObject(gServiceStop, 180000);
  const ULONGLONG until = GetTickCount64() + gLingerMs;
  while (GetTickCount64() < until) {
    report(SERVICE_STOP_PENDING, 2, gLingerMs + 5000);
    Sleep(200);
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
  WaitForSingleObject(e, 180000);
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

/**
 * A fixture service: created, started, and deleted by this test, named after this process.
 *
 * Nothing here ever names a service it did not create. The product's service name is compared
 * against and refused before anything is opened.
 */
struct FixtureService {
  SC_HANDLE handle = nullptr;
  std::wstring name;
  bool created = false;
  bool deleted = false;

  bool create(SC_HANDLE manager, const std::wstring& serviceName, DWORD lingerMs) {
    name = serviceName;
    if (name == kSecureInputServiceName) return false;  // never, under any circumstances
    const std::wstring binPath =
        L"\"" + own_path() + L"\" --fixture-service " + std::to_wstring(lingerMs);
    handle = CreateServiceW(manager, name.c_str(), L"GNLink update fixture (test)",
                            SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START,
                            SERVICE_ERROR_NORMAL, binPath.c_str(), nullptr, nullptr, nullptr,
                            nullptr, nullptr);
    created = handle != nullptr;
    return created;
  }

  bool start_and_wait(int tries = 100) {
    if (!handle) return false;
    if (!StartServiceW(handle, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
      return false;
    }
    for (int i = 0; i < tries; ++i) {
      if (snapshot_service(handle).state == SERVICE_RUNNING) return true;
      Sleep(100);
    }
    return false;
  }

  /**
   * Removes the stop right from the service's own DACL.
   *
   * This is how a REAL refusal is produced. r4 supplied "the stop was refused" as a boolean and
   * called that a refusal case; this makes the production request_service_stop genuinely fail, at
   * OpenService, with access denied -- the same way a service the updater can see but not control
   * fails in the field.
   *
   * The rights kept are the ones this test still needs to clean up after itself: query, read and
   * write the DAC, and delete. WP -- stop and pause -- is the one that is gone.
   */
  bool deny_stop() {
    if (!handle) return false;
    const wchar_t* sddl = L"D:(A;;CCLCRPRCWDWODT;;;BA)(A;;CCLCRPRCWDWODT;;;SY)";
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd,
                                                              nullptr)) {
      return false;
    }
    const bool ok = SetServiceObjectSecurity(handle, DACL_SECURITY_INFORMATION, sd) != FALSE;
    LocalFree(sd);
    return ok;
  }

  /** Stops if it can, waits for STOPPED if it can, and deletes. Reports what it managed. */
  void destroy() {
    if (!handle) return;
    SERVICE_STATUS st{};
    ControlService(handle, SERVICE_CONTROL_STOP, &st);
    for (int i = 0; i < 150; ++i) {
      const DWORD state = snapshot_service(handle).state;
      if (state == SERVICE_STOPPED || state == 0) break;
      Sleep(100);
    }
    deleted = DeleteService(handle) != FALSE;
    CloseServiceHandle(handle);
    handle = nullptr;
  }

  ~FixtureService() {
    if (handle) destroy();
  }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 3 && std::string(argv[1]) == "--fixture-service") {
    return run_fixture_service(static_cast<DWORD>(std::strtoul(argv[2], nullptr, 10)));
  }
  if (argc >= 3 && std::string(argv[1]) == "--fixture-linger") {
    const std::string narrowName(argv[2]);
    return run_fixture_linger(std::wstring(narrowName.begin(), narrowName.end()));
  }

  bool requireReal = false;
  bool mockOnly = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg == "--require-real") requireReal = true;
    if (arg == "--mock-only") mockOnly = true;
  }
  if (requireReal && mockOnly) {
    std::cout << "--require-real and --mock-only contradict each other\n";
    return 2;
  }

  std::cout << "update_service_stop_test"
            << (requireReal ? "  [--require-real]" : (mockOnly ? "  [--mock-only]" : "")) << "\n";

  if (scratch_root().empty()) {
    std::cout << "RESULT: FAILED  (no scratch root -- nothing may be written anywhere else)\n";
    return 1;
  }

  const auto always = [](bool answer) -> StopRequest {
    return [answer](const ProcessTarget&) { return answer; };
  };

  // ======================================================================= PART 1 (mock evidence)
  std::cout << "\n-- part 1: what an accepted stop is worth (mock: no service involved)\n";

  {
    // Accepted, and the process is still there: the shape of SERVICE_STOP_PENDING.
    Linger l;
    check("mock: a process to stand in for a service started", l.start(L"pending"));
    const std::wstring dir = scratch(L"svc-mock-pending");
    const AttemptResult r = attempt_against(dir, l.pid(), always(true), 500, 800);
    check("mock: an accepted stop whose process has not gone does not swap",
          r.result == UpdateResult::AbandonedBeforeSwap,
          std::string(result_name(r.result)) + " / " + r.detail + " / " + r.lastError);
    check("mock: ...with a target, not an empty list", r.targets == 1,
          "targets=" + std::to_string(r.targets));
    check("mock: ...the state machine never reached Swap", !r.reachedSwap);
    check("mock: ...and it waited for the budget before giving up", r.elapsedMs + 32 >= 800,
          std::to_string(r.elapsedMs) + "ms of an 800ms quiesce budget");
    assert_install_preserved("mock", dir);
    clean_attempt_dirs(dir);
  }

  {
    // Accepted, and the process goes while the update is waiting: the attempt proceeds.
    Linger l;
    check("mock: a process that will leave started", l.start(L"goes"));
    const std::wstring dir = scratch(L"svc-mock-goes");
    HANDLE releaser = l.release;
    std::thread letGo([releaser]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      SetEvent(releaser);
    });
    const AttemptResult r = attempt_against(dir, l.pid(), always(true), 500, 5000);
    letGo.join();
    check("mock: a process that actually stops lets the attempt through",
          r.result == UpdateResult::Updated,
          std::string(result_name(r.result)) + " / " + r.detail + " / " + r.lastError);
    check("mock: ...and this time Swap was reached", r.reachedSwap);
    check("mock: ...and the payload was replaced",
          read_text(dir + L"\\AlphaPayload.bin") == kArtifact,
          read_text(dir + L"\\AlphaPayload.bin"));
    clean_attempt_dirs(dir);
  }

  {
    // Refused outright: abandoned before the swap, install preserved.
    Linger l;
    check("mock: a process for the refused case started", l.start(L"refused"));
    const std::wstring dir = scratch(L"svc-mock-refused");
    const AttemptResult r = attempt_against(dir, l.pid(), always(false), 500, 800);
    check("mock: a refused stop abandons before the swap",
          r.result == UpdateResult::AbandonedBeforeSwap,
          std::string(result_name(r.result)) + " / " + r.detail + " / " + r.lastError);
    check("mock: ...without reaching Swap", !r.reachedSwap);
    assert_install_preserved("mock refused", dir);
    clean_attempt_dirs(dir);
  }

  // ================================================================= PART 2 (real service, or not)
  const std::wstring nameStops =
      L"GNLinkUpdFixtureSvc" + std::to_wstring(GetCurrentProcessId()) + L"A";
  const std::wstring nameLingers =
      L"GNLinkUpdFixtureSvc" + std::to_wstring(GetCurrentProcessId()) + L"B";
  const std::wstring nameDenied =
      L"GNLinkUpdFixtureSvc" + std::to_wstring(GetCurrentProcessId()) + L"C";

  if (mockOnly) {
    std::cout << "\n-- part 2: not attempted (--mock-only)\n";
  } else {
    std::cout << "\n-- part 2: the same three questions, against a real isolated service\n";

    check("the fixture services are not the product's",
          nameStops != kSecureInputServiceName && nameLingers != kSecureInputServiceName &&
              nameDenied != kSecureInputServiceName,
          narrow(nameStops) + ", " + narrow(nameLingers) + ", " + narrow(nameDenied));

    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    const DWORD managerErr = GetLastError();
    if (!manager) {
      const std::string why =
          "OpenSCManager(SC_MANAGER_CREATE_SERVICE) failed with " + std::to_string(managerErr) +
          (managerErr == ERROR_ACCESS_DENIED ? " (access denied -- this run is not elevated)" : "");
      blocked("real service: STOP_PENDING is crossed inside one attempt and then the swap happens",
              why);
      blocked("real service: a stop that never completes times out with the install preserved",
              why);
      blocked("real service: a real refusal leaves the install untouched", why);
      std::cout << "\n  Not run, and therefore not claimed. Installing a service needs\n"
                   "  SC_MANAGER_CREATE_SERVICE, which needs elevation; nothing here falls back\n"
                   "  to the product's service, because stopping that would be operating on the\n"
                   "  user's machine rather than on a fixture. Run this binary from an elevated\n"
                   "  shell with --require-real to get the three checks above.\n";
    } else {
      // ---- 1. one attempt, all the way across the window.
      {
        FixtureService svc;
        // Three seconds of STOP_PENDING against an eight second quiesce budget: long enough that
        // a swap decided on the SCM's answer alone would happen while the process was still there.
        const bool made = svc.create(manager, nameStops, 3000);
        check("real service: the fixture service was created", made,
              "err=" + std::to_string(GetLastError()));
        const bool running = made && svc.start_and_wait();
        check("real service: it reached RUNNING", running);
        if (running) {
          const ServiceSnapshot snap = snapshot_service(svc.handle);
          check("real service: ...and has a process", snap.pid != 0,
                "pid=" + std::to_string(snap.pid));

          const StateProbe probe = [&svc]() { return snapshot_service(svc.handle).state; };
          // THE point of the rework: the production function, inside the attempt, as its stop.
          const StopRequest realStop = [&nameStops](const ProcessTarget&) {
            return request_service_stop(nameStops.c_str());
          };

          const std::wstring dir = scratch(L"svc-real-crosses");
          const AttemptResult r = attempt_against(dir, snap.pid, realStop, 1000, 8000, probe);

          check("real service: the target was captured while it was RUNNING",
                r.stateAtEnumerate == SERVICE_RUNNING && r.targets == 1,
                std::string(service_state_name(r.stateAtEnumerate)) +
                    ", targets=" + std::to_string(r.targets));
          check("real service: the production request_service_stop was accepted",
                r.stopCalls == 1 && r.stopAnswer, "calls=" + std::to_string(r.stopCalls));
          check("real service: ...and right afterwards it was STOP_PENDING, not STOPPED",
                r.stateAfterStop == SERVICE_STOP_PENDING, service_state_name(r.stateAfterStop));
          check("real service: STOP_PENDING is crossed inside one attempt and then the swap "
                "happens",
                r.result == UpdateResult::Updated && r.reachedSwap,
                std::string(result_name(r.result)) + " / " + r.detail + " / " + r.lastError);
          check("real service: ...and the wait really did span the pending window",
                r.elapsedMs + 32 >= 3000, std::to_string(r.elapsedMs) + "ms over a 3000ms linger");
          check("real service: ...and the payload was replaced",
                read_text(dir + L"\\AlphaPayload.bin") == kArtifact,
                read_text(dir + L"\\AlphaPayload.bin"));
          check("real service: ...and the service really is stopped now",
                snapshot_service(svc.handle).state == SERVICE_STOPPED,
                service_state_name(snapshot_service(svc.handle).state));
          clean_attempt_dirs(dir);
        }
        svc.destroy();
        check("real service: the fixture service was removed", svc.deleted,
              "err=" + std::to_string(GetLastError()));
      }

      // ---- 2. the same wiring, but the service never finishes leaving.
      {
        FixtureService svc;
        // Twenty seconds of STOP_PENDING against a 1200ms budget. The SCM accepts, the process
        // stays, and the attempt must give up with the install exactly as it was.
        const bool made = svc.create(manager, nameLingers, 20000);
        check("real service (lingering): the fixture service was created", made,
              "err=" + std::to_string(GetLastError()));
        const bool running = made && svc.start_and_wait();
        check("real service (lingering): it reached RUNNING", running);
        if (running) {
          const ServiceSnapshot snap = snapshot_service(svc.handle);
          const StateProbe probe = [&svc]() { return snapshot_service(svc.handle).state; };
          const StopRequest realStop = [&nameLingers](const ProcessTarget&) {
            return request_service_stop(nameLingers.c_str());
          };
          const std::wstring dir = scratch(L"svc-real-lingers");
          const AttemptResult r = attempt_against(dir, snap.pid, realStop, 500, 1200, probe);

          check("real service (lingering): the stop was accepted", r.stopAnswer);
          check("real service (lingering): ...and the service sat in STOP_PENDING",
                r.stateAfterStop == SERVICE_STOP_PENDING, service_state_name(r.stateAfterStop));
          check("real service: a stop that never completes times out with the install preserved",
                r.result == UpdateResult::AbandonedBeforeSwap && !r.reachedSwap,
                std::string(result_name(r.result)) + " / " + r.detail);
          check("real service (lingering): ...with a target, not an empty list", r.targets == 1,
                "targets=" + std::to_string(r.targets));
          assert_install_preserved("real service lingering", dir);
          clean_attempt_dirs(dir);
        }
        svc.destroy();
        check("real service (lingering): the fixture service was removed", svc.deleted);
      }

      // ---- 3. a refusal the SCM really produces.
      {
        FixtureService svc;
        const bool made = svc.create(manager, nameDenied, 1000);
        check("real service (denied): the fixture service was created", made,
              "err=" + std::to_string(GetLastError()));
        const bool running = made && svc.start_and_wait();
        check("real service (denied): it reached RUNNING", running);
        const bool denied = running && svc.deny_stop();
        if (!denied) {
          blocked("real service: a real refusal leaves the install untouched",
                  "the service DACL could not be narrowed (err " + std::to_string(GetLastError()) +
                      "), so no genuine refusal was available");
        } else {
          const ServiceSnapshot snap = snapshot_service(svc.handle);
          const StopRequest realStop = [&nameDenied](const ProcessTarget&) {
            return request_service_stop(nameDenied.c_str());
          };
          const std::wstring dir = scratch(L"svc-real-denied");
          const AttemptResult r = attempt_against(dir, snap.pid, realStop, 500, 1200);

          check("real service (denied): the production stop really was refused",
                r.stopCalls == 1 && !r.stopAnswer, "calls=" + std::to_string(r.stopCalls));
          check("real service: a real refusal leaves the install untouched",
                r.result == UpdateResult::AbandonedBeforeSwap && !r.reachedSwap,
                std::string(result_name(r.result)) + " / " + r.detail);
          check("real service (denied): ...and the service is still running, untouched",
                snapshot_service(svc.handle).state == SERVICE_RUNNING,
                service_state_name(snapshot_service(svc.handle).state));
          assert_install_preserved("real service denied", dir);
          clean_attempt_dirs(dir);
        }
        svc.destroy();
        check("real service (denied): the fixture service was removed", svc.deleted,
              "err=" + std::to_string(GetLastError()));
      }

      CloseServiceHandle(manager);
    }
  }

  // ------------------------------------------------------------------------------- the accounting
  std::cout << "\nattempts run: " << gAttempts << "\n";
  std::cout << "scratch directories left behind: " << gCleanupFailures;
  for (const std::string& left : gCleanupLeft) std::cout << "\n  " << left;
  std::cout << "\n";
  if (gCleanupFailures != 0) {
    // Reported as a failure, not a note: a directory that will not go means something this test
    // started is still holding it.
    check("every scratch directory was cleaned up", false,
          std::to_string(gCleanupFailures) + " left");
  }

  const bool blockedFatal = requireReal && gBlocked > 0;
  if (blockedFatal) {
    std::cout << "\n--require-real was given and " << gBlocked
              << " check(s) were blocked. That is a failure of this run, not a pass.\n";
  }
  std::cout << "\n" << ((gFailures == 0 && !blockedFatal) ? "RESULT: ALL PASS" : "RESULT: FAILED")
            << "  (" << gChecks << " checks, " << gFailures << " failed, " << gBlocked
            << " blocked, " << gAttempts << " attempts)\n";
  return (gFailures == 0 && !blockedFatal) ? 0 : 1;
}
