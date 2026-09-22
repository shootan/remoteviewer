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

int main(int argc, char** argv) {
  if (argc != 4) { std::puts("usage: client_recovery_ui_test <fixture-url> <delay-flag> <screenshot>"); return 2; }
  // Refuse the test before opening a WebView unless its profile is a private runner fixture.
  // In particular, never share the installed client's normal TEMP/GNLinkClient environment.
  const char* fixture = std::getenv("GNLINK_RECOVERY_TEST_ROOT");
  wchar_t temporary[MAX_PATH]{}; GetTempPathW(MAX_PATH, temporary);
  if (!fixture || !std::filesystem::exists(std::filesystem::path(fixture) / ".fixture") ||
      std::filesystem::weakly_canonical(temporary) !=
          std::filesystem::weakly_canonical(std::filesystem::path(fixture) / "profile") ||
      std::string(argv[1]).rfind("http://127.0.0.1:", 0) != 0) {
    std::puts("FAIL isolated fixture/profile required; no browser process was opened or terminated"); return 2;
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
    recovery_check(SUCCEEDED(create_shell_webview()), "production WebView creation starts");
    recovery_check(recovery_dom(L"!!document.getElementById('signIn')"), "production login UI loads");
    const auto signIn = [&](const wchar_t* account) {
      const std::wstring script = L"document.getElementById('server').value='" + widen(argv[1]) +
        L"';document.getElementById('account').value='" + account +
        L"';document.getElementById('password').value='fixture-password';document.getElementById('signIn').click();true";
      recovery_eval(script);
      recovery_check(recovery_dom(L"!document.getElementById('hostsCard').classList.contains('hidden')"),
                     "DOM login crosses native handler and real HTTP into host list");
    };
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
    HANDLE browser = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, oldBrowser);
    recovery_check(browser != nullptr, "pin isolated browser process handle");
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

      ShellConnectRequest host{};
      host.hostId = "leak-host";
      for (int i = 0; i < 50; ++i) {
        // What begin_session does: create the event, record it, and on the next round replace
        // the record -- which has to close the one it displaces.
        HANDLE event = remote60::native_poc::viewer::viewer_create_cancel_event();
        const auto previous = gViewerByHost.find(host.hostId);
        if (previous != gViewerByHost.end()) {
          if (previous->second.cancelEvent) CloseHandle(previous->second.cancelEvent);
          gViewerByHost.erase(previous);
        }
        gViewerByHost[host.hostId] = ViewerCancelSlot{static_cast<DWORD>(1000 + i), event};
      }
      // And the last one leaves by the exit path instead.
      handle_viewer_exit(host, gViewerOperation, gOwnerEpoch.load(), WAIT_OBJECT_0, 0, 1000,
                         static_cast<DWORD>(1000 + 49));

      // Launches that never happened. begin_session creates the event BEFORE CreateProcess,
      // so a failed launch has one to dispose of -- and the failure path returned without
      // doing so, one handle at a time, for as long as the shell stayed open.
      for (int i = 0; i < 50; ++i) {
        HANDLE orphan = remote60::native_poc::viewer::viewer_create_cancel_event();
        if (orphan) CloseHandle(orphan);   // what the failure path has to do
      }

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

      DWORD after = 0;
      GetProcessHandleCount(GetCurrentProcess(), &after);
      recovery_check(gViewerByHost.find(host.hostId) == gViewerByHost.end(),
                     "the viewer's exit forgets its cancel handle");
      // Not equality: this process has a WebView2 in it, doing its own work on its own threads,
      // so the count moves for reasons that have nothing to do with this. Fifty replacements
      // leaking would show as fifty; a few either way is the rest of the program breathing.
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
      const auto liveViewersFor = [](const std::string& hostId) {
        const auto it = gViewerByHost.find(hostId);
        if (it == gViewerByHost.end()) return 0;
        if (!it->second.process) return 1;   // recorded, liveness unknown
        return WaitForSingleObject(it->second.process, 0) == WAIT_TIMEOUT ? 1 : 0;
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
      recovery_check(liveViewersFor(req.hostId) <= 1,
                     "...leaving at most one live viewer for that host");

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
          HANDLE killable = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, after->second.pid);
          if (killable) {
            TerminateProcess(killable, 0);
            WaitForSingleObject(killable, 5000);
            CloseHandle(killable);
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
    ComPtr<IStream> screenshot;
    recovery_check(SUCCEEDED(SHCreateStreamOnFileEx(widen(argv[3]).c_str(), STGM_CREATE|STGM_READWRITE,
                                                   FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, &screenshot)), "open isolated screenshot output");
    auto captured = std::make_shared<std::atomic<bool>>(false);
    gWebView->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, screenshot.Get(),
      Callback<ICoreWebView2CapturePreviewCompletedHandler>([captured](HRESULT hr) -> HRESULT {
        captured->store(SUCCEEDED(hr)); return S_OK;
      }).Get());
    recovery_check(recovery_wait([&] { return captured->load(); }), "capture rendered production page");
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
    HANDLE killable = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.second.pid);
    if (killable) {
      if (WaitForSingleObject(killable, 2000) != WAIT_OBJECT_0) TerminateProcess(killable, 0);
      WaitForSingleObject(killable, 3000);
      CloseHandle(killable);
    }
    if (entry.second.process) { CloseHandle(entry.second.process); entry.second.process = nullptr; }
  }
  gViewerByHost.clear();

  if (gWindow && IsWindow(gWindow)) DestroyWindow(gWindow);
  gClosing=true; gWorkers.Shutdown();
  if (gController) gController->Close();
  gWebView.Reset(); gController.Reset();
  return result;
}
