// prelogin-field D2: when the streaming host's main thread throws, its exit 49 says what threw and at which
// step. The 0.2.160 pre-login worker died with 49 after each viewer connect and nothing said where.
//
// The host is GNLinkStreamMigrationTest (the product's sources; REMOTE60_STREAM_TEST_SEAM adds
// GNLINK_STREAM_TEST_THROW_AT_STEP, which throws at the named step). Each case starts it with no client and a
// step that comes before any network wait, and reads its output and exit code. The catch, the step names and
// the line are the product's (native_video_host_main.cpp); only the throw is injected.
//   std   std::runtime_error at startup_configure_from_env  -> FATAL std::exception "...", step named, 49
//   winrt winrt::hresult_error at startup_configure_from_env         -> FATAL winrt::hresult_error hr=0x80070005, 49
//   none  no injection (control): no FATAL line from the injection path
// prelogin-field D2 -- the two WinRT calls of startup on the ORDINARY path (the pre-login path is the login e2e's, run
// with the same trap): a client's Hello (plain UDP, as a LAN client sends it) takes the host past the connect, then
//   probe-trap    GNLINK_STREAM_TEST_WINRT_TRAP=probe   -> exit 49 naming IsSupported at startup_init_graphics
//                 (the trap is live on this path: a pre-login run that passes with it set did not make the call)
//   wrapper-trap  GNLINK_STREAM_TEST_WINRT_TRAP=wrapper -> exit 49 naming the D3D wrapper at startup_init_encoder
//   wgc-throw     IsSupported throws  -> exit 6, "WGC not supported (IsSupported threw hr=0x80070005)", no FATAL
//   wgc-false     IsSupported false   -> exit 6, "WGC not supported", no "threw"
//   normal        nothing injected    -> past both (the encoder line), no FATAL

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <thread>
#include <atomic>

#include "poc_protocol.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
int gChecks = 0, gFailures = 0;
void check(const std::string& what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
}
std::string read_all(const std::wstring& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
// Runs the host with `step` injected (empty = none); returns the exit code (or 0xFFFFFFFF on timeout).
DWORD run(const std::wstring& dir, const std::wstring& tag, const std::string& step, std::string* out, int timeoutMs) {
  const std::wstring log = dir + L"fatal_" + tag + L".log";
  SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", step.empty() ? nullptr : step.c_str());
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE h = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = h;
  si.hStdError = h;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  // a port nobody uses, a short run; the injected steps come before any wait for a client
  std::wstring cmd = L"\"" + dir + L"GNLinkStreamMigrationTest.exe\" --transport udp --codec h264 --seconds 3";
  PROCESS_INFORMATION pi{};
  DWORD code = 0xFFFFFFFFu;
  if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi)) {
    if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0) {
      GetExitCodeProcess(pi.hProcess, &code);
    } else {
      TerminateProcess(pi.hProcess, 99);
      WaitForSingleObject(pi.hProcess, 5000);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
  CloseHandle(h);
  SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", nullptr);
  *out = read_all(log);
  return code;
}
uint16_t free_udp_port() {
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
  int n = sizeof(a);
  getsockname(s, reinterpret_cast<sockaddr*>(&a), &n);
  closesocket(s);
  return ntohs(a.sin_port);
}

// The host on `port` with `env` set (the product's stream switches always), a client Hello every 200 ms until it
// ends or `seenNeedle` appears; returns the exit code (0xFFFFFFFF: killed at the deadline -- normal for "normal").
DWORD run_with_client(const std::wstring& dir, const std::wstring& tag, const std::vector<std::pair<std::string, std::string>>& env,
                      const std::string& seenNeedle, std::string* out) {
  const uint16_t port = free_udp_port();
  const std::wstring log = dir + L"fatal_" + tag + L".log";
  for (const auto& kv : env) SetEnvironmentVariableA(kv.first.c_str(), kv.second.c_str());
  SetEnvironmentVariableA("REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", "1");
  SetEnvironmentVariableA("REMOTE60_NATIVE_ENCODER_TUNE_MODE", "low_latency");
  SetEnvironmentVariableA("REMOTE60_NATIVE_NV12_SURFACE", "0");
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE h = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = h;
  si.hStdError = h;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  std::wstring cmd = L"\"" + dir + L"GNLinkStreamMigrationTest.exe\" --transport udp --codec h264 --bind-port " + std::to_wstring(port) + L" --control-port " + std::to_wstring(free_udp_port()) +
                     L" --seconds 20";
  PROCESS_INFORMATION pi{};
  DWORD code = 0xFFFFFFFFu;
  if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi)) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    remote60::native_poc::UdpHelloPacket hello{};
    hello.magic = remote60::native_poc::kMagic;
    hello.kind = static_cast<uint16_t>(remote60::native_poc::UdpPacketKind::Hello);
    hello.version = remote60::native_poc::kUdpProtocolVersion;
    hello.features = remote60::native_poc::kUdpFeatureVideoFec;
    const ULONGLONG deadline = GetTickCount64() + 25000;
    for (;;) {
      if (WaitForSingleObject(pi.hProcess, 200) == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &code);
        break;
      }
      if (!seenNeedle.empty() && read_all(log).find(seenNeedle) != std::string::npos) {
        TerminateProcess(pi.hProcess, 0xFFFFFFFFu);
        WaitForSingleObject(pi.hProcess, 5000);
        break;
      }
      if (GetTickCount64() > deadline) {
        TerminateProcess(pi.hProcess, 0xFFFFFFFFu);
        WaitForSingleObject(pi.hProcess, 5000);
        break;
      }
      sendto(s, reinterpret_cast<const char*>(&hello), sizeof(hello), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
    }
    closesocket(s);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
  CloseHandle(h);
  for (const auto& kv : env) SetEnvironmentVariableA(kv.first.c_str(), nullptr);
  *out = read_all(log);
  return code;
}
}  // namespace

int wmain() {
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("the streaming host's exit 49 names what threw and where (prelogin-field D2)\n\n");
  wchar_t self[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  std::wstring dir(self);
  dir = dir.substr(0, dir.find_last_of(L'\\') + 1);
  check("the stream test build is beside this test", std::filesystem::exists(dir + L"GNLinkStreamMigrationTest.exe"));

  std::string out;
  DWORD code = run(dir, L"std", "startup_configure_from_env", &out, 20000);
  check("std: exit 49", code == 49, std::to_string(code));
  check("std: the line says std::exception, its what(), and the step",
        out.find("FATAL: std::exception \"stream-test-seam: injected throw\" at step=startup_configure_from_env -- exit 49") !=
            std::string::npos);

  code = run(dir, L"winrt", "winrt:startup_configure_from_env", &out, 20000);
  check("winrt: exit 49", code == 49, std::to_string(code));
  check("winrt: the line says winrt::hresult_error, its HRESULT, its message, and the step",
        out.find("FATAL: winrt::hresult_error hr=0x80070005 \"stream-test-seam: injected winrt\" at step=startup_configure_from_env") !=
            std::string::npos);

  code = run(dir, L"none", "", &out, 30000);
  check("control (nothing injected): no FATAL line", out.find("FATAL:") == std::string::npos, "exit " + std::to_string(code));

  // ---- D2: the ordinary path's two WinRT calls ----
  code = run_with_client(dir, L"probe_trap", {{"GNLINK_STREAM_TEST_WINRT_TRAP", "probe"}}, "", &out);
  check("ordinary path, probe trap: exit 49 naming IsSupported at startup_init_graphics (the trap is live here)",
        code == 49 && out.find("WinRT trap reached: GraphicsCaptureSession::IsSupported\" at step=startup_init_graphics") != std::string::npos,
        "exit " + std::to_string(code));
  code = run_with_client(dir, L"wrapper_trap", {{"GNLINK_STREAM_TEST_WINRT_TRAP", "wrapper"}}, "", &out);
  check("ordinary path, wrapper trap: exit 49 naming the D3D wrapper at startup_init_encoder (the ordinary path keeps it)",
        code == 49 && out.find("WinRT trap reached: CreateDirect3D11DeviceFromDXGIDevice\" at step=startup_init_encoder") != std::string::npos,
        "exit " + std::to_string(code));
  code = run_with_client(dir, L"wgc_throw", {{"GNLINK_STREAM_TEST_WGC_SUPPORTED", "throw"}}, "", &out);
  check("IsSupported throws: the explicit 'not supported' exit 6 with the HRESULT -- not 49, not taken as supported",
        code == 6 && out.find("WGC not supported (IsSupported threw hr=0x80070005)") != std::string::npos &&
            out.find("FATAL:") == std::string::npos,
        "exit " + std::to_string(code));
  code = run_with_client(dir, L"wgc_false", {{"GNLINK_STREAM_TEST_WGC_SUPPORTED", "false"}}, "", &out);
  check("IsSupported false: exit 6, 'WGC not supported' (as before)",
        code == 6 && out.find("WGC not supported") != std::string::npos && out.find("threw") == std::string::npos,
        "exit " + std::to_string(code));
  code = run_with_client(dir, L"normal", {}, "H264 encoder backend=", &out);
  check("nothing injected: the ordinary path passes both calls (the encoder line is reached), no FATAL",
        out.find("H264 encoder backend=") != std::string::npos && out.find("FATAL:") == std::string::npos &&
            out.find("WGC not supported") == std::string::npos);

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
