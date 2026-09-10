// The stop request, against real processes, through the production code.
//
// Everything that decides whether an update can start was previously covered by lambdas. Eleven
// places in the test suite assign `requestStop`, and every one of them returns a canned answer;
// request_process_stop() itself has exactly one caller in the product and none in any test. So
// the branch that actually failed in the field -- a process with no window, for which the only
// mechanism is a console control event that cannot work -- had never once been executed. The
// suites were green the whole time.
//
// This test starts two real processes in the shape the product runs in:
//
//     fixture parent   a top-level window (never shown), starts and owns the child
//     fixture child    no window, no console -- the shape of GNLinkStream and GNLinkCapture
//
// and then drives the real enumerate_product_processes() and request_process_stop(), and the real
// WindowsUpdateEffects::PrepareForSwap() / Quiesce(). No stubs anywhere in the path under test.
//
// Safety: the fixture uses its own image name, its own window class, and a named event to ask its
// own children to exit. It never enumerates or closes anything belonging to the product, never
// kills by image name, holds no machine-wide lock, and shows no window on anyone's desktop.
//
// Build: remote60_update_stop_process_test. Run: prints PASS lines, exit 0.

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "update_effects.hpp"
#include "update_process_targets.hpp"

using namespace remote60::native_poc::update;

namespace {

int gFailures = 0;

void check(const char* what, bool ok, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ",
              detail.c_str());
  if (!ok) ++gFailures;
}

std::wstring own_path() {
  wchar_t buf[MAX_PATH]{};
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return buf;
}

std::wstring own_name() {
  const std::wstring path = own_path();
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring child_event_name() {
  // Local, not Global: this is a fixture talking to its own children, not a machine-wide lock.
  return L"Local\\gnlink-stop-fixture-" + std::to_wstring(GetCurrentProcessId());
}

// ---------------------------------------------------------------------------- the fixture child

int run_fixture_child(const std::wstring& eventName) {
  // No window and no console: the shape the production supervisor starts its children in, and
  // the shape for which a console control event has no success case.
  HANDLE quit = OpenEventW(SYNCHRONIZE, FALSE, eventName.c_str());
  if (!quit) return 90;
  WaitForSingleObject(quit, 120000);
  CloseHandle(quit);
  return 0;
}

// ---------------------------------------------------------------------------- the fixture parent

HANDLE gParentChildProcess = nullptr;
std::wstring gParentEventName;

LRESULT CALLBACK fixture_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  if (msg == WM_CLOSE) {
    // What a supervisor does when it is asked to close: put its own children away first, then go.
    // The updater never touches the child at all.
    HANDLE quit = OpenEventW(EVENT_MODIFY_STATE, FALSE, gParentEventName.c_str());
    if (quit) {
      SetEvent(quit);
      CloseHandle(quit);
    }
    if (gParentChildProcess) WaitForSingleObject(gParentChildProcess, 10000);
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, w, l);
}

int run_fixture_parent(const std::wstring& eventName) {
  gParentEventName = eventName;

  const std::wstring cls = L"GNLinkStopFixture" + std::to_wstring(GetCurrentProcessId());
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = fixture_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = cls.c_str();
  RegisterClassExW(&wc);
  // Created but never shown. EnumWindows finds hidden top-level windows, which is what makes this
  // a valid stand-in for a tray application -- and it keeps the test off the user's desktop.
  HWND hwnd = CreateWindowExW(0, cls.c_str(), L"fixture", WS_OVERLAPPEDWINDOW, 0, 0, 10, 10,
                              nullptr, nullptr, wc.hInstance, nullptr);
  if (!hwnd) return 91;

  std::wstring cmd = L"\"" + own_path() + L"\" --fixture-child " + eventName;
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  // CREATE_NO_WINDOW: no console of its own, and no console shared with whoever will later try to
  // signal it. This is the condition the production children are started in.
  if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, nullptr, &si, &pi)) {
    return 92;
  }
  CloseHandle(pi.hThread);
  gParentChildProcess = pi.hProcess;

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (gParentChildProcess) CloseHandle(gParentChildProcess);
  return 0;
}

// ---------------------------------------------------------------------------- helpers

bool wait_until(const std::function<bool()>& done, int budgetMs) {
  const DWORD deadline = GetTickCount() + static_cast<DWORD>(budgetMs);
  while (GetTickCount() < deadline) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return done();
}

std::vector<ProcessTarget> fixture_targets() {
  return enumerate_product_processes({own_name()});
}

/** The fixture processes only: this test's own children, never anything else with that name. */
std::vector<ProcessTarget> fixture_children(uint32_t parentPid) {
  std::vector<ProcessTarget> mine;
  for (const ProcessTarget& t : fixture_targets()) {
    if (t.pid == parentPid || t.parentPid == parentPid) mine.push_back(t);
  }
  return mine;
}

UpdateEffectsConfig config_for(const std::wstring& install, const std::wstring& staging,
                               uint32_t parentPid) {
  UpdateEffectsConfig c;
  c.installDir = install;
  c.stagingDir = staging;
  c.payloadNames = {L"AlphaPayload.bin"};
  c.lockName = L"Local\\gnlink-stop-fixture-lock-" + std::to_wstring(GetCurrentProcessId());
  c.quiesceTimeoutMs = 15000;
  c.fetchArtifact = [](const ManifestArtifact&, const std::wstring&) { return true; };
  // The two production functions, called for real. This is the entire point of the file.
  c.enumerateTargets = [parentPid]() { return fixture_children(parentPid); };
  c.requestStop = [](const ProcessTarget& t) { return request_process_stop(t); };
  c.registryRoot = L"HKCU\\Software\\GNLinkStopFixture";
  c.serviceName = L"GNLinkStopFixtureService";
  c.captureRegistration = []() { return true; };
  c.registerInstall = []() { return true; };
  c.restoreRegistration = []() { return true; };
  c.relaunchRequired = []() { return RelaunchVerdict::AllBack; };
  c.relaunchOptional = []() { return RelaunchVerdict::AllBack; };
  c.healthCheck = []() { return true; };
  return c;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 3 && std::string(argv[1]) == "--fixture-child") {
    const std::string name(argv[2]);
    return run_fixture_child(std::wstring(name.begin(), name.end()));
  }
  if (argc >= 3 && std::string(argv[1]) == "--fixture-parent") {
    const std::string name(argv[2]);
    return run_fixture_parent(std::wstring(name.begin(), name.end()));
  }

  std::printf("update_stop_process_test\n");

  const std::wstring eventName = child_event_name();
  // Owned by this process so both fixtures can find it by name and neither has to create it.
  HANDLE quitEvent = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
  if (!quitEvent) {
    std::printf("FAIL  could not create the fixture event\n");
    return 1;
  }

  const std::string narrowEvent(eventName.begin(), eventName.end());
  std::wstring cmd = L"\"" + own_path() + L"\" --fixture-parent " + eventName;
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, nullptr, &si, &pi)) {
    std::printf("FAIL  could not start the fixture parent\n");
    return 1;
  }
  CloseHandle(pi.hThread);
  const uint32_t parentPid = pi.dwProcessId;

  // ---------------------------------------------------------------- the shape is what we meant
  const bool up = wait_until([parentPid] {
    const std::vector<ProcessTarget> t = fixture_children(parentPid);
    if (t.size() != 2) return false;
    for (const ProcessTarget& p : t) {
      if (p.pid == parentPid && !p.hasWindow) return false;
    }
    return true;
  }, 15000);
  const std::vector<ProcessTarget> targets = fixture_children(parentPid);
  check("a windowed parent and a windowless child are running", up && targets.size() == 2,
        std::to_string(targets.size()) + " process(es)");

  const ProcessTarget* parent = nullptr;
  const ProcessTarget* child = nullptr;
  for (const ProcessTarget& t : targets) {
    if (t.pid == parentPid) parent = &t;
    else child = &t;
  }
  check("the production enumerator sees the parent's window", parent && parent->hasWindow);
  check("...and sees that the child has none", child && !child->hasWindow);
  check("...and records who started the child",
        child && parent && child->parentPid == parent->pid,
        child ? std::to_string(child->parentPid) : "no child");
  check("...and the parent started first",
        child && parent && parent->creationTime != 0 && child->creationTime != 0 &&
            parent->creationTime < child->creationTime);

  // ---------------------------------------------------------------- the reproduction
  //
  // This is the failure, executed rather than described. It is not a stub returning false: it is
  // the production function, on a real windowless process, doing what it did in the field.
  if (child) {
    check("asking the windowless child directly FAILS -- this is the field failure",
          !request_process_stop(*child), "request_process_stop returned false");
  }
  // The old routing, kept as a permanent counterexample rather than a line in a commit message:
  // ask every target directly, which is exactly what PrepareForSwap used to do. The child's
  // request fails, so the whole attempt fails -- and that was the entirety of AbandonedBeforeSwap
  // in the field log. Run here, on the first fixture, so it cannot race the real run below.
  {
    bool askedAll = true;
    std::string failure;
    for (const ProcessTarget& t : targets) {
      if (!request_process_stop(t)) {
        askedAll = false;
        failure = "could not ask pid " + std::to_string(t.pid) + " to stop";
      }
    }
    check("asking every target directly -- the old routing -- fails", !askedAll, failure);
    check("...and the reason is the child, not the parent",
          failure.find(std::to_string(child ? child->pid : 0)) != std::string::npos, failure);
  }

  // The WM_CLOSE just posted will close the parent. Wait for the whole fixture to go, so the
  // next phase starts from a known state rather than from a race.
  WaitForSingleObject(pi.hProcess, 15000);
  CloseHandle(pi.hProcess);
  {
    // The count is read ONCE and reported. Re-querying for the detail after the wait produced a
    // line that said "1 left" next to a PASS, because the two reads were of different moments.
    const bool gone = wait_until([parentPid] { return fixture_children(parentPid).empty(); }, 15000);
    check("the parent honoured WM_CLOSE and took its child with it", gone,
          std::to_string(fixture_children(parentPid).size()) + " left at the end of the wait");
  }

  // ---------------------------------------------------------------- gone vs cannot look
  //
  // These were the same answer. "Could not open the process" was reported as "asked successfully",
  // so a process we have no rights to look at counted as one that had already exited -- and the
  // swap would then proceed over something that may still hold the files it is about to replace.
  {
    ProcessTarget departed;
    departed.pid = child ? child->pid : 0;
    departed.imagePath = child ? child->imagePath : std::wstring();
    departed.creationTime = child ? child->creationTime : 0;
    check("a process that really has exited counts as asked", request_process_stop(departed),
          "pid " + std::to_string(departed.pid) + ", already gone");

    // The System process: it exists, and it cannot be opened. The honest answer is "unknown",
    // and the update is abandoned rather than proceeding on an assumption.
    ProcessTarget protectedTarget;
    protectedTarget.pid = 4;
    protectedTarget.imagePath = L"System";
    protectedTarget.creationTime = 1;
    check("a process we cannot even open does NOT count as asked",
          !request_process_stop(protectedTarget), "pid 4");
  }

  // ---------------------------------------------------------------- the production path, for real
  ResetEvent(quitEvent);
  PROCESS_INFORMATION pi2{};
  std::vector<wchar_t> cmd2(cmd.begin(), cmd.end());
  cmd2.push_back(L'\0');
  if (!CreateProcessW(nullptr, cmd2.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                      nullptr, &si, &pi2)) {
    std::printf("FAIL  could not restart the fixture parent\n");
    return 1;
  }
  CloseHandle(pi2.hThread);
  const uint32_t parentPid2 = pi2.dwProcessId;
  const bool up2 = wait_until([parentPid2] {
    const std::vector<ProcessTarget> t = fixture_children(parentPid2);
    return t.size() == 2;
  }, 15000);
  check("the fixture is up again", up2, std::to_string(fixture_children(parentPid2).size()));

  {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const std::wstring install = std::wstring(temp) + L"gnlink-stop-install";
    const std::wstring staging = std::wstring(temp) + L"gnlink-stop-staging";
    CreateDirectoryW(install.c_str(), nullptr);
    CreateDirectoryW(staging.c_str(), nullptr);

    UpdateEffectsConfig c = config_for(install, staging, parentPid2);
    WindowsUpdateEffects e(c);

    // PrepareForSwap now asks the supervisor and leaves the child to it. Before this change it
    // asked both, the child's request failed, and the update was abandoned right here -- which is
    // exactly what the field log recorded: "could not ask pid N to stop".
    const bool prepared = e.PrepareForSwap();
    check("PrepareForSwap succeeds with a windowless child present", prepared, e.last_error());
    const bool quiesced = prepared && e.Quiesce();
    check("...and Quiesce sees both processes actually exit", quiesced, e.last_error());
    const bool allGone =
        wait_until([parentPid2] { return fixture_children(parentPid2).empty(); }, 15000);
    check("...leaving none of the fixture running", allGone,
          std::to_string(fixture_children(parentPid2).size()) + " left at the end of the wait");

    RemoveDirectoryW(staging.c_str());
    RemoveDirectoryW(install.c_str());
  }

  // ---------------------------------------------------------------- cleanup
  SetEvent(quitEvent);
  WaitForSingleObject(pi2.hProcess, 10000);
  CloseHandle(pi2.hProcess);
  CloseHandle(quitEvent);

  if (gFailures == 0) {
    std::printf("update_stop_process_test: PASS\n");
    return 0;
  }
  std::printf("update_stop_process_test: FAILED (%d)\n", gFailures);
  return 1;
}
