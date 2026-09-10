// The uploader's shutdown contract, exercised in isolated child processes.
//
// Why children rather than in-process cases: the defect is what happens when a process EXITS
// while the uploader's worker thread is still joinable. The worker lives in a function-local
// static; its destructor runs during exit, destroying a joinable std::thread, and that is
// std::terminate() by definition. An in-process test cannot observe its own termination, and --
// more to the point -- log_upload_test.cpp already calls log_upload_stop() nine times while the
// product called it zero times, so a test that calls stop() itself proves nothing about whether
// anything ships that call. Here the assertion is the child's EXIT CODE.
//
// The last case is the negative control: the same work with no shutdown guard. It must NOT exit
// cleanly. If it ever does, every other case in this file has stopped meaning anything.
//
// Separately, and because an exit code cannot see it: the two product entry points are read from
// source and checked for the guard. That is the link between "the contract works" and "the
// product uses it" -- the exact link that was missing.
//
// Build: remote60_log_upload_shutdown_test (CMake). Run: prints PASS lines, exit 0.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "log_upload.hpp"

#pragma comment(lib, "ws2_32.lib")

using namespace remote60::native_poc;

namespace {

int gFailures = 0;

void check(const char* what, bool ok, const std::string& detail) {
  std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : " -- ",
              detail.c_str());
  if (!ok) ++gFailures;
}

uint64_t now_ms() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

// ---------------------------------------------------------------------------- the child's server

/**
 * A loopback sink with three behaviours, which are the three states a shutdown has to survive:
 * an answer, no answer at all, and a refusal.
 */
class Sink {
 public:
  enum class Mode { Ok, Hold, Reject };

  bool Start(Mode mode) {
    mode_ = mode;
    listen_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_ == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(listen_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    if (::listen(listen_, 16) != 0) return false;
    int len = sizeof(addr);
    getsockname(listen_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    thread_ = std::thread([this] { Loop(); });
    return true;
  }

  void Stop() {
    if (listen_ != INVALID_SOCKET) {
      closesocket(listen_);
      listen_ = INVALID_SOCKET;
    }
    for (SOCKET s : held_) closesocket(s);
    held_.clear();
    if (thread_.joinable()) thread_.join();
  }

  std::string url() const { return "http://127.0.0.1:" + std::to_string(port_); }
  int accepted() const { return accepted_.load(); }

 private:
  void Loop() {
    for (;;) {
      SOCKET c = accept(listen_, nullptr, nullptr);
      if (c == INVALID_SOCKET) return;
      char buf[4096];
      recv(c, buf, sizeof(buf), 0);
      ++accepted_;
      if (mode_ == Mode::Hold) {
        // Accepted, read, and then nothing: what a request in flight looks like from the
        // uploader's side. The socket is kept open so the send blocks on its receive timeout.
        held_.push_back(c);
        continue;
      }
      const char* answer = mode_ == Mode::Reject
                               ? "HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n"
                               : "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
      send(c, answer, static_cast<int>(strlen(answer)), 0);
      closesocket(c);
    }
  }

  Mode mode_ = Mode::Ok;
  SOCKET listen_ = INVALID_SOCKET;
  uint16_t port_ = 0;
  std::thread thread_;
  std::atomic<int> accepted_{0};
  std::vector<SOCKET> held_;
};

LogUploadConfig config_for(const std::string& url) {
  LogUploadConfig c;
  c.directoryUrl = url;
  c.hostToken = "fixture-host-token";  // a fixture, never a real credential
  c.device = "shutdown-fixture";
  c.identity = "fixture@" + url;
  c.flushIntervalMs = 100;
  return c;
}

bool wait_until(const std::function<bool()>& done, uint64_t budgetMs) {
  const uint64_t deadline = now_ms() + budgetMs;
  while (now_ms() < deadline) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return done();
}

// ---------------------------------------------------------------------------- the child modes

int run_child(const std::string& mode, const std::string& reportPath) {
  // No fault dialog, no abort message box: the negative control below deliberately terminates,
  // and a test may not put a window on anyone's desktop.
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);

  if (mode == "unstarted") {
    // The guard has to be safe when nothing ever ran: the host constructs it before it knows
    // whether there is a cached token, and without one it never configures.
    const LogUploadShutdown guard;
    return 0;
  }

  if (mode == "normal") {
    Sink sink;
    if (!sink.Start(Sink::Mode::Ok)) return 90;
    {
      const LogUploadShutdown guard;
      std::string reason;
      if (!log_upload_configure(config_for(sink.url()), &reason)) return 91;
      log_upload_enqueue("host", "a line that has somewhere to go");
      if (!wait_until([] { return log_upload_status().sentBatches >= 1; }, 8000)) return 92;
    }
    // Past the guard: the worker is joined, so nothing can be running and a late line is dropped
    // rather than queued for a thread that no longer exists.
    if (log_upload_running()) return 93;
    log_upload_enqueue("host", "after the join");
    sink.Stop();
    return 0;
  }

  if (mode == "inflight") {
    Sink sink;
    if (!sink.Start(Sink::Mode::Hold)) return 90;
    uint64_t elapsed = 0;
    {
      const LogUploadShutdown guard;
      std::string reason;
      if (!log_upload_configure(config_for(sink.url()), &reason)) return 91;
      // Enough batches that an unbounded drain would be visible: without a budget the worker
      // would try each of these in turn, one http timeout apiece.
      for (int i = 0; i < 40; ++i) {
        log_upload_enqueue("host", std::string(4096, 'x'));
      }
      if (!wait_until([&sink] { return sink.accepted() >= 1; }, 8000)) return 92;
      const uint64_t start = now_ms();
      log_upload_shutdown();  // the guard's work, done here so it can be timed
      elapsed = now_ms() - start;
    }
    if (log_upload_running()) return 93;
    if (!reportPath.empty()) {
      std::ofstream out(reportPath, std::ios::binary);
      out << elapsed;
    }
    sink.Stop();
    return 0;
  }

  if (mode == "paused") {
    Sink sink;
    if (!sink.Start(Sink::Mode::Reject)) return 90;
    {
      const LogUploadShutdown guard;
      std::string reason;
      if (!log_upload_configure(config_for(sink.url()), &reason)) return 91;
      log_upload_enqueue("host", "a line the server will refuse");
      if (!wait_until([] { return log_upload_status().authRejected; }, 8000)) return 92;
      // A paused uploader has a held batch and no way to send it. The stop must end anyway
      // rather than wait for a token that is not coming.
    }
    if (log_upload_running()) return 93;
    sink.Stop();
    return 0;
  }

  if (mode == "race") {
    Sink sink;
    if (!sink.Start(Sink::Mode::Ok)) return 90;
    std::atomic<bool> stop{false};
    std::atomic<int> refused{0};
    std::thread signer([&sink, &stop, &refused] {
      // A sign-in arriving while the window is closing. Every one of these after the latch must
      // be refused: one that started a worker would leave a thread nobody joins, which is the
      // original defect with an extra step.
      while (!stop.load()) {
        std::string reason;
        if (!log_upload_configure(config_for(sink.url()), &reason)) ++refused;
      }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    log_upload_shutdown();
    // Keep racing for a while AFTER the join: this is the window that matters.
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    stop.store(true);
    signer.join();
    if (refused.load() == 0) return 94;   // the race never actually raced
    if (log_upload_running()) return 93;  // something restarted it
    sink.Stop();
    return 0;
  }

  if (mode == "noguard") {
    // The negative control. Identical to "normal" except that nothing stops the worker, so the
    // static's destructor destroys a joinable std::thread on the way out. This child must NOT
    // exit 0; if it does, the guard is not what is keeping the others alive.
    Sink sink;
    if (!sink.Start(Sink::Mode::Ok)) return 90;
    std::string reason;
    if (!log_upload_configure(config_for(sink.url()), &reason)) return 91;
    log_upload_enqueue("host", "a line that has somewhere to go");
    wait_until([] { return log_upload_status().sentBatches >= 1; }, 8000);
    // Stopped deliberately, so the ONLY thread still joinable at exit is the uploader's worker.
    // Left running, the sink's own thread would terminate the process too and this control would
    // be passing for the wrong reason.
    sink.Stop();
    return 0;  // ... and exit takes it from here
  }

  return 95;
}

// ---------------------------------------------------------------------------- the parent

std::wstring own_path() {
  wchar_t buf[MAX_PATH]{};
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return buf;
}

/** Runs this executable in the given mode. Returns false on timeout (the child is then killed). */
bool run_mode(const std::string& mode, const std::string& reportPath, DWORD* outExit) {
  std::wstring cmd = L"\"" + own_path() + L"\" ";
  cmd += std::wstring(mode.begin(), mode.end());
  if (!reportPath.empty()) {
    cmd += L" \"" + std::wstring(reportPath.begin(), reportPath.end()) + L"\"";
  }
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, nullptr, &si, &pi)) {
    return false;
  }
  CloseHandle(pi.hThread);
  const DWORD waited = WaitForSingleObject(pi.hProcess, 60000);
  if (waited != WAIT_OBJECT_0) {
    TerminateProcess(pi.hProcess, 0xDEAD);
    CloseHandle(pi.hProcess);
    return false;
  }
  GetExitCodeProcess(pi.hProcess, outExit);
  CloseHandle(pi.hProcess);
  return true;
}

std::string temp_report_path() {
  wchar_t dir[MAX_PATH]{};
  GetTempPathW(MAX_PATH, dir);
  std::wstring w = std::wstring(dir) + L"remote60-shutdown-elapsed.txt";
  std::string narrow(w.begin(), w.end());
  return narrow;
}

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream os;
  os << in.rdbuf();
  return os.str();
}

/**
 * Whether the product's entry points declare the guard.
 *
 * Read from source because no exit code can answer it, and because the answer used to be "no"
 * while nine tests called log_upload_stop() and everything was green.
 */
void check_product_wiring() {
#ifdef REMOTE60_MAIN_SRC_DIR
  const char* owners[] = {"host_app_main.cpp", "client_shell_main.cpp"};
  for (const char* owner : owners) {
    const std::string text = read_file(std::string(REMOTE60_MAIN_SRC_DIR) + "/" + owner);
    const bool found = !text.empty() && text.find("LogUploadShutdown") != std::string::npos;
    check((std::string("the product declares the guard: ") + owner).c_str(), found,
          text.empty() ? "source not readable" : "");
  }
#else
  check("the product wiring is checked", false, "REMOTE60_MAIN_SRC_DIR not defined");
#endif
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2) {
    return run_child(argv[1], argc >= 3 ? argv[2] : std::string());
  }

  std::printf("log_upload_shutdown_test\n");

  struct Case {
    const char* mode;
    const char* what;
  };
  const Case cases[] = {
      {"normal", "a process that sent something exits cleanly"},
      {"unstarted", "a process that never configured exits cleanly"},
      {"inflight", "a process exits cleanly with a request in flight"},
      {"paused", "a process paused on 401 exits cleanly"},
      {"race", "a configure racing the shutdown cannot restart the worker"},
  };
  for (const Case& c : cases) {
    const std::string report = std::string(c.mode) == "inflight" ? temp_report_path() : "";
    if (!report.empty()) DeleteFileA(report.c_str());
    DWORD code = 0;
    const bool finished = run_mode(c.mode, report, &code);
    check(c.what, finished && code == 0,
          finished ? "exit=" + std::to_string(code) : "timed out (60s)");
    if (std::string(c.mode) == "inflight" && finished && code == 0) {
      const std::string elapsed = read_file(report);
      const long ms = elapsed.empty() ? -1 : std::strtol(elapsed.c_str(), nullptr, 10);
      // The bound, recorded rather than assumed: the drain budget plus the one request already
      // in flight, which waits out directory_client's receive timeout. Not a tight assertion --
      // the point is that it is bounded at all, and the number is in the log.
      check("...and the wait is bounded", ms >= 0 && ms <= 20000,
            "elapsed=" + std::to_string(ms) + "ms (budget 3000 + one in-flight http_post)");
      DeleteFileA(report.c_str());
    }
  }

  // The negative control. Everything above is only evidence if this one fails to exit cleanly.
  {
    DWORD code = 0;
    const bool finished = run_mode("noguard", "", &code);
    check("without the guard the same process does NOT exit cleanly", finished && code != 0,
          finished ? "exit=" + std::to_string(code) : "timed out (60s)");
  }

  check_product_wiring();

  if (gFailures == 0) {
    std::printf("log_upload_shutdown_test: PASS\n");
    return 0;
  }
  std::printf("log_upload_shutdown_test: FAILED (%d)\n", gFailures);
  return 1;
}
