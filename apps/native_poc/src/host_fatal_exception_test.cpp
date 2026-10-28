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
//
// 2026-10-28 (Codex codex_test_injection_finding.md, fatal_exception): every host here gets
// `--input-injection-mode none`. The run() cases have no client at all (no socket): they die at a startup step
// before any input path -- their OFF line is not confirmed, and they are judged as startup faults with zero
// connections and zero input. The run_with_client() cases send a client Hello only AFTER this launch said the whole
// "input injection disabled (unsupported mode) mode=none" line; a host that dies before it gets no Hello at all; any
// other injection state ends the case at once, nothing sent (INVALID). Their logs and the pre-login log root live in
// a NEW folder of this run under the trusted test root (REMOTE60_TEST_TMP directly under
// D:/remote/remote/.claude/test-tmp, as host_agent_punch_test) -- the only place this test deletes in, and only
// inside that folder. (Before: logs beside the executable and a fatal_prelogin_logroot there, remove_all'd -- a
// recursive delete outside the repository for a build outside it.)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <sddl.h>

#include <thread>
#include <atomic>

#include "poc_protocol.hpp"
#include "product_version.hpp"
#include "e2e_injection_guard.hpp"

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
const wchar_t kTestAnchor[] = L"D:\\remote\\remote\\.claude\\test-tmp";
std::wstring gRun;  // this run's own new folder (with a trailing backslash): logs and the log root live in it

bool is_reparse(const std::wstring& p) {
  const DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

// REMOTE60_TEST_TMP: one plain folder directly under the anchor (no links); in it, a NEW folder of this run's own
// (CreateDirectory fails on any existing name). Checked before anything is created; refuses otherwise.
bool init_run_root(std::string* why) {
  wchar_t given[1024] = {};
  const DWORD n = GetEnvironmentVariableW(L"REMOTE60_TEST_TMP", given, 1024);
  if (n == 0 || n >= 1024) { *why = "REMOTE60_TEST_TMP is not set"; return false; }
  wchar_t full[1024] = {};
  wchar_t* namePart = nullptr;
  if (GetFullPathNameW(given, 1024, full, &namePart) == 0 || !namePart || !*namePart) {
    *why = "REMOTE60_TEST_TMP is not a usable path";
    return false;
  }
  const std::wstring name(namePart);
  for (wchar_t ch : name) {
    const bool ok = (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') ||
                    ch == L'-' || ch == L'_' || ch == L'.';
    if (!ok || name == L"." || name == L"..") { *why = "the test root's name must be a plain ASCII folder name"; return false; }
  }
  std::wstring parent(full, namePart - full);
  while (!parent.empty() && (parent.back() == L'\\' || parent.back() == L'/')) parent.pop_back();
  if (_wcsicmp(parent.c_str(), kTestAnchor) != 0) {
    *why = "REMOTE60_TEST_TMP must be a folder directly under D:/remote/remote/.claude/test-tmp";
    return false;
  }
  const DWORD anchorAttr = GetFileAttributesW(kTestAnchor);
  if (anchorAttr == INVALID_FILE_ATTRIBUTES || !(anchorAttr & FILE_ATTRIBUTE_DIRECTORY) || is_reparse(kTestAnchor)) {
    *why = "the test anchor is missing or a link";
    return false;
  }
  const std::wstring root = std::wstring(kTestAnchor) + L"\\" + name;
  if (!CreateDirectoryW(root.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
    *why = "the test root could not be created";
    return false;
  }
  const DWORD rootAttr = GetFileAttributesW(root.c_str());
  if (rootAttr == INVALID_FILE_ATTRIBUTES || !(rootAttr & FILE_ATTRIBUTE_DIRECTORY) || (rootAttr & FILE_ATTRIBUTE_REPARSE_POINT)) {
    *why = "the test root is not a plain folder";
    return false;
  }
  for (int attempt = 0; attempt < 16; ++attempt) {
    const std::wstring run = root + L"\\fatal-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                             std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt);
    if (CreateDirectoryW(run.c_str(), nullptr)) {
      if (is_reparse(run)) { *why = "this run's folder is a link"; return false; }
      gRun = run + L"\\";
      return true;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) break;
  }
  *why = "no new folder for this run could be made";
  return false;
}

// The one recursive delete: a path strictly inside THIS run's own new folder, no ".." in it, no link on the way.
void remove_inside_run(const std::wstring& path) {
  std::error_code ec;
  const std::filesystem::path canon = std::filesystem::weakly_canonical(path, ec);
  const std::wstring c = canon.wstring();
  if (gRun.empty() || ec || c.size() <= gRun.size() || _wcsnicmp(c.c_str(), gRun.c_str(), gRun.size()) != 0 ||
      c.find(L"..") != std::wstring::npos || is_reparse(c)) {
    std::printf("FAIL  refused to delete outside this run's folder\n");
    ++gFailures;
    return;
  }
  std::filesystem::remove_all(canon, ec);
}

// Runs the host with `step` injected (empty = none); returns the exit code (or 0xFFFFFFFF on timeout).
DWORD run(const std::wstring& dir, const std::wstring& tag, const std::string& step, std::string* out, int timeoutMs) {
  const std::wstring log = gRun + L"fatal_" + tag + L".log";
  SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", step.empty() ? nullptr : step.c_str());
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE h = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = h;
  si.hStdError = h;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  // a port nobody uses, a short run; the injected steps come before any wait for a client. NO client: no socket is
  // opened here at all -- a startup-fault case (its OFF line not confirmed; zero connections, zero input)
  std::wstring cmd = L"\"" + dir + L"GNLinkStreamMigrationTest.exe\" --transport udp --codec h264 --seconds 3" +
                     remote60::native_poc::e2e::e2e_injection_off_arg(dir);
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
constexpr DWORD kInjectionInvalid = 0xFFFFFFFEu;  // the injection gate refused: the case is INVALID, nothing sent
DWORD run_with_client(const std::wstring& dir, const std::wstring& tag, const std::vector<std::pair<std::string, std::string>>& env,
                      const std::string& seenNeedle, std::string* out, int* hellosSent) {
  *hellosSent = 0;
  const uint16_t port = free_udp_port();
  const std::wstring log = gRun + L"fatal_" + tag + L".log";
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
                     L" --seconds 20" + remote60::native_poc::e2e::e2e_injection_off_arg(dir);
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
      // the gate: a Hello only after THIS launch said its whole input-injection-OFF line; another state -> the
      // case ends here, nothing sent (a host that dies before saying it simply never gets one)
      std::string injWhy;
      const int inj = remote60::native_poc::e2e::e2e_injection_state(read_all(log), &injWhy);
      if (inj < 0) {
        TerminateProcess(pi.hProcess, 0xFFFFFFFFu);
        WaitForSingleObject(pi.hProcess, 5000);
        code = kInjectionInvalid;
        std::printf("FAIL  SETUP %ls: input injection not provably off (%s) -- no Hello sent\n", tag.c_str(), injWhy.c_str());
        break;
      }
      if (inj == 0) continue;  // not yet said: no Hello
      ++*hellosSent;
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
  {
    std::string why;
    if (!init_run_root(&why)) {
      std::printf("FAIL  no valid test root (%s): refused to run -- nothing created or deleted\nRESULT: FAILED\n", why.c_str());
      return 2;
    }
  }

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

  // ---- r3 (D3 through the worker's own main): --prelogin with the test log root, a throw at the first step ->
  // exit 49, and the worker's log holds its header and the exit-49 line (the sink installed first thing, drained at
  // main()'s return). No client is needed: the throw comes before any wait.
  {
    // inside this run's own new folder: nothing to clear first
    const std::wstring root = gRun + L"fatal_prelogin_logroot";
    const std::wstring logs = root + L"\\GNLink\\logs";
    CreateDirectoryW(root.c_str(), nullptr);
    {
      // the folders as the product makes them, naming this user (the test build's owner rule trusts it)
      HANDLE tok = nullptr;
      OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
      BYTE buf[256];
      DWORD n = 0;
      GetTokenInformation(tok, TokenUser, buf, sizeof(buf), &n);
      CloseHandle(tok);
      LPWSTR sid = nullptr;
      ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf)->User.Sid, &sid);
      const std::wstring sddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;" + std::wstring(sid) + L")";
      LocalFree(sid);
      for (const std::wstring& d : {root + L"\\GNLink", logs}) {
        PSECURITY_DESCRIPTOR sd = nullptr;
        ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr);
        SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
        CreateDirectoryW(d.c_str(), &sa);
        LocalFree(sd);
      }
    }
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_LOG_ROOT", root.c_str());
    SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", "startup_configure_from_env");
    const std::wstring log = gRun + L"fatal_prelogin.log";
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE h = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = h;
    si.hStdError = h;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    std::wstring cmd = L"\"" + dir + L"GNLinkStreamMigrationTest.exe\" --prelogin";
    PROCESS_INFORMATION pi{};
    DWORD pcode = 0xFFFFFFFFu;
    bool ended = false;
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi)) {
      ended = WaitForSingleObject(pi.hProcess, 20000) == WAIT_OBJECT_0;
      if (ended) GetExitCodeProcess(pi.hProcess, &pcode);
      else TerminateProcess(pi.hProcess, 99);
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
    }
    CloseHandle(h);
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_LOG_ROOT", nullptr);
    SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", nullptr);
    const std::string wl = read_all(logs + L"\\prelogin_worker.log");
    check("--prelogin worker, throw at the first step: exit 49 by itself", ended && pcode == 49, "exit " + std::to_string(pcode));
    check("...its own log: the start header, then the exit-49 line naming the step (written before main returned)",
          wl.find("=== GNLinkStream --prelogin worker start") != std::string::npos &&
              wl.find("FATAL: std::exception \"stream-test-seam: injected throw\" at step=startup_configure_from_env -- exit 49") !=
                  std::string::npos &&
              wl.find("=== GNLinkStream --prelogin worker start") < wl.find("FATAL:"),
          std::to_string(wl.size()) + " bytes");
    {
      const std::wstring v = remote60::native_poc::kProductVersion;
      check("...the header names this build's version (the one kProductVersion)",
            wl.find("version=" + std::string(v.begin(), v.end()) + " ===") != std::string::npos, std::string(v.begin(), v.end()));
    }
    check("...and the same line still reached the stdout it was started with (the test's)",
          read_all(log).find("at step=startup_configure_from_env -- exit 49") != std::string::npos);
    remove_inside_run(root);
  }

  // ---- D2: the ordinary path's two WinRT calls ----
  int hellos = 0;
  code = run_with_client(dir, L"probe_trap", {{"GNLINK_STREAM_TEST_WINRT_TRAP", "probe"}}, "", &out, &hellos);
  check("...the injection gate held (no Hello before the OFF line)", code != kInjectionInvalid);
  check("ordinary path, probe trap: exit 49 naming IsSupported at startup_init_graphics (the trap is live here)",
        code == 49 && out.find("WinRT trap reached: GraphicsCaptureSession::IsSupported\" at step=startup_init_graphics") != std::string::npos,
        "exit " + std::to_string(code));
  code = run_with_client(dir, L"wrapper_trap", {{"GNLINK_STREAM_TEST_WINRT_TRAP", "wrapper"}}, "", &out, &hellos);
  check("...the injection gate held (no Hello before the OFF line)", code != kInjectionInvalid);
  check("ordinary path, wrapper trap: exit 49 naming the D3D wrapper at startup_init_encoder (the ordinary path keeps it)",
        code == 49 && out.find("WinRT trap reached: CreateDirect3D11DeviceFromDXGIDevice\" at step=startup_init_encoder") != std::string::npos,
        "exit " + std::to_string(code));
  code = run_with_client(dir, L"wgc_throw", {{"GNLINK_STREAM_TEST_WGC_SUPPORTED", "throw"}}, "", &out, &hellos);
  check("...the injection gate held (no Hello before the OFF line)", code != kInjectionInvalid);
  check("IsSupported throws: the explicit 'not supported' exit 6 with the HRESULT -- not 49, not taken as supported",
        code == 6 && out.find("WGC not supported (IsSupported threw hr=0x80070005)") != std::string::npos &&
            out.find("FATAL:") == std::string::npos,
        "exit " + std::to_string(code));
  code = run_with_client(dir, L"wgc_false", {{"GNLINK_STREAM_TEST_WGC_SUPPORTED", "false"}}, "", &out, &hellos);
  check("...the injection gate held (no Hello before the OFF line)", code != kInjectionInvalid);
  check("IsSupported false: exit 6, 'WGC not supported' (as before)",
        code == 6 && out.find("WGC not supported") != std::string::npos && out.find("threw") == std::string::npos,
        "exit " + std::to_string(code));
  code = run_with_client(dir, L"normal", {}, "H264 encoder backend=", &out, &hellos);
  check("...the injection gate held (no Hello before the OFF line)", code != kInjectionInvalid);
  check("nothing injected: the ordinary path passes both calls (the encoder line is reached), no FATAL",
        out.find("H264 encoder backend=") != std::string::npos && out.find("FATAL:") == std::string::npos &&
            out.find("WGC not supported") == std::string::npos);

  // this run's own logs go with its folder (bounded to it); anything unexpected in it stays
  for (const wchar_t* t : {L"std", L"winrt", L"none", L"probe_trap", L"wrapper_trap", L"wgc_throw", L"wgc_false", L"normal"}) {
    DeleteFileW((gRun + L"fatal_" + t + L".log").c_str());
  }
  DeleteFileW((gRun + L"fatal_prelogin.log").c_str());
  RemoveDirectoryW(gRun.c_str());
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
