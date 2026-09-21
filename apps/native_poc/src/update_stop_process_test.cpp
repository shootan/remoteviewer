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

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <fstream>
#include <sstream>
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

/**
 * A process this test owns and has deliberately made unopenable. (item 9)
 *
 * An empty DACL -- present, and containing no ACE at all -- denies everyone everything, which is
 * what makes OpenProcess fail with ERROR_ACCESS_DENIED. That is the only condition the enumerator's
 * "running but unidentifiable" branch exists for, and it is otherwise produced only by processes
 * this test has no business touching: a system service, or something running as another user.
 * Borrowing one of those would make the result depend on how this machine happens to be configured
 * and on whether the test is elevated. This depends on neither.
 *
 * The parent's handle from CreateProcess is unaffected. Access is checked when a handle is opened,
 * not when it is used, so the fixture can still be waited on and cleaned up.
 */
int run_fixture_denied(const std::wstring& eventName) {
  ACL empty{};
  SECURITY_DESCRIPTOR sd{};
  if (InitializeAcl(&empty, sizeof(empty), ACL_REVISION) &&
      InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) &&
      SetSecurityDescriptorDacl(&sd, TRUE, &empty, FALSE)) {
    // Failure is reported by the parent failing to see what it expects, not by a code here: this
    // process cannot usefully complain to anyone.
    SetKernelObjectSecurity(GetCurrentProcess(), DACL_SECURITY_INFORMATION, &sd);
  }
  HANDLE quit = OpenEventW(SYNCHRONIZE, FALSE, eventName.c_str());
  if (!quit) return 90;
  WaitForSingleObject(quit, 120000);
  CloseHandle(quit);
  return 0;
}

// ------------------------------------------------------------------- the fixture middle tier

/**
 * Windowless, and a supervisor in its own right -- the shape of GNLinkStream.
 *
 * It starts a child and waits on the same quit event, so one SetEvent releases the whole branch.
 * Nothing here has a window, which is the entire point: this tier is why a single-link ownership
 * check could not reach the one below it.
 */
int run_fixture_middle(const std::wstring& eventName, bool leafOutlives = false) {
  // leafOutlives models a helper that is still running when its branch is asked to go: the leaf
  // waits on an event of its own, which the root's WM_CLOSE does not touch, and the middle leaves
  // without it.
  const std::wstring leafEvent = leafOutlives ? eventName + L"-leaf" : eventName;
  std::wstring cmd = L"\"" + own_path() + L"\" --fixture-child " + leafEvent;
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, nullptr, &si, &pi)) {
    return 93;
  }
  CloseHandle(pi.hThread);

  HANDLE quit = OpenEventW(SYNCHRONIZE, FALSE, eventName.c_str());
  if (quit) {
    WaitForSingleObject(quit, 120000);
    CloseHandle(quit);
  }
  // A supervisor waits for what it started before going. The leaf is on the same event, so this
  // is a short wait rather than a second request -- unless this is the case where it is not.
  if (!leafOutlives) WaitForSingleObject(pi.hProcess, 10000);
  CloseHandle(pi.hProcess);
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

/**
 * A process that is standing down: its window is gone, and it has not exited yet.
 *
 * This is the state the field log records and nothing tested. The host acknowledges the handoff,
 * destroys its window, and takes a few tens of milliseconds to finish leaving. In that window it
 * still owns its pid, so it is neither askable nor gone.
 *
 * Creates a window, waits to be told, destroys it, and then stays alive until released.
 *
 * Started DETACHED_PROCESS, which is what makes this a stand-in for a production child. The
 * first version used CREATE_NO_WINDOW and inherited this test's console, so the console-control
 * fallback in request_process_stop SUCCEEDED and the reproduction measured nothing. The
 * product's children are started by a GUI supervisor that has no console to inherit.
 */
/**
 * Asks request_process_stop about one pid, from a process with no console of its own.
 *
 * The caller's console is what decides this, not the target's. request_process_stop falls back to
 * GenerateConsoleCtrlEvent for a windowless process, and that call succeeds when the caller has a
 * console the target belongs to -- which this test, a console program, does. The updater is not a
 * console program, so in the field the fallback has nothing to work with and the ask fails.
 *
 * Run DETACHED_PROCESS, this has no console and answers the production question. Reports through
 * the exit code because it has nowhere to print.
 */
int run_fixture_askprobe(uint32_t pid) {
  ProcessTarget target;
  if (!capture_process_identity(pid, &target)) return 90;
  target.hasWindow = true;  // the stale view PrepareForSwap carries from its own enumeration
  const bool asked = request_process_stop(target);
  SetLastError(0);
  const bool console = GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid) != FALSE;
  // 2 bits: asked, console fallback.
  return (asked ? 1 : 0) + (console ? 2 : 0);
}

int run_fixture_closing(const std::wstring& eventName) {
  const std::wstring cls = L"GNLinkClosingFixture" + std::to_wstring(GetCurrentProcessId());
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = cls.c_str();
  RegisterClassExW(&wc);
  HWND hwnd = CreateWindowExW(0, cls.c_str(), L"closing", WS_OVERLAPPEDWINDOW, 0, 0, 10, 10,
                              nullptr, nullptr, wc.hInstance, nullptr);
  if (!hwnd) return 91;

  // "the window is up" -- the test waits for this before it looks.
  HANDLE up = CreateEventW(nullptr, TRUE, FALSE, (eventName + L"-up").c_str());
  if (up) SetEvent(up);

  // Held until the test says to start standing down.
  HANDLE close = OpenEventW(SYNCHRONIZE, FALSE, (eventName + L"-close").c_str());
  if (close) {
    WaitForSingleObject(close, 30000);
    CloseHandle(close);
  }
  DestroyWindow(hwnd);
  UnregisterClassW(cls.c_str(), wc.hInstance);
  // Pump briefly so the destroy is actually processed before we report it gone.
  MSG msg{};
  const DWORD until = GetTickCount() + 300;
  while (GetTickCount() < until) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    Sleep(10);
  }
  HANDLE gone = CreateEventW(nullptr, TRUE, FALSE, (eventName + L"-gone").c_str());
  if (gone) SetEvent(gone);

  // Still alive, still holding its pid. This is the whole point.
  HANDLE quit = OpenEventW(SYNCHRONIZE, FALSE, eventName.c_str());
  if (quit) {
    WaitForSingleObject(quit, 120000);
    CloseHandle(quit);
  }
  if (up) CloseHandle(up);
  if (gone) CloseHandle(gone);
  return 0;
}

int run_fixture_parent(const std::wstring& eventName, bool threeTier = false,
                       bool leafOutlives = false) {
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

  const wchar_t* childMode = !threeTier          ? L"--fixture-child "
                             : leafOutlives     ? L"--fixture-middle-hold "
                                                : L"--fixture-middle ";
  std::wstring cmd = L"\"" + own_path() + L"\" " + childMode + eventName;
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

/** Small file helpers: the swap gate is about bytes on disk, so the test has to read them. */
void write_text(const std::wstring& path, const std::string& text) {
  FILE* f = nullptr;
  if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return;
  std::fwrite(text.data(), 1, text.size(), f);
  std::fclose(f);
}

std::string read_text(const std::wstring& path) {
  FILE* f = nullptr;
  if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return {};
  std::string out;
  char buf[512];
  size_t n = 0;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
  std::fclose(f);
  return out;
}


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

/**
 * The fixture's whole tree, not just its first generation.
 *
 * fixture_children only reaches direct children, which is enough for the two-tier cases above and
 * misses precisely the tier this section exists for.
 */
std::vector<ProcessTarget> fixture_tree(uint32_t rootPid) {
  const std::vector<ProcessTarget> all = fixture_targets();
  std::vector<uint32_t> inTree{rootPid};
  std::vector<ProcessTarget> mine;
  // Two passes is enough for three tiers and does not assume enumeration order.
  for (int pass = 0; pass < 3; ++pass) {
    for (const ProcessTarget& t : all) {
      const bool already = std::find_if(mine.begin(), mine.end(), [&](const ProcessTarget& m) {
                             return m.pid == t.pid;
                           }) != mine.end();
      if (already) continue;
      const bool linked = std::find(inTree.begin(), inTree.end(), t.pid) != inTree.end() ||
                          std::find(inTree.begin(), inTree.end(), t.parentPid) != inTree.end();
      if (!linked) continue;
      inTree.push_back(t.pid);
      mine.push_back(t);
    }
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
  if (argc >= 3 && std::string(argv[1]) == "--fixture-askprobe") {
    return run_fixture_askprobe(static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10)));
  }
  if (argc >= 3 && std::string(argv[1]) == "--fixture-closing") {
    const std::string name(argv[2]);
    return run_fixture_closing(std::wstring(name.begin(), name.end()));
  }
  if (argc >= 3 && std::string(argv[1]) == "--fixture-denied") {
    const std::string name(argv[2]);
    return run_fixture_denied(std::wstring(name.begin(), name.end()));
  }
  if (argc >= 3 && std::string(argv[1]) == "--fixture-middle") {
    const std::string name(argv[2]);
    return run_fixture_middle(std::wstring(name.begin(), name.end()));
  }
  if (argc >= 3 && std::string(argv[1]) == "--fixture-middle-hold") {
    const std::string name(argv[2]);
    return run_fixture_middle(std::wstring(name.begin(), name.end()), true);
  }
  if (argc >= 3 && std::string(argv[1]) == "--fixture-parent3hold") {
    const std::string name(argv[2]);
    return run_fixture_parent(std::wstring(name.begin(), name.end()), true, true);
  }
  if (argc >= 3 && std::string(argv[1]) == "--fixture-parent") {
    const std::string name(argv[2]);
    return run_fixture_parent(std::wstring(name.begin(), name.end()));
  }
  if (argc >= 3 && std::string(argv[1]) == "--fixture-parent3") {
    const std::string name(argv[2]);
    return run_fixture_parent(std::wstring(name.begin(), name.end()), true);
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

  // ---------------------------------------------------------------- the orphan
  //
  // The field's FIRST attempt, which the supervisor routing did not cover. The host had already
  // closed -- the log's "relaunch (required) GNLinkHost.exe: started" proves it -- leaving a
  // console child behind. With the parent gone it is not among the targets, so ownership cannot be
  // verified, and the child fell through to a direct request that cannot succeed for a process
  // with no window. "could not ask pid 13528 to stop", every time.
  {
    ResetEvent(quitEvent);
    PROCESS_INFORMATION po{};
    std::vector<wchar_t> cmdO(cmd.begin(), cmd.end());
    cmdO.push_back(L'\0');
    if (CreateProcessW(nullptr, cmdO.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                       nullptr, &si, &po)) {
      CloseHandle(po.hThread);
      const uint32_t op = po.dwProcessId;
      const bool upO = wait_until([op] { return fixture_children(op).size() == 2; }, 15000);

      // Make the child an orphan: end the parent WITHOUT letting it tidy up, so the child is
      // still running with a parent that no longer exists. TerminateProcess on a process this test
      // started is the only way to produce that shape; nothing in the product does this.
      TerminateProcess(po.hProcess, 0);
      WaitForSingleObject(po.hProcess, 10000);
      CloseHandle(po.hProcess);

      const bool orphaned = wait_until([op] {
        const std::vector<ProcessTarget> t = fixture_children(op);
        return t.size() == 1 && !t[0].hasWindow;
      }, 15000);
      const std::vector<ProcessTarget> left = fixture_children(op);
      check("a windowless child outlives its parent", upO && orphaned && left.size() == 1,
            std::to_string(left.size()) + " left");

      if (left.size() == 1) {
        check("...and asking it directly still cannot work", !request_process_stop(left[0]));

        wchar_t temp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, temp);
        const std::wstring inst = std::wstring(temp) + L"gnlink-orphan-install";
        const std::wstring stg = std::wstring(temp) + L"gnlink-orphan-staging";
        CreateDirectoryW(inst.c_str(), nullptr);
        CreateDirectoryW(stg.c_str(), nullptr);
        UpdateEffectsConfig c = config_for(inst, stg, op);
        c.quiesceTimeoutMs = 10000;
        WindowsUpdateEffects e(c);
        // Prepare must no longer refuse: there is nobody to ask, so it waits instead.
        const bool prepared = e.PrepareForSwap();
        check("PrepareForSwap does not refuse an orphan it cannot ask", prepared, e.last_error());
        // Release it, as its own supervisor would have.
        SetEvent(quitEvent);
        const bool quiesced = prepared && e.Quiesce();
        check("...and Quiesce waits for it to go", quiesced, e.last_error());
        RemoveDirectoryW(stg.c_str());
        RemoveDirectoryW(inst.c_str());
      }
      SetEvent(quitEvent);
      wait_until([op] { return fixture_children(op).empty(); }, 10000);
    }
  }

  // ---------------------------------------------------------------- the orphan that will not go
  //
  // The other branch, and the one that decides whether waiting is a fix or only a better message.
  // An orphan that is on its way out gets picked up by the wait above. An orphan that simply keeps
  // running cannot be helped by waiting: the deadline expires and the update is abandoned, exactly
  // as it is today. What must be different is the REASON -- "nobody was left to ask" is a different
  // problem from "it refused", and they used to print the same sentence.
  {
    ResetEvent(quitEvent);
    PROCESS_INFORMATION pk{};
    std::vector<wchar_t> cmdK(cmd.begin(), cmd.end());
    cmdK.push_back(L'\0');
    if (CreateProcessW(nullptr, cmdK.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                       nullptr, &si, &pk)) {
      CloseHandle(pk.hThread);
      const uint32_t kp = pk.dwProcessId;
      wait_until([kp] { return fixture_children(kp).size() == 2; }, 15000);
      TerminateProcess(pk.hProcess, 0);
      WaitForSingleObject(pk.hProcess, 10000);
      CloseHandle(pk.hProcess);
      const bool orphaned = wait_until([kp] {
        const std::vector<ProcessTarget> t = fixture_children(kp);
        return t.size() == 1 && !t[0].hasWindow;
      }, 15000);
      check("an orphan that keeps running is still there", orphaned,
            std::to_string(fixture_children(kp).size()) + " left");

      wchar_t temp[MAX_PATH]{};
      GetTempPathW(MAX_PATH, temp);
      const std::wstring inst = std::wstring(temp) + L"gnlink-orphan2-install";
      const std::wstring stg = std::wstring(temp) + L"gnlink-orphan2-staging";
      CreateDirectoryW(inst.c_str(), nullptr);
      CreateDirectoryW(stg.c_str(), nullptr);
      UpdateEffectsConfig c = config_for(inst, stg, kp);
      c.quiesceTimeoutMs = 2000;  // short: the point is what the expiry says
      WindowsUpdateEffects e(c);
      const bool prepared = e.PrepareForSwap();
      check("...PrepareForSwap still does not refuse it", prepared, e.last_error());
      const bool quiesced = prepared && e.Quiesce();
      check("...but Quiesce gives up when it never leaves", !quiesced, e.last_error());
      check("...and the reason says nobody was left to ask",
            e.last_error().find("orphaned windowless") != std::string::npos &&
                e.last_error().find("nobody to ask") != std::string::npos,
            e.last_error());
      check("...and it is NOT reported as a refusal to be asked",
            e.last_error().find("could not ask") == std::string::npos, e.last_error());
      RemoveDirectoryW(stg.c_str());
      RemoveDirectoryW(inst.c_str());

      SetEvent(quitEvent);
      wait_until([kp] { return fixture_children(kp).empty(); }, 10000);
    }
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

  // ---------------------------------------------------------------- the host's own exit contract
  //
  // ⚠ STATIC WIRING CHECK, not a behavioural one. It reads the source and confirms the handoff
  // exit path stops the child and writes down whether it went. It does NOT prove the Host does
  // that when it really stands down -- only running the real Host can show that, and that belongs
  // to the field re-test. Labelled so nobody reads it as more than it is.
  //
  // Worth having anyway: the defect was that the exit path stopped nothing verifiably and said
  // nothing, so a child that outlived its parent left no trace until the NEXT host failed to bind.
#ifdef REMOTE60_HOST_APP_SRC
  {
    std::ifstream in(REMOTE60_HOST_APP_SRC, std::ios::binary);
    std::ostringstream os;
    os << in.rdbuf();
    const std::string src = os.str();
    const size_t exitNow = src.find("The updater holds the lock and has a verified download");
    const size_t stopCall = exitNow == std::string::npos
                                ? std::string::npos
                                : src.find("g.streaming.Stop(&detail)", exitNow);
    const size_t destroy = exitNow == std::string::npos
                               ? std::string::npos
                               : src.find("DestroyWindow(window)", exitNow);
    check("(static) the handoff exit stops the child", stopCall != std::string::npos);
    check("(static) ...before destroying the window",
          stopCall != std::string::npos && destroy != std::string::npos && stopCall < destroy);
    check("(static) ...and records whether it went",
          src.find("update: standing down -- ") != std::string::npos);
    check("(static) ...and warns when a child outlived it",
          src.find("a streaming child outlived this process") != std::string::npos);
  }
#else
  check("(static) the host exit contract is checked", false, "REMOTE60_HOST_APP_SRC not defined");
#endif

  // ------------------------------------------------- three real tiers (B1)
  //
  // The shape the product actually has, produced rather than described: a windowed root starts a
  // windowless middle, which starts a windowless leaf. Before B1 the leaf could not be recognised
  // as anybody's child -- its parent has no window -- so it fell through to a direct request that
  // cannot succeed, and the update was abandoned. The synthetic matrix in update_effects_test
  // covers the branches; this is the one that checks the OS reports parentage the way that walk
  // assumes.
  {
    ResetEvent(quitEvent);
    const std::wstring cmd3 = L"\"" + own_path() + L"\" --fixture-parent3 " + eventName;
    std::vector<wchar_t> c3(cmd3.begin(), cmd3.end());
    c3.push_back(L'\0');
    STARTUPINFOW si3{};
    si3.cb = sizeof(si3);
    PROCESS_INFORMATION p3{};
    if (!CreateProcessW(nullptr, c3.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &si3, &p3)) {
      check("three-tier fixture started", false);
    } else {
      CloseHandle(p3.hThread);
      const uint32_t root = p3.dwProcessId;
      const bool up3 = wait_until([root] { return fixture_tree(root).size() == 3; }, 20000);
      std::vector<ProcessTarget> tree = fixture_tree(root);
      check("a windowed root, a windowless middle and a windowless leaf are running",
            up3 && tree.size() == 3, std::to_string(tree.size()) + " process(es)");

      const ProcessTarget* rootT = nullptr;
      const ProcessTarget* midT = nullptr;
      const ProcessTarget* leafT = nullptr;
      for (const ProcessTarget& t : tree) {
        if (t.pid == root) rootT = &t;
        else if (t.parentPid == root) midT = &t;
        else leafT = &t;
      }
      check("the enumerator reports the root's window", rootT && rootT->hasWindow);
      check("...the middle with none", midT && !midT->hasWindow);
      check("...the leaf with none either", leafT && !leafT->hasWindow);
      check("...and the leaf's parent is the middle, not the root",
            leafT && midT && leafT->parentPid == midT->pid,
            leafT ? std::to_string(leafT->parentPid) : "no leaf");
      // Each link older than the one it started. The walk refuses an edge without this, so a
      // fixture that did not satisfy it would be testing the rejection path by accident.
      check("...and each tier started before the one below it",
            rootT && midT && leafT && rootT->creationTime != 0 && midT->creationTime != 0 &&
                leafT->creationTime != 0 && rootT->creationTime <= midT->creationTime &&
                midT->creationTime <= leafT->creationTime);

      // The trap this section is built to avoid, stated as a check so it stays avoided.
      //
      // fixture_children filters on `t.parentPid == parentPid`, one level. The leaf's parent is
      // the middle, so a three-tier test enumerating that way would hand PrepareForSwap a set
      // that does not contain the process the test is about -- and would pass without ever
      // exercising the chain. The enumeration has to follow parentage, which is what
      // fixture_tree does.
      {
        const std::vector<ProcessTarget> oneLevel = fixture_children(root);
        const bool leafInOneLevel =
            leafT && std::find_if(oneLevel.begin(), oneLevel.end(),
                                  [&](const ProcessTarget& t) { return t.pid == leafT->pid; }) !=
                         oneLevel.end();
        check("one-level enumeration MISSES the leaf -- which is why this uses the chain",
              !leafInOneLevel, std::to_string(oneLevel.size()) + " of 3 found one level down");
        const bool leafInTree =
            leafT && std::find_if(tree.begin(), tree.end(), [&](const ProcessTarget& t) {
                       return t.pid == leafT->pid;
                     }) != tree.end();
        check("...and following parentage finds it", leafInTree);
      }

      // The failure, executed. The leaf has no window and its parent has none either.
      if (leafT) {
        check("asking the leaf directly cannot work", !request_process_stop(*leafT));
      }

      wchar_t temp3[MAX_PATH]{};
      GetTempPathW(MAX_PATH, temp3);
      const std::wstring inst3 = std::wstring(temp3) + L"gnlink-3tier-install";
      const std::wstring stg3 = std::wstring(temp3) + L"gnlink-3tier-staging";
      CreateDirectoryW(inst3.c_str(), nullptr);
      CreateDirectoryW(stg3.c_str(), nullptr);

      auto asked = std::make_shared<std::vector<uint32_t>>();
      UpdateEffectsConfig c = config_for(inst3, stg3, root);
      c.enumerateTargets = [root]() { return fixture_tree(root); };
      c.requestStop = [asked](const ProcessTarget& t) {
        asked->push_back(t.pid);
        return request_process_stop(t);
      };
      c.quiesceTimeoutMs = 20000;
      WindowsUpdateEffects e(c);

      const bool prepared = e.PrepareForSwap();
      check("PrepareForSwap succeeds on a real three-tier tree", prepared, e.last_error());
      check("...having asked exactly one process", asked->size() == 1,
            std::to_string(asked->size()) + " asked");
      check("...and that one is the root that owns a window",
            asked->size() == 1 && asked->front() == root);

      // The root's WM_CLOSE releases the branch, the middle waits for its leaf, and Quiesce is
      // what confirms every one of the three actually went. Nothing is force-terminated.
      const bool quiesced = prepared && e.Quiesce();
      check("Quiesce waits for all three and sees them gone", quiesced, e.last_error());
      const bool empty = wait_until([root] { return fixture_tree(root).empty(); }, 20000);
      check("...and the tree really is empty", empty,
            std::to_string(fixture_tree(root).size()) + " left");

      WaitForSingleObject(p3.hProcess, 20000);
      CloseHandle(p3.hProcess);
      RemoveDirectoryW(stg3.c_str());
      RemoveDirectoryW(inst3.c_str());
    }
  }

  // ------------------------------------------------- a leaf that outlives its branch
  //
  // Quiesce is the gate the swap sits behind, so what matters is that a leftover child makes it
  // say no. The leaf is held open here while the rest of the branch goes.
  {
    ResetEvent(quitEvent);
    const std::wstring holdName = L"Local\\gnlink-stop-fixture-hold-" +
                                  std::to_wstring(GetCurrentProcessId());
    // Two events: one the root signals when it closes, and one only this test can set. The leaf
    // waits on the second, so closing the branch leaves it behind -- which is the whole case.
    HANDLE branch = CreateEventW(nullptr, TRUE, FALSE, holdName.c_str());
    HANDLE hold = CreateEventW(nullptr, TRUE, FALSE, (holdName + L"-leaf").c_str());
    const std::wstring cmdH = L"\"" + own_path() + L"\" --fixture-parent3hold " + holdName;
    std::vector<wchar_t> cH(cmdH.begin(), cmdH.end());
    cH.push_back(L'\0');
    STARTUPINFOW siH{};
    siH.cb = sizeof(siH);
    PROCESS_INFORMATION pH{};
    if (branch && hold &&
        CreateProcessW(nullptr, cH.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                       nullptr, &siH, &pH)) {
      CloseHandle(pH.hThread);
      const uint32_t root = pH.dwProcessId;
      const bool upH = wait_until([root] { return fixture_tree(root).size() == 3; }, 20000);
      check("hold fixture is up", upH, std::to_string(fixture_tree(root).size()) + " process(es)");

      wchar_t tempH[MAX_PATH]{};
      GetTempPathW(MAX_PATH, tempH);
      const std::wstring instH = std::wstring(tempH) + L"gnlink-hold-install";
      const std::wstring stgH = std::wstring(tempH) + L"gnlink-hold-staging";
      CreateDirectoryW(instH.c_str(), nullptr);
      CreateDirectoryW(stgH.c_str(), nullptr);

      // What the gate protects, written down before the attempt. Quiesce returning false is the
      // mechanism; the files being untouched is the point, and only one of those was checked.
      const std::wstring guarded = instH + L"\\AlphaPayload.bin";
      write_text(guarded, "installed-bytes-that-must-not-change");
      const std::string before = read_text(guarded);

      UpdateEffectsConfig c = config_for(instH, stgH, root);
      c.enumerateTargets = [root]() { return fixture_tree(root); };
      c.requestStop = [](const ProcessTarget& t) { return request_process_stop(t); };
      c.quiesceTimeoutMs = 3000;  // short: the leaf is not going to leave
      WindowsUpdateEffects e(c);
      const bool prepared = e.PrepareForSwap();
      check("prepare still succeeds -- the leaf is owned, so nobody asks it", prepared,
            e.last_error());
      // The root closes, but the leaf is waiting on an event nobody has set.
      const bool refused = prepared && !e.Quiesce();
      check("Quiesce refuses while a child is still running", refused, e.last_error());
      check("...and names it as a child that outlived its parent",
            e.last_error().find("outlived") != std::string::npos ||
                e.last_error().find("did not exit") != std::string::npos,
            e.last_error());
      check("...and the installed file is byte for byte what it was",
            read_text(guarded) == before,
            "the refusal is only worth anything if the disk was not touched");

      SetEvent(hold);    // release the leaf that was left behind
      SetEvent(branch);  // and anything still waiting on the branch event
      WaitForSingleObject(pH.hProcess, 20000);
      CloseHandle(pH.hProcess);
      wait_until([root] { return fixture_tree(root).empty(); }, 20000);
      CloseHandle(hold);
      CloseHandle(branch);
      RemoveDirectoryW(stgH.c_str());
      RemoveDirectoryW(instH.c_str());
    } else {
      check("hold fixture started", false);
      if (hold) CloseHandle(hold);
      if (branch) CloseHandle(branch);
    }
  }

  // ------------------------------------------------- what moves between enumeration and the wait
  //
  // Quiesce waits for the list PrepareForSwap worked from, not a fresh one. That is deliberate --
  // re-enumerating would wait for whatever started in the meantime, and would silently skip
  // something already asked and on its way out. It has two consequences that nothing checked.
  {
    ResetEvent(quitEvent);
    const std::wstring raceEvent = L"Local\\gnlink-stop-fixture-race-" +
                                   std::to_wstring(GetCurrentProcessId());
    HANDLE raceQuit = CreateEventW(nullptr, TRUE, FALSE, raceEvent.c_str());
    const std::wstring cmdR = L"\"" + own_path() + L"\" --fixture-parent3 " + raceEvent;
    std::vector<wchar_t> cR(cmdR.begin(), cmdR.end());
    cR.push_back(L'\0');
    STARTUPINFOW siR{};
    siR.cb = sizeof(siR);
    PROCESS_INFORMATION pR{};
    if (raceQuit && CreateProcessW(nullptr, cR.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                   nullptr, nullptr, &siR, &pR)) {
      CloseHandle(pR.hThread);
      const uint32_t root = pR.dwProcessId;
      const bool upR = wait_until([root] { return fixture_tree(root).size() == 3; }, 20000);
      check("race fixture is up", upR, std::to_string(fixture_tree(root).size()) + " process(es)");

      wchar_t tempR[MAX_PATH]{};
      GetTempPathW(MAX_PATH, tempR);
      const std::wstring instR = std::wstring(tempR) + L"gnlink-race-install";
      const std::wstring stgR = std::wstring(tempR) + L"gnlink-race-staging";
      CreateDirectoryW(instR.c_str(), nullptr);
      CreateDirectoryW(stgR.c_str(), nullptr);

      // The list is taken here, and then the world moves.
      const std::vector<ProcessTarget> snapshot = fixture_tree(root);
      UpdateEffectsConfig c = config_for(instR, stgR, root);
      c.enumerateTargets = [snapshot]() { return snapshot; };
      c.requestStop = [](const ProcessTarget& t) { return request_process_stop(t); };
      c.quiesceTimeoutMs = 20000;
      WindowsUpdateEffects e(c);
      const bool prepared = e.PrepareForSwap();
      check("prepare succeeds on the snapshot", prepared, e.last_error());

      // A child that leaves between the enumeration and the wait is not a failure -- leaving is
      // what the wait was for.
      SetEvent(raceQuit);
      const bool quiesced = prepared && e.Quiesce();
      check("a child that exits between enumeration and the wait is not a failure", quiesced,
            e.last_error());

      wait_until([root] { return fixture_tree(root).empty(); }, 20000);
      WaitForSingleObject(pR.hProcess, 20000);
      CloseHandle(pR.hProcess);

      // And the other direction: something windowless appears AFTER the list was taken. It is not
      // in preparedTargets_, so the wait must not block on it -- a helper started a moment too
      // late would otherwise hold an update open for its whole lifetime.
      ResetEvent(quitEvent);
      PROCESS_INFORMATION pLate{};
      std::wstring cmdL = L"\"" + own_path() + L"\" --fixture-child " + eventName;
      std::vector<wchar_t> cL(cmdL.begin(), cmdL.end());
      cL.push_back(L'\0');
      STARTUPINFOW siL{};
      siL.cb = sizeof(siL);
      if (CreateProcessW(nullptr, cL.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                         nullptr, &siL, &pLate)) {
        CloseHandle(pLate.hThread);
        const uint32_t latePid = pLate.dwProcessId;
        const bool lateUp = wait_until([latePid] {
          for (const ProcessTarget& t : fixture_targets()) {
            if (t.pid == latePid) return true;
          }
          return false;
        }, 15000);
        check("a late windowless process is running", lateUp);

        const DWORD before = GetTickCount();
        const bool stillQuiesces = e.Quiesce();
        const DWORD took = GetTickCount() - before;
        check("a process that appeared after the enumeration is not waited for", stillQuiesces,
              e.last_error());
        check("...and the wait did not stall on it", took < 5000,
              std::to_string(took) + "ms with a 20s budget");

        SetEvent(quitEvent);
        WaitForSingleObject(pLate.hProcess, 15000);
        CloseHandle(pLate.hProcess);
      }

      RemoveDirectoryW(stgR.c_str());
      RemoveDirectoryW(instR.c_str());
      CloseHandle(raceQuit);
    } else {
      check("race fixture started", false);
      if (raceQuit) CloseHandle(raceQuit);
    }
  }

  // ------------------------------------------- a running process this updater cannot identify
  //
  // The enumerator used to drop these where they were found. Nothing downstream could object,
  // because nothing downstream ever saw them: the swap went ahead over a process that was still
  // running and still holding its own files. This drives the real enumerate_product_processes
  // against a process that genuinely refuses to be opened.
  {
    const std::wstring deniedEvent = eventName + L"-denied";
    HANDLE deniedQuit = CreateEventW(nullptr, TRUE, FALSE, deniedEvent.c_str());
    std::wstring cmdD = L"\"" + own_path() + L"\" --fixture-denied " + deniedEvent;
    std::vector<wchar_t> mutableCmdD(cmdD.begin(), cmdD.end());
    mutableCmdD.push_back(L'\0');
    STARTUPINFOW siD{};
    siD.cb = sizeof(siD);
    PROCESS_INFORMATION pDenied{};
    const bool startedD =
        deniedQuit != nullptr &&
        CreateProcessW(nullptr, mutableCmdD.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, nullptr, &siD, &pDenied) != FALSE;
    // An ordinary fixture on the same event, so the sweep below contains both kinds at once.
    // By this point every earlier fixture has been released, and a control that is not actually
    // running at the same moment is no control at all -- it was the absence of one that made the
    // first version of this check fail.
    std::wstring cmdP = L"\"" + own_path() + L"\" --fixture-child " + deniedEvent;
    std::vector<wchar_t> mutableCmdP(cmdP.begin(), cmdP.end());
    mutableCmdP.push_back(L'\0');
    STARTUPINFOW siP{};
    siP.cb = sizeof(siP);
    PROCESS_INFORMATION pPlain{};
    const bool startedP =
        deniedQuit != nullptr &&
        CreateProcessW(nullptr, mutableCmdP.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, nullptr, &siP, &pPlain) != FALSE;
    check("the ordinary control fixture started", startedP);
    if (startedP) CloseHandle(pPlain.hThread);

    check("the unopenable fixture started", startedD);
    if (startedD) {
      const uint32_t deniedPid = pDenied.dwProcessId;
      CloseHandle(pDenied.hThread);

      // It sets its own DACL after it starts, so wait for the denial to actually be in place
      // rather than for the process to merely exist.
      const bool denied = wait_until([deniedPid] {
        SetLastError(0);
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, deniedPid);
        if (h) {
          CloseHandle(h);
          return false;
        }
        return GetLastError() == ERROR_ACCESS_DENIED;
      }, 15000);
      check("the fixture has made itself unopenable", denied,
            "without this the rest of this section proves nothing");

      if (denied) {
        // The classifier, on a process that is demonstrably running.
        ProcessTarget probe;
        IdentityFailure why = IdentityFailure::None;
        check("a denied process does not identify",
              !capture_process_identity(deniedPid, &probe, &why));
        check("...and is Unknowable, not Gone", why == IdentityFailure::Unknowable,
              "why=" + std::to_string(static_cast<int>(why)));

        // Both have to be in one snapshot for the comparison below to mean anything, and the
        // control was started second.
        const bool controlUp = wait_until([&pPlain] {
          for (const ProcessTarget& t : fixture_targets()) {
            if (t.pid == pPlain.dwProcessId) return true;
          }
          return false;
        }, 15000);
        check("the control fixture is enumerable", controlUp);

        // The enumerator. This is the line the defect was on.
        const std::vector<ProcessTarget> all = fixture_targets();
        const ProcessTarget* found = nullptr;
        for (const ProcessTarget& t : all) {
          if (t.pid == deniedPid) found = &t;
        }
        check("the enumerator carries it forward instead of dropping it", found != nullptr,
              std::to_string(all.size()) + " targets enumerated");
        if (found) {
          check("...marked as not identified", !found->identityKnown);
          check("...carrying the image name the snapshot could read", !found->imagePath.empty(),
                std::string(found->imagePath.begin(), found->imagePath.end()));
          check("...and no creation time, because none could be read", found->creationTime == 0);
        }

        // The negative control that matters: everything else in the same enumeration is
        // identified normally, so "not identified" is not simply what this build now says.
        bool sawIdentified = false;
        for (const ProcessTarget& t : all) {
          if (t.pid == pPlain.dwProcessId && t.identityKnown && t.creationTime != 0) {
            sawIdentified = true;
          }
        }
        check("...while the ordinary fixture in the same sweep is identified", sawIdentified,
              std::to_string(all.size()) + " targets enumerated");

        // And the swap refuses, naming it -- the enumerator and the guard, end to end.
        wchar_t tempD[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tempD);
        const std::wstring instD = std::wstring(tempD) + L"gnlink-denied-install";
        const std::wstring stgD = std::wstring(tempD) + L"gnlink-denied-staging";
        CreateDirectoryW(instD.c_str(), nullptr);
        CreateDirectoryW(stgD.c_str(), nullptr);
        // fixture_children(deniedPid) is exactly this one process: it matches on t.pid, and this
        // fixture has no children of its own.
        UpdateEffectsConfig cD = config_for(instD, stgD, deniedPid);
        WindowsUpdateEffects eD(cD);
        check("the swap is abandoned", !eD.PrepareForSwap());
        check("...naming the pid",
              eD.last_error().find(std::to_string(deniedPid)) != std::string::npos,
              eD.last_error());
        RemoveDirectoryW(stgD.c_str());
        RemoveDirectoryW(instD.c_str());
      }

      SetEvent(deniedQuit);
      const bool left = WaitForSingleObject(pDenied.hProcess, 15000) == WAIT_OBJECT_0;
      if (startedP) {
        WaitForSingleObject(pPlain.hProcess, 15000);
        CloseHandle(pPlain.hProcess);
      }
      check("the unopenable fixture exits when asked", left);
      CloseHandle(pDenied.hProcess);

      // The other half of the classification, on the same pid: once it has really exited, the
      // enumerator does not carry it at all. A pid that stopped being a process must not block an
      // update -- which is what would happen if "cannot open" were read as one answer.
      if (left) {
        bool stillThere = false;
        for (const ProcessTarget& t : fixture_targets()) {
          if (t.pid == deniedPid) stillThere = true;
        }
        check("an exited pid is not carried forward", !stillThere);
      }
    }
    if (deniedQuit) CloseHandle(deniedQuit);
  }

  // --------------------------------------- a process that is standing down, but has not gone
  //
  // The most frequent failure in the field: 23 abandoned updates since 09-10, every one of them
  // "could not ask pid N to stop". The log of 2026-09-21 17:15 shows why -- the caller acknowledged
  // the handoff at .423 and the attempt was abandoned at .512, 89 ms later. In between the process
  // destroyed its window and had not yet exited.
  //
  // PrepareForSwap already forgives a target that is GONE. This measures the state in between,
  // which it does not forgive, and which is what the handoff itself produces.
  {
    const std::wstring closingEvent = eventName + L"-closing";
    HANDLE quit = CreateEventW(nullptr, TRUE, FALSE, closingEvent.c_str());
    HANDLE up = CreateEventW(nullptr, TRUE, FALSE, (closingEvent + L"-up").c_str());
    HANDLE goClose = CreateEventW(nullptr, TRUE, FALSE, (closingEvent + L"-close").c_str());
    HANDLE gone = CreateEventW(nullptr, TRUE, FALSE, (closingEvent + L"-gone").c_str());

    std::wstring cmdC = L"\"" + own_path() + L"\" --fixture-closing " + closingEvent;
    std::vector<wchar_t> mutableCmdC(cmdC.begin(), cmdC.end());
    mutableCmdC.push_back(L'\0');
    STARTUPINFOW siC{};
    siC.cb = sizeof(siC);
    PROCESS_INFORMATION pC{};
    const bool startedC =
        quit && up && goClose && gone &&
        CreateProcessW(nullptr, mutableCmdC.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS,
                       nullptr, nullptr, &siC, &pC) != FALSE;
    check("the standing-down fixture started", startedC);
    if (startedC) {
      CloseHandle(pC.hThread);
      check("...and its window is up", WaitForSingleObject(up, 10000) == WAIT_OBJECT_0);

      // While the window exists it can be asked, which is the ordinary case.
      std::vector<ProcessTarget> before = fixture_children(pC.dwProcessId);
      check("with a window it is a target with one", before.size() == 1 && before[0].hasWindow,
            std::to_string(before.size()) + " targets");

      // Now it stands down: window destroyed, process still alive.
      SetEvent(goClose);
      check("...and it reports the window gone", WaitForSingleObject(gone, 10000) == WAIT_OBJECT_0);
      check("...while still running", WaitForSingleObject(pC.hProcess, 0) == WAIT_TIMEOUT);

      ProcessTarget closing;
      check("its identity is still readable", capture_process_identity(pC.dwProcessId, &closing));
      // hasWindow as the enumeration saw it a moment ago -- which is exactly the stale view the
      // updater carries from PrepareForSwap's own enumeration.
      closing.hasWindow = true;

      // The window really is gone.
      {
        std::vector<ProcessTarget> now;
        for (const ProcessTarget& t : fixture_targets()) {
          if (t.pid == pC.dwProcessId) now.push_back(t);
        }
        check("the destroyed window is gone from enumeration",
              now.size() == 1 && !now[0].hasWindow,
              now.empty() ? "not enumerated" : (now[0].hasWindow ? "still has a window"
                                                                 : "no window"));
      }

      // From HERE -- a console program -- the ask succeeds, because the console-control fallback
      // has a console to work with. That is not the updater's situation and it is why the first
      // version of this measurement found nothing.
      check("from a console caller the ask still succeeds", request_process_stop(closing),
            "the fallback finds a console");

      // From a caller with no console, which is what the updater is.
      {
        std::wstring cmdP = L"\"" + own_path() + L"\" --fixture-askprobe " +
                            std::to_wstring(pC.dwProcessId);
        std::vector<wchar_t> mutableCmdP(cmdP.begin(), cmdP.end());
        mutableCmdP.push_back(L'\0');
        STARTUPINFOW siP{};
        siP.cb = sizeof(siP);
        PROCESS_INFORMATION pP{};
        const bool startedP = CreateProcessW(nullptr, mutableCmdP.data(), nullptr, nullptr, FALSE,
                                             DETACHED_PROCESS, nullptr, nullptr, &siP, &pP) != FALSE;
        check("the console-less prober started", startedP);
        if (startedP) {
          CloseHandle(pP.hThread);
          const bool done = WaitForSingleObject(pP.hProcess, 15000) == WAIT_OBJECT_0;
          DWORD code = 99;
          GetExitCodeProcess(pP.hProcess, &code);
          CloseHandle(pP.hProcess);
          check("the prober answered", done && code <= 3, "code=" + std::to_string(code));
          check("MEASURED: with no console the fallback has nothing to use", (code & 2) == 0,
                "console fallback " + std::string((code & 2) ? "SUCCEEDED" : "failed"));
          check("MEASURED: and the ask therefore fails on a process that is standing down",
                (code & 1) == 0,
                std::string("request_process_stop returned ") + ((code & 1) ? "true" : "false"));
        }
      }

      wchar_t tempC[MAX_PATH]{};
      GetTempPathW(MAX_PATH, tempC);
      const std::wstring instC = std::wstring(tempC) + L"gnlink-closing-install";
      const std::wstring stgC = std::wstring(tempC) + L"gnlink-closing-staging";
      CreateDirectoryW(instC.c_str(), nullptr);
      CreateDirectoryW(stgC.c_str(), nullptr);
      // The link from that false to the abandoned attempt. This test is a console program, so its
      // own ask would succeed; the seam supplies the answer the updater actually gets, which is the
      // one measured above.
      UpdateEffectsConfig cC = config_for(instC, stgC, pC.dwProcessId);
      cC.requestStop = [](const ProcessTarget&) { return false; };
      WindowsUpdateEffects eC(cC);
      const bool prepared = eC.PrepareForSwap();
      check("a failed ask on a process still running abandons the attempt", !prepared,
            eC.last_error());
      check("...with the exact error the field log shows",
            eC.last_error().find("could not ask pid") != std::string::npos &&
                eC.last_error().find(std::to_string(pC.dwProcessId)) != std::string::npos,
            eC.last_error());
      RemoveDirectoryW(stgC.c_str());
      RemoveDirectoryW(instC.c_str());

      // The negative control, and the reason this is a race rather than a refusal: once it has
      // actually gone, the very same target is forgiven and the attempt proceeds.
      SetEvent(quit);
      const bool left = WaitForSingleObject(pC.hProcess, 15000) == WAIT_OBJECT_0;
      check("the fixture exits when released", left);
      if (left) {
        CreateDirectoryW(instC.c_str(), nullptr);
        CreateDirectoryW(stgC.c_str(), nullptr);
        UpdateEffectsConfig cG = config_for(instC, stgC, pC.dwProcessId);
        cG.enumerateTargets = [closing]() { return std::vector<ProcessTarget>{closing}; };
        WindowsUpdateEffects eG(cG);
        check("once it has gone, the same target is forgiven", eG.PrepareForSwap(),
              eG.last_error());
        RemoveDirectoryW(stgC.c_str());
        RemoveDirectoryW(instC.c_str());
      }
      CloseHandle(pC.hProcess);
    }
    if (quit) CloseHandle(quit);
    if (up) CloseHandle(up);
    if (goClose) CloseHandle(goClose);
    if (gone) CloseHandle(gone);
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
