// "로그인 전에도 원격 접속 허용" in GNLinkHost's own window, worked from outside through UI Automation
// (prelogin-ui r1, item 4).
//
// The window is the product's, in the asInvoker test build (GNLinkHostUiTest.exe, host_app_ui_test.cpp
// says what that build replaces); it comes up signed in from a fixture host.json. The service end is
// this process: a test pipe (\\.\pipe\GNLinkTest-...) served by the PRODUCT's server and decision
// (RunPreloginAdminServer / HandleAdminRequest) over in-memory primitives standing in for the machine
// store, the floor and the SCM -- so what reaches "the service" is exactly what GNLinkHost sent.
//
// What it shows: the checkbox is visible, enabled and not covered at its centre; toggling it on sends
// the host's directory credential and the service writes the record + raises the start type, and the
// window says so; toggling it off withdraws both; with the product's admin check (this run is not
// elevated) the window shows the refusal and the service is untouched.
// What it does NOT show: requireAdministrator / UAC / UIPI (asInvoker build), the real SCM change, the
// real ProgramData DACL -- those are G (one user-run batch).

#include <windows.h>
#include <sddl.h>
#include <UIAutomation.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "prelogin_admin_pipe.hpp"

namespace pl = remote60::native_poc::prelogin;

namespace {
int gChecks = 0, gFailures = 0;
void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}

std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

bool wait_until(const std::function<bool()>& done, int ms) {
  const ULONGLONG until = GetTickCount64() + ms;
  while (GetTickCount64() < until) {
    if (done()) return true;
    Sleep(50);
  }
  return done();
}

const std::wstring kPath = L"C:\\ProgramData\\GNLink\\credential\\machine.cred";

// r6 V3: the Run value in the test build's own key (Software\\GNLinkHostUiTest\\Run, never the user's).
void set_test_run_value(bool on) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\GNLinkHostUiTest\\Run", 0, nullptr, 0, KEY_WRITE, nullptr, &key,
                      nullptr) != ERROR_SUCCESS) {
    return;
  }
  const wchar_t value[] = L"\"C:\\nowhere\\GNLinkHost.exe\" --tray";
  if (on) {
    RegSetValueExW(key, L"GNLink", 0, REG_SZ, reinterpret_cast<const BYTE*>(value), sizeof(value));
  } else {
    RegDeleteValueW(key, L"GNLink");
  }
  RegCloseKey(key);
}
bool test_run_value_present() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\GNLinkHostUiTest\\Run", 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
  const bool present = RegQueryValueExW(key, L"GNLink", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
  RegCloseKey(key);
  return present;
}

struct World {
  std::string allowedUrl;
  std::mutex mu;
  std::map<std::wstring, pl::MachineRecord> files;
  uint64_t floor = 0;
  uint32_t startType = 3;
  bool haveRemembered = false;
  uint32_t remembered = 0;
  int calls = 0;
  // r7 H2: the order the service handled Enables (E) and Disables (D) in, and a stall: armed, the next
  // floor read waits until released -- the service is busy with that request, later ones queue at the pipe.
  std::string order;
  std::atomic<bool> stallArmed{false};
  std::atomic<bool> stalled{false};
  HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  pl::AdminPrimitives primitives() {
    pl::AdminPrimitives p;
    p.recordPath = [this] { std::lock_guard<std::mutex> l(mu); ++calls; return kPath; };
    p.machineId = [this] { std::lock_guard<std::mutex> l(mu); ++calls; order += 'E'; return std::string("ui-test-machine"); };
    p.readFloor = [this](uint64_t* out) {
      if (stallArmed.exchange(false)) {
        stalled = true;
        WaitForSingleObject(release, 15000);
      }
      std::lock_guard<std::mutex> l(mu);
      ++calls;
      *out = floor;
      return true;
    };
    p.raiseFloor = [this](uint64_t f) { std::lock_guard<std::mutex> l(mu); ++calls; order += 'D'; if (f > floor) floor = f; return true; };
    p.readRecord = [this](const std::wstring& path, pl::MachineRecord* out) {
      std::lock_guard<std::mutex> l(mu);
      ++calls;
      auto it = files.find(path);
      if (it == files.end()) return false;
      *out = it->second;
      return true;
    };
    p.writeRecord = [this](const std::wstring& path, const pl::MachineRecord& r) -> uint32_t {
      std::lock_guard<std::mutex> l(mu);
      ++calls;
      files[path] = r;
      return 0;
    };
    p.removeRecord = [this](const std::wstring& path) -> uint32_t { std::lock_guard<std::mutex> l(mu); ++calls; files.erase(path); return 0; };
    p.allowedDirectoryUrl = allowedUrl;  // r6 V7: the fixture server the window signs in to
    p.featureEnabled = [this] {
      std::lock_guard<std::mutex> l(mu);
      ++calls;
      auto it = files.find(kPath);
      return it != files.end() && it->second.enabled && it->second.ownerGeneration >= floor;
    };
    p.queryStartType = [this](uint32_t* out) { std::lock_guard<std::mutex> l(mu); ++calls; *out = startType; return true; };
    p.setStartType = [this](uint32_t t) -> uint32_t { std::lock_guard<std::mutex> l(mu); ++calls; startType = t; return 0; };
    p.loadRememberedStartType = [this](uint32_t* out) {
      std::lock_guard<std::mutex> l(mu);
      ++calls;
      if (!haveRemembered) return false;
      *out = remembered;
      return true;
    };
    p.rememberStartType = [this](uint32_t t) { std::lock_guard<std::mutex> l(mu); ++calls; haveRemembered = true; remembered = t; return true; };
    p.forgetRememberedStartType = [this] { std::lock_guard<std::mutex> l(mu); ++calls; haveRemembered = false; };
    return p;
  }
};

std::wstring current_user_sddl() {
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

struct Server {
  std::atomic<bool> run{false};
  std::thread t;
  void start(World* w, const std::wstring& name, pl::AdminIdentityCheck check) {
    run = true;
    const std::wstring sddl = current_user_sddl();
    t = std::thread([this, w, name, sddl, check] {
      pl::AdminPrimitives p = w->primitives();
      pl::RunPreloginAdminServer(p, [this] { return run.load(); }, name.c_str(), sddl.c_str(), check);
    });
    Sleep(200);
  }
  void stop() {
    run = false;
    if (t.joinable()) t.join();
  }
};

HWND window_of(DWORD pid) {
  struct Find {
    DWORD pid;
    HWND hwnd;
  } f{pid, nullptr};
  EnumWindows(
      [](HWND h, LPARAM lp) -> BOOL {
        auto* f = reinterpret_cast<Find*>(lp);
        DWORD p = 0;
        GetWindowThreadProcessId(h, &p);
        if (p == f->pid && IsWindowVisible(h) && GetWindow(h, GW_OWNER) == nullptr) {
          f->hwnd = h;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&f));
  return f.hwnd;
}

// The window as the user sees it, to a BMP beside the report.
bool save_window_bmp(HWND hwnd, const std::wstring& path) {
  RECT r{};
  GetWindowRect(hwnd, &r);
  const int w = r.right - r.left, h = r.bottom - r.top;
  if (w <= 0 || h <= 0) return false;
  HDC screen = GetDC(nullptr);
  HDC mem = CreateCompatibleDC(screen);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void* bits = nullptr;
  HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HGDIOBJ old = SelectObject(mem, bmp);
  const BOOL ok = PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT);
  SelectObject(mem, old);
  bool saved = false;
  if (ok && bits) {
    std::ofstream out(path, std::ios::binary);
    BITMAPFILEHEADER fh{};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(bi.bmiHeader);
    fh.bfSize = fh.bfOffBits + static_cast<DWORD>(w * h * 4);
    out.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
    out.write(reinterpret_cast<const char*>(&bi.bmiHeader), sizeof(bi.bmiHeader));
    out.write(static_cast<const char*>(bits), static_cast<std::streamsize>(w) * h * 4);
    saved = out.good();
  }
  DeleteObject(bmp);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
  return saved;
}

struct Ui {
  IUIAutomation* uia = nullptr;
  IUIAutomationElement* root = nullptr;
  IUIAutomationElement* find_by_id(int controlId) {
    VARIANT v;
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(std::to_wstring(controlId).c_str());
    IUIAutomationCondition* c = nullptr;
    uia->CreatePropertyCondition(UIA_AutomationIdPropertyId, v, &c);
    VariantClear(&v);
    IUIAutomationElement* e = nullptr;
    if (c) {
      root->FindFirst(TreeScope_Descendants, c, &e);
      c->Release();
    }
    return e;
  }
  static std::wstring name_of(IUIAutomationElement* e) {
    BSTR b = nullptr;
    std::wstring s;
    if (e && SUCCEEDED(e->get_CurrentName(&b)) && b) {
      s = b;
      SysFreeString(b);
    }
    return s;
  }
  static ToggleState toggle_state(IUIAutomationElement* e) {
    IUIAutomationTogglePattern* t = nullptr;
    ToggleState s = ToggleState_Indeterminate;
    if (e && SUCCEEDED(e->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&t))) && t) {
      t->get_CurrentToggleState(&s);
      t->Release();
    }
    return s;
  }
  static bool toggle(IUIAutomationElement* e) {
    IUIAutomationTogglePattern* t = nullptr;
    bool ok = false;
    if (e && SUCCEEDED(e->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&t))) && t) {
      ok = SUCCEEDED(t->Toggle());
      t->Release();
    }
    return ok;
  }
  static bool invoke(IUIAutomationElement* e) {
    IUIAutomationInvokePattern* i = nullptr;
    bool ok = false;
    if (e && SUCCEEDED(e->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&i))) && i) {
      ok = SUCCEEDED(i->Invoke());
      i->Release();
    }
    return ok;
  }
  // Visible, enabled, on screen, and the element at its centre is itself (nothing covers it).
  bool reachable(IUIAutomationElement* e, std::string* why) {
    BOOL offscreen = TRUE, enabled = FALSE;
    RECT r{};
    e->get_CurrentIsOffscreen(&offscreen);
    e->get_CurrentIsEnabled(&enabled);
    e->get_CurrentBoundingRectangle(&r);
    POINT c{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    IUIAutomationElement* at = nullptr;
    uia->ElementFromPoint(c, &at);
    BOOL same = FALSE;
    if (at) {
      uia->CompareElements(e, at, &same);
      at->Release();
    }
    *why = "offscreen=" + std::to_string(offscreen) + " enabled=" + std::to_string(enabled) + " rect=" +
           std::to_string(r.right - r.left) + "x" + std::to_string(r.bottom - r.top) + " atCentre=" + std::to_string(same);
    return !offscreen && enabled && r.right > r.left && r.bottom > r.top && same;
  }
};

constexpr int kIdPreloginAccess = 1025;
constexpr int kIdPreloginStatus = 1027;
constexpr int kIdSignOut = 1012;  // host_app_main.cpp ControlId::IdSignOut

}  // namespace

int wmain() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  wchar_t self[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  const std::filesystem::path exeDir = std::filesystem::path(self).parent_path();
  const std::filesystem::path hostExe = exeDir / L"GNLinkHostUiTest.exe";
  check("the GNLinkHost test build is beside this test", std::filesystem::exists(hostExe));

  // The fixture: everything the window may write lives here, inside the repository's scratch root.
  std::filesystem::path repo = exeDir;
  while (!repo.empty() && !std::filesystem::exists(repo / L"AGENTS.md")) repo = repo.parent_path();
  const std::filesystem::path root = repo / L".claude" / L"test-tmp" / (L"prelogin-toggle-" + std::to_wstring(GetCurrentProcessId()));
  std::filesystem::create_directories(root / L"appdata" / L"remote60");
  std::ofstream(root / L".fixture") << "owned test profile";
  const std::string url = "http://127.0.0.1:9";  // nothing answers: the window comes up signed in from the cache alone
  {
    std::ofstream hj(root / L"appdata" / L"remote60" / L"host.json");
    hj << "{\n  \"directoryUrl\": \"" << url << "\",\n  \"accountId\": \"host-user\",\n  \"machineId\": \"ui-test-machine\",\n"
       << "  \"hostName\": \"UI-TEST-PC\",\n  \"hostId\": \"h-ui-1\",\n  \"hostToken\": \"tok-ui-fixture\"\n}\n";
  }
  const std::wstring pipe = L"\\\\.\\pipe\\GNLinkTest-prelogin-ui-" + std::to_wstring(GetCurrentProcessId());
  World world;
  world.allowedUrl = url;
  Server server;
  server.start(&world, pipe, [](void*) { return true; });  // stands in for the admin check (asInvoker build)

  // The window, with only the fixture's paths and the test pipe.
  std::wstring env;
  {
    LPWCH block = GetEnvironmentStringsW();
    for (LPWCH p = block; *p; p += wcslen(p) + 1) {
      const std::wstring kv = p;
      if (kv.rfind(L"LOCALAPPDATA=", 0) == 0 || kv.rfind(L"GNLINK_HOST_TEST_", 0) == 0) continue;
      env += kv;
      env.push_back(L'\0');
    }
    FreeEnvironmentStringsW(block);
    for (const std::wstring& kv : {L"GNLINK_HOST_TEST_ROOT=" + root.wstring(), L"LOCALAPPDATA=" + (root / L"appdata").wstring(),
                                   std::wstring(L"GNLINK_HOST_TEST_DIRECTORY=") + std::wstring(url.begin(), url.end()),
                                   L"GNLINK_HOST_TEST_PRELOGIN_PIPE=" + pipe,
                                   // r5 G3: the Host trusts this test's server -- this process -- and nothing else.
                                   L"GNLINK_HOST_TEST_PRELOGIN_SERVER_PID=" + std::to_wstring(GetCurrentProcessId())}) {
      env += kv;
      env.push_back(L'\0');
    }
    env.push_back(L'\0');
  }
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
  // r6 V3: as if "Start with Windows" were on before -- in the TEST build's own key (the test build never
  // touches the user's real Run key: kRunKey under REMOTE60_HOST_TEST_SEAM).
  RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\GNLinkHostUiTest");
  set_test_run_value(true);
  std::wstring cmd = L"\"" + hostExe.wstring() + L"\"";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  const bool started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
                                      env.data(), root.wstring().c_str(), &si, &pi) != FALSE;
  if (started) {
    AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
  }
  check("GNLinkHost (test build) started", started);

  HWND hwnd = nullptr;
  wait_until([&] { return (hwnd = window_of(pi.dwProcessId)) != nullptr; }, 15000);
  check("its window is up", hwnd != nullptr);
  Ui ui;
  CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&ui.uia));
  if (hwnd && ui.uia) ui.uia->ElementFromHandle(hwnd, &ui.root);
  SetForegroundWindow(hwnd);
  IUIAutomationElement* box = nullptr;
  IUIAutomationElement* status = nullptr;
  wait_until([&] { return ui.root && (box = ui.find_by_id(kIdPreloginAccess)) != nullptr && !Ui::name_of(box).empty(); }, 10000);
  status = ui.root ? ui.find_by_id(kIdPreloginStatus) : nullptr;
  check("signed in from the cache, the toggle is there: \"로그인 전에도 원격 접속 허용\"",
        box && Ui::name_of(box) == L"로그인 전에도 원격 접속 허용", box ? narrow(Ui::name_of(box)) : "");
  const std::filesystem::path shots = repo / L".claude";
  if (box && status) {
    std::string why;
    wait_until([&] { return ui.reachable(box, &why); }, 5000);
    check("the toggle is visible, enabled, on screen and not covered at its centre", ui.reachable(box, &why), why);
    wait_until([&] { return !Ui::name_of(status).empty(); }, 5000);
    check("it starts off, as the service says (asked on show)", Ui::toggle_state(box) == ToggleState_Off && Ui::name_of(status) == L"꺼짐",
          narrow(Ui::name_of(status)));
    check("screenshot: off", save_window_bmp(hwnd, (shots / L"prelogin_toggle_0_off.bmp").wstring()));

    // On.
    check("toggled on through UI Automation", Ui::toggle(box));
    const bool on = wait_until([&] { std::lock_guard<std::mutex> l(world.mu); return world.files.count(kPath) == 1; }, 8000);
    pl::MachineRecord rec;
    {
      std::lock_guard<std::mutex> l(world.mu);
      if (world.files.count(kPath)) rec = world.files[kPath];
    }
    pl::DirectoryCredential dc;
    check("the service wrote the record from the window's request: enabled, the host's credential",
          on && rec.enabled && pl::UnpackDirectoryCredential(rec.credential, &dc) && dc.accountId == "host-user" &&
              dc.hostId == "h-ui-1" && dc.hostToken == "tok-ui-fixture" && rec.directoryOrigin == url);
    check("...and raised its start type to AUTO, remembering DEMAND", world.startType == 2 && world.haveRemembered && world.remembered == 3);
    wait_until([&] { return Ui::name_of(status).find(L"켜짐") == 0; }, 5000);
    check("the window says it is on, and the box is ticked", Ui::toggle_state(box) == ToggleState_On &&
                                                               Ui::name_of(status).find(L"켜짐") == 0,
          narrow(Ui::name_of(status)));
    check("screenshot: on", save_window_bmp(hwnd, (shots / L"prelogin_toggle_1_on.bmp").wstring()));
    // r6 V3: the service now starts GNLinkHost at logon -- the Run entry that would start a second one is gone.
    check("r6 V3: with access on, the Run entry is removed (the service starts GNLink at logon)",
          wait_until([] { return !test_run_value_present(); }, 3000));

    // r6 V3: and a second GNLinkHost in this session does not run beside the first.
    {
      PROCESS_INFORMATION second{};
      STARTUPINFOW si2{};
      si2.cb = sizeof(si2);
      std::wstring cmd2 = L"\"" + hostExe.wstring() + L"\" --tray";
      const bool s2 = CreateProcessW(nullptr, cmd2.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, env.data(),
                                     root.wstring().c_str(), &si2, &second) != FALSE;
      if (s2) AssignProcessToJobObject(job, second.hProcess);
      DWORD rc = STILL_ACTIVE;
      const bool left = s2 && WaitForSingleObject(second.hProcess, 8000) == WAIT_OBJECT_0 && GetExitCodeProcess(second.hProcess, &rc);
      check("r6 V3: a second GNLinkHost started in the same session leaves at once (exit 0), the first stays",
            left && rc == 0 && IsWindow(hwnd), std::to_string(rc));
      if (s2) {
        CloseHandle(second.hThread);
        CloseHandle(second.hProcess);
      }
    }

    // Off.
    check("toggled off through UI Automation", Ui::toggle(box));
    const bool off = wait_until([&] { std::lock_guard<std::mutex> l(world.mu); return world.files.empty(); }, 8000);
    check("the service withdrew the record and put the start type back", off && world.startType == 3 && !world.haveRemembered &&
                                                                               world.floor > rec.ownerGeneration);
    wait_until([&] { return Ui::name_of(status) == L"꺼짐"; }, 5000);
    check("the window says it is off", Ui::toggle_state(box) == ToggleState_Off && Ui::name_of(status) == L"꺼짐",
          narrow(Ui::name_of(status)));
    check("screenshot: off again", save_window_bmp(hwnd, (shots / L"prelogin_toggle_2_off.bmp").wstring()));
    check("r6 V3: with access off, the Run entry it took over is given back",
          wait_until([] { return test_run_value_present(); }, 3000));

    // The product's admin check: this run is not elevated, so the service refuses and nothing moves.
    server.stop();
    const int callsBefore = world.calls;
    server.start(&world, pipe, pl::ClientIsElevatedAdministrator);
    check("toggled on again, now against the product's admin check", Ui::toggle(box));
    wait_until([&] { return Ui::name_of(status).find(L"관리자") != std::wstring::npos; }, 8000);
    check("refused: the window says only an elevated GNLink Host may change it, and the box is not ticked",
          Ui::name_of(status).find(L"관리자") != std::wstring::npos && Ui::toggle_state(box) == ToggleState_Off,
          narrow(Ui::name_of(status)));
    check("...and the service was not touched", world.calls == callsBefore && world.files.empty() && world.startType == 3,
          std::to_string(world.calls - callsBefore));
    check("screenshot: refused", save_window_bmp(hwnd, (shots / L"prelogin_toggle_3_refused.bmp").wstring()));

    // r5 G6 + r7 H2: signing out of GNLink withdraws the machine's pre-login access -- also when the user
    // had just asked for it and that request has not reached the service yet. The service is kept busy
    // (another client's request stalls in it), "on" is clicked, then "Sign out": both wait at the pipe.
    // When the service is free, the Disable must be the last word: before r7 the two requests raced on
    // their own threads, and an Enable that got in after the Disable turned access back on.
    server.stop();
    server.start(&world, pipe, [](void*) { return true; });
    uint64_t floorBefore = 0;
    {
      std::lock_guard<std::mutex> l(world.mu);
      floorBefore = world.floor;
      world.order.clear();
    }
    ResetEvent(world.release);
    world.stallArmed = true;
    std::thread busy([&] {
      pl::AdminRequest q;
      q.op = pl::AdminOp::Query;
      pl::SendPreloginAdminRequest(q, 5000, pipe.c_str(), [](void*) { return true; });
    });
    const bool serviceBusy = wait_until([&] { return world.stalled.load(); }, 5000);
    check("H2: the service is busy with another request (stalled)", serviceBusy);
    check("H2: \"on\" is clicked while it is busy", Ui::toggle(box));
    Sleep(200);
    IUIAutomationElement* signOut = ui.find_by_id(kIdSignOut);
    check("G6/H2: ...and \"Sign out\" is pressed right after, through UI Automation", signOut && Ui::invoke(signOut));
    if (signOut) signOut->Release();
    Sleep(100);
    check("screenshot: signed out", save_window_bmp(hwnd, (shots / L"prelogin_toggle_4_signed_out.bmp").wstring()));
    // r7 H2: and the app is closed (tray Exit) while both requests still wait -- closing must not abandon
    // the sign-out's withdrawal. (Before r7 they died with the process: the service never heard of it.)
    PostMessageW(hwnd, WM_COMMAND, 2005 /* host_app_main.cpp IdMenuExit */, 0);
    Sleep(200);
    SetEvent(world.release);
    busy.join();
    const bool exited = WaitForSingleObject(pi.hProcess, 15000) == WAIT_OBJECT_0;
    check("H2: GNLinkHost closed (tray Exit)", exited);
    const bool settled = wait_until([&] {
      std::lock_guard<std::mutex> l(world.mu);
      return world.order.find('D') != std::string::npos;
    }, 8000);
    Sleep(1500);  // anything still on its way arrives
    {
      std::lock_guard<std::mutex> l(world.mu);
      const auto it = world.files.find(kPath);
      const bool on = it != world.files.end() && it->second.enabled && it->second.ownerGeneration >= world.floor;
      check("H2: when the service is free, the sign-out's Disable is the last word: access is OFF",
            settled && !on && world.files.empty(), "service order " + world.order);
      check("H2: ...no Enable was handled after the Disable", world.order.find("DE") == std::string::npos, world.order);
      check("G6: ...the floor raised past what was there, the start type put back",
            world.floor > floorBefore && world.startType == 3, std::to_string(world.floor));
    }
  }
  if (box) box->Release();
  if (status) status->Release();
  if (ui.root) ui.root->Release();
  if (ui.uia) ui.uia->Release();
  server.stop();
  RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\GNLinkHostUiTest");
  if (started) {
    TerminateProcess(pi.hProcess, 0);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
  CloseHandle(job);  // takes the window's own children (its stand-in streaming child) with it
  std::error_code ec;
  for (int i = 0; i < 40 && std::filesystem::exists(root); ++i) {
    std::filesystem::remove_all(root, ec);
    if (std::filesystem::exists(root)) Sleep(250);
  }
  check("the fixture directory is removed", !std::filesystem::exists(root));
  CoUninitialize();
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
