// hostapp-log-upload r2 (V1): the REAL supervisor exit-observation -> enqueue -> uploader -> server
// chain, not a direct formatter/enqueue call.
//
// It compiles the product host_app_main.cpp (entry renamed, REMOTE60_HOST_TEST_SEAM on) and drives a
// real StreamingHostProcess whose streaming child is THIS executable in stand-in mode. The stand-in
// prints a chosen marker, runs for a chosen time and exits with a chosen code, so the supervisor's
// own WaitForSingleObject/GetExitCodeProcess path runs and logs through AppendLogLineOnce ->
// log_upload_enqueue("host", ...). A fake log server receives what the product actually uploads.
//
// Covered (Codex V1): (a) a long-run (>15 s) nonzero exit -> the exit line on the server carries the
// real code; (b) H1: child A prints a marker, child B prints nothing -> B's exit line says
// lastLine="none" and never inherits A's marker; (c) H2: Stop() -> the exit line says
// restart_planned=false; (d) the free-function append_host_app_log also reaches the server. Deleting
// either product enqueue, or the H1 lastChildLine_.clear(), makes a check here FAIL (see the report).

#define wWinMain host_product_entry
#include "host_app_main.cpp"
#undef wWinMain

#include "log_upload.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

using namespace remote60::native_poc;

namespace {

int gChecks = 0, gFailures = 0;
void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ",
              detail.c_str());
  std::fflush(stdout);
}

// ---- a minimal fake log server (accepts POSTs, keeps every request body) ----------------------
class FakeLogServer {
 public:
  bool Start() {
    sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_ == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    if (listen(sock_, 16) != 0) return false;
    int len = sizeof(addr);
    getsockname(sock_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    thread_ = std::thread([this] { Loop(); });
    return true;
  }
  void Stop() {
    if (sock_ != INVALID_SOCKET) { closesocket(sock_); sock_ = INVALID_SOCKET; }
    if (thread_.joinable()) thread_.join();
  }
  uint16_t port() const { return port_; }
  std::string url() const { return "http://127.0.0.1:" + std::to_string(port_); }
  std::string body() {
    std::lock_guard<std::mutex> lk(mu_);
    return body_;
  }

 private:
  void Loop() {
    for (;;) {
      SOCKET c = accept(sock_, nullptr, nullptr);
      if (c == INVALID_SOCKET) return;
      std::string raw;
      char buf[4096];
      size_t headerEnd = std::string::npos, contentLength = 0;
      for (;;) {
        const int n = recv(c, buf, sizeof(buf), 0);
        if (n <= 0) break;
        raw.append(buf, static_cast<size_t>(n));
        if (headerEnd == std::string::npos) {
          headerEnd = raw.find("\r\n\r\n");
          if (headerEnd != std::string::npos) {
            const size_t cl = raw.find("Content-Length:");
            if (cl != std::string::npos && cl < headerEnd)
              contentLength = static_cast<size_t>(std::strtoul(raw.c_str() + cl + 15, nullptr, 10));
          }
        }
        if (headerEnd != std::string::npos && raw.size() >= headerEnd + 4 + contentLength) break;
      }
      if (headerEnd != std::string::npos) {
        std::lock_guard<std::mutex> lk(mu_);
        body_ += raw.substr(headerEnd + 4, contentLength);
        body_ += "\n";
      }
      const char* resp = "HTTP/1.1 200 X\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";
      send(c, resp, static_cast<int>(std::strlen(resp)), 0);
      shutdown(c, SD_BOTH);
      closesocket(c);
    }
  }
  SOCKET sock_ = INVALID_SOCKET;
  uint16_t port_ = 0;
  std::thread thread_;
  std::mutex mu_;
  std::string body_;
};

uint64_t now_ms() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
template <class Pred>
bool wait_until(Pred p, uint64_t timeoutMs) {
  const uint64_t deadline = now_ms() + timeoutMs;
  while (now_ms() < deadline) {
    if (p()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return p();
}

// Every "[host-app] the streaming host exited" line the server has seen, one per vector entry.
std::vector<std::string> exit_lines(const std::string& body) {
  std::vector<std::string> out;
  size_t pos = 0;
  while (pos < body.size()) {
    size_t nl = body.find('\n', pos);
    std::string line = body.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    if (line.rfind("[host-app] the streaming host exited", 0) == 0) out.push_back(line);
    if (nl == std::string::npos) break;
    pos = nl + 1;
  }
  return out;
}

std::wstring widen_ascii(const std::string& s) { return std::wstring(s.begin(), s.end()); }

void set_launch(int idx, const std::string& line, unsigned long runMs, unsigned long exitCode) {
  const std::string p = "FIXTURE_" + std::to_string(idx) + "_";
  SetEnvironmentVariableW(widen_ascii(p + "LINE").c_str(), widen_ascii(line).c_str());
  SetEnvironmentVariableW(widen_ascii(p + "RUNMS").c_str(), std::to_wstring(runMs).c_str());
  SetEnvironmentVariableW(widen_ascii(p + "EXIT").c_str(), std::to_wstring(exitCode).c_str());
}

std::wstring gStateFile;
void reset_launch_index() {
  const std::filesystem::path p{gStateFile};
  std::ofstream out(p);
  out << 0;
}

// ---- stand-in streaming child -----------------------------------------------------------------
int run_fixture_child() {
  // Pick behaviour by launch index, stored in a shared file the supervisor's relaunches step through.
  int idx = 0;
  const std::filesystem::path statePath{gStateFile};
  {
    std::ifstream in(statePath);
    if (in) in >> idx;
  }
  { std::ofstream out(statePath); out << (idx + 1); }

  auto env = [](const std::string& name) -> std::string {
    wchar_t v[1024] = {};
    const DWORD n = GetEnvironmentVariableW(widen_ascii(name).c_str(), v, 1024);
    if (n == 0 || n >= 1024) return {};
    std::wstring w(v, n);
    return std::string(w.begin(), w.end());
  };
  const std::string pfx = "FIXTURE_" + std::to_string(idx) + "_";
  const std::string line = env(pfx + "LINE");
  const std::string runMsS = env(pfx + "RUNMS");
  const std::string exitS = env(pfx + "EXIT");
  const unsigned long runMs = runMsS.empty() ? 100u : std::strtoul(runMsS.c_str(), nullptr, 10);
  const unsigned long exitCode = exitS.empty() ? 0u : std::strtoul(exitS.c_str(), nullptr, 10);

  if (!line.empty()) { std::printf("%s\n", line.c_str()); std::fflush(stdout); }
  Sleep(runMs);
  ExitProcess(static_cast<UINT>(exitCode));  // exact DWORD exit code (0xC0000005 etc.)
}

}  // namespace

int main() {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  // Stand-in streaming child: the supervisor launches us with the product's --transport flag.
  gStateFile = widen_ascii(std::string(std::getenv("GNLINK_FIXTURE_STATE") ? std::getenv("GNLINK_FIXTURE_STATE") : ""));
  for (int i = 1; argv && i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--transport") return run_fixture_child();
  }

  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("host_exit_upload_test\n");

  // Isolate LOCALAPPDATA (host_app.log, uploader diag, crash-dump probe) to a temp dir.
  wchar_t tmp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, tmp);
  const std::filesystem::path root =
      std::filesystem::path(tmp) / ("gnlink-exitup-" + std::to_string(GetCurrentProcessId()));
  std::filesystem::create_directories(root);
  SetEnvironmentVariableW(L"LOCALAPPDATA", root.wstring().c_str());
  gStateFile = (root / "launch-index.txt").wstring();
  SetEnvironmentVariableW(L"GNLINK_FIXTURE_STATE", gStateFile.c_str());

  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);
  FakeLogServer server;
  if (!server.Start()) { std::printf("could not start fake server\n"); return 2; }

  // Configure the process-wide uploader the product's enqueue uses, pointed at the fake server.
  LogUploadConfig cfg;
  cfg.directoryUrl = server.url();
  cfg.hostToken = "HOST-TOKEN";
  cfg.device = "test-device";
  cfg.identity = "acct/machine-A";
  cfg.flushIntervalMs = 50;
  std::string reason;
  check("uploader configures against the fake server", log_upload_configure(cfg, &reason), reason);

  const std::wstring exe = own_executable_path();
  gHostTest.streamExe = exe;  // the supervisor launches THIS exe (stand-in child) instead of GNLinkStream

  // ---- Session 1: long-run nonzero exit (the incident shape) + H1 two-child cross -------------
  {
    reset_launch_index();
    set_launch(0, "host-directory online CHILDA-marker", 15500, 0xC0000005UL);  // >15s, nonzero
    set_launch(1, "", 100, 0);                                                   // no output, clean
    StreamingHostProcess sp;
    sp.Configure(L"http://127.0.0.1:1/", L"acct", L"host-A");
    sp.Start();

    const bool sawBoth = wait_until([&] {
      const auto lines = exit_lines(server.body());
      bool a = false, b = false;
      for (const auto& l : lines) {
        if (l.find("code=0xC0000005") != std::string::npos) a = true;
        if (l.find("code=0x00000000 (0)") != std::string::npos &&
            l.find("lastLine=\"none\"") != std::string::npos) b = true;
      }
      return a && b;
    }, 30000);
    sp.Stop();

    const auto lines = exit_lines(server.body());
    std::string childA, childB;
    for (const auto& l : lines) {
      if (l.find("code=0xC0000005") != std::string::npos) childA = l;
      if (l.find("code=0x00000000 (0)") != std::string::npos &&
          l.find("lastLine=\"none\"") != std::string::npos)
        childB = l;
    }
    check("the real supervisor uploaded a long-run nonzero exit line (removal mutation of the product "
          "enqueue fails here)", sawBoth && !childA.empty(), childA);
    check("the long-run exit line carries the real code and is flagged ABNORMAL",
          childA.find("code=0xC0000005") != std::string::npos &&
              childA.find("(ABNORMAL: nonzero after a long run)") != std::string::npos, childA);
    check("the long-run exit line carries the child's marker as preceding state",
          childA.find("CHILDA-marker") != std::string::npos, childA);
    // H1: the no-output child's exit line says none and never inherits child A's marker.
    check("H1: a child that printed nothing logs lastLine=\"none\"", !childB.empty(), childB);
    bool anyCleanHasMarker = false;
    for (const auto& l : lines)
      if (l.find("code=0x00000000 (0)") != std::string::npos && l.find("CHILDA") != std::string::npos)
        anyCleanHasMarker = true;
    check("H1: the next child's exit line does NOT inherit the previous child's line "
          "(clear-removal mutation fails here)", !anyCleanHasMarker);
  }

  // ---- Session 2: Stop() -> restart_planned=false ---------------------------------------------
  {
    reset_launch_index();
    set_launch(0, "host-directory online LONGCHILD", 60000, 0);  // runs until Stop terminates it
    StreamingHostProcess sp;
    sp.Configure(L"http://127.0.0.1:1/", L"acct", L"host-B");
    sp.Start();
    const bool up = wait_until([&] {
      return sp.ChildAlive() && server.body().find("LONGCHILD") != std::string::npos;
    }, 15000);
    check("the long-lived stand-in child came up", up);
    sp.Stop();  // running_=false, then the child is terminated -> exit observed with no relaunch planned
    const bool sawStop = wait_until([&] {
      for (const auto& l : exit_lines(server.body()))
        if (l.find("restart_planned=false") != std::string::npos) return true;
      return false;
    }, 15000);
    check("H2: a Stop-terminated child logs restart_planned=false", sawStop);
    bool sawStarted = server.body().find("[host-app] streaming host started pid=") != std::string::npos &&
                      server.body().find("attempt=#1") != std::string::npos;
    check("H2: the actual launch is a separate started/attempt line", sawStarted);
  }

  // ---- the free-function append_host_app_log also reaches the server --------------------------
  {
    append_host_app_log("[host-app] direct-enqueue-check marker=DIRECTXYZ");
    const bool got = wait_until([&] { return server.body().find("DIRECTXYZ") != std::string::npos; }, 8000);
    check("append_host_app_log uploads on the host stream (its enqueue-removal mutation fails here)", got);
  }

  log_upload_stop();
  server.Stop();
  WSACleanup();
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::printf("\n%s  (%d checks, %d failed)\n", gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED",
              gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
