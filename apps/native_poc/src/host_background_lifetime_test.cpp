// prelogin-ui r12 (L1): the streaming host's two long-running background tasks end before what they read.
//
// r11 ran both detached, for ever: the periodic Ready (readyNow reads SessionState::directoryAgent) and the
// pre-login worker's input-desktop watch (writes CaptureState). Here each runs under its product owner --
// SessionState::readyNotifier (stopped by ~SessionState before the directory agent, and by shutdown_host),
// and the StoppableWorker main() declares after `capture` running prelogin::RunLogonDesktopWatch -- and is
// ended the ways the host ends: a scope left normally, an early return, an exception. After that: no
// callback, no Ready, and the end was bounded.
//
// What differs from the product: a test admin pipe served by this process (the product's server and
// decision), a fake desktop name instead of OpenInputDesktop, and onLeft records instead of leaving the
// process. shutdown_host's own Stop() is not driven here (it needs the whole HostContext); it is the same
// call ~SessionState makes, first in shutdown_host.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <sddl.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include "host_session.hpp"
#include "prelogin_admin_pipe.hpp"
#include "prelogin_logon_desktop.hpp"
#include "stoppable_worker.hpp"

namespace pl = remote60::native_poc::prelogin;
namespace n = remote60::native_poc;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}
bool wait_until(const std::function<bool()>& done, int ms) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    if (done()) return true;
    Sleep(20);
  }
  return done();
}
// A Stop() that does not return within `ms` is a hang: report it and leave (the thread cannot be joined).
template <typename F>
uint64_t bounded(const char* what, F stop, int ms) {
  std::atomic<bool> done{false};
  const ULONGLONG t0 = GetTickCount64();
  std::thread t([&] { stop(); done = true; });
  if (!wait_until([&] { return done.load(); }, ms)) {
    check(what, false, "did not return within " + std::to_string(ms) + " ms (hang)");
    std::printf("\nRESULT: FAILED  (%d checks, %d failed)\n", gChecks, gFailures);
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 1);
  }
  t.join();
  return GetTickCount64() - t0;
}
std::wstring user_sddl() {
  HANDLE tok = nullptr;
  OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
  DWORD need = 0;
  GetTokenInformation(tok, TokenUser, nullptr, 0, &need);
  std::string buf(need, '\0');
  GetTokenInformation(tok, TokenUser, buf.data(), need, &need);
  CloseHandle(tok);
  LPWSTR sid = nullptr;
  ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &sid);
  std::wstring s = L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sid) + L")";
  LocalFree(sid);
  return s;
}
// The service's side: readies counted.
struct Service {
  std::atomic<int> readies{0};
  std::atomic<bool> run{true};
  pl::RebootLedger ledger;
  std::thread t;
  void start(const std::wstring& pipe) {
    wchar_t me[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, me, MAX_PATH);
    const std::wstring self = me;
    t = std::thread([this, pipe, self] {
      pl::AdminPrimitives p;
      p.recordPath = [] { return std::wstring(); };
      p.machineId = [] { return std::string(); };
      p.readFloor = [](uint64_t* out) { *out = 0; return true; };
      p.raiseFloor = [](uint64_t) { return true; };
      p.readRecord = [](const std::wstring&, pl::MachineRecord*) { return false; };
      p.writeRecord = [](const std::wstring&, const pl::MachineRecord&) -> uint32_t { return 5; };
      p.removeRecord = [](const std::wstring&) -> uint32_t { return 0; };
      p.featureEnabled = [] { return false; };
      p.queryStartType = [](uint32_t*) { return false; };
      p.setStartType = [](uint32_t) -> uint32_t { return 5; };
      p.loadRememberedStartType = [](uint32_t*) { return false; };
      p.rememberStartType = [](uint32_t) { return false; };
      p.forgetRememberedStartType = [] {};
      p.noteUserHostReady = [this](uint32_t, uint64_t, const std::string&, uint32_t) { ++readies; };
      const std::wstring sddl = user_sddl();
      pl::RunPreloginAdminServer(p, [this] { return run.load(); }, pipe.c_str(), sddl.c_str(), [](void*) { return true; },
                                 [self](void* h) { return pl::ClientImageIs(h, self); }, &ledger);
    });
    Sleep(200);
  }
  void stop() {
    run = false;
    if (t.joinable()) t.join();
  }
};
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("the streaming host's background tasks end before what they read (prelogin-ui r12, L1)\n\n");
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);
  const std::wstring pipe = L"\\\\.\\pipe\\GNLinkTest-lifetime-" + std::to_wstring(GetCurrentProcessId());
  Service service;
  service.start(pipe);
  const pl::ServerCheck selfServer = [](void* p) { return pl::ServerIsProcess(p, GetCurrentProcessId()); };

  // --- the Ready notifier, owned by the product's SessionState ---------------------------------------------
  std::atomic<bool> sessionGone{false}, inReady{false};
  std::atomic<int> calls{0}, callsAfter{0};
  auto run_session = [&](int how) {  // 0 leaves normally, 1 returns early, 2 throws
    auto session = std::make_unique<n::SessionState>();
    pl::UserHostReadyNotifier::Options o;
    o.refreshMs = 300;
    o.pipeName = pipe;
    o.check = selfServer;
    // what the product's readyNow reads: the session's directory agent
    session->readyNotifier.Start(
        [s = session.get(), &calls, &callsAfter, &sessionGone, &inReady] {
          ++calls;
          if (sessionGone) ++callsAfter;
          (void)s->directoryAgent.StatusLine();
          // still inside the callback when the session is destroyed (the end is aimed at this moment):
          // the owner must wait for it -- a callback that sees the session gone ran past its owner
          inReady = true;
          Sleep(150);
          if (sessionGone) ++callsAfter;
          inReady = false;
          return true;
        },
        o);
    const int before = service.readies.load();
    wait_until([&] { return service.readies.load() > before; }, 5000);
    wait_until([&] { return inReady.load(); }, 3000);  // the end comes while readyNow runs
    if (how == 2) throw std::runtime_error("startup failed");
    if (how == 1) {
      session.reset();  // an early failure unwinds the session
      sessionGone = true;
      return;
    }
    session.reset();
    sessionGone = true;
  };
  const char* names[] = {"normal end", "early failure", "exception"};
  for (int how = 0; how < 3; ++how) {
    sessionGone = false;
    calls = 0;
    callsAfter = 0;
    const int readiesBefore = service.readies.load();
    const uint64_t took = bounded("L1: the session's end", [&] {
      try {
        run_session(how);
      } catch (const std::exception&) {
        sessionGone = true;  // the unwind destroyed the session
      }
    }, 10000);
    const int readiesAtEnd = service.readies.load();
    const int callsAtEnd = calls.load();
    Sleep(1200);  // four refresh periods
    const std::string what = std::string("L1 (") + names[how] + "): the session's Ready ran, and after the session is gone: "
                             "no readyNow, no Ready (1.2 s, refresh 0.3 s)";
    check(what.c_str(),
          readiesAtEnd > readiesBefore && callsAfter.load() == 0 && calls.load() == callsAtEnd &&
              service.readies.load() == readiesAtEnd,
          std::to_string(callsAfter.load()) + " calls after, " + std::to_string(service.readies.load() - readiesAtEnd) +
              " readies after, " + std::to_string(took) + " ms");
  }

  // --- the pre-login desktop watch, owned beside the capture state -----------------------------------------
  struct Capture {  // CaptureState's two fields the watch reads and writes
    std::wstring preloginServingDesktop = L"Winlogon";
    std::atomic<bool> preloginDesktopServing{false};
  };
  std::atomic<int> reads{0}, readsAfter{0}, left{0};
  std::atomic<bool> captureGone{false}, inRead{false};
  auto run_watch = [&](int how, const wchar_t* desktop) {
    auto capture = std::make_unique<Capture>();
    n::StoppableWorker watch;  // declared after capture, as in main(): stopped before capture goes
    watch.Start([c = capture.get(), &reads, &readsAfter, &left, &captureGone, &inRead, desktop](n::StoppableWorker& w) {
      pl::RunLogonDesktopWatch(
          w, c->preloginServingDesktop, c->preloginDesktopServing,
          [&] {
            ++reads;
            if (captureGone) ++readsAfter;
            inRead = true;  // the end is aimed at this moment, as above
            Sleep(60);
            if (captureGone) ++readsAfter;
            inRead = false;
            return std::wstring(desktop);
          },
          [] {}, [&left](const std::wstring&) { ++left; }, [] { return false; }, nullptr, 50);
    });
    wait_until([&] { return reads.load() >= 3 || left.load() > 0; }, 3000);
    if (left.load() == 0) wait_until([&] { return inRead.load(); }, 3000);
    const bool served = capture->preloginDesktopServing.load();
    if (how == 2) throw std::runtime_error("startup failed");
    (void)served;
    // leaving: watch (declared last) stops first, then capture
  };
  for (int how = 0; how < 3; ++how) {
    reads = 0;
    readsAfter = 0;
    captureGone = false;
    const uint64_t took = bounded("L1: main()'s end with the desktop watch running", [&] {
      try {
        run_watch(how, L"Winlogon");
      } catch (const std::exception&) {
      }
      captureGone = true;
    }, 5000);
    const int readsAtEnd = reads.load();
    Sleep(500);  // ten watch periods
    const std::string what = std::string("L1 (") + names[how] + "): the desktop watch ran, ended within the bound, and "
                             "read nothing after the capture state was gone";
    check(what.c_str(), readsAtEnd >= 3 && readsAfter.load() == 0 && reads.load() == readsAtEnd && took < 1000,
          std::to_string(reads.load() - readsAtEnd) + " reads after, " + std::to_string(took) + " ms");
  }
  {
    left = 0;
    reads = 0;
    run_watch(0, L"Default");
    check("...a desktop that is not the served one: onLeft once, and the watch ends itself", left.load() == 1 &&
                                                                                              reads.load() == 1);
  }

  service.stop();
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
