// Compile the actual shell entry/handlers into an isolated executable. Only its process entry
// is renamed: HTML, WebView, native bridge, workers, HTTP and owner adoption are production code.
#define wWinMain recovery_product_entry
#include "client_shell_main.cpp"
#undef wWinMain
#include <shlwapi.h>
#include <cstdio>
#include <stdexcept>
#include <filesystem>

namespace {
void recovery_pump(unsigned ms) {
  const uint64_t end = GetTickCount64() + ms;
  do {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    Sleep(5);
  } while (GetTickCount64() < end);
}
bool recovery_wait(const std::function<bool()>& predicate, unsigned ms = 10000) {
  const uint64_t end = GetTickCount64() + ms;
  while (GetTickCount64() < end) { if (predicate()) return true; recovery_pump(20); }
  return predicate();
}
std::string recovery_eval(const std::wstring& script) {
  struct Reply { bool done=false; std::string text; };
  auto reply = std::make_shared<Reply>();
  if (!gWebView) return {};
  gWebView->ExecuteScript(script.c_str(), Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
    [reply](HRESULT hr, LPCWSTR value) -> HRESULT {
      if (SUCCEEDED(hr) && value) reply->text = narrow(value);
      reply->done = true; return S_OK;
    }).Get());
  recovery_wait([&] { return reply->done; }, 3000);
  return reply->text;
}
void recovery_check(bool value, const char* label) {
  std::printf("%s %s\n", value ? "PASS" : "FAIL", label);
  if (!value) throw std::runtime_error(label);
}
bool recovery_dom(const std::wstring& script, unsigned timeout = 10000) {
  const uint64_t end = GetTickCount64() + timeout;
  while (GetTickCount64() < end) {
    if (recovery_eval(script) == "true") return true;
    recovery_pump(30);
  }
  return false;
}
}

/** The one place this test points the shell at its own directory -- and the mutation target. */
void set_shell_test_data_dir(const std::filesystem::path& dir) {
  std::filesystem::create_directories(dir);
  gShellTestDataDir = dir.wstring();
}

/**
 * The one place this test names the directory the shell talks to -- and the mutation target for
 * "the address comes from the program, not from client.txt or the page".
 */
void set_shell_test_directory_url(const std::string& url) {
  gShellTestDirectoryUrl = url;
}

/** Photographs what the WebView has rendered, into a file inside the fixture's reach. */
bool recovery_capture(const std::wstring& path) {
  ComPtr<IStream> stream;
  if (FAILED(SHCreateStreamOnFileEx(path.c_str(), STGM_CREATE | STGM_READWRITE,
                                    FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, &stream))) {
    return false;
  }
  auto captured = std::make_shared<std::atomic<int>>(0);
  gWebView->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream.Get(),
    Callback<ICoreWebView2CapturePreviewCompletedHandler>([captured](HRESULT hr) -> HRESULT {
      captured->store(SUCCEEDED(hr) ? 1 : -1); return S_OK;
    }).Get());
  recovery_wait([&] { return captured->load() != 0; });
  return captured->load() == 1;
}

int main(int argc, char** argv) {
  // Stand-in viewer mode. begin_session starts THIS executable in place of GNLinkViewer.exe when
  // the handle-leak case points the test seam at it: it waits on the cancel event it was handed,
  // exactly as a connecting viewer would be told to stop, and exits 0. Nothing else runs. (RV-16)
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--cancel-event") {
      // What directory the shell told its viewer to use, written down where the test can read
      // it: the viewer is the one consumer of the address that lives in another process.
      if (const char* root = std::getenv("GNLINK_RECOVERY_TEST_ROOT")) {
        for (int j = 1; j + 1 < argc; ++j) {
          if (std::string(argv[j]) != "--directory-url") continue;
          std::ofstream seen(std::filesystem::path(root) / "viewer-directory-urls.txt",
                             std::ios::app);
          seen << argv[j + 1] << "\n";
        }
      }
      HANDLE cancel = remote60::native_poc::viewer::viewer_cancel_handle_from_arg(widen(argv[i + 1]));
      if (cancel) WaitForSingleObject(cancel, 60000);
      return 0;
    }
  }
  if (argc != 5) {
    std::puts("usage: client_recovery_ui_test <fixture-url> <delay-flag> <screenshot> <decoy-url>");
    return 2;
  }
  // Refuse the test before opening a WebView unless its profile is a private runner fixture.
  // In particular, never share the installed client's normal TEMP/GNLinkClient environment.
  const char* fixture = std::getenv("GNLINK_RECOVERY_TEST_ROOT");
  wchar_t temporary[MAX_PATH]{}; GetTempPathW(MAX_PATH, temporary);
  if (!fixture || !std::filesystem::exists(std::filesystem::path(fixture) / ".fixture") ||
      std::filesystem::weakly_canonical(temporary) !=
          std::filesystem::weakly_canonical(std::filesystem::path(fixture) / "profile") ||
      std::string(argv[1]).rfind("http://127.0.0.1:", 0) != 0 ||
      std::string(argv[4]).rfind("http://127.0.0.1:", 0) != 0 ||
      std::string(argv[4]) == argv[1]) {
    std::puts("FAIL isolated fixture/profile required; no browser process was opened or terminated"); return 2;
  }
  // Before anything in the shell runs, for the same reason as the data directory below: the
  // start-up update check asks for the address, and a test build with none ends the process.
  set_shell_test_directory_url(argv[1]);
  // Where the shell keeps its settings and logs, inside the fixture -- set before anything in the
  // shell runs, and checked. This test wrote the user's real %LOCALAPPDATA%\GNLink\client.txt on
  // 2026-09-23 because the path comes from SHGetKnownFolderPath, which the runner's LOCALAPPDATA
  // does not reach. (RV-00)
  set_shell_test_data_dir(std::filesystem::path(fixture) / "shelldata");
  {
    const std::filesystem::path root = std::filesystem::weakly_canonical(fixture);
    const std::filesystem::path settings = std::filesystem::weakly_canonical(settings_path());
    const std::wstring rel = settings.lexically_relative(root).wstring();
    const bool inside = !rel.empty() && rel.rfind(L"..", 0) != 0;
    wchar_t appData[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", appData, MAX_PATH);
    const std::wstring appRel = (n > 0 && n < MAX_PATH)
        ? std::filesystem::weakly_canonical(appData).lexically_relative(root).wstring()
        : std::wstring();
    const bool appInside = !appRel.empty() && appRel.rfind(L"..", 0) != 0;
    std::printf("%s the shell's settings and logs live inside the fixture\n", inside ? "PASS" : "FAIL");
    std::printf("%s LOCALAPPDATA (viewers, host health log) is inside the fixture\n",
                appInside ? "PASS" : "FAIL");
    if (!inside || !appInside) {
      std::puts("FAIL refusing to start: the shell could reach the user's real files");
      return 2;
    }
  }
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  std::string socketError;
  if (!initialize_sockets(&socketError)) return 2;
  LogUploadShutdown uploader;
  int result = 0;
  try {
    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=wnd_proc;
    wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=kWindowClass;
    wc.hCursor=LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    RegisterClassExW(&wc);
    gWindow=CreateWindowExW(0,kWindowClass,L"GNLink isolated recovery test",WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT,CW_USEDEFAULT,560,760,nullptr,nullptr,wc.hInstance,nullptr);
    ShowWindow(gWindow,SW_SHOW);
    // ------------------------------------------------ fixed-server: the file names another server
    //
    // What an installed client has on disk after years of the old form: an address on line 1.
    // Here it is the decoy -- a listener the runner owns and counts requests on. Everything below
    // runs with this file in place, and the runner fails the run if the decoy heard anything.
    {
      std::ofstream stored(settings_path(), std::ios::trunc);
      stored << argv[4] << "\nstored-account\n9000\n30\n1\n";
    }
    recovery_check(SUCCEEDED(create_shell_webview()), "production WebView creation starts");
    recovery_check(recovery_dom(L"!!document.getElementById('signIn')"), "production login UI loads");

    // ------------------------------------------------ fixed-server: what the sign-in form shows
    //
    // Asked of the rendered page, not of the source: every input, select and textarea that has a
    // box on screen inside the sign-in card, in document order.
    const std::string shownInputs = recovery_eval(
        L"(function(){var out=[];"
        L"document.querySelectorAll('#signInCard input,#signInCard select,#signInCard textarea')"
        L".forEach(function(el){var r=el.getBoundingClientRect();"
        L"if(r.width>0&&r.height>0&&getComputedStyle(el).visibility!=='hidden')"
        L"out.push(el.id+':'+el.type);});return out.join(',');})()");
    std::printf("      (sign-in inputs rendered: %s)\n", shownInputs.c_str());
    recovery_check(shownInputs == "\"account:text,password:password\"",
                   "[fixed-server] the sign-in form shows an id and a password and nothing else");
    recovery_check(recovery_eval(L"document.getElementById('server')===null&&"
                                 L"!/서버|server/i.test(document.getElementById('signInCard').innerText)") == "true",
                   "[fixed-server] no server field and no mention of one on the sign-in card");
    recovery_check(recovery_dom(L"document.getElementById('account').value==='stored-account'&&"
                                L"document.getElementById('bitrate').value==='9000'", 5000),
                   "[fixed-server] the stored file WAS read: its account and settings are restored");
    recovery_check(recovery_eval(L"document.activeElement&&document.activeElement.id") == "\"password\"",
                   "[fixed-server] with the id remembered, the caret is in the password field");
    // "Can be pressed" is more than exists: on screen, enabled, and nothing lying over it.
    recovery_check(recovery_eval(
        L"(function(){var b=document.getElementById('signIn'),r=b.getBoundingClientRect();"
        L"var x=r.left+r.width/2,y=r.top+r.height/2;"
        L"return !b.disabled&&r.width>0&&r.height>0&&x>=0&&y>=0&&x<=innerWidth&&y<=innerHeight&&"
        L"document.elementFromPoint(x,y)===b;})()") == "true",
                   "[fixed-server] the sign-in button is on screen, enabled and not covered");
    recovery_check(recovery_capture(widen(std::string(argv[3]) + ".login.png")),
                   "[fixed-server] the sign-in screen is photographed");

    // Nothing typed: the message names the two things that are asked for, and nothing is sent.
    recovery_eval(L"document.getElementById('account').value='';"
                  L"document.getElementById('signIn').click();true");
    recovery_check(recovery_eval(L"document.getElementById('signInMsg').textContent==="
                                 L"'아이디와 비밀번호를 모두 입력해 주세요.'") == "true" &&
                   recovery_eval(L"document.activeElement&&document.activeElement.id") == "\"account\"" &&
                   gSessionToken.empty(),
                   "[fixed-server] an empty form asks for the id and password, and the caret goes to the id");

    const auto typeCredentials = [&](const wchar_t* account) {
      recovery_eval(std::wstring(L"document.getElementById('account').value='") + account +
                    L"';document.getElementById('password').value='fixture-password';true");
    };
    const auto expectHostList = [&](const char* label) {
      recovery_check(recovery_dom(L"!document.getElementById('hostsCard').classList.contains('hidden')"),
                     label);
    };
    const auto signIn = [&](const wchar_t* account) {
      typeCredentials(account);
      recovery_eval(L"document.getElementById('signIn').click();true");
      expectHostList("DOM login crosses native handler and real HTTP into host list");
    };

    // Enter in the password field, which is how a person with two fields signs in.
    typeCredentials(L"account-a");
    recovery_eval(L"document.getElementById('password').dispatchEvent("
                  L"new KeyboardEvent('keydown',{key:'Enter',bubbles:true}));true");
    expectHostList("[fixed-server] id + password + Enter reaches the host list over real HTTP");
    {
      // What was written back: the same layout an older build reads, with the program's own
      // address on line 1 -- not the decoy that was there, and not nothing.
      std::ifstream saved(settings_path());
      std::string line1, line2;
      std::getline(saved, line1);
      std::getline(saved, line2);
      recovery_check(line1 == argv[1] && line2 == "account-a",
                     "[fixed-server] client.txt keeps its layout: line 1 the program's server, line 2 the id");
    }

    // A page that names a server of its own. The old message had this field and the native side
    // obeyed it; now it is not read, so the sign-in still goes to the program's server.
    recovery_eval(L"document.getElementById('signOut').click();true");
    recovery_check(recovery_dom(L"!document.getElementById('signInCard').classList.contains('hidden')"),
                   "[fixed-server] signed out again");
    recovery_eval(std::wstring(L"window.chrome.webview.postMessage(JSON.stringify({type:'login',server:'") +
                  widen(argv[4]) + L"',accountId:'account-a',password:'fixture-password'}));true");
    expectHostList("[fixed-server] a login message naming another server signs in to the program's server");
    recovery_eval(L"document.getElementById('signOut').click();true");
    recovery_check(recovery_dom(L"!document.getElementById('signInCard').classList.contains('hidden')"),
                   "[fixed-server] signed out before the recovery cases");

    signIn(L"account-a");
    { std::ofstream flag(argv[2]); flag << '1'; }
    recovery_eval(L"document.getElementById('refresh').click();true");
    const std::wstring seen = widen(std::string(argv[2]) + ".seen");
    recovery_check(recovery_wait([&] { return GetFileAttributesW(seen.c_str()) != INVALID_FILE_ATTRIBUTES; }),
                   "real host-list request is delayed at server boundary");
    recovery_eval(L"document.getElementById('signOut').click();true");
    recovery_check(recovery_dom(L"!document.getElementById('signInCard').classList.contains('hidden')"), "logout returns to login UI");
    recovery_pump(1800);
    recovery_check(gSessionToken.empty() && recovery_eval(L"document.getElementById('hostsCard').classList.contains('hidden')") == "true",
                   "late refresh cannot restore signed-out UI or credentials");
    signIn(L"account-b");
    const std::string token = gSessionToken;
    UINT oldBrowser = 0;
    recovery_check(gWebView && SUCCEEDED(gWebView->get_BrowserProcessId(&oldBrowser)) && oldBrowser,
                   "identify browser belonging to isolated WebView environment");
    // The one pid-based open left in this test, and why it is still here: WebView2 starts its
    // browser process itself, so there is no launch handle to use. It is therefore VERIFIED
    // before anything is terminated, while the handle is held (so the pid cannot be reused
    // underneath us): the image must be msedgewebview2.exe, it must have been created after
    // this test started, and WebView must still report this same pid. Any mismatch is a
    // failure and nothing is killed. (RV-17)
    HANDLE browser = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                 FALSE, oldBrowser);
    recovery_check(browser != nullptr, "pin isolated browser process handle");
    {
      wchar_t image[MAX_PATH]{};
      DWORD imageLen = MAX_PATH;
      const bool named = QueryFullProcessImageNameW(browser, 0, image, &imageLen) &&
                         std::wstring(image).find(L"msedgewebview2.exe") != std::wstring::npos;
      FILETIME createdThem{}, createdUs{}, x1{}, x2{}, x3{}, x4{};
      const bool times = GetProcessTimes(browser, &createdThem, &x1, &x2, &x3) &&
                         GetProcessTimes(GetCurrentProcess(), &createdUs, &x1, &x2, &x4);
      const bool newer = times && CompareFileTime(&createdThem, &createdUs) >= 0;
      UINT stillReported = 0;
      const bool same = gWebView && SUCCEEDED(gWebView->get_BrowserProcessId(&stillReported)) &&
                        stillReported == oldBrowser;
      recovery_check(named && newer && same,
                     "the process about to be terminated is this test's own WebView browser");
    }
    const BOOL terminated = TerminateProcess(browser, 99);
    WaitForSingleObject(browser, 3000); CloseHandle(browser);
    recovery_check(terminated != FALSE, "inject failure only into this test browser");
    recovery_check(recovery_wait([&] {
      UINT current = 0;
      return gWebView && SUCCEEDED(gWebView->get_BrowserProcessId(&current)) && current && current != oldBrowser;
    }, 20000), "production ProcessFailed handler recreates WebView");
    recovery_check(recovery_dom(L"!document.getElementById('hostsCard').classList.contains('hidden') && document.getElementById('accountLabel').textContent === 'account-b'", 20000)
                   && gSessionToken == token, "browser recovery restores current account without another login");
    gReconnectRequest = ShellConnectRequest{};
    post_status("reconnecting", "재연결 시험");
    recovery_check(recovery_dom(L"!document.getElementById('cancelReconnect').classList.contains('hidden')"), "reconnect cancel control is visible");
    recovery_eval(L"document.getElementById('cancelReconnect').click();true");
    recovery_check(recovery_wait([&] { return !gReconnectRequest; }), "DOM reconnect cancel reaches native cancellation");
    // ---------------------------------------------- the cancel handles do not pile up
    //
    // The shell holds one event handle per host and closes it on two paths: when the viewer is
    // replaced, and when it exits. Two paths means two chances to forget, and a handle leak is
    // invisible until a long-running shell runs out -- so this counts them rather than trusting
    // the reading.
    //
    // Replacement is the interesting one: it happens whenever the user picks the same PC again,
    // which for a flaky connection is a thing they do repeatedly.
    {
      DWORD before = 0;
      GetProcessHandleCount(GetCurrentProcess(), &before);

      // Through begin_session itself, fifty times. (RV-16) This used to be a hand-written copy of
      // what begin_session does, so deleting the product's CloseHandle left it passing. The
      // viewer is a stand-in -- this executable, waiting on the cancel event it is handed (see
      // the top of main) -- because the thing counted is the shell's handles, and fifty real
      // viewers would each open a window and a connection for nothing.
      wchar_t selfPath[MAX_PATH]{};
      GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
      gShellTestViewerExe = selfPath;
      ShellConnectRequest host{};
      host.hostId = "leak-host";
      host.hostName = "leak-pc";
      const uint32_t viewersAtStart = gActiveViewers.load();
      for (int i = 0; i < 50; ++i) begin_session(host);  // each replaces the one before
      // The last one is called off here; every one of the fifty exits is then adopted.
      {
        const auto last = gViewerByHost.find(host.hostId);
        if (last != gViewerByHost.end()) {
          remote60::native_poc::viewer::viewer_request_cancel(last->second.cancelEvent);
        }
      }
      recovery_check(recovery_wait([&] {
        return gActiveViewers.load() == viewersAtStart &&
               gViewerByHost.find(host.hostId) == gViewerByHost.end();
      }, 30000), "fifty replacements through the real begin_session all end and are adopted");
      {
        // Each of those stand-ins wrote down the --directory-url it was started with. client.txt
        // named the decoy when this shell started; a viewer sent there would say so here.
        std::ifstream seen(std::filesystem::path(fixture) / "viewer-directory-urls.txt");
        size_t viewers = 0, elsewhere = 0;
        for (std::string url; std::getline(seen, url);) {
          ++viewers;
          if (url != argv[1]) ++elsewhere;
        }
        std::printf("      (viewers that recorded their directory: %zu, not the program's: %zu)\n",
                    viewers, elsewhere);
        recovery_check(viewers >= 1 && elsewhere == 0,
                       "[fixed-server] every viewer was handed the program's server as --directory-url");
      }

      // Launches that fail, through the product's own failure path: a file that exists (so the
      // existence check passes) but is not a program (so CreateProcessW refuses it). begin_session
      // creates the event before the launch, and the failure path has to dispose of it.
      const std::filesystem::path notAProgram = std::filesystem::path(fixture) / "not-a-program.exe";
      { std::ofstream stub(notAProgram); stub << "not a program\n"; }
      gShellTestViewerExe = notAProgram.wstring();
      ShellConnectRequest failing{};
      failing.hostId = "fail-host";
      failing.hostName = "fail-pc";
      for (int i = 0; i < 50; ++i) begin_session(failing);
      recovery_check(gActiveViewers.load() == viewersAtStart &&
                         gViewerByHost.find(failing.hostId) == gViewerByHost.end(),
                     "fifty launches that fail start nothing and record nothing");
      gShellTestViewerExe.clear();
      recovery_pump(300);

      // The pipe-less fallback. Without the pipe the child inherits nothing and is passed no
      // --cancel-event, so keeping the event would mean signalling something nobody waits on
      // and reporting asked=1 while the old viewer carries on.
      {
        // Driven through the helper begin_session itself uses, so this asks the real question.
        // Built by hand first, which tested nothing: it asserted that signalling a null handle
        // fails, which was never in doubt, and a mutation that kept the event sailed past it.
        HANDLE notInherited = remote60::native_poc::viewer::viewer_create_cancel_event();
        const HANDLE stored =
            remote60::native_poc::viewer::viewer_cancel_slot_handle(false, notInherited);
        recovery_check(stored == nullptr,
                       "an event the child never received is not kept");
        gViewerByHost["pipeless-host"] = ViewerCancelSlot{4242, stored};
        const bool asked = remote60::native_poc::viewer::viewer_request_cancel(
            gViewerByHost["pipeless-host"].cancelEvent);
        recovery_check(!asked,
                       "...so a replacement reports asked=0 rather than a false success");
        gViewerByHost.erase("pipeless-host");

        // And the ordinary case still keeps it, or the rule above would be "never keep one".
        HANDLE inherited = remote60::native_poc::viewer::viewer_create_cancel_event();
        const HANDLE keptHandle =
            remote60::native_poc::viewer::viewer_cancel_slot_handle(true, inherited);
        recovery_check(keptHandle == inherited && keptHandle != nullptr,
                       "an event the child did receive is kept");
        if (keptHandle) CloseHandle(keptHandle);
      }

      // The shell's worker group joins a finished worker only when the next one is launched, so
      // the watcher and pipe-reader threads of viewers that ended after the loop are still held.
      // That is the product's reaping, not a leak, and it is triggered here the product's way --
      // by launching (twice: the first reaps, and its own thread is reaped by the second).
      gWorkers.Launch([] {});
      recovery_pump(300);
      gWorkers.Launch([] {});
      recovery_pump(300);
      DWORD after = 0;
      GetProcessHandleCount(GetCurrentProcess(), &after);
      recovery_check(gViewerByHost.find(host.hostId) == gViewerByHost.end(),
                     "the viewer's exit forgets its cancel handle");
      // Not equality: this process has a WebView2 in it, doing its own work on its own threads,
      // so the count moves for reasons that have nothing to do with this. Fifty replacements
      // leaking would show as fifty; a few either way is the rest of the program breathing.
      std::printf("      (handles: %lu before, %lu after)\n", before, after);
      recovery_check(after <= before + 8,
                     "fifty replacements and fifty failed launches do not pile up handles");
    }

    // ------------------------- a viewer that cannot be cancelled is not replaced behind its back
    //
    // The hole this closes: when the old viewer had no cancel event (the pipe-less fallback) or
    // the signal failed, begin_session logged asked=0, dropped the slot, and started another one
    // anyway. Two viewers then raced for the same host with no fence between them -- exactly the
    // situation the cancel channel exists to prevent, reappearing on the path where the channel
    // is missing.
    //
    // Driven through the real begin_session, not a copy of its logic. The predecessor is a real
    // live process with a real SYNCHRONIZE handle, which is what the decision actually reads.
    {
      const auto liveProcess = [](PROCESS_INFORMATION* out) {
        // Something that stays up and needs nothing: a viewer with no arguments exits at once,
        // so this is a plain wait.
        std::wstring cmd = L"cmd.exe /c ping -n 60 127.0.0.1 > nul";
        std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
        mutableCmd.push_back(L'\0');
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        return CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                              CREATE_NO_WINDOW, nullptr, nullptr, &si, out) != 0;
      };
      const auto syncHandle = [](HANDLE process) {
        HANDLE dup = nullptr;
        DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(), &dup, SYNCHRONIZE,
                        FALSE, 0);
        return dup;
      };

      ShellConnectRequest req{};
      req.hostId = "fence-host";
      req.hostName = "fence-pc";

      // (1) No event at all -- the pipe-less fallback. The predecessor is alive, so the
      // replacement must be refused and nothing new started.
      PROCESS_INFORMATION older{};
      recovery_check(liveProcess(&older), "a stand-in for an older viewer is running");
      gViewerByHost[req.hostId] = ViewerCancelSlot{older.dwProcessId, nullptr,
                                                   syncHandle(older.hProcess)};
      const uint32_t viewersBefore = gActiveViewers.load();
      begin_session(req);
      recovery_check(gActiveViewers.load() == viewersBefore,
                     "an uncancellable live viewer is not replaced -- nothing was started");
      recovery_check(gViewerByHost.find(req.hostId) != gViewerByHost.end(),
                     "...and its slot is kept, not dropped");
      // (A "at most one live viewer for that host" check stood here. The map holds one entry per
      // host, so it could only ever say 0 or 1 and could not fail. The count that can fail is the
      // gActiveViewers one above: a second viewer started would move it. RV-16)

      // (2) An event that cannot be signalled -- closed underneath us, so SetEvent fails. Same
      // rule: alive means refused.
      HANDLE doomed = remote60::native_poc::viewer::viewer_create_cancel_event();
      CloseHandle(doomed);   // the handle is now invalid; SetEvent on it fails
      gViewerByHost[req.hostId] = ViewerCancelSlot{older.dwProcessId, doomed,
                                                   syncHandle(older.hProcess)};
      begin_session(req);
      recovery_check(gActiveViewers.load() == viewersBefore,
                     "a failed signal is treated as not-asked, and the replacement is refused");

      // ...but once that process is gone, the same uncancellable slot must not block forever.
      TerminateProcess(older.hProcess, 0);           // the stand-in, not a product process
      WaitForSingleObject(older.hProcess, 5000);
      // (2b) Liveness that cannot be established at all. DuplicateHandle can fail at launch,
      // and then the slot has no process handle -- so "is it still running" has no answer.
      // With the test written as "refuse if confirmed alive" that fell through to replacing,
      // which is the same fail-open the whole branch exists to close. No answer must mean no.
      {
        PROCESS_INFORMATION ghost{};
        recovery_check(liveProcess(&ghost), "another stand-in is running");
        const uint32_t before = gActiveViewers.load();
        gViewerByHost[req.hostId] = ViewerCancelSlot{ghost.dwProcessId, nullptr, nullptr};
        begin_session(req);
        recovery_check(gActiveViewers.load() == before,
                       "a slot with no liveness handle refuses the replacement too");
        recovery_check(gViewerByHost.find(req.hostId) != gViewerByHost.end(),
                       "...and keeps its slot");
        gViewerByHost.erase(req.hostId);
        TerminateProcess(ghost.hProcess, 0);
        WaitForSingleObject(ghost.hProcess, 5000);
        CloseHandle(ghost.hProcess);
        CloseHandle(ghost.hThread);
      }

      // (3) The other half of the same rule, and the one that matters for not wedging the UI: an
      // uncancellable slot whose process has ALREADY GONE must not block the next session
      // forever. begin_session has to look, find it dead, and carry on.
      //
      // The first version of this checked that the test's own WaitForSingleObject said "dead",
      // which is a statement about the test rather than about begin_session -- and a mutant that
      // never looked at liveness passed it. What follows calls begin_session and watches what it
      // does.
      const uint32_t beforeDead = gActiveViewers.load();
      begin_session(req);
      const auto after = gViewerByHost.find(req.hostId);
      recovery_check(after != gViewerByHost.end() && after->second.pid != older.dwProcessId,
                     "a dead predecessor does not block the next session");
      recovery_check(gActiveViewers.load() == beforeDead + 1,
                     "...and a new viewer really was started");
      CloseHandle(older.hProcess);
      CloseHandle(older.hThread);

      // Stop the viewer this case started. The cancel is tried first because that is what the
      // shell does -- but the watcher stops once connect is over, so a viewer that got as far
      // as a session will not answer it. This is the TEST cleaning up its own process, and
      // leaving it running holds GNLinkViewer.exe open and fails the next link.
      if (after != gViewerByHost.end()) {
        remote60::native_poc::viewer::viewer_request_cancel(after->second.cancelEvent);
        if (after->second.process &&
            WaitForSingleObject(after->second.process, 5000) != WAIT_OBJECT_0) {
          // Through the handle taken at launch, never by pid. (RV-17)
          if (after->second.testTerminate) {
            TerminateProcess(after->second.testTerminate, 0);
            WaitForSingleObject(after->second.testTerminate, 5000);
          }
        }
      }

      // Wait for the shell to ADOPT that exit rather than tidying the map by hand.
      //
      // Two reasons. The exit path is part of the wiring this case claims to drive, and
      // erasing the entry ourselves skips the real handle_viewer_exit. And it was leaking
      // into the next section: the worker thread notices the viewer has gone and posts to
      // the UI thread, which calls post_status and overwrites hostsMsg -- landing in the
      // middle of the legacy checks below and breaking "older viewer exit still updates the
      // live-session count" in four runs out of eight. The product was fine; this case was
      // walking away from an event it had started.
      const bool adopted = recovery_wait([&] {
        return gViewerByHost.find(req.hostId) == gViewerByHost.end() &&
               gActiveViewers.load() == beforeDead;
      }, 5000);
      recovery_check(adopted, "the shell adopts the viewer's exit on its own");
    }

    // ------------------------------ RV-05: a replaced viewer's exit leaves the shell to the new one
    //
    // The user presses a PC, then presses it again while that connection is still going. The old
    // viewer is called off and exits a moment later -- and that exit used to post idle: the list
    // unlocked and the card said "연결 가능" while the NEW viewer was still connecting, so the next
    // press cancelled the connection in progress. What is checked is what the user sees after
    // the old viewer's exit has been adopted: the card still connecting, the list still locked.
    // Real viewers both times, started by the real begin_session; the first press goes through
    // the page, the second is begin_session itself (on the page it follows a "새로 고침").
    {
      std::vector<DirectoryHostEntry> hosts(1);
      hosts[0].hostId = "replace-host";
      hosts[0].hostName = "replace-pc";
      hosts[0].online = true;
      std::string list = shell_hosts_json(hosts);
      post_to_page(list.substr(0, list.size() - 1) + ",\"accountId\":\"account-b\"}");
      recovery_check(recovery_dom(L"document.querySelectorAll('.host').length === 1 && !document.querySelector('.host').disabled"),
                     "[replace] the PC card is shown and can be pressed");
      const uint32_t viewersAtStart = gActiveViewers.load();
      recovery_eval(L"document.querySelector('.host').click();true");
      recovery_check(recovery_wait([&] {
        const auto it = gViewerByHost.find("replace-host");
        return it != gViewerByHost.end() && gActiveViewers.load() == viewersAtStart + 1;
      }), "[replace] pressing it starts a real viewer through begin_session");
      recovery_check(recovery_dom(L"document.querySelector('.host .state').textContent.includes('연결하는 중') && document.querySelector('.host').disabled"),
                     "[replace] the card says it is connecting and the list is locked");
      const uint64_t firstOperation = gViewerByHost["replace-host"].operation;

      ShellConnectRequest again{};
      again.hostId = "replace-host";
      again.hostName = "replace-pc";
      begin_session(again);
      recovery_check(gViewerByHost.count("replace-host") == 1 &&
                         gViewerByHost["replace-host"].operation > firstOperation,
                     "[replace] pressing it again replaces the first viewer with a second");
      // The first viewer was asked to stop; its exit is adopted when the count drops back to one.
      const bool firstGone = recovery_wait([&] {
        return gActiveViewers.load() == viewersAtStart + 1;
      }, 20000);
      recovery_pump(500);  // the adoption itself is posted to this thread
      recovery_check(firstGone, "[replace] the first viewer ended after being called off");
      recovery_check(recovery_dom(L"document.querySelector('.host .state').textContent.includes('연결하는 중') && document.querySelector('.host').disabled && document.getElementById('signIn').disabled", 3000),
                     "[replace] AFTER THE OLD VIEWER'S EXIT THE CARD STILL SAYS CONNECTING AND THE LIST STAYS LOCKED");

      // Clean up the second viewer the way the shell would, then through its launch handle.
      const auto second = gViewerByHost.find("replace-host");
      if (second != gViewerByHost.end()) {
        remote60::native_poc::viewer::viewer_request_cancel(second->second.cancelEvent);
        if (second->second.process &&
            WaitForSingleObject(second->second.process, 5000) != WAIT_OBJECT_0 &&
            second->second.testTerminate) {
          TerminateProcess(second->second.testTerminate, 0);
          WaitForSingleObject(second->second.testTerminate, 5000);
        }
      }
      recovery_check(recovery_wait([&] {
        return gViewerByHost.find("replace-host") == gViewerByHost.end() &&
               gActiveViewers.load() == viewersAtStart;
      }, 10000), "[replace] the second viewer's exit is adopted too");
      recovery_check(recovery_dom(L"!document.querySelector('.host').disabled", 5000),
                     "[replace] ...and only then does the list unlock");
    }

    gViewerOperation = 100;
    const uint64_t currentOwner = gOwnerEpoch.load();
    const ShellConnectRequest olderViewer{};
    gActiveViewers = 1;
    handle_viewer_exit(olderViewer, 99, currentOwner, WAIT_OBJECT_0, 0, 1000, 0);
    recovery_check(recovery_dom(L"document.getElementById('hostsMsg').textContent.includes('1개 연결')"),
                   "older viewer exit still updates the live-session count");
    gActiveViewers = 0;
    handle_viewer_exit(olderViewer, 99, currentOwner, WAIT_OBJECT_0, 0, 1000, 0);
    recovery_check(recovery_dom(L"document.getElementById('hostsMsg').textContent === ''"),
                   "last older viewer exit clears the remaining-session message");
    gReconnectRequest = ShellConnectRequest{};
    gReconnectAttempts = 2;
    handle_viewer_exit(olderViewer, 99, currentOwner, WAIT_OBJECT_0, 43, 90000, 0);
    recovery_check(gReconnectRequest.has_value() && gReconnectAttempts == 2,
                   "older viewer cannot replace or reset a newer reconnect");
    gReconnectRequest.reset(); gReconnectAttempts = 0;
    recovery_eval(L"document.getElementById('openSettings').click();true");
    post_status("reconnecting", "fixture busy");
    recovery_check(recovery_dom(L"document.getElementById('signIn').disabled"), "busy state reaches UI while settings is open");
    post_status("error", "fixture recovery error");
    recovery_check(recovery_dom(L"!document.getElementById('signIn').disabled"), "failure in settings also clears global busy state");
    recovery_eval(L"document.getElementById('closeSettings').click();true");
    recovery_check(recovery_capture(widen(argv[3])), "capture rendered production page");
    std::puts("client_recovery_ui_test: ALL PASS (real UI/native/HTTP; no updater installation)");
  } catch (const std::exception& error) { std::printf("client_recovery_ui_test: FAIL %s\n", error.what()); result=1; }
  // Whatever is still in the registry goes, pass or fail.
  //
  // The per-case cleanup only runs if that case got to the end of itself -- so a run that
  // FAILED partway (a mutation run, most often) left the viewer it had started alive, holding
  // GNLinkViewer.exe open and failing the next link with LNK1104. Found because one was still
  // running after a mutation round and somebody else noticed it before the next build did.
  for (auto& entry : gViewerByHost) {
    if (entry.second.cancelEvent) {
      remote60::native_poc::viewer::viewer_request_cancel(entry.second.cancelEvent);
      CloseHandle(entry.second.cancelEvent);
      entry.second.cancelEvent = nullptr;
    }
    // Only a slot whose viewer was LAUNCHED here has a terminate handle. The made-up pids some
    // cases put in the map (1000+i, 4242) have none and are left alone: OpenProcess ignores a
    // pid's low two bits, so reopening one of those would have reached a real process. (RV-17)
    if (entry.second.testTerminate) {
      if (WaitForSingleObject(entry.second.testTerminate, 2000) != WAIT_OBJECT_0) {
        TerminateProcess(entry.second.testTerminate, 0);
      }
      WaitForSingleObject(entry.second.testTerminate, 3000);
    }
    close_slot_handles(entry.second);
  }
  gViewerByHost.clear();

  if (gWindow && IsWindow(gWindow)) DestroyWindow(gWindow);
  gClosing=true; gWorkers.Shutdown();
  if (gController) gController->Close();
  gWebView.Reset(); gController.Reset();
  return result;
}
