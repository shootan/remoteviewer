// ka-pc-impl r1 (KA ③): the managed standby viewer end to end, with this test playing the client shell's part.
//
// Real processes, loopback only:
//  - apps/directory/server.js under node (relay with host-facing sockets on 127.0.0.1, as in the standby e2e);
//  - GNLinkStream of this build (standby-capable) registered as "KA-A" -- and, for the unsupported case, the PUBLISHED
//    0.2.163 GNLinkStream (REMOTE60_KA_OLD_STREAM) registered as "KA-OLD";
//  - GNLinkViewerKaTest --managed-standby (the product viewer + the test window seam: its window is placed beyond
//    the right edge of the virtual screen and shown without focus), driven over the same two private pipes the
//    shell uses (managed_viewer_ipc.hpp): Init, Activate, Park, Release.
// Everything this test writes -- server data and logs, host caches, LOCALAPPDATA of every child, captures -- goes
// under REMOTE60_TEST_TMP (one folder directly under D:/remote/remote/.claude/test-tmp), and the only deletes are of
// that run's own folder content at the end (none: kept as evidence). Every child is in a job of this process.
//
// The rendered evidence is a Windows.Graphics.Capture image of the viewer window (what DWM composes for it): the
// product picker after Activate, and the first picture after the product picker's desktop card is pressed (the
// mouse messages a press produces, at the card the viewer's own log reports).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "directory_session_client.hpp"
#include "e2e_isolation.hpp"
#include "e2e_window_capture.hpp"
#include "managed_viewer_ipc.hpp"
#include "native_socket.hpp"

using namespace remote60::native_poc;
using managed_ipc::Message;
using managed_ipc::Type;

namespace {

int gFailures = 0;
void check(const std::string& name, bool ok, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
  if (!ok) ++gFailures;
}
uint64_t now_ms() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}
std::wstring W(const std::string& s) { return std::wstring(s.begin(), s.end()); }
std::string N(const std::wstring& s) {
  std::string o;
  for (wchar_t c : s) o.push_back(static_cast<char>(c));
  return o;
}

std::wstring gRoot;  // the test root (validated), with a trailing backslash
std::wstring gBuild;  // this executable's directory, with a trailing backslash

bool init_root(std::string* why) {
  wchar_t given[1024] = {};
  if (!GetEnvironmentVariableW(L"REMOTE60_TEST_TMP", given, 1024)) { *why = "REMOTE60_TEST_TMP is not set"; return false; }
  wchar_t full[1024] = {};
  wchar_t* name = nullptr;
  if (!GetFullPathNameW(given, 1024, full, &name) || !name || !*name) { *why = "unusable REMOTE60_TEST_TMP"; return false; }
  std::wstring parent(full, name - full);
  while (!parent.empty() && (parent.back() == L'\\' || parent.back() == L'/')) parent.pop_back();
  if (_wcsicmp(parent.c_str(), L"D:\\remote\\remote\\.claude\\test-tmp") != 0) {
    *why = "REMOTE60_TEST_TMP must be a folder directly under D:/remote/remote/.claude/test-tmp";
    return false;
  }
  const DWORD a = GetFileAttributesW(full);
  if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT)) { *why = "the test root is a link"; return false; }
  CreateDirectoryW(full, nullptr);
  gRoot = std::wstring(full) + L"\\";
  wchar_t self[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  std::wstring s(self);
  gBuild = s.substr(0, s.find_last_of(L"\\/") + 1);
  return true;
}

// One child process: in a job of ours (KILL_ON_JOB_CLOSE), its stdout/stderr collected.
struct Child {
  PROCESS_INFORMATION pi{};
  HANDLE job = nullptr;
  HANDLE out = nullptr;
  std::thread reader;
  std::mutex mu;
  std::string text;
  std::wstring logPath;
  bool Start(const std::wstring& cmd, const std::wstring& dir, const std::vector<wchar_t>* env,
             std::vector<HANDLE> extraInherit = {}) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE w = nullptr;
    if (!CreatePipe(&out, &w, &sa, 1 << 20)) return false;
    SetHandleInformation(out, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    std::vector<HANDLE> inherit = {w, nul};
    for (HANDLE h : extraInherit) inherit.push_back(h);
    SIZE_T sz = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &sz);
    std::vector<char> buf(sz);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buf.data());
    InitializeProcThreadAttributeList(attrs, 1, 0, &sz);
    UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit.data(), inherit.size() * sizeof(HANDLE),
                              nullptr, nullptr);
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdOutput = w;
    si.StartupInfo.hStdError = w;
    si.StartupInfo.hStdInput = nul;
    si.lpAttributeList = attrs;
    std::vector<wchar_t> c(cmd.begin(), cmd.end());
    c.push_back(0);
    const BOOL ok = CreateProcessW(nullptr, c.data(), nullptr, nullptr, TRUE,
                                   CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT |
                                       (env ? CREATE_UNICODE_ENVIRONMENT : 0),
                                   env ? const_cast<wchar_t*>(env->data()) : nullptr, dir.c_str(), &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(attrs);
    CloseHandle(w);
    CloseHandle(nul);
    if (!ok) return false;
    job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim{};
    lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &lim, sizeof(lim));
    if (!AssignProcessToJobObject(job, pi.hProcess)) {
      TerminateProcess(pi.hProcess, 1);
      return false;
    }
    ResumeThread(pi.hThread);
    reader = std::thread([this] {
      char b[4096];
      DWORD got = 0;
      while (ReadFile(out, b, sizeof(b), &got, nullptr) && got > 0) {
        std::lock_guard<std::mutex> lock(mu);
        text.append(b, got);
      }
    });
    return true;
  }
  std::string Text() {
    std::lock_guard<std::mutex> lock(mu);
    return text;
  }
  bool WaitFor(const std::function<bool(const std::string&)>& pred, int ms) {
    const uint64_t until = now_ms() + ms;
    while (now_ms() < until) {
      if (pred(Text())) return true;
      Sleep(100);
    }
    return pred(Text());
  }
  bool Exited(int ms) { return pi.hProcess && WaitForSingleObject(pi.hProcess, ms) == WAIT_OBJECT_0; }
  void End() {
    if (job) {
      TerminateJobObject(job, 0);
      CloseHandle(job);
      job = nullptr;
    }
    if (pi.hProcess) WaitForSingleObject(pi.hProcess, 3000);
    if (reader.joinable()) reader.join();
    if (out) CloseHandle(out), out = nullptr;
    if (!logPath.empty()) {
      HANDLE f = CreateFileW(logPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
      if (f != INVALID_HANDLE_VALUE) {
        const std::string t = Text();
        DWORD put = 0;
        WriteFile(f, t.data(), static_cast<DWORD>(t.size()), &put, nullptr);
        CloseHandle(f);
      }
    }
    if (pi.hThread) CloseHandle(pi.hThread), pi.hThread = nullptr;
    if (pi.hProcess) CloseHandle(pi.hProcess), pi.hProcess = nullptr;
  }
  ~Child() { End(); }
};

std::vector<wchar_t> env_with(const std::wstring& localAppData, const std::vector<std::pair<std::wstring, std::wstring>>& add) {
  for (const auto& kv : add) SetEnvironmentVariableW(kv.first.c_str(), kv.second.c_str());
  CreateDirectoryW(localAppData.c_str(), nullptr);
  return e2e::e2e_isolated_environment(localAppData);
}

// The managed viewer, with this test as its shell.
struct Managed {
  Child proc;
  managed_ipc::Channel ch;
  std::string key, inc, epoch;
  std::mutex mu;
  std::vector<Message> seen;
  std::thread pump;
  std::atomic<bool> stop{false};

  bool Start(const std::string& url, const std::string& hostId, const std::string& tag) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE toChildR, toChildW, fromChildR, fromChildW;
    CreatePipe(&toChildR, &toChildW, &sa, 1 << 16);
    CreatePipe(&fromChildR, &fromChildW, &sa, 1 << 16);
    SetHandleInformation(toChildW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(fromChildR, HANDLE_FLAG_INHERIT, 0);
    const std::wstring dir = gRoot + L"viewer-" + W(tag) + L"\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    Add_offscreen();
    const auto env = env_with(dir + L"lad", {});
    const std::wstring cmd = L"\"" + gBuild + L"GNLinkViewerKaTest.exe\" --transport udp --codec h264 --enable-input-channel"
                             L" --initial-view targets --directory-url \"" + W(url) + L"\" --directory-host-id \"" + W(hostId) +
                             L"\" --managed-standby --ipc-read " + std::to_wstring(reinterpret_cast<uintptr_t>(toChildR)) +
                             L" --ipc-write " + std::to_wstring(reinterpret_cast<uintptr_t>(fromChildW));
    proc.logPath = dir + L"viewer.log";
    const bool ok = proc.Start(cmd, gBuild, &env, {toChildR, fromChildW});
    CloseHandle(toChildR);
    CloseHandle(fromChildW);
    if (!ok) return false;
    ch.Start(fromChildR, toChildW);
    pump = std::thread([this] {
      while (!stop.load()) {
        Message m;
        if (ch.Receive(&m, 100)) {
          std::lock_guard<std::mutex> lock(mu);
          seen.push_back(m);
        } else if (!ch.open()) {
          Sleep(50);
        }
      }
    });
    return true;
  }
  static void Add_offscreen() {
    const int right = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    SetEnvironmentVariableW(L"REMOTE60_VIEWER_TEST_WINDOW_X", std::to_wstring(right + 200).c_str());
    SetEnvironmentVariableW(L"REMOTE60_VIEWER_TEST_WINDOW_Y", std::to_wstring(GetSystemMetrics(SM_YVIRTUALSCREEN) + 40).c_str());
  }
  bool Send(Type t, std::map<std::string, std::string> f) {
    Message m;
    m.type = t;
    m.fields = std::move(f);
    m.fields["key"] = key;
    m.fields["inc"] = inc;
    m.fields["epoch"] = epoch;
    return ch.Send(m);
  }
  // Waits for a message of `t` (from position `from` in `seen`) matching `pred`; returns its index or -1.
  int WaitMsg(Type t, size_t from, int ms, const std::function<bool(const Message&)>& pred = nullptr) {
    const uint64_t until = now_ms() + ms;
    for (;;) {
      {
        std::lock_guard<std::mutex> lock(mu);
        for (size_t i = from; i < seen.size(); ++i)
          if (seen[i].type == t && (!pred || pred(seen[i]))) return static_cast<int>(i);
      }
      if (now_ms() >= until) return -1;
      Sleep(50);
    }
  }
  Message At(int i) {
    std::lock_guard<std::mutex> lock(mu);
    return seen[static_cast<size_t>(i)];
  }
  size_t Count() {
    std::lock_guard<std::mutex> lock(mu);
    return seen.size();
  }
  void End() {
    stop = true;
    if (pump.joinable()) pump.join();
    ch.Close();
    proc.End();
  }
  ~Managed() { End(); }
};

HWND viewer_window(DWORD pid) {
  struct Find {
    DWORD pid;
    HWND hwnd;
  } f{pid, nullptr};
  EnumWindows(
      [](HWND h, LPARAM p) -> BOOL {
        auto* f = reinterpret_cast<Find*>(p);
        DWORD wp = 0;
        GetWindowThreadProcessId(h, &wp);
        wchar_t cls[64] = {};
        GetClassNameW(h, cls, 64);
        if (wp == f->pid && wcscmp(cls, L"Remote60NativeVideoClient") == 0 && IsWindowVisible(h)) {
          f->hwnd = h;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&f));
  return f.hwnd;
}

// How varied a capture is (distinct colors in a coarse sample): a blank or a single fill reads as 1-2.
size_t variety(const e2e::WindowCapture& c) {
  std::vector<uint32_t> seen;
  for (int y = 0; y < c.h; y += 7)
    for (int x = 0; x < c.w; x += 7) {
      const uint32_t v = c.px[static_cast<size_t>(y) * c.w + x] & 0xF0F0F0;
      if (std::find(seen.begin(), seen.end(), v) == seen.end()) seen.push_back(v);
      if (seen.size() > 64) return seen.size();
    }
  return seen.size();
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::string why;
  if (!init_root(&why)) {
    std::printf("FAIL  no test root: %s\n", why.c_str());
    return 2;
  }
  std::string socketError;
  initialize_sockets(&socketError);
  std::printf("test root: %s\n", N(gRoot).c_str());

  // ---------------------------------------------------------------- the directory
  wchar_t nodeExe[MAX_PATH] = {};
  if (!SearchPathW(nullptr, L"node.exe", nullptr, MAX_PATH, nodeExe, nullptr)) {
    std::printf("FAIL  node.exe is not on PATH\n");
    return 2;
  }
  wchar_t repoBuf[1024] = {};
  GetEnvironmentVariableW(L"REMOTE60_KA_REPO", repoBuf, 1024);
  const std::wstring serverJs = std::wstring(repoBuf) + L"\\apps\\directory\\server.js";
  if (GetFileAttributesW(serverJs.c_str()) == INVALID_FILE_ATTRIBUTES) {
    std::printf("FAIL  REMOTE60_KA_REPO must point at the worktree (server.js)\n");
    return 2;
  }
  const uint16_t httpPort = e2e::e2e_pick_free_tcp_port();
  const uint16_t udpPort = e2e::e2e_pick_free_udp_port();
  const uint16_t relayPort = e2e::e2e_pick_free_udp_port();
  const std::wstring dirData = gRoot + L"directory\\";
  CreateDirectoryW(dirData.c_str(), nullptr);
  const auto dirEnv = env_with(dirData + L"lad", {{L"REMOTE60_DIR_DATA", dirData + L"directory.json"},
                                                  {L"REMOTE60_LOG_DIR", dirData + L"logs"},
                                                  {L"REMOTE60_DIR_PORT", std::to_wstring(httpPort)},
                                                  {L"REMOTE60_DIR_UDP_PORT", std::to_wstring(udpPort)},
                                                  {L"REMOTE60_RELAY_ENABLED", L"1"},
                                                  {L"REMOTE60_RELAY_IP", L"127.0.0.1"},
                                                  {L"REMOTE60_RELAY_PORT", std::to_wstring(relayPort)},
                                                  {L"REMOTE60_RELAY_GRACE_MS", L"400"},
                                                  {L"REMOTE60_RELAY_ALLOW_IPS", L"127.0.0.1"},
                                                  {L"REMOTE60_RELAY_ALLOW_ACCOUNTS", L"tester"},
                                                  {L"REMOTE60_RELAY_HOST_FACING_IP", L"127.0.0.1"},
                                                  {L"REMOTE60_RELAY_HOST_FACING_BIND", L"127.0.0.1"}});
  {
    Child add;
    add.Start(L"\"" + std::wstring(nodeExe) + L"\" \"" + serverJs + L"\" --add-account tester e2e-pass-1234", dirData, &dirEnv);
    add.Exited(15000);
  }
  Child server;
  server.logPath = dirData + L"server.log";
  check("the directory starts", server.Start(L"\"" + std::wstring(nodeExe) + L"\" \"" + serverJs + L"\"", dirData, &dirEnv));
  Sleep(1500);
  const std::string url = "http://127.0.0.1:" + std::to_string(httpPort);

  // ---------------------------------------------------------------- hosts
  auto startHost = [&](Child& h, const std::wstring& exe, const std::string& name) {
    const std::wstring dir = gRoot + L"host-" + W(name) + L"\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    CopyFileW(exe.c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE);
    const auto env = env_with(dir + L"lad", {});
    const uint16_t media = e2e::e2e_pick_free_udp_port(), control = e2e::e2e_pick_free_tcp_port();
    h.logPath = dir + L"host.log";
    return h.Start(L"\"" + dir + L"GNLinkStream.exe\" --transport udp --codec h264 --bind-address 127.0.0.1 --bind-port " +
                       std::to_wstring(media) + L" --control-port " + std::to_wstring(control) + L" --directory-url " + W(url) +
                       L" --directory-id tester --directory-pw e2e-pass-1234 --host-name " + W(name) +
                       L" --directory-cache \"" + dir + L"cache.json\" --seconds 900",
                   dir, &env);
  };
  Child hostA;
  check("host KA-A (this build) starts", startHost(hostA, gBuild + L"GNLinkStream.exe", "KA-A"));
  wchar_t oldStream[1024] = {};
  const bool haveOld = GetEnvironmentVariableW(L"REMOTE60_KA_OLD_STREAM", oldStream, 1024) > 0;
  Child hostOld;
  if (haveOld) check("host KA-OLD (the published 0.2.163) starts", startHost(hostOld, oldStream, "KA-OLD"));

  std::string session;
  directory::ObserveEndpoint adv;
  std::string err;
  std::string idA, idOld;
  const uint64_t listUntil = now_ms() + 30000;
  while (now_ms() < listUntil) {
    if (session.empty()) directory_login(url, "tester", "e2e-pass-1234", &session, &err, &adv);
    std::vector<DirectoryHostEntry> hosts;
    if (!session.empty() && directory_list_hosts(url, session, &hosts, &err)) {
      for (const auto& h : hosts) {
        if (h.hostName == "KA-A" && h.online) idA = h.hostId;
        if (h.hostName == "KA-OLD" && h.online) idOld = h.hostId;
      }
    }
    if (!idA.empty() && (!haveOld || !idOld.empty())) break;
    Sleep(500);
  }
  check("both hosts are online in the directory", !idA.empty() && (!haveOld || !idOld.empty()), "A=" + idA + " old=" + idOld);
  if (idA.empty()) return 1;

  // ---------------------------------------------------------------- the managed viewer of KA-A
  Managed a;
  a.key = url + "\ntester\n" + idA;
  a.inc = "1";
  a.epoch = "1";
  check("the managed viewer starts (suspended, in its job, then resumed)", a.Start(url, idA, "A"));
  const std::string genDir = N(gRoot) + "viewer-A\\standby";
  a.ch.Send([&] {
    Message m;
    m.type = Type::Init;
    m.fields = {{"url", url}, {"session", session}, {"tokenVersion", "1"}, {"hostId", idA}, {"hostName", "KA-A"},
                {"key", a.key}, {"inc", a.inc}, {"epoch", a.epoch}, {"genDir", genDir}};
    return m;
  }());
  check("it says hello", a.WaitMsg(Type::Hello, 0, 10000) >= 0);
  const int ready1 = a.WaitMsg(Type::State, 0, 30000, [](const Message& m) { return m.get("state") == "ready"; });
  check("standby: ready (the Host admitted its entry, Ping keeps it)", ready1 >= 0);
  check("...and no window, no decoder while standing by",
        viewer_window(a.proc.pi.dwProcessId) == nullptr && a.proc.Text().find("MFStartup") == std::string::npos);
  const auto capLines = [&] {
    const std::string t = a.proc.Text();
    size_t n = 0, p = 0;
    while ((p = t.find("capability: ", p)) != std::string::npos) ++n, ++p;
    return n;
  };
  const size_t capsBefore = capLines();

  for (int round = 1; round <= 2; ++round) {
    const std::string op = std::to_string(round);
    const size_t from = a.Count();
    const uint64_t t0 = now_ms();
    a.Send(Type::Activate, {{"op", op}});
    const int pr = a.WaitMsg(Type::PickerReady, from, 15000, [&](const Message& m) { return m.get("op") == op; });
    check("[" + op + "] Activate -> the product picker on the screen (PickerReady)", pr >= 0,
          pr >= 0 ? "ms=" + a.At(pr).get("ms") + " wall=" + std::to_string(now_ms() - t0) : "");
    HWND w = viewer_window(a.proc.pi.dwProcessId);
    RECT wr{};
    if (w) GetWindowRect(w, &wr);
    const int right = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    check("[" + op + "] ...its window is beyond every monitor (no user screen covered)", w && wr.left >= right,
          "left=" + std::to_string(wr.left) + " virtualRight=" + std::to_string(right));
    if (w) {
      Sleep(400);
      const auto shot = e2e::e2e_capture_window(w, [] {}, 3000);
      if (shot.ok) e2e::e2e_save_bmp(gRoot + L"A-round" + W(op) + L"-picker.bmp", shot);
      check("[" + op + "] ...a WGC capture of it shows the picker (not blank)", shot.ok && variety(shot) > 4,
            shot.ok ? "variety=" + std::to_string(variety(shot)) + " " + std::to_string(shot.w) + "x" + std::to_string(shot.h)
                    : shot.why);
      // the desktop card, from the viewer's own log (the product layout), pressed the way a mouse does
      std::smatch mm;
      const std::string t = a.proc.Text();
      const std::regex re("desktopCard=(\\d+),(\\d+),(\\d+),(\\d+)");
      std::string last;
      for (std::sregex_iterator it(t.begin(), t.end(), re), end; it != end; ++it) last = it->str();
      if (std::regex_search(last, mm, re)) {
        const int x = (std::stoi(mm[1]) + std::stoi(mm[3])) / 2, y = (std::stoi(mm[2]) + std::stoi(mm[4])) / 2;
        const size_t from2 = a.Count();
        PostMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
        PostMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        Sleep(60);
        PostMessageW(w, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        const int pres = a.WaitMsg(Type::Presented, from2 > 0 ? from : from, 20000, [&](const Message& m) { return m.get("op") == op; });
        check("[" + op + "] desktop pressed -> the first picture presented (Presented)", pres >= 0,
              pres >= 0 ? "ms(since Activate)=" + a.At(pres).get("ms") : "");
        Sleep(800);
        const auto shot2 = e2e::e2e_capture_window(w, [] {}, 3000);
        if (shot2.ok) e2e::e2e_save_bmp(gRoot + L"A-round" + W(op) + L"-presented.bmp", shot2);
        check("[" + op + "] ...a WGC capture after it is a picture (not blank)", shot2.ok && variety(shot2) > 4,
              shot2.ok ? "variety=" + std::to_string(variety(shot2)) : shot2.why);
      } else {
        check("[" + op + "] the viewer logged where its desktop card is", false);
      }
    }
    const size_t from3 = a.Count();
    a.Send(Type::Park, {{"op", op}});
    const int ended = a.WaitMsg(Type::SessionEnded, from3, 15000, [&](const Message& m) { return m.get("op") == op; });
    check("[" + op + "] Park -> the session ends (connected, by park)",
          ended >= 0 && a.At(ended).get("connected") == "1" && a.At(ended).get("reason") == "park",
          ended >= 0 ? "reason=" + a.At(ended).get("reason") + " rc=" + a.At(ended).get("rc") : "");
    check("[" + op + "] ...the window is gone", [&] {
      for (int i = 0; i < 30; ++i) {
        if (!viewer_window(a.proc.pi.dwProcessId)) return true;
        Sleep(100);
      }
      return false;
    }());
    const int back = a.WaitMsg(Type::State, from3, 30000, [](const Message& m) { return m.get("state") == "ready"; });
    check("[" + op + "] ...and the SAME lease is ready again", back >= 0);
  }
  check("no new directory capability across both activations (the same lease, the same socket)", capLines() == capsBefore,
        "capability lines " + std::to_string(capsBefore) + " -> " + std::to_string(capLines()));
  const std::string vt = a.proc.Text();
  check("each activation ran on the lease socket, never an ordinary Hello",
        vt.find("standby activation on the lease socket") != std::string::npos &&
            vt.find("[attempt] hello ok") == std::string::npos);

  // ---------------------------------------------------------------- the unsupported Host
  if (haveOld && !idOld.empty()) {
    Managed o;
    o.key = url + "\ntester\n" + idOld;
    o.inc = "1";
    o.epoch = "1";
    check("a managed viewer for the 0.2.163 Host starts", o.Start(url, idOld, "OLD"));
    o.ch.Send([&] {
      Message m;
      m.type = Type::Init;
      m.fields = {{"url", url}, {"session", session}, {"tokenVersion", "1"}, {"hostId", idOld}, {"hostName", "KA-OLD"},
                  {"key", o.key}, {"inc", o.inc}, {"epoch", o.epoch}, {"genDir", N(gRoot) + "viewer-OLD\\standby"}};
      return m;
    }());
    const int uns = o.WaitMsg(Type::State, 0, 40000, [](const Message& m) { return m.get("state") == "unsupported"; });
    check("the published 0.2.163 Host: standby unsupported (no answer to the standby Hello)", uns >= 0);
    check("...and never an ordinary Hello to it", o.proc.Text().find("hello ok") == std::string::npos);
    const std::string hostOldLog = hostOld.Text();
    check("...and the old Host never had a session", hostOldLog.find("session epoch=") == std::string::npos);
    o.Send(Type::Release, {{"reason", "test"}});
    check("...Release: it ends", o.proc.Exited(8000));
  }

  // ---------------------------------------------------------------- the message fence
  {
    const size_t from = a.Count();
    Message wrong;
    wrong.type = Type::Activate;
    wrong.fields = {{"op", "9"}, {"key", a.key}, {"inc", a.inc}, {"epoch", "0"}};  // another (older) owner
    a.ch.Send(wrong);
    check("an Activate of another owner epoch is ignored", a.WaitMsg(Type::PickerReady, from, 4000) < 0 &&
                                                               viewer_window(a.proc.pi.dwProcessId) == nullptr);
  }

  // ---------------------------------------------------------------- release, then a job close
  {
    a.Send(Type::Release, {{"reason", "test"}});
    const int bye = a.WaitMsg(Type::Bye, 0, 8000);
    check("Release: the viewer says bye and ends", bye >= 0 && a.proc.Exited(8000));
  }
  {
    Managed c;
    c.key = url + "\ntester\n" + idA;
    c.inc = "2";
    c.epoch = "2";
    check("another managed viewer starts", c.Start(url, idA, "C"));
    c.ch.Send([&] {
      Message m;
      m.type = Type::Init;
      m.fields = {{"url", url}, {"session", session}, {"tokenVersion", "1"}, {"hostId", idA}, {"hostName", "KA-A"},
                  {"key", c.key}, {"inc", c.inc}, {"epoch", c.epoch}, {"genDir", genDir}};
      return m;
    }());
    const int ready = c.WaitMsg(Type::State, 0, 30000, [](const Message& m) { return m.get("state") == "ready"; });
    check("...ready again on the same PC key (its generation store reused, not locked by the ended one)", ready >= 0);
    const uint64_t t0 = now_ms();
    CloseHandle(c.proc.job);  // the shell's last job handle goes (as when the shell crashes)
    c.proc.job = nullptr;
    check("closing the job's last handle ends the child (KILL_ON_JOB_CLOSE)", c.proc.Exited(5000),
          std::to_string(now_ms() - t0) + " ms");
  }

  hostA.End();
  hostOld.End();
  server.End();
  std::printf("\nka_pc_managed_e2e_test: %s\n", gFailures == 0 ? "ALL PASS" : (std::to_string(gFailures) + " FAILED").c_str());
  return gFailures == 0 ? 0 : 1;
}
