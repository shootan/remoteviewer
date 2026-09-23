#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

// A viewer that is already connecting, called off the way the shell calls it off.
//
// The shell starts another GNLinkViewer when the user picks a PC and never closes the one that
// was already there -- checked, not assumed: begin_session bumps a counter and spawns, the
// process handle lives only inside the exit watcher, and client_shell_main.cpp contains no
// TerminateProcess and no WM_CLOSE. So an older viewer kept punching for four seconds and saying
// hello for up to thirty, and could still bind a session afterwards, against a host that had
// already been handed a newer capability.
//
// What this drives is the real thing: a real GNLinkViewer.exe, a scripted fake directory it can
// reach but never get a host out of, and the same viewer_request_cancel the shell calls. What it
// measures is how long the process takes to stop after being told, and whether it stopped for
// the right reason -- "cancelled", not "the host did not answer", which is what it used to look
// like and is a different fault.
//
// Off unless REMOTE60_ALLOW_HOST_E2E=1. Starting a viewer opens listening sockets, and on Windows
// an unanswered firewall prompt becomes a Block rule on the user's machine. Skipping is loud and
// exits 0.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "directory_fake_server.hpp"
#include "viewer_cancel_channel.hpp"

using remote60::native_poc::test_support::FakeDirectory;
using remote60::native_poc::test_support::Reply;
using remote60::native_poc::viewer::viewer_cancel_handle_arg;
using remote60::native_poc::viewer::viewer_create_cancel_event;
using remote60::native_poc::viewer::viewer_request_cancel;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ",
              detail.c_str());
}

bool host_e2e_allowed() {
  wchar_t value[8]{};
  const DWORD n = GetEnvironmentVariableW(L"REMOTE60_ALLOW_HOST_E2E", value, 8);
  return n > 0 && value[0] == L'1';
}

std::wstring self_path() {
  wchar_t path[MAX_PATH * 2]{};
  GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
  return path;
}

std::wstring directory_of(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? std::wstring(L".") : path.substr(0, slash + 1);
}

/** A spawned GNLinkViewer whose stdout this test can read. */
struct SpawnedViewer {
  PROCESS_INFORMATION pi{};
  HANDLE job = nullptr;
  HANDLE readEnd = nullptr;
  std::thread reader;
  std::atomic<bool> reading{false};
  std::mutex mu;
  std::string output;

  std::string log() {
    std::lock_guard<std::mutex> lock(mu);
    return output;
  }
  bool WaitFor(const char* marker, uint32_t budgetMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
    while (std::chrono::steady_clock::now() < deadline) {
      if (log().find(marker) != std::string::npos) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return false;
  }
  void Stop() {
    reading = false;
    if (job) { CloseHandle(job); job = nullptr; }
    if (readEnd) { CloseHandle(readEnd); readEnd = nullptr; }
    if (reader.joinable()) reader.join();
    if (pi.hThread) CloseHandle(pi.hThread);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    pi = PROCESS_INFORMATION{};
  }
  ~SpawnedViewer() { Stop(); }
};

/** The launch, with whatever cancel argument the caller wants -- including none. */
bool StartViewerRaw(SpawnedViewer* viewer, const std::string& directoryUrl,
                    const std::wstring& cancelArg);

bool StartViewer(SpawnedViewer* viewer, const std::string& directoryUrl, HANDLE cancel) {
  return StartViewerRaw(viewer, directoryUrl,
                        cancel ? L" --cancel-event " + viewer_cancel_handle_arg(cancel)
                               : std::wstring());
}

bool StartViewerRaw(SpawnedViewer* viewer, const std::string& directoryUrl,
                    const std::wstring& cancelArg) {
  const std::wstring exe = directory_of(self_path()) + L"GNLinkViewer.exe";
  if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE writeEnd = nullptr;
  if (!CreatePipe(&viewer->readEnd, &writeEnd, &sa, 1 << 20)) return false;
  SetHandleInformation(viewer->readEnd, HANDLE_FLAG_INHERIT, 0);

  viewer->job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(viewer->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

  SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
  const std::wstring url(directoryUrl.begin(), directoryUrl.end());
  std::wstring cmd = L"\"" + exe + L"\" --transport udp --codec h264" +
                     L" --directory-url " + url +
                     L" --directory-session test-session-token" +
                     L" --directory-host-id h-cancel";
  // Whatever the caller asked for. bInheritHandles at the launch below is what makes a real
  // handle value mean anything in the child.
  cmd += cancelArg;
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = writeEnd;
  si.hStdError = writeEnd;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  const bool launched =
      CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                     nullptr, nullptr, &si, &viewer->pi) != FALSE;
  CloseHandle(writeEnd);
  if (!launched) return false;
  AssignProcessToJobObject(viewer->job, viewer->pi.hProcess);

  viewer->reading = true;
  viewer->reader = std::thread([viewer] {
    char buf[4096];
    DWORD got = 0;
    while (viewer->reading.load() && viewer->readEnd &&
           ReadFile(viewer->readEnd, buf, sizeof(buf), &got, nullptr) && got > 0) {
      std::lock_guard<std::mutex> lock(viewer->mu);
      viewer->output.append(buf, got);
    }
  });
  return true;
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  if (!host_e2e_allowed()) {
    std::printf("SKIP  viewer_cancel_e2e_test (starts a viewer that opens sockets)\n");
    std::printf("      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n");
    std::printf("\nRESULT: SKIPPED\n");
    return 77;  // kE2eSkippedExit (e2e_isolation.hpp): a skip is not a pass (RV-18)
  }

  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::printf("FAIL  winsock did not start\n");
    return 1;
  }

  // The event is UNNAMED now, and this is the check that says so.
  //
  // The first version named it Local\\GNLinkViewerCancel-<pid> with default security, which any
  // process in the session could open with EVENT_MODIFY_STATE knowing only a pid -- and then
  // cancel somebody's connection whenever it liked. A secret in the name would not have helped:
  // a viewer's command line is readable by those same processes.
  //
  // ORDER MATTERS, and the first version of this check got it wrong: it tried to open the name
  // BEFORE creating anything, so of course nothing was there, and a mutant that went back to
  // naming the event passed. The event has to exist first -- then the question "can it be
  // reached by name" means something.
  HANDLE probe = viewer_create_cancel_event();
  check("an unnamed event is created and can be signalled by whoever holds it",
        probe != nullptr && viewer_request_cancel(probe));
  {
    const std::wstring oldName = L"Local\\GNLinkViewerCancel-" +
                                std::to_wstring(GetCurrentProcessId());
    HANDLE byName = OpenEventW(EVENT_MODIFY_STATE, FALSE, oldName.c_str());
    check("...and it cannot be reached by the old pid-derived name", byName == nullptr,
          byName ? "it opened -- the event is named, and anyone in this session can signal it"
                 : "ERROR_FILE_NOT_FOUND");
    if (byName) CloseHandle(byName);
  }
  if (probe) CloseHandle(probe);
  check("...and signalling nothing is refused rather than pretended",
        !viewer_request_cancel(nullptr));
  FakeDirectory dir;
  check("the fake directory starts", dir.Start());
  // Enough for the viewer to get through login and into the connect steps, and no further: the
  // host it asks about answers nothing, so observe/punch/hello is where it stays.
  dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                      std::to_string(dir.udpPort()) + "}}"}});
  dir.Script("/api/hosts", {Reply{200, "{\"ok\":true,\"hosts\":[{\"hostId\":\"h-cancel\","
                                       "\"hostName\":\"cancel-pc\",\"online\":true}]}"}});
  // A candidate on a port nothing answers on, so PunchAny spends its budget and the hello that
  // follows spends its own -- which is the state an older viewer sits in.
  dir.Script("/api/connect",
             {Reply{200, "{\"ok\":true,\"hostPublicIp\":\"127.0.0.1\",\"hostPublicUdpPort\":9,"
                         "\"candidates\":[{\"ip\":\"127.0.0.1\",\"port\":9,\"kind\":\"private\"}],"
                         "\"punchToken\":\"" + std::string(32, 'c') + "\"}"}});

  SpawnedViewer viewer;
  HANDLE cancelEvent = viewer_create_cancel_event();
  check("the cancel event is created", cancelEvent != nullptr);
  check("a real GNLinkViewer starts", StartViewer(&viewer, dir.url(), cancelEvent));

  // It has to actually be in the connect path before cancelling proves anything: cancelling a
  // process that has not got there yet would pass for the wrong reason.
  const bool connecting = viewer.WaitFor("[native-video-client]", 20000);
  check("...and reaches the connect path", connecting,
        viewer.log().size() > 300 ? viewer.log().substr(viewer.log().size() - 300) : viewer.log());

  // Long enough to be inside a wait rather than between two of them.
  std::this_thread::sleep_for(std::chrono::milliseconds(600));

  const auto askedAt = std::chrono::steady_clock::now();
  check("the cancel is delivered", viewer_request_cancel(cancelEvent),
        "through the handle, not a name");

  const DWORD waited = WaitForSingleObject(viewer.pi.hProcess, 5000);
  const auto stoppedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - askedAt).count();
  check("the viewer stops", waited == WAIT_OBJECT_0,
        waited == WAIT_TIMEOUT ? "still running after 5s" : "wait=" + std::to_string(waited));
  // The requirement is a second. Recorded as well as asserted, because the number is the point:
  // it used to be up to thirty-four (four of punching, thirty of hello).
  check("...within a second of being told", stoppedMs <= 1000,
        std::to_string(stoppedMs) + "ms");

  const std::string log = viewer.log();
  check("...and says it was cancelled, not that the host failed to answer",
        log.find("connect cancelled") != std::string::npos,
        log.size() > 400 ? log.substr(log.size() - 400) : log);
  check("...naming the shell as the reason",
        log.find("by the shell") != std::string::npos);
  // The whole point of stopping it: no session may exist afterwards.
  check("...and no session was established",
        log.find("client connected") == std::string::npos &&
            log.find("hello ok=1") == std::string::npos);

  viewer.Stop();
  if (cancelEvent) CloseHandle(cancelEvent);

  // ======================================== and again, but cancelled while it is saying hello
  //
  // The first phase cancels during the punch, which is where a cancel usually lands -- and that
  // means it never exercises the hello's own stop. Removing that stop left the first phase
  // passing, which is how this second one came to exist: the hello is the long wait, thirty
  // seconds of it now, and it is the one that most needs interrupting.
  {
    SpawnedViewer inHello;
    HANDLE helloCancel = viewer_create_cancel_event();
    check("a second cancel event is created", helloCancel != nullptr);
    check("a second GNLinkViewer starts", StartViewer(&inHello, dir.url(), helloCancel));

    // Waiting for the punch to have finished is what puts the cancel inside the hello rather
    // than before it. A fixed sleep would drift with the punch budget.
    const bool punched = inHello.WaitFor("punch answered=", 30000);
    check("...and gets past the punch", punched,
          inHello.log().size() > 300 ? inHello.log().substr(inHello.log().size() - 300)
                                     : inHello.log());
    // Far enough in to be inside a hello wait slice rather than between two of them.
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    const bool inHelloNow = inHello.log().find("hello ok=") == std::string::npos;
    check("...and is still in the hello, not past it", inHelloNow,
          inHelloNow ? "" : "the hello had already ended");

    const auto askedAt = std::chrono::steady_clock::now();
    check("the cancel is delivered (hello)", viewer_request_cancel(helloCancel),
          "through the handle, not a name");
    const DWORD waited = WaitForSingleObject(inHello.pi.hProcess, 5000);
    const auto stoppedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - askedAt).count();
    check("a viewer cancelled mid-hello stops", waited == WAIT_OBJECT_0,
          waited == WAIT_TIMEOUT ? "still running after 5s" : "wait=" + std::to_string(waited));
    check("...within a second of being told", stoppedMs <= 1000,
          std::to_string(stoppedMs) + "ms of a 30000ms hello budget");

    const std::string log = inHello.log();
    check("...having actually been in the hello when it was told",
          log.find("attempts=") != std::string::npos &&
              log.find("budgetMs=30000") != std::string::npos,
          "budgetMs=30000 is the ceiling this viewer now runs with");
    check("...and says it was cancelled", log.find("connect cancelled") != std::string::npos,
          log.size() > 400 ? log.substr(log.size() - 400) : log);
    check("...and established no session",
          log.find("client connected") == std::string::npos &&
              log.find("hello ok=1") == std::string::npos);
    inHello.Stop();
    if (helloCancel) CloseHandle(helloCancel);
  }


  // ============ a viewer with no usable cancel handle connects anyway, which is the old behaviour
  //
  // Three ways to get here and all of them ordinary: an older shell that does not pass the flag,
  // a viewer started by hand, and a value that is not a handle this process holds. The digits on
  // the command line are just digits -- nothing stops a caller passing 12345 -- so the viewer
  // checks whether the handle is its own before waiting on it. What must NOT happen is a refusal
  // to connect: that would turn a missing convenience into a broken session.
  {
    // A number that is a plausible handle and is not one of ours. Handles are multiples of four;
    // this one is deliberately large enough to be unallocated.
    SpawnedViewer bogus;
    check("a viewer starts with a handle it does not own",
          StartViewerRaw(&bogus, dir.url(), L" --cancel-event 4294967292"));
    const bool reached = bogus.WaitFor("[native-video-client][attempt]", 20000);
    check("...and still reaches the connect path", reached,
          bogus.log().size() > 300 ? bogus.log().substr(bogus.log().size() - 300) : bogus.log());
    check("...saying once that it cannot be called off",
          bogus.log().find("cannot be called off") != std::string::npos);
    check("...and not dying over it",
          WaitForSingleObject(bogus.pi.hProcess, 200) == WAIT_TIMEOUT);
    bogus.Stop();
  }

  {
    // No flag at all: the older shell, and every hand-run.
    SpawnedViewer plain;
    check("a viewer starts with no cancel flag", StartViewerRaw(&plain, dir.url(), L""));
    const bool reached = plain.WaitFor("[native-video-client][attempt]", 20000);
    check("...and reaches the connect path just the same", reached);
    check("...without complaining about a flag nobody passed",
          plain.log().find("cannot be called off") == std::string::npos);
    plain.Stop();
  }

  std::printf("\n%s  (%d checks, %d failed)\n",
              gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED", gChecks, gFailures);
  WSACleanup();
  return gFailures == 0 ? 0 : 1;
}
