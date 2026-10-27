// ka-pc-impl r2 (Codex N1-N4, N6, D1/D2): kept PCs through the PRODUCT shell -- its WebView page, its bridge, its
// native handlers -- and the real viewer, against a real directory and four real hosts. Loopback only.
//
//   this exe = client_shell_main.cpp with its entry renamed (REMOTE60_SHELL_TEST_SEAM: settings, logs and the
//     directory address move into the test root; the viewer it starts is GNLinkViewerKaTest -- the product viewer
//     plus the window seam that places its window beyond the right edge of the virtual screen, shown without focus)
//   the page is driven the way a person drives it: a card is .click()ed in the DOM, and what the person would read
//     (the card's state line, its "연결 해제" button, the status line) is read back from the DOM; CapturePreview
//     PNGs of the page are kept as evidence
//   a viewer window is closed the way its X closes it: WM_CLOSE to that window
//   apps/directory/server.js under node (relay on 127.0.0.1); four GNLinkStream of this build, KA-A..KA-D. Every host
//     process on one PC has the same machine id, and the directory keys a PC by (account, machine id) -- so the test
//     registers the four records itself (four made-up machine ids) and hands each host its record through its
//     ordinary --directory-cache (the file the host itself writes after registering). The host code path is the
//     product's: it loads that cache and never registers again.
//
// The shell's own window is placed beyond the right edge of the virtual screen too: nothing covers the user's screen.
// Writes only under REMOTE60_TEST_TMP (a folder directly under D:/remote/remote/.claude/test-tmp); deletes nothing.
// Every child this test starts directly (directory, hosts, the standalone viewer) is in a job of this process.
//
// The shared standby FINDING (a new lease of the same PC key within ~30 s of its previous admission gets no answer)
// is respected: no PC key is re-leased here within 31 s of its last admission.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define wWinMain ka_shell_product_entry
#include "client_shell_main.cpp"
#undef wWinMain

#include <shlwapi.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "e2e_isolation.hpp"

namespace {

uint64_t gT0 = 0;
double secs() { return (GetTickCount64() - gT0) / 1000.0; }
int gFailures = 0;
void check(bool ok, const std::string& label, const std::string& detail = {}) {
  std::printf("%s [%6.1fs] %s%s%s\n", ok ? "PASS" : "FAIL", secs(), label.c_str(), detail.empty() ? "" : "  ",
              detail.c_str());
  std::fflush(stdout);
  if (!ok) ++gFailures;
}
void note(const std::string& text) {
  std::printf("      [%6.1fs] %s\n", secs(), text.c_str());
  std::fflush(stdout);
}

std::wstring gRoot;   // with a trailing backslash
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

// ---------------------------------------------------------------- the UI thread's pump and the page
void pump(unsigned ms) {
  const uint64_t end = GetTickCount64() + ms;
  do {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message != WM_QUIT) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
    }
    Sleep(5);
  } while (GetTickCount64() < end);
}
bool wait_for(const std::function<bool()>& predicate, unsigned ms) {
  const uint64_t end = GetTickCount64() + ms;
  while (GetTickCount64() < end) {
    if (predicate()) return true;
    pump(100);
  }
  return predicate();
}
std::string js(const std::wstring& script) {
  struct Reply {
    bool done = false;
    std::string text;
  };
  auto reply = std::make_shared<Reply>();
  if (!gWebView) return {};
  gWebView->ExecuteScript(script.c_str(), Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                                              [reply](HRESULT hr, LPCWSTR value) -> HRESULT {
                                                if (SUCCEEDED(hr) && value) reply->text = narrow(value);
                                                reply->done = true;
                                                return S_OK;
                                              })
                                              .Get());
  const uint64_t end = GetTickCount64() + 3000;
  while (!reply->done && GetTickCount64() < end) pump(10);
  return reply->text;
}
std::string unquote(std::string v) {
  if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
  std::string out;
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] == '\\' && i + 1 < v.size()) {
      ++i;
      if (v[i] == 'u' && i + 4 < v.size()) {
        const unsigned cp = std::stoul(v.substr(i + 1, 4), nullptr, 16);
        i += 4;
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
      } else {
        out += v[i] == 'n' ? '\n' : v[i];
      }
    } else {
      out += v[i];
    }
  }
  return out;
}
std::wstring card_expr(const std::string& name) {
  return L"Array.from(document.querySelectorAll('.hostRow')).find(function(r){var n=r.querySelector('.name');"
         L"return n&&n.textContent==='" + widen(name) + L"';})";
}
// What the person reads on that PC's card.
std::string card_state(const std::string& name) {
  return unquote(js(L"(function(){var r=" + card_expr(name) + L";if(!r)return '(no card)';var s=r.querySelector('.state');"
                    L"return s?s.textContent:'';})()"));
}
bool card_has_release(const std::string& name) {
  return js(L"(function(){var r=" + card_expr(name) + L";return !!(r&&r.querySelector('.keepRelease'));})()") == "true";
}
// The card is pressed as a click on its button -- the page's own handler, the bridge, the native connect.
bool tap(const std::string& name) {
  return js(L"(function(){var r=" + card_expr(name) + L";if(!r)return false;var b=r.querySelector('.host');"
            L"if(!b||b.disabled)return false;b.click();return true;})()") == "true";
}
bool press_release(const std::string& name) {
  return js(L"(function(){var r=" + card_expr(name) + L";if(!r)return false;var b=r.querySelector('.keepRelease');"
            L"if(!b||b.disabled)return false;b.click();return true;})()") == "true";
}
std::string status_line() { return unquote(js(L"document.getElementById('hostsMsg').textContent")); }
bool wait_card(const std::string& name, const std::string& text, unsigned ms) {
  return wait_for([&] { return card_state(name) == text; }, ms);
}

// The page as the WebView renders it, kept as a PNG.
bool capture_page(const std::wstring& path) {
  if (!gWebView) return false;
  IStream* stream = nullptr;
  if (FAILED(SHCreateStreamOnFileEx(path.c_str(), STGM_CREATE | STGM_WRITE, FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, &stream)))
    return false;
  auto done = std::make_shared<std::pair<bool, HRESULT>>(false, E_FAIL);
  gWebView->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream,
                           Callback<ICoreWebView2CapturePreviewCompletedHandler>([done](HRESULT hr) -> HRESULT {
                             done->first = true;
                             done->second = hr;
                             return S_OK;
                           }).Get());
  wait_for([&] { return done->first; }, 5000);
  stream->Release();
  return done->first && SUCCEEDED(done->second);
}

// ---------------------------------------------------------------- logs
std::string read_file(const std::wstring& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}
std::string client_log() { return read_file(log_dir_path() + L"\\client.log"); }
std::string viewer_log() { return read_file(log_dir_path() + L"\\viewer.log.1") + read_file(log_dir_path() + L"\\viewer.log"); }
std::string lines_of_pid(const std::string& text, DWORD pid) {
  const std::string tag = " viewerPid=" + std::to_string(pid) + " ";
  std::string out;
  std::istringstream in(text);
  for (std::string l; std::getline(in, l);)
    if (l.find(tag) != std::string::npos) out += l + "\n";
  return out;
}
size_t count_of(const std::string& text, const std::string& needle) {
  size_t n = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) ++n;
  return n;
}
bool log_has(const std::string& needle, unsigned ms) {
  return wait_for([&] { return client_log().find(needle) != std::string::npos; }, ms);
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
// The ordinary viewer the shell holds for that PC (its launch slot), or 0.
DWORD ordinary_pid(const std::string& hostId) {
  const auto it = gViewerByHost.find(hostId);
  return it == gViewerByHost.end() ? 0 : it->second.pid;
}
DWORD managed_pid(const std::string& hostId) {
  const auto* c = gManaged ? gManaged->Find(hostId) : nullptr;
  return c ? c->pid : 0;
}
// The window's X: WM_CLOSE to that viewer's window.
bool close_window_of(DWORD pid) {
  HWND w = nullptr;
  wait_for([&] { return (w = viewer_window(pid)) != nullptr; }, 15000);
  return w && PostMessageW(w, WM_CLOSE, 0, 0);
}
bool ordinary_connected(DWORD pid, unsigned ms) {
  return wait_for([&] { return lines_of_pid(viewer_log(), pid).find("[native-video-client] connected host=") != std::string::npos; }, ms);
}

// ---------------------------------------------------------------- children of this test (directory, hosts)
struct Child {
  PROCESS_INFORMATION pi{};
  HANDLE job = nullptr;
  HANDLE out = nullptr;
  std::thread reader;
  std::mutex mu;
  std::string text;
  std::wstring logPath;
  bool Start(const std::wstring& cmd, const std::wstring& dir, const std::vector<wchar_t>* env) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE w = nullptr;
    if (!CreatePipe(&out, &w, &sa, 1 << 20)) return false;
    SetHandleInformation(out, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    HANDLE inherit[2] = {w, nul};
    SIZE_T sz = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &sz);
    std::vector<char> buf(sz);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buf.data());
    InitializeProcThreadAttributeList(attrs, 1, 0, &sz);
    UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit), nullptr, nullptr);
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
      std::ofstream f(logPath, std::ios::binary | std::ios::trunc);
      f << Text();
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

struct TestHost {
  std::string name, id;
  Child proc;
};

}  // namespace

int main() {
  gT0 = GetTickCount64();
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::string why;
  if (!init_root(&why)) {
    std::printf("FAIL  no test root: %s\n", why.c_str());
    return 2;
  }
  std::printf("test root: %s\n", narrow(gRoot).c_str());
  wchar_t nodeExe[MAX_PATH] = {};
  wchar_t repoBuf[1024] = {};
  GetEnvironmentVariableW(L"REMOTE60_KA_REPO", repoBuf, 1024);
  const std::wstring serverJs = std::wstring(repoBuf) + L"\\apps\\directory\\server.js";
  if (!SearchPathW(nullptr, L"node.exe", nullptr, MAX_PATH, nodeExe, nullptr) ||
      GetFileAttributesW(serverJs.c_str()) == INVALID_FILE_ATTRIBUTES) {
    std::printf("FAIL  node.exe on PATH and REMOTE60_KA_REPO (the worktree) are required\n");
    return 2;
  }
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  std::string socketError;
  if (!initialize_sockets(&socketError)) return 2;
  SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");

  // ---------------------------------------------------------------- the directory
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
  check(server.Start(L"\"" + std::wstring(nodeExe) + L"\" \"" + serverJs + L"\"", dirData, &dirEnv), "the directory starts");
  Sleep(1500);
  const std::string url = "http://127.0.0.1:" + std::to_string(httpPort);

  // ---------------------------------------------------------------- four hosts, one record each
  std::vector<std::unique_ptr<TestHost>> hosts;
  const std::string realMachine = remote60::native_poc::directory::machine_id();
  for (const char* name : {"KA-A", "KA-B", "KA-C", "KA-D"}) {
    auto h = std::make_unique<TestHost>();
    h->name = name;
    std::string token, err;
    const bool reg = remote60::native_poc::directory::register_host(url, "tester", "e2e-pass-1234", name,
                                                         std::string("ka-shell-e2e-") + name, &h->id, &token, &err);
    const std::wstring dir = gRoot + L"host-" + widen(name) + L"\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    remote60::native_poc::directory::HostCache cache;
    cache.directoryUrl = url;
    cache.accountId = "tester";
    cache.machineId = realMachine;  // what the host compares with its own
    cache.hostName = name;
    cache.hostId = h->id;
    cache.hostToken = token;
    const bool saved = reg && remote60::native_poc::directory::save_host_cache(narrow(dir + L"cache.json"), cache);
    CopyFileW((gBuild + L"GNLinkStream.exe").c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE);
    const auto env = env_with(dir + L"lad", {});
    const uint16_t media = e2e::e2e_pick_free_udp_port(), control = e2e::e2e_pick_free_tcp_port();
    h->proc.logPath = dir + L"host.log";
    const bool started = saved && h->proc.Start(
        L"\"" + dir + L"GNLinkStream.exe\" --transport udp --codec h264 --bind-address 127.0.0.1 --bind-port " +
            std::to_wstring(media) + L" --control-port " + std::to_wstring(control) + L" --directory-url " + widen(url) +
            L" --directory-id tester --host-name " + widen(name) + L" --directory-cache \"" + dir + L"cache.json\" --seconds 1800",
        dir, &env);
    check(started, std::string("host ") + name + " registered as its own record and started from its cache", "id=" + h->id);
    hosts.push_back(std::move(h));
  }
  auto H = [&](const char* name) -> TestHost& {
    for (auto& h : hosts)
      if (h->name == name) return *h;
    return *hosts[0];
  };
  {
    std::string session, err;
    remote60::native_poc::directory::ObserveEndpoint adv;
    size_t online = 0;
    const uint64_t until = GetTickCount64() + 40000;
    while (GetTickCount64() < until) {
      if (session.empty()) remote60::native_poc::directory_login(url, "tester", "e2e-pass-1234", &session, &err, &adv);
      std::vector<DirectoryHostEntry> list;
      online = 0;
      if (!session.empty() && remote60::native_poc::directory_list_hosts(url, session, &list, &err))
        for (const auto& e : list)
          for (auto& h : hosts)
            if (e.hostId == h->id && e.online) ++online;
      if (online == hosts.size()) break;
      Sleep(500);
    }
    check(online == hosts.size(), "the four hosts are four online PCs of the account", std::to_string(online) + "/4");
    if (online != hosts.size()) return 1;
  }

  // ---------------------------------------------------------------- the product shell, beyond the screen's edge
  const int right = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN);
  SetEnvironmentVariableW(L"REMOTE60_VIEWER_TEST_WINDOW_X", std::to_wstring(right + 200).c_str());
  SetEnvironmentVariableW(L"REMOTE60_VIEWER_TEST_WINDOW_Y", std::to_wstring(GetSystemMetrics(SM_YVIRTUALSCREEN) + 40).c_str());
  const std::wstring shellLad = gRoot + L"shell-lad";
  CreateDirectoryW(shellLad.c_str(), nullptr);
  SetEnvironmentVariableW(L"LOCALAPPDATA", shellLad.c_str());  // the shell's children inherit this environment
  gShellTestDirectoryUrl = url;
  gShellTestDataDir = gRoot + L"shelldata";
  gShellTestViewerExe = gBuild + L"GNLinkViewerKaTest.exe";
  gShellTestManagedViewerExe = gBuild + L"GNLinkViewerKaTest.exe";
  const std::wstring shots = gRoot + L"shots\\";
  CreateDirectoryW(shots.c_str(), nullptr);

  LogUploadShutdown uploader;
  int result = 0;
  {
    struct WorkerShutdown {
      ~WorkerShutdown() { gClosing.store(true); gWorkers.Shutdown(); }
    } workerShutdown;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    RegisterClassExW(&wc);
    gWindow = CreateWindowExW(0, kWindowClass, L"GNLink ka-pc shell e2e", WS_OVERLAPPEDWINDOW, right + 200,
                              GetSystemMetrics(SM_YVIRTUALSCREEN) + 40, 520, 760, nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(gWindow, SW_SHOWNOACTIVATE);
    make_managed_viewers();
    check(SUCCEEDED(create_shell_webview()), "the product shell's WebView starts (beyond the screen's right edge)");
    check(wait_for([] { return js(L"!!document.getElementById('signIn')&&!document.getElementById('signIn').disabled") == "true"; }, 20000),
          "the sign-in form is up");
    js(L"document.getElementById('account').value='tester';document.getElementById('password').value='e2e-pass-1234';"
       L"document.getElementById('signIn').click();true");
    check(wait_for([] { return js(L"!document.getElementById('hostsCard').classList.contains('hidden')") == "true"; }, 30000),
          "signed in through the page: the PC list is shown");
    check(wait_for([&] {
            for (auto& h : hosts)
              if (card_state(h->name) != "연결 가능") return false;
            return true;
          }, 30000),
          "the four PCs are listed, each '연결 가능'");
    check(log_has("kept PCs: directory standby supported", 20000), "the shell learned the directory's standby support");

    // ======================================================== N1: each PC's own first conversion, any close order
    for (const char* n : {"KA-A", "KA-B", "KA-C"}) {
      check(tap(n), std::string("tap ") + n + " (an ordinary connect)");
      DWORD pid = 0;
      wait_for([&] { return (pid = ordinary_pid(H(n).id)) != 0; }, 10000);
      check(pid != 0 && ordinary_connected(pid, 40000), std::string(n) + ": the shell's viewer connected", "pid=" + std::to_string(pid));
    }
    {
      using namespace remote60::native_poc::shell;
      const auto recent = recent_for(recent_load(recent_path()), pc_origin(), "tester");
      const std::string order = recent.size() >= 3 ? recent[0] + "," + recent[1] + "," + recent[2] : "(short)";
      check(recent.size() >= 3 && recent[0] == H("KA-C").id && recent[1] == H("KA-B").id && recent[2] == H("KA-A").id,
            "the recent order is the CONNECT order (newest first: C, B, A)", order);
    }
    capture_page(shots + L"1-three-open.png");
    // A first: two newer viewers are open, so A's exit is not the newest operation (Codex N1's case); then C, then B.
    for (const char* n : {"KA-A", "KA-C", "KA-B"}) {
      const DWORD pid = ordinary_pid(H(n).id);
      check(close_window_of(pid), std::string("close ") + n + "'s viewer window (its X)");
      check(wait_card(n, "대기 중", 45000), std::string(n) + ": its card says '대기 중' (kept, standing by)", card_state(n));
      check(card_has_release(n), std::string(n) + ": ...with its own '연결 해제' button");
    }
    check(count_of(client_log(), "kept PC: first conversion to standby host=") >= 3,
          "the shell log names three first conversions");
    {
      using namespace remote60::native_poc::shell;
      const auto recent = recent_for(recent_load(recent_path()), pc_origin(), "tester");
      check(recent.size() >= 3 && recent[0] == H("KA-C").id && recent[1] == H("KA-B").id && recent[2] == H("KA-A").id,
            "closing windows did not move the recent order (still C, B, A)");
    }
    capture_page(shots + L"2-three-standby.png");
    const uint64_t admittedABC = GetTickCount64();

    // ======================================================== N2: a fourth PC -- a standby goes only once it connected
    {
      const size_t logBefore = client_log().size();
      check(tap("KA-D"), "tap KA-D beside three standing-by PCs");
      DWORD pid = 0;
      wait_for([&] { return (pid = ordinary_pid(H("KA-D").id)) != 0; }, 10000);
      const bool connected = pid != 0 && ordinary_connected(pid, 40000);
      check(connected, "KA-D: the shell's viewer connected");
      check(log_has("kept PCs: making room after host=" + H("KA-D").id + " succeeded, releasing host=" + H("KA-A").id, 10000),
            "...and only then the least recently USED standby (A) was let go");
      const std::string slice = client_log().substr(logBefore);
      check(count_of(slice, "releasing host=") == 1, "...exactly one", std::to_string(count_of(slice, "releasing host=")));
      check(wait_card("KA-A", "연결 가능", 10000) && !card_has_release("KA-A"), "KA-A's card: no longer kept", card_state("KA-A"));
      check(card_state("KA-B") == "대기 중" && card_state("KA-C") == "대기 중", "B and C stay kept");
      check(close_window_of(pid), "close KA-D's viewer window");
      check(wait_card("KA-D", "대기 중", 45000), "KA-D: kept on close (B, C, D)", card_state("KA-D"));
    }
    const uint64_t admittedD = GetTickCount64();

    // ======================================================== N6 + D1: tap a kept PC -> 여는 중 -> 연결됨; park -> returning
    std::map<std::string, DWORD> activePid;
    for (const char* n : {"KA-B", "KA-C", "KA-D"}) {
      check(tap(n), std::string("tap kept ") + n);
      // the page marks the pressed card '연결하는 중…' until the shell answers; the kept state is '여는 중' / '연결됨'
      const bool opening = wait_for([&] { const auto s = card_state(n); return s == "연결하는 중…" || s == "여는 중" || s == "연결됨"; }, 5000);
      check(opening, std::string(n) + ": the card says it is opening", card_state(n));
      check(wait_card(n, "연결됨", 30000), std::string(n) + ": '연결됨' (picker-ready of this op)", card_state(n));
      activePid[n] = managed_pid(H(n).id);
    }
    capture_page(shots + L"3-three-active.png");
    check(log_has("picker-ready ms=", 1000), "the shell log has the activation's picker-ready with its op");
    // N2: three PCs on screen; a fourth is refused and nothing is let go
    {
      const size_t logBefore = client_log().size();
      check(tap("KA-A"), "tap KA-A while B, C, D are all on screen");
      check(wait_for([] { return status_line().find("PC 3대가 모두 열려") != std::string::npos; }, 5000),
            "the status line refuses it ('PC 3대가 모두 열려 ...')", status_line());
      pump(1500);
      const std::string slice = client_log().substr(logBefore);
      check(slice.find("releasing host=") == std::string::npos && ordinary_pid(H("KA-A").id) == 0,
            "...no viewer started for A and nobody let go");
    }
    // park all three (their windows' X): returning, then 대기 중
    for (const char* n : {"KA-B", "KA-C", "KA-D"}) check(close_window_of(activePid[n]), std::string("close ") + n + "'s session window");
    check(wait_for([] { return card_state("KA-B").find("이전 연결 정리 중일 수 있음") != std::string::npos; }, 10000),
          "after the session: '대기 중 · 이전 연결 정리 중일 수 있음' (returning -- not a grant proof)", card_state("KA-B"));
    for (const char* n : {"KA-B", "KA-C", "KA-D"}) check(wait_card(n, "대기 중", 30000), std::string(n) + ": back to '대기 중'", card_state(n));

    // ======================================================== N3: 연결 해제
    {
      const DWORD pidC = managed_pid(H("KA-C").id);
      check(press_release("KA-C"), "press KA-C's '연결 해제'");
      check(log_has("kept PC: released by the user host=" + H("KA-C").id, 5000), "the shell released it (bridge -> native)");
      check(wait_card("KA-C", "연결 가능", 10000) && !card_has_release("KA-C"), "KA-C's card: not kept, no button", card_state("KA-C"));
      HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pidC);
      check(p && WaitForSingleObject(p, 12000) == WAIT_OBJECT_0, "its managed viewer process ended");
      if (p) CloseHandle(p);
      js(L"document.getElementById('refresh').click();true");
      pump(4000);
      check(card_state("KA-C") == "연결 가능" && managed_pid(H("KA-C").id) == 0, "a list refresh does not bring it back");
      check(card_state("KA-B") == "대기 중" && card_state("KA-D") == "대기 중", "B and D are untouched");
      capture_page(shots + L"4-released.png");
    }

    // ======================================================== N2: a fourth PC that FAILS costs nobody
    {
      // A again: its key was last admitted at the three conversions -- wait out the shared FINDING window first.
      while (GetTickCount64() < admittedABC + 31000) pump(500);
      check(tap("KA-A"), "tap KA-A (B, D kept): an ordinary connect");
      DWORD pid = 0;
      wait_for([&] { return (pid = ordinary_pid(H("KA-A").id)) != 0; }, 10000);
      check(pid && ordinary_connected(pid, 40000), "KA-A connected");
      check(close_window_of(pid), "close KA-A's window");
      check(wait_card("KA-A", "대기 중", 45000), "KA-A kept again (A, B, D)", card_state("KA-A"));
      // C's host goes away; C (released, so not kept) is tapped: its connect fails
      H("KA-C").proc.End();
      const size_t logBefore = client_log().size();
      check(tap("KA-C"), "tap KA-C, whose host is gone, beside three kept PCs");
      DWORD pidC = 0;
      wait_for([&] { return (pidC = ordinary_pid(H("KA-C").id)) != 0; }, 10000);
      pump(45000);  // longer than a connect to a live host takes here
      const bool failed = pidC && lines_of_pid(viewer_log(), pidC).find("connected host=") == std::string::npos;
      check(failed, "KA-C's viewer did not connect (its host is gone)");
      check(client_log().substr(logBefore).find("releasing host=") == std::string::npos &&
                card_state("KA-A") == "대기 중" && card_state("KA-B") == "대기 중" && card_state("KA-D") == "대기 중",
            "...and the three kept PCs are all still kept (no eviction for a failed fourth)");
      if (pidC) close_window_of(pidC);
      if (!wait_for([&] { return ordinary_pid(H("KA-C").id) == 0; }, 20000)) {
        // its failure screen did not close on WM_CLOSE: ended with the test-only launch handle (RV-17), so the
        // sign-out below is not the one that ends it
        const auto it = gViewerByHost.find(H("KA-C").id);
        if (it != gViewerByHost.end() && it->second.testTerminate) TerminateProcess(it->second.testTerminate, 0);
        note("KA-C's failed viewer did not close on WM_CLOSE; ended by the test's launch handle");
        wait_for([&] { return ordinary_pid(H("KA-C").id) == 0; }, 10000);
      }
    }

    // ======================================================== N6: a handshake that fails after the take
    {
      (void)admittedD;  // an activation of D's existing lease, not a new lease: no wait needed
      H("KA-D").proc.End();  // D's host goes away; D's lease has not noticed yet
      check(tap("KA-D"), "tap kept KA-D right after its host went away");
      check(log_has("kept PC session ended host=" + H("KA-D").id, 60000), "D's activation ended");
      const std::string l = client_log();
      const size_t at = l.rfind("kept PC session ended host=" + H("KA-D").id);
      const std::string line = at == std::string::npos ? "" : l.substr(at, l.find('\n', at) - at);
      check(line.find("connected=0") != std::string::npos, "...reported as NOT connected (not the socket take)", line);
      check(wait_for([] { return status_line().find("연결에 실패했습니다") != std::string::npos ||
                                 status_line().find("아직 준비하는 중") != std::string::npos; }, 5000),
            "the status line says it failed -- not idle", status_line());
      capture_page(shots + L"5-failed-activation.png");
    }

    // ======================================================== N4: sign-out ends the shell's own viewers with the fence
    {
      // B's standby is released by the user, then B is connected as an ordinary session and left open
      check(press_release("KA-B"), "release KA-B");
      wait_card("KA-B", "연결 가능", 10000);
      check(tap("KA-B"), "tap KA-B: an ordinary session, left open");
      DWORD pidB = 0;
      wait_for([&] { return (pidB = ordinary_pid(H("KA-B").id)) != 0; }, 10000);
      check(pidB && ordinary_connected(pidB, 40000), "KA-B's ordinary viewer is connected");
      // a viewer this shell did NOT start (another program): it must survive the sign-out
      Child outside;
      const std::wstring odir = gRoot + L"outside-viewer\\";
      CreateDirectoryW(odir.c_str(), nullptr);
      outside.logPath = odir + L"viewer.log";
      std::string session, err;
      remote60::native_poc::directory::ObserveEndpoint adv;
      remote60::native_poc::directory_login(url, "tester", "e2e-pass-1234", &session, &err, &adv);
      const auto oenv = env_with(odir + L"lad", {});
      outside.Start(L"\"" + gBuild + L"GNLinkViewerKaTest.exe\" --transport udp --codec h264 --initial-view stream"
                    L" --directory-url \"" + widen(url) + L"\" --directory-session \"" + widen(session) +
                    L"\" --directory-host-id \"" + widen(H("KA-A").id) + L"\" --seconds 120", gBuild, &oenv);
      const bool outsideUp = wait_for([&] { return outside.Text().find("[present]") != std::string::npos; }, 40000);
      check(outsideUp, "a viewer this shell did not start is presenting (KA-A, from another program)");
      HANDLE pB = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pidB);
      std::vector<DWORD> managedPids;
      for (const auto& id : gManaged->HostIds()) managedPids.push_back(managed_pid(id));
      const size_t presentsBefore = count_of(outside.Text(), "[present]");
      const size_t logBefore = client_log().size();
      js(L"document.getElementById('signOut').click();true");
      const bool bEnded = pB && wait_for([&] { return WaitForSingleObject(pB, 0) == WAIT_OBJECT_0; }, 12000);
      DWORD code = 99;
      if (pB) GetExitCodeProcess(pB, &code);
      check(bEnded, "sign-out: the shell's ordinary viewer of KA-B ended");
      check(lines_of_pid(viewer_log(), pidB).find("owner fence: input and clipboard cut") != std::string::npos,
            "...through the owner fence (input and clipboard cut, then its own WM_CLOSE teardown)");
      check(code == 0 && client_log().substr(logBefore).find("ended by its handle") == std::string::npos,
            "...exit 0, not ended by the shell's handle", "exit=" + std::to_string(code));
      if (pB) CloseHandle(pB);
      bool managedGone = true;
      for (DWORD mp : managedPids) {
        HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, mp);
        if (p) {
          managedGone = managedGone && WaitForSingleObject(p, 12000) == WAIT_OBJECT_0;
          CloseHandle(p);
        }
      }
      check(managedGone, "...the kept PCs' viewers ended too", std::to_string(managedPids.size()) + " viewer(s)");
      pump(3000);
      const size_t presentsAfter = count_of(outside.Text(), "[present]");
      check(!outside.Exited(0) && presentsAfter > presentsBefore + 5,
            "the viewer this shell did not start kept presenting", std::to_string(presentsBefore) + " -> " + std::to_string(presentsAfter));
      check(wait_for([] { return js(L"!document.getElementById('signInCard').classList.contains('hidden')") == "true"; }, 10000),
            "the page is back at the sign-in form");
      capture_page(shots + L"6-signed-out.png");
      outside.End();
    }

    DestroyWindow(gWindow);
    pump(500);
    if (gManaged) {
      gManaged->JoinReapers();
      gManaged.reset();
    }
  }
  for (auto& h : hosts) h->proc.End();
  server.End();
  std::printf("\nka_pc_shell_e2e_test: %s (%d failed)\n", gFailures ? "FAILED" : "ALL PASS", gFailures);
  return gFailures ? 1 : (result ? result : 0);
}
