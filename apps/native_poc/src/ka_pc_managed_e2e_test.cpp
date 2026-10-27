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
#include "client_managed_viewers.hpp"
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

// The shared FINDING of r1 (a NEW lease of the same PC key within ~30 s of its previous admission got no answer) was
// fixed in the shared code (ka-r3 a7773e6: the Host's punch trains no longer block its directory cycle) and merged
// here. The cases below therefore start the same PC key again AT ONCE -- after EndAll, after a crash -- and count it.
// (Before the fix this helper waited 31 s first.)
void wait_after_admission(const std::string& hostLog, uint64_t* lastAdmitAt) {
  (void)hostLog;
  (void)lastAdmitAt;
}

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
  // what the product shell sets for its children: h264 is behind this build switch
  SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");

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
  // KA-OLD runs only after KA-A has ended: every host process on this machine registers under the same machine
  // id, so two at once would be one host record that the later one takes over.
  wchar_t oldStream[1024] = {};
  const bool haveOld = GetEnvironmentVariableW(L"REMOTE60_KA_OLD_STREAM", oldStream, 1024) > 0;
  Child hostOld;

  std::string session;
  directory::ObserveEndpoint adv;
  std::string err;
  std::string idA, idOld;
  const uint64_t listUntil = now_ms() + 30000;
  while (now_ms() < listUntil) {
    if (session.empty()) directory_login(url, "tester", "e2e-pass-1234", &session, &err, &adv);
    std::vector<DirectoryHostEntry> hosts;
    if (!session.empty() && directory_list_hosts(url, session, &hosts, &err)) {
      for (const auto& h : hosts)
        if (h.hostName == "KA-A" && h.online) idA = h.hostId;
    }
    if (!idA.empty()) break;
    Sleep(500);
  }
  check("KA-A is online in the directory", !idA.empty(), "A=" + idA);
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
  uint64_t lastAdmitAt = now_ms();
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
    // Not "ready" at once: the Host ends the session only after 10 s of a silent control channel, and until then
    // this device's entry still holds it (model A: a Pong is not the grant coming back).
    const uint64_t parkedAt = now_ms();
    const int ret = a.WaitMsg(Type::State, from3, 5000, [](const Message& m) { return m.get("state") == "returning"; });
    check("[" + op + "] ...the lease says \"returning\" first, not ready", ret >= 0);
    const int back = a.WaitMsg(Type::State, from3, 40000, [](const Message& m) { return m.get("state") == "ready"; });
    check("[" + op + "] ...and the SAME lease is ready again once the Host can have ended the session", back >= 0 &&
              now_ms() - parkedAt >= 10000,
          "after " + std::to_string(now_ms() - parkedAt) + " ms");
  }
  check("no new directory capability across both activations (the same lease, the same socket)", capLines() == capsBefore,
        "capability lines " + std::to_string(capsBefore) + " -> " + std::to_string(capLines()));
  const std::string vt = a.proc.Text();
  check("each activation ran on the lease socket by the lease's handshake, never an ordinary Hello",
        vt.find("standby activation on the lease socket") != std::string::npos &&
            vt.find("[attempt] activation ok=1") != std::string::npos && vt.find("[attempt] hello ok") == std::string::npos);
  {
    const std::string hl = hostA.Text();
    size_t sessions = 0, p = 0;
    while ((p = hl.find("first session by activation", p)) != std::string::npos) ++sessions, ++p;
    p = 0;
    while ((p = hl.find("(standby activation", p)) != std::string::npos) ++sessions, ++p;
    check("the Host's sessions all came from activations", sessions >= 2 && hl.find("hello admitted") != std::string::npos,
          std::to_string(sessions) + " activation lines");
  }

  // ---------------------------------------------------------------- another device using the Host
  // An ordinary client (the same test viewer, its ordinary directory path: observe, connect, punch, Hello) holds
  // the Host. The kept PC's lease keeps pinging the whole time: the other session must go on undisturbed. Only the
  // tap (Activate) takes the Host over; the other session then ends, and nothing takes it back automatically.
  {
    Child other;
    const std::wstring odir = gRoot + L"viewer-other\\";
    CreateDirectoryW(odir.c_str(), nullptr);
    Managed::Add_offscreen();
    const auto oenv = env_with(odir + L"lad", {});
    other.logPath = odir + L"viewer.log";
    const bool started = other.Start(L"\"" + gBuild + L"GNLinkViewerKaTest.exe\" --transport udp --codec h264 --enable-input-channel"
                                     L" --initial-view stream --directory-url \"" + W(url) + L"\" --directory-session \"" + W(session) +
                                     L"\" --directory-host-id \"" + W(idA) + L"\" --seconds 60",
                                     gBuild, &oenv);
    const bool connected = started && other.WaitFor([](const std::string& t) { return t.find("connected host=") != std::string::npos; }, 20000);
    check("another device (an ordinary client) holds the Host", connected);
    const std::string before = hostA.Text();
    const size_t standbyPingsSeen = a.Count();
    auto presents = [&other] {
      const std::string t = other.Text();
      size_t n = 0, p = 0;
      while ((p = t.find("[present]", p)) != std::string::npos) ++n, ++p;
      return n;
    };
    const size_t presentsBefore = presents();
    Sleep(12000);  // two Ping periods and more, the kept PC standing by
    const size_t presentsAfter = presents();
    check("...the other device kept presenting through those 12 s", presentsAfter > presentsBefore + 5,
          std::to_string(presentsBefore) + " -> " + std::to_string(presentsAfter) + " present lines");
    const std::string during = hostA.Text().substr(before.size());
    check("...the kept PC kept standing by meanwhile (its state still reported)", a.Count() > standbyPingsSeen);
    check("...and the other session was not disturbed: no new session, no takeover, it is still running",
          during.find("session epoch=") == std::string::npos && during.find("standby activation") == std::string::npos &&
              !other.Exited(0),
          during.find("session epoch=") == std::string::npos ? "" : "a session started on the Host");
    const size_t from = a.Count();
    a.Send(Type::Activate, {{"op", "3"}});
    const int pr = a.WaitMsg(Type::PickerReady, from, 15000, [](const Message& m) { return m.get("op") == "3"; });
    check("the tap takes the Host over (PickerReady)", pr >= 0);
    const bool otherEnded = other.WaitFor([](const std::string& t) {
      return t.find("control-lost") != std::string::npos || t.find("session ended") != std::string::npos ||
             t.find("peer") != std::string::npos;
    }, 1) || other.Exited(20000);
    check("...and the other device's session ends", otherEnded || other.Text().find("done") != std::string::npos);
    const size_t from2 = a.Count();
    a.Send(Type::Park, {{"op", "3"}});
    check("...park", a.WaitMsg(Type::SessionEnded, from2, 15000) >= 0);
    a.WaitMsg(Type::State, from2, 40000, [](const Message& m) { return m.get("state") == "ready"; });
    other.End();
  }

  // ---------------------------------------------------------------- the first conversion, as shipped
  // The shell keeps a PC when the user closes a connected ORDINARY session (kKeepOnListReturn): the managed viewer
  // starts seconds after that session ended, while the Host still counts it (10 s control silence). The key is one
  // never kept before (a fresh device id), as for a PC kept the first time.
  {
    Child ord;
    const std::wstring odir = gRoot + L"viewer-ordinary\\";
    CreateDirectoryW(odir.c_str(), nullptr);
    Managed::Add_offscreen();
    const auto oenv = env_with(odir + L"lad", {});
    ord.logPath = odir + L"viewer.log";
    // the kept PC A must not hold the Host for this: it is parked (ready) here
    const bool started = ord.Start(L"\"" + gBuild + L"GNLinkViewerKaTest.exe\" --transport udp --codec h264 --enable-input-channel"
                                   L" --initial-view stream --directory-url \"" + W(url) + L"\" --directory-session \"" + W(session) +
                                   L"\" --directory-host-id \"" + W(idA) + L"\" --seconds 6",
                                   gBuild, &oenv);
    const bool connected = started && ord.WaitFor([](const std::string& t) { return t.find("connected host=") != std::string::npos; }, 20000);
    check("an ordinary session connects (the user's explicit connect)", connected);
    check("...and ends (the user closes it)", ord.Exited(30000));
    Managed f;
    f.key = url + "\ntester-first-conversion\n" + idA;  // never kept before: a new device id
    f.inc = "1";
    f.epoch = "5";
    const uint64_t t0 = now_ms();
    check("at once, the managed viewer of that PC starts (the first conversion)", f.Start(url, idA, "FIRST"));
    f.ch.Send([&] {
      Message m;
      m.type = Type::Init;
      m.fields = {{"url", url}, {"session", session}, {"tokenVersion", "1"}, {"hostId", idA}, {"hostName", "KA-A"},
                  {"key", f.key}, {"inc", f.inc}, {"epoch", f.epoch}, {"genDir", N(gRoot) + "viewer-FIRST\\standby"}};
      return m;
    }());
    const int r = f.WaitMsg(Type::State, 0, 45000, [](const Message& m) {
      return m.get("state") == "ready" || m.get("state") == "unsupported";
    });
    const std::string got = r >= 0 ? f.At(r).get("state") : std::string("none in 45 s");
    check("...and it is ready (not 'unsupported') -- the shipped list-return keep", got == "ready",
          got + " after " + std::to_string(now_ms() - t0) + " ms");
    f.Send(Type::Release, {{"reason", "test"}});
    f.proc.Exited(8000);
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
    // ka-pc-impl r2 (Codex N4, init in reverse): an Activate, and the Release right behind it -- before that session
    // could have made its window. Nothing of it may come up: no window, no connected session, no picker.
    const size_t from = a.Count();
    const size_t textFrom = a.proc.Text().size();
    // Back to back: the Release is already queued when the activation starts. (20 ms later was measured to be too
    // late here -- the warm lease's handshake had already connected, and the fence then closed an ACTIVE session:
    // the other case, run in the shell e2e.)
    const size_t hostFrom = hostA.Text().size();
    a.Send(Type::Activate, {{"op", "7"}});
    a.Send(Type::Release, {{"reason", "test"}});
    bool windowSeen = false;
    const uint64_t until = now_ms() + 4000;
    while (now_ms() < until && !a.proc.Exited(0)) {
      windowSeen = windowSeen || viewer_window(a.proc.pi.dwProcessId) != nullptr;
      Sleep(10);
    }
    const std::string after = a.proc.Text().substr(textFrom);
    const int ended = a.WaitMsg(Type::SessionEnded, from, 8000);
    check("Activate then Release at once: no window shown, no session connected, no picker",
          !windowSeen && after.find("connected host=") == std::string::npos && a.WaitMsg(Type::PickerReady, from, 100) < 0,
          std::string("window=") + (windowSeen ? "1" : "0"));
    check("...its SessionEnded says release, not connected",
          ended >= 0 && a.At(ended).get("reason") == "release" && a.At(ended).get("connected") == "0",
          ended >= 0 ? "reason=" + a.At(ended).get("reason") + " connected=" + a.At(ended).get("connected") +
                           " taken=" + a.At(ended).get("taken")
                     : "none");
    const int bye = a.WaitMsg(Type::Bye, 0, 8000);
    check("Release: the viewer says bye and ends", bye >= 0 && a.proc.Exited(8000));
    Sleep(500);
    // Model A, measured (r2): the activation was already granted a session at the Host (StandbyActivate, taken=1)
    // when the Release arrived, so the Host refuses that Release ("the entry holds the session") -- there is no wire
    // to say a session ended (Codex D1) -- and ends that session itself on control silence ~10 s later. Either way
    // nothing stays: the Release taken, or the session ended within 15 s.
    const bool refused = hostA.Text().substr(hostFrom).find("release refused") != std::string::npos;
    bool sessionEnded = !refused;
    if (refused) {
      const uint64_t until2 = now_ms() + 15000;
      while (!sessionEnded && now_ms() < until2) {
        sessionEnded = hostA.Text().substr(hostFrom).find("udp control session ended") != std::string::npos;
        Sleep(200);
      }
    }
    check("...the Host took the Release, or (refused while the granted session was held) ended that session itself",
          sessionEnded, refused ? "refused, then the session ended on control silence" : "taken");
  }
  // ---------------------------------------------------------------- the shell's ManagedViewers (product module)
  // The product module the client shell uses: suspended -> its own job -> resumed, Init over the pipes, events
  // stamped with the child and the owner, EndAll (the sign-out path: the caller has moved the owner epoch first),
  // and the shell's death (its job handles closing).
  {
    struct Queue {
      std::mutex mu;
      std::vector<std::function<void()>> q;
    } queue;
    std::vector<shell::ManagedEvent> events;
    shell::ManagedViewers mv(
        [&queue](std::function<void()> f) { std::lock_guard<std::mutex> l(queue.mu); queue.q.push_back(std::move(f)); },
        [](const std::string& line) { std::printf("  [module] %s\n", line.c_str()); },
        [](HANDLE read, uint32_t, const std::string&) {
          std::thread([read] {
            char b[4096];
            DWORD got = 0;
            while (ReadFile(read, b, sizeof(b), &got, nullptr) && got > 0) {}
            CloseHandle(read);
          }).detach();
        },
        [&events](const shell::ManagedEvent& e) { events.push_back(e); });
    auto drain = [&](int ms) {
      const uint64_t until = now_ms() + ms;
      while (now_ms() < until) {
        std::vector<std::function<void()>> q;
        {
          std::lock_guard<std::mutex> l(queue.mu);
          q.swap(queue.q);
        }
        for (auto& f : q) f();
        Sleep(50);
      }
    };
    wait_after_admission(hostA.Text(), &lastAdmitAt);
    shell::ManagedLaunch spec;
    spec.exe = gBuild + L"GNLinkViewerKaTest.exe";
    spec.workDir = gBuild;
    spec.baseArgs = L"--transport udp --codec h264 --enable-input-channel --initial-view targets --directory-url \"" + W(url) +
                    L"\" --directory-host-id \"" + W(idA) + L"\"";
    spec.url = url;
    spec.session = session;
    spec.tokenVersion = 1;
    spec.hostId = idA;
    spec.hostName = "KA-A";
    spec.key = url + "\ntester\n" + idA;
    spec.genDir = N(gRoot) + "viewer-A\\standby";
    spec.ownerEpoch = 7;
    std::string why;
    check("the shell module launches the managed viewer", mv.Launch(spec, &why), why);
    std::string state;
    const uint64_t until = now_ms() + 40000;
    while (now_ms() < until && state != "ready") {
      drain(200);
      for (const auto& e : events)
        if (!e.exited && e.message.type == Type::State && mv.Current(e, 7)) state = e.message.get("state");
    }
    check("...its State reaches the shell: ready", state == "ready", state);
    lastAdmitAt = now_ms();
    // an event under a newer owner epoch (the shell signed out in between) is not the current child's
    bool dropped = true;
    for (const auto& e : events)
      if (mv.Current(e, 8)) dropped = false;
    check("...events of an older owner are dropped (Current under epoch 8 is none)", dropped);
    const auto* c = mv.Find(idA);
    const DWORD pid = c ? c->pid : 0;
    HANDLE ph = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr;
    const uint64_t t0 = now_ms();
    mv.EndAll("signed out");
    check("EndAll returns at once (the waits are elsewhere)", now_ms() - t0 < 500, std::to_string(now_ms() - t0) + " ms");
    check("...and the child ends within the bound", ph && WaitForSingleObject(ph, 6000) == WAIT_OBJECT_0);
    if (ph) CloseHandle(ph);
    mv.JoinReapers();
    check("...nothing is kept after it", mv.size() == 0);
    // the shell's death: a child launched by the module, then the module (its job handles) destroyed without EndAll
    // is covered by ~ManagedViewers -> EndAllNow; the raw job close is the case above (KILL_ON_JOB_CLOSE).
  }

  {
    wait_after_admission(hostA.Text(), &lastAdmitAt);
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
    lastAdmitAt = now_ms();
    const uint64_t t0 = now_ms();
    CloseHandle(c.proc.job);  // the shell's last job handle goes (as when the shell crashes)
    c.proc.job = nullptr;
    check("closing the job's last handle ends the child (KILL_ON_JOB_CLOSE)", c.proc.Exited(5000),
          std::to_string(now_ms() - t0) + " ms");
  }
  {
    // (r2: counted since the shared fix a7773e6) IMMEDIATELY after a crash (the child above ended by its job, its entry and relay
    // lease left behind), a NEW viewer for the same PC key. Observed 10-28: the relay forwards its standby Hellos
    // and the Host answers none (relay "no StandbyAck", h2c=0; no standby line in the host log), so it ends as
    // "unsupported". Shared Host / relay code (not this task's files): measured here, handed over.
    Managed d;
    d.key = url + "\ntester\n" + idA;
    d.inc = "3";
    d.epoch = "3";
    if (d.Start(url, idA, "D")) {
      d.ch.Send([&] {
        Message m;
        m.type = Type::Init;
        m.fields = {{"url", url}, {"session", session}, {"tokenVersion", "1"}, {"hostId", idA}, {"hostName", "KA-A"},
                    {"key", d.key}, {"inc", d.inc}, {"epoch", d.epoch}, {"genDir", genDir}};
        return m;
      }());
      const int okReady = d.WaitMsg(Type::State, 0, 45000, [](const Message& m) {
        return m.get("state") == "ready" || m.get("state") == "unsupported";
      });
      const std::string got = okReady >= 0 ? d.At(okReady).get("state") : std::string("none in 45 s");
      check("a new viewer for the same PC right after a crash is ready (the shared FINDING, fixed in a7773e6)",
            got == "ready", got);
      d.Send(Type::Release, {{"reason", "test"}});
      d.proc.Exited(8000);
    }
  }

  // ---------------------------------------------------------------- the unsupported Host
  hostA.End();
  if (haveOld) {
    check("host KA-OLD (the published 0.2.163) starts, after KA-A ended", startHost(hostOld, oldStream, "KA-OLD"));
    const uint64_t until = now_ms() + 30000;
    while (now_ms() < until && idOld.empty()) {
      std::vector<DirectoryHostEntry> hosts;
      if (directory_list_hosts(url, session, &hosts, &err))
        for (const auto& h : hosts)
          if (h.hostName == "KA-OLD" && h.online) idOld = h.hostId;
      if (idOld.empty()) Sleep(500);
    }
    check("KA-OLD is online in the directory", !idOld.empty(), "old=" + idOld);
  }
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

  hostOld.End();
  server.End();
  std::printf("\nka_pc_managed_e2e_test: %s\n", gFailures == 0 ? "ALL PASS" : (std::to_string(gFailures) + " FAILED").c_str());
  return gFailures == 0 ? 0 : 1;
}
