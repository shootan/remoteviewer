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
// The shell's WebView profile is a NEW folder of this run, fixed and preflighted before any WebView exists and its real
// use confirmed before the sign-in (see "the shell's WebView profile, isolated" below; Codex N1). Kept, never deleted.
//
// The shared standby FINDING of r1 (a new lease of the same PC key within ~30 s of its previous admission got no
// answer) is fixed in the shared code (a7773e6, merged): A is kept again right after its eviction, without a wait.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define wWinMain ka_shell_product_entry
#include "client_shell_main.cpp"
#undef wWinMain

#include <shlwapi.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "e2e_injection_guard.hpp"
#include "ka_pc_e2e_child.hpp"
#include "ka_pc_e2e_webview.hpp"
#include "e2e_isolation.hpp"

namespace {

uint64_t gT0 = 0;
double secs() { return (GetTickCount64() - gT0) / 1000.0; }
int gFailures = 0;
// The detail is computed after the predicate: a function argument's evaluation order is unspecified (MSVC goes right
// to left), so a detail passed beside a waiting predicate would describe the moment BEFORE the wait.
void check_after(const std::function<bool()>& predicate, const std::string& label, const std::function<std::string()>& detail);
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

void check_after(const std::function<bool()>& predicate, const std::string& label, const std::function<std::string()>& detail) {
  const bool ok = predicate();
  check(ok, label, detail());
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
  // ka-pc r7+ (the shared guard: a repository fixture this run OWNS): a NEW folder of this run inside the root --
  // made new here (an existing one is refused, never reused) -- and everything of the run goes in it.
  const std::wstring run = std::wstring(full) + L"\\run-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                           std::to_wstring(GetTickCount64());
  if (!CreateDirectoryW(run.c_str(), nullptr)) {
    *why = "this run's own folder could not be made new";
    return false;
  }
  gRoot = run + L"\\";
  wchar_t self[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  std::wstring s(self);
  gBuild = s.substr(0, s.find_last_of(L"\\/") + 1);
  return true;
}

// ---------------------------------------------------------------- the shell's WebView profile, isolated (Codex N1)
// The product (create_shell_webview, unchanged) asks for GetTempPathW + "GNLinkClient" as its user data folder (UDF).
// The WebView2 loader lets WEBVIEW2_USER_DATA_FOLDER and the WebView2 registry policies replace that argument, and other
// WEBVIEW2_* variables / policies change the runtime and its arguments. So BEFORE any environment is created:
//   * this run's TEMP checked (ka_pc_e2e::private_temp), the UDF a NEW path inside the run, within MAX_PATH;
//   * WEBVIEW2_USER_DATA_FOLDER set to exactly that UDF (and read back);
//   * any other WEBVIEW2_* variable, and any value or key under SOFTWARE\Policies\Microsoft\Edge\WebView2 (HKLM / HKCU,
//     64- and 32-bit views, read only), refuses the run -- nobody has to know which one would win;
//   * additional browser arguments: none (the product passes no options; no variable, no policy).
// Anything not confirmed: INVALID, exit 2, CreateCoreWebView2Environment never called. After the product created its
// environment, ICoreWebView2Environment7::get_UserDataFolder (the folder really in use) must be that UDF before the
// sign-in and before any client -- else the run stops. Downloads, new windows and OS dialogs are refused and stop it.
// The profile is evidence: kept, never deleted here.
// The preflight itself (variables, policies, the product's own path computation) is shared with the managed test:
// ka_pc_e2e_webview.hpp. Here: the shell's UDF for this process, then -- once the shell's environment is confirmed
// -- the macro's UDF for every viewer the shell starts after it (they inherit this environment).
std::string webview_preflight(const std::wstring& expectedUdf) {
  return ka_pc_e2e::webview_preflight(gRoot, ka_pc_e2e::kShellUdfName, expectedUdf);
}

// The folder the product's environment REALLY uses (ICoreWebView2Environment7::get_UserDataFolder), or "" + why.
std::wstring effective_udf(std::string* why) {
  if (!gWebView) { *why = "no WebView"; return {}; }
  ComPtr<ICoreWebView2_2> w2;
  ComPtr<ICoreWebView2Environment> env;
  ComPtr<ICoreWebView2Environment7> env7;
  if (FAILED(gWebView.As(&w2)) || !w2) { *why = "ICoreWebView2_2 unavailable"; return {}; }
  if (FAILED(w2->get_Environment(&env)) || !env) { *why = "get_Environment failed"; return {}; }
  if (FAILED(env.As(&env7)) || !env7) { *why = "ICoreWebView2Environment7 unavailable"; return {}; }
  LPWSTR p = nullptr;
  if (FAILED(env7->get_UserDataFolder(&p)) || !p) { *why = "get_UserDataFolder failed"; return {}; }
  std::wstring s(p);
  CoTaskMemFree(p);
  return s;
}

// Anything that would leave the run's own folder (a download, a new window, an OS dialog): refused at once, then the
// run stops (pump). A literal, set once.
std::atomic<const char*> gExternal{nullptr};
std::atomic<bool> gStopping{false};
void pump(unsigned ms);
[[noreturn]] void stop_run(const std::string& why, int code);

// OS dialogs of this test (its own, or owned by one of its windows -- the WebView's file dialog is): closed, recorded.
// And a macro window of a viewer this run started: recorded (the run stops; the viewer is ended with the shell).
std::atomic<bool> gDialogWatchStop{false};
void dialog_watch() {
  while (!gDialogWatchStop.load()) {
    EnumWindows(
        [](HWND h, LPARAM) -> BOOL {
          wchar_t cls[16] = {};
          GetClassNameW(h, cls, 16);
          if (wcscmp(cls, L"#32770") != 0 || !IsWindowVisible(h)) return TRUE;
          DWORD pid = 0, ownerPid = 0;
          GetWindowThreadProcessId(h, &pid);
          if (HWND owner = GetWindow(h, GW_OWNER)) GetWindowThreadProcessId(owner, &ownerPid);
          if (pid == GetCurrentProcessId() || ownerPid == GetCurrentProcessId()) {
            const char* none = nullptr;
            gExternal.compare_exchange_strong(none, "an OS dialog");
            PostMessageW(h, WM_CLOSE, 0, 0);
          }
          return TRUE;
        },
        0);
    // a macro WebView in a viewer of this run: its folder was fixed (the macro UDF) but cannot be confirmed from here
    if (ka_pc_e2e::own_macro_window_exists(gBuild)) {
      const char* none = nullptr;
      gExternal.compare_exchange_strong(none, "a macro window (its WebView's folder cannot be confirmed here)");
    }
    Sleep(200);
  }
}

// The run stops (N1): the product shell closed (its viewers told to end), every child ended and judged (each host
// through its Finalize), the guard's final check -- the profile and every log kept -- then INVALID / FAILED.
[[noreturn]] void stop_run(const std::string& why, int code) {
  gStopping = true;
  std::printf("FAIL  STOP %s -- the run stops here; the WebView profile and every log are kept\n", why.c_str());
  std::fflush(stdout);
  if (gWindow && IsWindow(gWindow)) {
    DestroyWindow(gWindow);
    pump(500);
  }
  if (gManaged) {
    gManaged->JoinReapers();
    gManaged.reset();
  }
  ka_pc_e2e::children_end_all();
  e2e::e2e_injection_final_check_all();
  std::printf("RESULT: %s\n", code == 2 ? "INVALID" : "FAILED");
  std::fflush(stdout);
  std::_Exit(code);
}

// ---------------------------------------------------------------- the UI thread's pump and the page
void pump(unsigned ms) {
  const uint64_t end = GetTickCount64() + ms;
  do {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_APP + 3 && message.hwnd == gWindow && !gStopping.load()) {
        // the product would re-create the shell's WebView -- with the environment as it is now (the macro's UDF):
        // not preflighted for the shell, so it is not dispatched and the run stops
        const char* none = nullptr;
        gExternal.compare_exchange_strong(none, "a re-creation of the shell's WebView (its process failed)");
        continue;
      }
      if (message.message != WM_QUIT) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
    }
    if (!gStopping.load()) {
      if (const char* e = gExternal.load()) stop_run(std::string(e) + " came up during the run (refused / closed)", 1);
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
// Fills the form and presses 로그인, as a person does -- again every few seconds until the list shows. (r3-2: the
// page's own 'restore' message can arrive after the form was filled and empty the id field; the press then only
// says "아이디와 비밀번호를 모두 입력해 주세요" and nothing is sent. A person types again; so does this.)
bool sign_in_through_page() {
  const uint64_t until = GetTickCount64() + 40000;
  while (GetTickCount64() < until) {
    js(L"(function(){var b=document.getElementById('signIn');if(b.disabled)return false;"
       L"document.getElementById('account').value='tester';document.getElementById('password').value='e2e-pass-1234';"
       L"b.click();return true;})()");
    if (wait_for([] { return js(L"!document.getElementById('hostsCard').classList.contains('hidden')") == "true"; }, 4000))
      return true;
  }
  return false;
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
// "대기 중" -- with or without "· 이전 연결 정리 중일 수 있음" (returning)
bool standing_by(const std::string& name) { return card_state(name).rfind("대기 중", 0) == 0; }
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
// A line written AFTER `from` (a size of client.log taken before the action): an earlier identical line does not count.
bool log_since(size_t from, const std::string& needle, unsigned ms) {
  return wait_for([&] { const std::string l = client_log(); return l.size() > from && l.find(needle, from) != std::string::npos; }, ms);
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
// One child of this test: the shared ka-pc e2e Child (ka_pc_e2e_child.hpp) -- every set-up step checked, its own
// log written as it is read, its end judged for every child (N2), a host through the shared guard as it is.
using ka_pc_e2e::Child;

std::vector<wchar_t> env_with(const std::wstring& localAppData, const std::vector<std::pair<std::wstring, std::wstring>>& add) {
  for (const auto& kv : add) SetEnvironmentVariableW(kv.first.c_str(), kv.second.c_str());
  CreateDirectoryW(localAppData.c_str(), nullptr);
  return e2e::e2e_isolated_environment(localAppData);
}

// r2b (Codex N6 counter-cases): a message from a kept PC's viewer exactly as the shell's forwarder hands it to the UI
// (ManagedEvent -> on_managed_event), stamped with the identity given -- so a stale op, or a child that is no longer
// the current one, can be delivered at a chosen moment. The shell's own handler decides; nothing is bypassed.
struct ChildIdentity {
  std::string hostId, key;
  uint64_t inc = 0, epoch = 0;
};
ChildIdentity identity_of(const std::string& hostId) {
  ChildIdentity id;
  if (const auto* c = gManaged ? gManaged->Find(hostId) : nullptr) {
    id.hostId = hostId;
    id.key = c->key;
    id.inc = c->incarnation;
    id.epoch = c->ownerEpoch;
  }
  return id;
}
void deliver(const ChildIdentity& who, remote60::native_poc::managed_ipc::Type t,
             std::map<std::string, std::string> fields) {
  remote60::native_poc::shell::ManagedEvent e;
  e.hostId = who.hostId;
  e.incarnation = who.inc;
  e.ownerEpoch = who.epoch;
  e.message.type = t;
  e.message.fields = std::move(fields);
  e.message.fields["key"] = who.key;
  e.message.fields["inc"] = std::to_string(who.inc);
  e.message.fields["epoch"] = std::to_string(who.epoch);
  on_managed_event(e);
}
// The native side of a page's connect message (what the bridge hands handle_page_message), for a press the page
// itself would not offer at that moment (a card it shows as busy).
void page_connect(const std::string& hostId, const std::string& name) {
  handle_page_message("{\"type\":\"connect\",\"hostId\":\"" + hostId + "\",\"hostName\":\"" + name + "\"}");
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
  // N1: TEMP / TMP of this process (and so of every child it starts) inside this run, then the shell's WebView profile
  // fixed and preflighted -- all BEFORE any child and before any WebView environment exists. Anything not confirmed:
  // INVALID, exit 2, CreateCoreWebView2Environment never called.
  std::wstring udf, macroUdf;
  {
    std::wstring tmp;
    if (!ka_pc_e2e::private_temp(gRoot, &tmp, &why)) {
      std::printf("FAIL  SETUP this run's own TEMP: %s\nRESULT: INVALID\n", why.c_str());
      return 2;
    }
    udf = tmp + L"GNLinkClient";
    wchar_t before[8] = {};
    const bool hadUdfVariable = GetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", before, 8) > 0;
    if (!SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", udf.c_str())) {
      std::printf("FAIL  SETUP WEBVIEW2_USER_DATA_FOLDER could not be set (error %lu)\nRESULT: INVALID\n", GetLastError());
      return 2;
    }
    const std::string pre = webview_preflight(udf);
    if (!pre.empty()) {
      std::printf("FAIL  SETUP the WebView profile preflight: %s -- no WebView environment is created\nRESULT: INVALID\n",
                  pre.c_str());
      return 2;
    }
    LPWSTR runtime = nullptr;
    std::string runtimeVersion = "unknown";
    if (SUCCEEDED(GetAvailableCoreWebView2BrowserVersionString(nullptr, &runtime)) && runtime) {
      runtimeVersion = narrow(runtime);
      CoTaskMemFree(runtime);
    }
    check(true, "WebView profile preflight: UDF " + narrow(udf) +
                    " (new, inside this run) = what the product asks for (GetTempPathW + GNLinkClient) = "
                    "WEBVIEW2_USER_DATA_FOLDER" + (hadUdfVariable ? " (an inherited value replaced)" : "") +
                    "; other WEBVIEW2_* variables: none; WebView2 policies (HKLM/HKCU, 64/32): none; additional browser "
                    "arguments: none (no options, no variable, no policy); runtime " + runtimeVersion);
    // the refusals, provoked without any environment: a second owned path, an extra variable, a moved UDF variable
    const bool otherRoot = !webview_preflight(gRoot + L"other-root\\GNLinkClient").empty();
    SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", L"--ka-pc-preflight-contrast");
    const bool extraVariable = webview_preflight(udf).find("other WebView2 variables") != std::string::npos;
    SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", nullptr);
    SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", (gRoot + L"other-root\\GNLinkClient").c_str());
    const bool movedVariable = webview_preflight(udf).find("WEBVIEW2_USER_DATA_FOLDER") != std::string::npos;
    SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", udf.c_str());
    const bool backToPass = webview_preflight(udf).empty();
    check(otherRoot && extraVariable && movedVariable && backToPass,
          "...the preflight refuses another owned path, an extra WEBVIEW2_* variable and a moved UDF variable, and "
          "passes again once restored (no environment created for any of them)");
    if (!(otherRoot && extraVariable && movedVariable && backToPass)) {  // a preflight that cannot refuse proves nothing
      std::printf("RESULT: INVALID\n");
      return 2;
    }
    // r2 addition: the macro's WebView (client_macro_window.cpp, GetTempPathW + "GNLinkMacro") lives in a VIEWER, and
    // every viewer the shell starts inherits this process's environment -- with the shell's UDF in it, a macro WebView
    // would share the shell's folder. Its own UDF is preflighted here too; it is put in place for the viewers once the
    // shell's environment is confirmed (below), before the sign-in.
    macroUdf = tmp + ka_pc_e2e::kMacroUdfName;
    const std::string macroPre = ka_pc_e2e::webview_fix_and_preflight(gRoot, ka_pc_e2e::kMacroUdfName, macroUdf);
    const std::string shellAgain = ka_pc_e2e::webview_fix_and_preflight(gRoot, ka_pc_e2e::kShellUdfName, udf);
    if (!macroPre.empty() || !shellAgain.empty()) {
      std::printf("FAIL  SETUP the macro WebView preflight: %s%s -- no WebView environment is created\nRESULT: INVALID\n",
                  macroPre.c_str(), shellAgain.c_str());
      return 2;
    }
    check(true, "macro WebView preflight: UDF " + narrow(macroUdf) +
                    " (new, inside this run) = what the viewer's macro window asks for (GetTempPathW + GNLinkMacro); "
                    "the same variable / policy checks");
  }
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
    add.logPath = dirData + L"add-account.log";
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
  for (const char* name : {"KA-A", "KA-B", "KA-C", "KA-D", "KA-E"}) {
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
    // r4: input injection OFF for every test host -- the standard the verifier set (Codex): an explicit
    // --input-injection-mode none on the argv, and no viewer connects before THIS launch says "input injection
    // disabled". GNLinkStream injects by default (host_args.hpp enableInputInjection = true); before this, each
    // session start's modifier key-ups reached this PC's input (2026-10-28, reported). The host still receives,
    // logs ([input-timing] ... injectionEnabled=0) and acknowledges every input event. (Not --config: a config
    // file clears the host's environment overrides, the h264 switch among them.)
    const auto env = env_with(dir + L"lad", {});
    const uint16_t media = e2e::e2e_pick_free_udp_port(), control = e2e::e2e_pick_free_tcp_port();
    h->proc.logPath = dir + L"host.log";
    const bool started = saved && h->proc.Start(
        L"\"" + dir + L"GNLinkStream.exe\" --transport udp --codec h264 --bind-address 127.0.0.1 --bind-port " +
            std::to_wstring(media) + L" --control-port " + std::to_wstring(control) + L" --directory-url " + widen(url) +
            L" --directory-id tester --host-name " + widen(name) + L" --directory-cache \"" + dir + L"cache.json\" --seconds 1800" +
            e2e::e2e_injection_off_arg(dir),
        dir, &env);
    check(started, std::string("host ") + name + " registered as its own record and started from its cache", "id=" + h->id);
    // The shared test-host guard (ka-r3 b8dcc93): THIS launch said input injection OFF and power management OFF,
    // both whole lines, before any viewer exists -- else INVALID, exit 2; then its monitor; its end is judged final.
    h->proc.Guard(name);
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
    check(online == hosts.size(), "the five hosts are five online PCs of the account", std::to_string(online) + "/5");
    if (online != hosts.size()) return 1;
  }

  // ---------------------------------------------------------------- the product shell, beyond the screen's edge
  const int right = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN);
  SetEnvironmentVariableW(L"REMOTE60_VIEWER_TEST_WINDOW_X", std::to_wstring(right + 200).c_str());
  SetEnvironmentVariableW(L"REMOTE60_VIEWER_TEST_WINDOW_Y", std::to_wstring(GetSystemMetrics(SM_YVIRTUALSCREEN) + 40).c_str());
  const std::wstring shellLad = gRoot + L"shell-lad";
  CreateDirectoryW(shellLad.c_str(), nullptr);
  if (!SetEnvironmentVariableW(L"LOCALAPPDATA", shellLad.c_str()))  // the shell's children inherit this environment
    stop_run("LOCALAPPDATA could not be set to the run's shell-lad (error " + std::to_string(GetLastError()) + ")", 2);
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
    std::thread dialogWatch(dialog_watch);  // from here: an OS dialog of this test is closed and stops the run
    // N1: the same preflight once more right before the product creates its environment (nothing may have moved)
    {
      const std::string pre = webview_preflight(udf);
      if (!pre.empty()) {
        gDialogWatchStop = true;
        dialogWatch.join();
        stop_run("the WebView profile preflight, right before the WebView: " + pre + " -- no environment created", 2);
      }
    }
    check(SUCCEEDED(create_shell_webview()), "the product shell's WebView starts (beyond the screen's right edge)");
    // N1: the folder the product's environment REALLY uses, before the sign-in and before any client. (The product's
    // own callback has already navigated to its local page, ui/shell.html -- that cannot be held back without a seam
    // in the product file, which this round does not touch.)
    wait_for([] { return static_cast<bool>(gWebView); }, 20000);
    {
      std::string gw;
      const std::wstring used = effective_udf(&gw);
      const bool same = !used.empty() && ka_pc_e2e::same_path(used, udf) && ka_pc_e2e::resolves_to_itself(udf) &&
                        ka_pc_e2e::is_inside(ka_pc_e2e::final_path_of(udf), gRoot);
      check(same, "the product's WebView environment really uses the run's UDF (ICoreWebView2Environment7::get_UserDataFolder)",
            used.empty() ? gw : narrow(used));
      if (!same) {
        gDialogWatchStop = true;
        dialogWatch.join();
        stop_run("the WebView's user data folder is not the run's (" + (used.empty() ? gw : narrow(used)) +
                     ") -- if it is another folder, that folder may already have been touched; nothing is cleaned",
                 2);
      }
      // downloads and new windows: refused at once, then the run stops (the default download folder is the user's)
      ComPtr<ICoreWebView2_4> w4;
      EventRegistrationToken token{};
      const bool downloads =
          SUCCEEDED(gWebView.As(&w4)) && w4 &&
          SUCCEEDED(w4->add_DownloadStarting(
              Callback<ICoreWebView2DownloadStartingEventHandler>(
                  [](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* a) -> HRESULT {
                    a->put_Cancel(TRUE);
                    a->put_Handled(TRUE);
                    const char* none = nullptr;
                    gExternal.compare_exchange_strong(none, "a download");
                    return S_OK;
                  })
                  .Get(),
              &token));
      const bool windows = SUCCEEDED(gWebView->add_NewWindowRequested(
          Callback<ICoreWebView2NewWindowRequestedEventHandler>(
              [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* a) -> HRESULT {
                a->put_Handled(TRUE);  // no window of any kind is opened
                const char* none = nullptr;
                gExternal.compare_exchange_strong(none, "a new window");
                return S_OK;
              })
              .Get(),
          &token));
      check(downloads && windows, "downloads and new windows are refused (and stop the run) from before the sign-in");
      if (!(downloads && windows)) {
        gDialogWatchStop = true;
        dialogWatch.join();
        stop_run("the download / new-window refusal could not be installed", 2);
      }
      // r2 addition: from here the viewers the shell starts inherit the MACRO's UDF (their macro window's WebView), not
      // the shell's. The shell's own environment already exists (the variable is read at creation); a re-creation of it
      // (the product's WM_APP + 3 after a WebView process failure) would not be preflighted, so pump() refuses it.
      const std::string macroPre = ka_pc_e2e::webview_fix_and_preflight(gRoot, ka_pc_e2e::kMacroUdfName, macroUdf);
      check(macroPre.empty(), "the viewers' environment now names the macro's own UDF (preflight holds)", macroPre);
      if (!macroPre.empty()) {
        gDialogWatchStop = true;
        dialogWatch.join();
        stop_run("the macro WebView preflight: " + macroPre, 2);
      }
    }
    check(wait_for([] { return js(L"!!document.getElementById('signIn')&&!document.getElementById('signIn').disabled") == "true"; }, 20000),
          "the sign-in form is up");
    check(sign_in_through_page(), "signed in through the page: the PC list is shown");
    check(wait_for([&] {
            for (auto& h : hosts)
              if (card_state(h->name) != "연결 가능") return false;
            return true;
          }, 30000),
          "the five PCs are listed, each '연결 가능'");
    check(log_has("kept PCs: directory standby supported", 20000), "the shell learned the directory's standby support");

    // ======================================================== R1 (r3): ONE reservation beside three PCs
    {
      // A standing by, B and C open: three logical PCs
      check(tap("KA-A"), "R1: tap KA-A");
      DWORD pa = 0;
      wait_for([&] { return (pa = ordinary_pid(H("KA-A").id)) != 0; }, 10000);
      check(pa && ordinary_connected(pa, 40000) && close_window_of(pa), "R1: A connected, and its window closed");
      check_after([&] { return wait_card("KA-A", "대기 중", 45000); }, "R1: A standing by", [&] { return card_state("KA-A"); });
      for (const char* n : {"KA-B", "KA-C"}) {
        check(tap(n), std::string("R1: tap ") + n);
        DWORD pid = 0;
        wait_for([&] { return (pid = ordinary_pid(H(n).id)) != 0; }, 10000);
        check(pid && ordinary_connected(pid, 40000), std::string("R1: ") + n + " open");
      }
      // D, then A (the only standby), then E -- pressed one after the other in ONE turn of the page, so all three
      // reach the shell while D is still connecting
      const size_t logFrom = client_log().size();
      js(L"(function(){function card(n){return Array.from(document.querySelectorAll('.hostRow')).find(function(r){"
         L"var e=r.querySelector('.name');return e&&e.textContent===n;}).querySelector('.host');}"
         L"card('KA-D').click();card('KA-A').click();card('KA-E').click();return true;})()");
      pump(500);
      const std::string slice = client_log().substr(logFrom);
      check(slice.find("host=" + H("KA-D").id + " reserved beside three PCs") != std::string::npos,
            "R1: D (beside A standing by, B and C open) took the one reservation");
      check(slice.find("kept PC activation host=" + H("KA-A").id + " refused") != std::string::npos,
            "R1: ...opening A -- the last standby that could make room for D -- is refused while D is reserved");
      check(slice.find("refused host=" + H("KA-E").id + " (connect): another PC is being opened") != std::string::npos &&
                ordinary_pid(H("KA-E").id) == 0,
            "R1: ...and E is refused ('다른 PC 를 여는 중'): no second candidate on the same standby");
      DWORD pd = 0;
      wait_for([&] { return (pd = ordinary_pid(H("KA-D").id)) != 0; }, 10000);
      check(pd && ordinary_connected(pd, 40000), "R1: D connected");
      check(log_has("reservation of host=" + H("KA-D").id + " committed", 10000) &&
                log_has("making room after host=" + H("KA-D").id + " succeeded, releasing host=" + H("KA-A").id, 5000),
            "R1: ...then, and only then, its reservation was committed and A let go");
      check(count_of(client_log().substr(logFrom), "releasing host=") == 1, "R1: ...exactly one PC let go");
      const size_t logE = client_log().size();
      check(tap("KA-E"), "R1: tap E with B, C, D all open");
      pump(1500);
      check(client_log().substr(logE).find("refused host=" + H("KA-E").id + " (connect): three PCs are in use") != std::string::npos &&
                ordinary_pid(H("KA-E").id) == 0,
            "R1: ...E refused ('PC 3대가 모두 열려'): never a fourth window", status_line());
      // back to nothing kept: B, C, D closed (each kept), then released by the user -- the next part starts clean
      for (const char* n : {"KA-B", "KA-C", "KA-D"}) close_window_of(ordinary_pid(H(n).id));
      for (const char* n : {"KA-B", "KA-C", "KA-D"}) {
        wait_for([&] { return card_has_release(n); }, 45000);
        press_release(n);
        wait_card(n, "연결 가능", 15000);
      }
      check(card_state("KA-A") == "연결 가능" && card_state("KA-B") == "연결 가능" && card_state("KA-C") == "연결 가능" &&
                card_state("KA-D") == "연결 가능",
            "R1: (set back: nothing kept)");
    }

    // ======================================================== N1: each PC's own first conversion, any close order
    for (const char* n : {"KA-A", "KA-B", "KA-C"}) {
      check(tap(n), std::string("tap ") + n + " (an ordinary connect)");
      DWORD pid = 0;
      wait_for([&] { return (pid = ordinary_pid(H(n).id)) != 0; }, 10000);
      check(pid != 0 && ordinary_connected(pid, 40000), std::string(n) + ": the shell's viewer connected", "pid=" + std::to_string(pid));
    }
    {
      using namespace remote60::native_poc::shell;
      // the connect is posted from the output pump to the UI thread: give the UI its turn (the list already has R1's
      // entries, so wait for THIS order's newest one)
      wait_for([&] {
        const auto r = recent_for(recent_load(recent_path()), pc_origin(), "tester");
        return !r.empty() && r[0] == H("KA-C").id;
      }, 5000);
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
      check_after([&] { return wait_card(n, "대기 중", 45000); }, std::string(n) + ": its card says '대기 중' (kept, standing by)", [&] { return card_state(n); });
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
      // read from the log AFTER the tap only (R1 above already wrote the same line for D once)
      const std::string evictLine = "kept PCs: making room after host=" + H("KA-D").id + " succeeded, releasing host=" + H("KA-A").id;
      check(wait_for([&] { return client_log().substr(logBefore).find(evictLine) != std::string::npos; }, 10000),
            "...and only then the least recently USED standby (A) was let go");
      const std::string slice = client_log().substr(logBefore);
      check(count_of(slice, "releasing host=") == 1, "...exactly one", std::to_string(count_of(slice, "releasing host=")));
      check_after([&] { return wait_card("KA-A", "연결 가능", 10000) && !card_has_release("KA-A"); }, "KA-A's card: no longer kept", [&] { return card_state("KA-A"); });
      check(card_state("KA-B") == "대기 중" && card_state("KA-C") == "대기 중", "B and C stay kept");
      check(close_window_of(pid), "close KA-D's viewer window");
      check_after([&] { return wait_card("KA-D", "대기 중", 45000); }, "KA-D: kept on close (B, C, D)", [&] { return card_state("KA-D"); });
    }
    const uint64_t admittedD = GetTickCount64();

    // ======================================================== N6 + D1: tap a kept PC -> 여는 중 -> 연결됨; park -> returning
    std::map<std::string, DWORD> activePid;
    for (const char* n : {"KA-B", "KA-C", "KA-D"}) {
      check(tap(n), std::string("tap kept ") + n);
      // the page marks the pressed card '연결하는 중…' until the shell answers; the kept state is '여는 중' / '연결됨'
      const bool opening = wait_for([&] { const auto s = card_state(n); return s == "연결하는 중…" || s == "여는 중" || s == "연결됨"; }, 5000);
      check(opening, std::string(n) + ": the card says it is opening", card_state(n));
      check_after([&] { return wait_card(n, "연결됨", 30000); }, std::string(n) + ": '연결됨' (picker-ready of this op)", [&] { return card_state(n); });
      activePid[n] = managed_pid(H(n).id);
    }
    capture_page(shots + L"3-three-active.png");
    check(log_has("picker-ready ms=", 1000), "the shell log has the activation's picker-ready with its op");
    // r2b (Codex N6): messages of an op this activation is not -- an older one and one never sent -- are dropped:
    // B stays '연결됨' with its activation, the status line is not touched
    {
      using remote60::native_poc::managed_ipc::Type;
      const ChildIdentity b = identity_of(H("KA-B").id);
      const uint64_t op = gManaged->Find(H("KA-B").id)->activeOp;
      const std::string statusBefore = status_line();
      const size_t logFrom = client_log().size();
      deliver(b, Type::SessionEnded, {{"op", std::to_string(op + 41)}, {"rc", "6"}, {"reason", "connect-failed"}, {"connected", "0"}});
      deliver(b, Type::SessionEnded, {{"op", "0"}, {"rc", "0"}, {"reason", "closed"}, {"connected", "1"}});
      deliver(b, Type::PickerReady, {{"op", std::to_string(op + 41)}, {"ms", "1"}});
      pump(800);
      const std::string slice = client_log().substr(logFrom);
      check(op != 0 && gManaged->Find(H("KA-B").id)->activeOp == op && card_state("KA-B") == "연결됨" &&
                status_line() == statusBefore,
            "N6: a SessionEnded / PickerReady of another op leaves B's activation as it is", "op=" + std::to_string(op));
      check(count_of(slice, "dropped (current op=" + std::to_string(op) + ")") == 3, "...each one dropped by op, and said",
            std::to_string(count_of(slice, "dropped (current op=")));
    }
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
    check_after([] { return wait_for([] { return card_state("KA-B").find("이전 연결 정리 중일 수 있음") != std::string::npos; }, 10000); },
                "after the session: '대기 중 · 이전 연결 정리 중일 수 있음' (returning -- not a grant proof)",
                [] { return card_state("KA-B"); });
    for (const char* n : {"KA-B", "KA-C", "KA-D"}) check_after([&] { return wait_card(n, "대기 중", 30000); }, std::string(n) + ": back to '대기 중'", [&] { return card_state(n); });

    // ======================================================== N3: 연결 해제
    const ChildIdentity releasedC = identity_of(H("KA-C").id);  // r2b: its messages are delivered late, below
    {
      const DWORD pidC = managed_pid(H("KA-C").id);
      const size_t relFrom = client_log().size();
      check(press_release("KA-C"), "press KA-C's '연결 해제'");
      check(log_since(relFrom, "kept PC: released by the user host=" + H("KA-C").id, 5000), "the shell released it (bridge -> native)");
      check_after([&] { return wait_card("KA-C", "연결 가능", 10000) && !card_has_release("KA-C"); }, "KA-C's card: not kept, no button", [&] { return card_state("KA-C"); });
      HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pidC);
      check(p && WaitForSingleObject(p, 12000) == WAIT_OBJECT_0, "its managed viewer process ended");
      if (p) CloseHandle(p);
      js(L"document.getElementById('refresh').click();true");
      pump(4000);
      check(card_state("KA-C") == "연결 가능" && managed_pid(H("KA-C").id) == 0, "a list refresh does not bring it back");
      check(card_state("KA-B") == "대기 중" && card_state("KA-D") == "대기 중", "B and D are untouched");
      capture_page(shots + L"4-released.png");
    }
    // r2b (Codex N6): while B is being opened, late messages of C's ended viewer arrive (its old incarnation) -- a
    // SessionEnded that would say "failed", a State that would say "ready", its exit. None of it reaches the page.
    {
      using remote60::native_poc::managed_ipc::Type;
      check(tap("KA-B"), "tap kept KA-B");
      for (const auto& t : {Type::SessionEnded, Type::State}) {
        if (t == Type::SessionEnded) deliver(releasedC, t, {{"op", "1"}, {"rc", "6"}, {"reason", "connect-failed"}, {"connected", "0"}});
        else deliver(releasedC, t, {{"state", "ready"}, {"lost", "0"}});
      }
      remote60::native_poc::shell::ManagedEvent gone;
      gone.hostId = releasedC.hostId;
      gone.incarnation = releasedC.inc;
      gone.ownerEpoch = releasedC.epoch;
      gone.exited = true;
      gone.exitCode = 0;
      on_managed_event(gone);
      check_after([&] { return wait_card("KA-B", "연결됨", 30000); }, "N6: B opens as usual ('연결됨')", [&] { return card_state("KA-B"); });
      check(card_state("KA-C") == "연결 가능" && status_line().find("실패") == std::string::npos,
            "...C's late messages changed nothing (C not kept, no failure said)", card_state("KA-C") + " / " + status_line());
      check(close_window_of(managed_pid(H("KA-B").id)), "close B's session window");
      check_after([&] { return wait_card("KA-B", "대기 중", 30000); }, "B back to '대기 중'", [&] { return card_state("KA-B"); });
    }

    // ======================================================== N2: a fourth PC that FAILS costs nobody
    {
      // A again, right after it was let go (evicted): with the shared fix (a7773e6) no wait is needed.
      (void)admittedABC;
      check(tap("KA-A"), "tap KA-A (B, D kept): an ordinary connect");
      DWORD pid = 0;
      wait_for([&] { return (pid = ordinary_pid(H("KA-A").id)) != 0; }, 10000);
      check(pid && ordinary_connected(pid, 40000), "KA-A connected");
      check(close_window_of(pid), "close KA-A's window");
      check_after([&] { return wait_card("KA-A", "대기 중", 45000); }, "KA-A kept again (A, B, D)", [&] { return card_state("KA-A"); });
      // C's host goes away; C (released, so not kept) is tapped: its connect fails
      H("KA-C").proc.End();
      // r3 (Codex R4): A is opened first; while C is still connecting, A's window is closed -- A's own, valid
      // SessionEnded arrives and must not end C's "connecting" mark
      check(tap("KA-A"), "R4: tap kept KA-A");
      check_after([&] { return wait_card("KA-A", "연결됨", 30000); }, "R4: A is open ('연결됨')", [&] { return card_state("KA-A"); });
      const DWORD activeA = managed_pid(H("KA-A").id);
      const size_t logBefore = client_log().size();
      check(tap("KA-C"), "tap KA-C, whose host is gone, beside three kept PCs");
      DWORD pidC0 = 0;
      wait_for([&] { return (pidC0 = ordinary_pid(H("KA-C").id)) != 0; }, 10000);
      const size_t closeFrom = client_log().size();
      check(close_window_of(activeA), "R4: close A's session window while C is connecting");
      check(log_since(closeFrom, "kept PC session ended host=" + H("KA-A").id + " op=", 15000), "R4: A's session ended (its SessionEnded reached the shell)");
      pump(500);
      check_after([] { return card_state("KA-C") == "연결하는 중…"; }, "R4: ...and C's card still says '연결하는 중…'",
                  [] { return card_state("KA-C"); });
      // r2b (Codex N2, a CANCELLED fourth): while that first attempt is still connecting, C is pressed again (the
      // bridge's connect for C; the page itself shows the busy card) -- the shell calls the first attempt off and
      // starts another. The called-off one must cost nobody and must not be kept.
      pump(2500);
      page_connect(H("KA-C").id, "KA-C");
      HANDLE p0 = pidC0 ? OpenProcess(SYNCHRONIZE, FALSE, pidC0) : nullptr;
      const bool calledOff = p0 && wait_for([&] { return WaitForSingleObject(p0, 0) == WAIT_OBJECT_0; }, 15000);
      if (p0) CloseHandle(p0);
      check(calledOff && lines_of_pid(viewer_log(), pidC0).find("connect cancelled") != std::string::npos,
            "N2: the first attempt for C was called off (its log: connect cancelled) and ended");
      pump(1000);
      check(client_log().substr(logBefore).find("releasing host=") == std::string::npos &&
                client_log().substr(logBefore).find("first conversion to standby host=" + H("KA-C").id) == std::string::npos &&
                standing_by("KA-A") && standing_by("KA-B") && standing_by("KA-D"),
            "...a cancelled fourth: nobody let go, C not kept");
      // r4 (Codex R1): C (still connecting) holds the reservation. A -- a standby C would have let go -- is released
      // by the user. E is pressed: the free place is C's, so E is refused.
      {
        check(press_release("KA-A"), "R1: release KA-A while C holds the reservation");
        check_after([&] { return wait_card("KA-A", "연결 가능", 10000); }, "R1: A not kept", [&] { return card_state("KA-A"); });
        const size_t eFrom = client_log().size();
        check(tap("KA-E"), "R1: tap KA-E");
        check(log_since(eFrom, "refused host=" + H("KA-E").id + " (connect): another PC is being opened", 3000) &&
                  ordinary_pid(H("KA-E").id) == 0,
              "R1: ...E refused -- the place A left is the reserved C's, not E's");
      }
      DWORD pidC = 0;
      wait_for([&] { return (pidC = ordinary_pid(H("KA-C").id)) != 0 && pidC != pidC0; }, 10000);
      pump(45000);  // longer than a connect to a live host takes here
      const bool failed = pidC && lines_of_pid(viewer_log(), pidC).find("connected host=") == std::string::npos;
      check(failed, "KA-C's viewer did not connect (its host is gone)");
      check(client_log().substr(logBefore).find("releasing host=") == std::string::npos &&
                standing_by("KA-B") && standing_by("KA-D"),
            "...and the kept PCs are still kept (no eviction for a failed fourth)");
      if (pidC) close_window_of(pidC);
      if (!wait_for([&] { return ordinary_pid(H("KA-C").id) == 0; }, 20000)) {
        // its failure screen did not close on WM_CLOSE: ended with the test-only launch handle (RV-17), so the
        // sign-out below is not the one that ends it
        const auto it = gViewerByHost.find(H("KA-C").id);
        if (it != gViewerByHost.end() && it->second.testTerminate) TerminateProcess(it->second.testTerminate, 0);
        note("KA-C's failed viewer did not close on WM_CLOSE; ended by the test's launch handle");
        wait_for([&] { return ordinary_pid(H("KA-C").id) == 0; }, 10000);
      }
      check(log_has("reservation of host=" + H("KA-C").id + " released (ended without connecting)", 5000),
            "R1: C's reservation was released when it ended without connecting");
    }

    // ======================================================== N6: a handshake that fails after the take
    {
      (void)admittedD;  // an activation of D's existing lease, not a new lease: no wait needed
      // r4 (Codex R4, mixed): an ORDINARY E is open; kept D is being opened (its host gone, so it takes a while);
      // E's window is closed -- E's exit status must not end D's "connecting" mark
      check(tap("KA-E"), "R4: tap KA-E (an ordinary session)");
      DWORD pidE = 0;
      wait_for([&] { return (pidE = ordinary_pid(H("KA-E").id)) != 0; }, 10000);
      check(pidE && ordinary_connected(pidE, 40000), "R4: E is open");
      H("KA-D").proc.End();  // D's host goes away; D's lease has not noticed yet
      const size_t logFrom = client_log().size();
      check(tap("KA-D"), "tap kept KA-D right after its host went away");
      pump(300);
      check(close_window_of(pidE), "R4: close E's window while D is being opened");
      check(wait_for([&] { return ordinary_pid(H("KA-E").id) == 0; }, 15000), "R4: E's viewer ended (its exit reached the shell)");
      pump(300);
      const bool dStillOpening = client_log().substr(logFrom).find("kept PC session ended host=" + H("KA-D").id) != std::string::npos ||
                                 card_state("KA-D") == "연결하는 중…";
      check_after([&] { return dStillOpening; }, "R4: ...and D's card still says '연결하는 중…' (or D had already ended)",
                  [&] { return card_state("KA-D"); });
      const std::string endedTag = "kept PC session ended host=" + H("KA-D").id;
      check(wait_for([&] { return client_log().substr(logFrom).find(endedTag) != std::string::npos; }, 60000),
            "D's activation ended");
      const std::string l = client_log().substr(logFrom);
      const size_t at = l.find(endedTag);
      const std::string line = at == std::string::npos ? "" : l.substr(at, l.find('\n', at) - at);
      check(line.find("connected=0") != std::string::npos, "...reported as NOT connected (not the socket take)", line);
      check(wait_for([] { return status_line().find("연결에 실패했습니다") != std::string::npos ||
                                 status_line().find("아직 준비하는 중") != std::string::npos; }, 5000),
            "the status line says it failed -- not idle", status_line());
      capture_page(shots + L"5-failed-activation.png");
    }
    // r2b (Codex N6): an Activate that cannot be sent -- the shell's channel to A's viewer is closed (as when that
    // pipe breaks) and A is pressed at once, before anything else runs on this thread. No activation may be left
    // behind and the user is told; nothing pretends to open.
    {
      wait_for([&] { const auto* k = gManaged->Find(H("KA-B").id); return k && k->state == "ready"; }, 20000);
      const auto* a = gManaged->Find(H("KA-B").id);
      check(a && a->state == "ready", "KA-B is kept and ready", a ? a->state : "(none)");
      if (a) const_cast<remote60::native_poc::shell::ManagedViewers::Child*>(a)->channel->Close();
      const size_t logFrom = client_log().size();
      page_connect(H("KA-B").id, "KA-B");
      const auto* after = gManaged->Find(H("KA-B").id);
      check(client_log().substr(logFrom).find("kept PC activation host=" + H("KA-B").id + " could not be sent") != std::string::npos,
            "N6: the Activate could not be sent, and the shell says so in its log");
      check(after == nullptr || after->activeOp == 0, "...no activation is left behind (activeOp 0)");
      check(wait_for([] { return status_line().find("여는 요청을 보내지 못했습니다") != std::string::npos; }, 3000),
            "...and the status line tells the user", status_line());
      check(ordinary_pid(H("KA-B").id) == 0, "...no ordinary viewer was started beside the lease");
      wait_for([&] { return gManaged->Find(H("KA-B").id) == nullptr; }, 15000);  // its viewer ends (shell gone)
    }

    // ======================================================== N4: sign-out ends the shell's own viewers with the fence
    uint64_t oldEpoch = 0;
    {
      // B (no longer kept) is connected as an ordinary session and left open. r4 (Codex R3): this viewer's sender is
      // held for 800 ms after the owner fence (test build only) -- the fence must wait for its releases.
      if (card_has_release("KA-B")) press_release("KA-B");
      wait_card("KA-B", "연결 가능", 15000);
      SetEnvironmentVariableW(L"REMOTE60_VIEWER_TEST_HOLD_SEND_AFTER_FENCE_MS", L"800");
      check(tap("KA-B"), "tap KA-B: an ordinary session, left open");
      DWORD pidB = 0;
      wait_for([&] { return (pidB = ordinary_pid(H("KA-B").id)) != 0; }, 10000);
      SetEnvironmentVariableW(L"REMOTE60_VIEWER_TEST_HOLD_SEND_AFTER_FENCE_MS", nullptr);
      check(pidB && ordinary_connected(pidB, 40000), "KA-B's ordinary viewer is connected");
      pump(800);
      // r4 (Codex decision 1): B is open -- a plain press brings it forward and says so; nothing is replaced
      {
        const size_t tFrom = client_log().size();
        check(tap("KA-B"), "decision 1: press KA-B again while it is open");
        check(log_since(tFrom, "connect: host=" + H("KA-B").id + " is open", 3000) && ordinary_pid(H("KA-B").id) == pidB,
              "decision 1: ...shown, not replaced (same viewer)", status_line());
        // and the connect's cancel -- what a replacement sends when the shell still saw it connecting -- reaches
        // nothing in an established session
        const auto slot = gViewerByHost.find(H("KA-B").id);
        if (slot != gViewerByHost.end()) remote60::native_poc::viewer::viewer_request_cancel(slot->second.cancelEvent);
        pump(1500);
        HANDLE pb = OpenProcess(SYNCHRONIZE, FALSE, pidB);
        const bool alive = pb && WaitForSingleObject(pb, 0) == WAIT_TIMEOUT;
        if (pb) CloseHandle(pb);
        check(alive && lines_of_pid(viewer_log(), pidB).find("no effect (session established)") != std::string::npos,
              "decision 1: the connect's cancel does not close an established session (it says 'no effect')");
      }
      const size_t hostBFrom = H("KA-B").proc.Text().size();
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
      oldEpoch = gOwnerEpoch.load();
      js(L"document.getElementById('signOut').click();true");
      const bool bEnded = pB && wait_for([&] { return WaitForSingleObject(pB, 0) == WAIT_OBJECT_0; }, 12000);
      DWORD code = 99;
      if (pB) GetExitCodeProcess(pB, &code);
      check(bEnded, "sign-out: the shell's ordinary viewer of KA-B ended");
      {
        const std::string vb = lines_of_pid(viewer_log(), pidB);
        const size_t at = vb.find("owner fence: held input released (modifierUps=");
        check(at != std::string::npos && vb.find("input and clipboard cut", at) != std::string::npos,
              "...through the owner fence: held input released FIRST, then input and clipboard cut, then its own teardown",
              at == std::string::npos ? "" : vb.substr(at, vb.find('\n', at) - at).substr(0, 110));
      }
      check(code == 0 && client_log().substr(logBefore).find("did not close in time") == std::string::npos,
            "...exit 0, not ended by the shell's handle", "exit=" + std::to_string(code));
      // r4 (Codex R3): the releases reached the host BEFORE the session ended -- with the sender held 800 ms
      {
        const std::string vb = lines_of_pid(viewer_log(), pidB);
        const size_t ack = vb.find("owner fence: releases confirmed after ");
        unsigned long waited = 0;
        if (ack != std::string::npos) waited = std::strtoul(vb.c_str() + ack + std::strlen("owner fence: releases confirmed after "), nullptr, 10);
        check(ack != std::string::npos && waited >= 800,
              "R3: the fence waited for its releases (sender held 800 ms): acknowledged by the host, then closed",
              "waited " + std::to_string(waited) + " ms");
        // the host ends its side when the viewer's close (or its silence) reaches it -- wait for that line
        wait_for([&] { return H("KA-B").proc.Text().substr(hostBFrom).find("udp control session ended") != std::string::npos; }, 15000);
        const std::string hb = H("KA-B").proc.Text().substr(hostBFrom);
        const size_t up = hb.find("[input-timing]");
        size_t ups = 0;
        for (size_t at = hb.find("eventKind=6"); at != std::string::npos; at = hb.find("eventKind=6", at + 1)) ++ups;
        const size_t ended = hb.find("udp control session ended");
        check(ups > 0 && up != std::string::npos && ended != std::string::npos && hb.find("eventKind=6") < ended &&
                  hb.find("injectionEnabled=1") == std::string::npos,
              "R3: ...the host logged the key-ups (eventKind=6) BEFORE the session ended (injection off)",
              std::to_string(ups) + " key-up(s)");
      }
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
    // ======================================================== R2 (r3): a late connect notice of the old owner
    {
      check(sign_in_through_page(), "R2: signed in again (a new owner)");
      const std::string recentBefore = read_file(recent_path());
      const size_t logFrom = client_log().size();
      // the notice a viewer of the signed-out owner would send late: its op, a host, the OLD owner epoch
      on_ordinary_connected(gViewerOperation, H("KA-B").id, oldEpoch);
      on_ordinary_connected(gViewerOperation + 1, H("KA-E").id, oldEpoch);
      pump(500);
      check(read_file(recent_path()) == recentBefore && client_log().substr(logFrom).find("committed") == std::string::npos &&
                client_log().substr(logFrom).find("making room") == std::string::npos,
            "R2: the old owner's late connect notice changes nothing for the new owner (recent list, PC count)");
      // r4 (Codex R2): the pump's end and the exit's decision, in both orders, through the shell's own two functions
      // (the same lock): nothing is left behind for either op, and a connected line after the exit is not counted
      const uint64_t opA = 900000001, opB = 900000002;
      note_exit_decided(opA);  // the exit's wait ran out first
      bool refusedLate = false;
      {
        std::lock_guard<std::mutex> lock(gConnectedMu);
        refusedLate = gEndedBeforePump.count(opA) != 0;  // the pump's late "connected" line is then not counted
      }
      note_pump_end(opA);
      note_pump_end(opB);      // the pump ended first
      note_exit_decided(opB);
      bool clean = false;
      {
        std::lock_guard<std::mutex> lock(gConnectedMu);
        clean = !gEndedBeforePump.count(opA) && !gPumpEnded.count(opA) && !gEndedBeforePump.count(opB) && !gPumpEnded.count(opB);
      }
      check(refusedLate && clean, "R2: pump end / exit in either order: a late line refused, nothing left behind");
    }

    DestroyWindow(gWindow);
    pump(500);
    if (gManaged) {
      gManaged->JoinReapers();
      gManaged.reset();
    }
    gDialogWatchStop = true;
    dialogWatch.join();
    const char* external = gExternal.load();
    check(!external, "no download, new window, OS dialog, macro window or WebView re-creation during the run",
          external ? external : "");
    check(GetFileAttributesW(macroUdf.c_str()) == INVALID_FILE_ATTRIBUTES,
          "...and no macro WebView was created under the run's macro UDF (its folder does not exist)");
  }
  for (auto& h : hosts) h->proc.End();  // each host's end is the guard's final judgement (Finalize)
  server.End();
  // N2: every child this test started -- ended here if still running -- judged on its end, EOF and log
  check(ka_pc_e2e::children_end_all() == 0, "every child ended (confirmed), read to EOF, kept in its log");
  // the shared guard: every launch of the run judged before the result
  e2e::e2e_injection_final_check_all();
  std::printf("\nka_pc_shell_e2e_test: %s (%d failed)\n", gFailures ? "FAILED" : "ALL PASS", gFailures);
  return gFailures ? 1 : (result ? result : 0);
}
