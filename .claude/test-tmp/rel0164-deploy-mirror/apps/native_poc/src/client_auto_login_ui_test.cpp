// Coming back signed in, in the client's own window.
//
// The whole of client_shell_main.cpp is compiled in -- the page, the WebView, the native bridge,
// the workers, the directory calls, the credential store -- and only its process entry is
// renamed. What differs from GNLinkClient.exe is what REMOTE60_SHELL_TEST_SEAM replaces: where
// the settings and the stored sign-in live, and which directory is talked to.
//
// One run of this executable is ONE START of the client. "Signed in once, closed, opened again"
// is two runs, and the runner (apps/directory/test/client_auto_login_runner.js) is what strings
// them together, restarts the directory between them, and reads what the directory recorded.
// That is on purpose: a test that kept one process alive could not show the thing this feature
// is for.
//
//   client_auto_login_ui_test <fixture-url> <mode> <screenshot> [argument]
//
//   sign-in <account>       type the id and password, press sign in, reach the PC list
//   expect-list <account>   reach the PC list WITHOUT TYPING ANYTHING
//   expect-form <what>      stay on the sign-in form; <what> is what it says: "" (nothing) or
//                           sign-in-again. A word, not the sentence: a command line does not
//                           carry Korean from one program to another reliably.
//   sign-out <account>      reach the PC list without typing, then press sign out
//   sign-out-offline <account>  the same, pressed after the runner has stopped the directory
//   stay-open <account>     reach the PC list, wait for the runner, press refresh, still listed
//   retry <account>         the directory is down: the form is usable while attempts go on,
//                           a retry is offered, and pressing it (once the runner says the
//                           directory is back) reaches the PC list
//   slow-sign-in <account>  press sign in and report what the form says when the answer comes
//   sign-in-then-out <account>  sign in with the id and password, then press sign out
//   refused-pending <account>   sign in with the id and the right password of an account that
//   refused-disabled <account>  waits for approval / is stopped: the form says the directory's
//                               sentence, and nothing is listed, held or stored
//
// Input is ExecuteScript on the rendered page. It is not a physical keyboard or mouse.

#define wWinMain auto_login_product_entry
#include "client_shell_main.cpp"
#undef wWinMain
#include <shlwapi.h>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

namespace {

void pump(unsigned ms) {
  const uint64_t end = GetTickCount64() + ms;
  do {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    Sleep(5);
  } while (GetTickCount64() < end);
}

bool wait_for(const std::function<bool()>& predicate, unsigned ms) {
  const uint64_t end = GetTickCount64() + ms;
  while (GetTickCount64() < end) { if (predicate()) return true; pump(20); }
  return predicate();
}

std::string eval(const std::wstring& script) {
  struct Reply { bool done = false; std::string text; };
  auto reply = std::make_shared<Reply>();
  if (!gWebView) return {};
  gWebView->ExecuteScript(script.c_str(), Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
    [reply](HRESULT hr, LPCWSTR value) -> HRESULT {
      if (SUCCEEDED(hr) && value) reply->text = narrow(value);
      reply->done = true; return S_OK;
    }).Get());
  wait_for([&] { return reply->done; }, 3000);
  return reply->text;
}

bool dom(const std::wstring& script, unsigned timeout) {
  const uint64_t end = GetTickCount64() + timeout;
  while (GetTickCount64() < end) {
    if (eval(script) == "true") return true;
    pump(30);
  }
  return false;
}

int gFailures = 0;
void check(bool value, const std::string& label, const std::string& detail = {}) {
  std::printf("%s %s%s%s\n", value ? "PASS" : "FAIL", label.c_str(), detail.empty() ? "" : "  ",
              detail.c_str());
  std::fflush(stdout);
  if (!value) ++gFailures;
}

bool capture(const std::wstring& path) {
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
  wait_for([&] { return captured->load() != 0; }, 10000);
  return captured->load() == 1;
}

const wchar_t kListShown[] = L"!document.getElementById('hostsCard').classList.contains('hidden')";
const wchar_t kFormShown[] = L"!document.getElementById('signInCard').classList.contains('hidden')";
// The form is up AND free: no attempt is holding it.
const wchar_t kFormFree[] =
    L"!document.getElementById('signInCard').classList.contains('hidden')&&"
    L"!document.getElementById('signIn').disabled";

std::string page_text(const wchar_t* id) {
  return eval(std::wstring(L"document.getElementById('") + id + L"').textContent");
}

bool typed_nothing() {
  return eval(L"document.getElementById('password').value===''") == "true";
}

void type_and_press(const std::wstring& account) {
  eval(L"document.getElementById('account').value='" + account +
       L"';document.getElementById('password').value='fixture-password-4417';"
       L"document.getElementById('signIn').click();true");
}

bool flag_exists(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::puts("usage: client_auto_login_ui_test <fixture-url> <mode> <screenshot> [argument]");
    return 2;
  }
  const std::string url = argv[1];
  const std::string mode = argv[2];
  const std::wstring shot = widen(argv[3]);
  const std::string argument = argc > 4 ? argv[4] : "";
  const std::wstring account = widen(argument);

  // Refused before a WebView exists unless everything this process can write is the runner's.
  const char* fixture = std::getenv("GNLINK_RECOVERY_TEST_ROOT");
  wchar_t temporary[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temporary);
  if (!fixture || !std::filesystem::exists(std::filesystem::path(fixture) / ".fixture") ||
      std::filesystem::weakly_canonical(temporary) !=
          std::filesystem::weakly_canonical(std::filesystem::path(fixture) / "profile") ||
      url.rfind("http://127.0.0.1:", 0) != 0) {
    std::puts("FAIL isolated fixture/profile required; nothing was started");
    return 2;
  }
  const std::filesystem::path root = std::filesystem::weakly_canonical(fixture);
  gShellTestDirectoryUrl = url;
  std::filesystem::create_directories(root / "shelldata");
  gShellTestDataDir = (root / "shelldata").wstring();
  {
    const std::wstring rel =
        std::filesystem::weakly_canonical(settings_path()).lexically_relative(root).wstring();
    wchar_t appData[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", appData, MAX_PATH);
    const std::wstring appRel = (n > 0 && n < MAX_PATH)
        ? std::filesystem::weakly_canonical(appData).lexically_relative(root).wstring()
        : std::wstring();
    const bool inside = !rel.empty() && rel.rfind(L"..", 0) != 0 && !appRel.empty() &&
                        appRel.rfind(L"..", 0) != 0;
    const login_store::Store store = sign_in_store();
    const std::wstring credRel = std::filesystem::weakly_canonical(store.credential_path())
                                     .lexically_relative(root).wstring();
    const bool credInside = !credRel.empty() && credRel.rfind(L"..", 0) != 0;
    check(inside && credInside,
          "the settings, LOCALAPPDATA and the stored sign-in are all inside the fixture");
    if (!inside || !credInside) return 2;
  }

  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  std::string socketError;
  if (!initialize_sockets(&socketError)) return 2;
  LogUploadShutdown uploader;

  try {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    RegisterClassExW(&wc);
    gWindow = CreateWindowExW(0, kWindowClass, L"GNLink isolated sign-in test", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 560, 760, nullptr, nullptr,
                              wc.hInstance, nullptr);
    ShowWindow(gWindow, SW_SHOWNOACTIVATE);
    if (FAILED(create_shell_webview())) throw std::runtime_error("the WebView did not start");
    if (!dom(L"!!document.getElementById('signIn')", 20000)) {
      throw std::runtime_error("the sign-in page did not load");
    }
    const std::string tag = "[" + mode + "] ";

    if (mode == "sign-in") {
      check(dom(kFormFree, 15000), tag + "with nothing stored the sign-in form comes up, free to use");
      check(eval(L"document.getElementById('retryAuto').classList.contains('hidden')") == "true",
            tag + "no retry is offered: there was nothing to retry");
      type_and_press(account);
      check(dom(kListShown, 15000), tag + "id + password reaches the PC list");
      check(page_text(L"accountLabel") == "\"" + argument + "\"", tag + "the list is " + argument + "'s");
      // The worker that stores the credential finishes before the list is posted; the owed
      // sign-outs are settled after. Waited for, so the process does not exit under it.
      const login_store::Store store = sign_in_store();
      check(wait_for([&] {
              return GetFileAttributesW(store.credential_path().c_str()) != INVALID_FILE_ATTRIBUTES;
            }, 5000),
            tag + "a sign-in credential is on disk");
    } else if (mode == "expect-list") {
      check(dom(kListShown, 20000), tag + "THE PC LIST IS REACHED WITHOUT TYPING ANYTHING",
            page_text(L"signInMsg"));
      check(typed_nothing(), tag + "the password field was never filled");
      check(page_text(L"accountLabel") == "\"" + argument + "\"", tag + "the list is " + argument + "'s",
            page_text(L"accountLabel"));
      check(!gSessionToken.empty(), tag + "the client holds a session");
    } else if (mode == "expect-form") {
      check(dom(kFormFree, 30000), tag + "the sign-in form is up and free to use");
      pump(800);  // anything that was going to replace the form would have by now
      check(eval(kFormShown) == "true" && eval(kListShown) == "false",
            tag + "THE FORM STAYS: THE PC LIST IS NOT SHOWN");
      check(gSessionToken.empty(), tag + "the client holds no session");
      const std::string said = page_text(L"signInMsg");
      if (argument.empty()) {
        check(said == "\"\"", tag + "it says nothing: nothing went wrong", said);
      } else if (argument == "sign-in-again") {
        check(eval(L"document.getElementById('signInMsg').textContent.includes('다시 로그인')") ==
                  "true",
              tag + "it asks to sign in again", said);
        check(eval(L"document.getElementById('account').value!==''") == "true",
              tag + "the id is still in the form");
      } else {
        check(false, tag + "unknown expectation " + argument);
      }
      check(eval(L"document.getElementById('retryAuto').classList.contains('hidden')") == "true",
            tag + "no retry is offered");
    } else if (mode == "sign-out") {
      check(dom(kListShown, 20000), tag + "the PC list is reached without typing");
      eval(L"document.getElementById('signOut').click();true");
      check(dom(kFormFree, 10000), tag + "pressing sign out returns to the form");
      check(eval(L"document.getElementById('account').value") == "\"" + argument + "\"",
            tag + "the id is still in the form",
            eval(L"document.getElementById('account').value"));
      const login_store::Store store = sign_in_store();
      check(wait_for([&] {
              return GetFileAttributesW(store.credential_path().c_str()) == INVALID_FILE_ATTRIBUTES;
            }, 10000),
            tag + "THE STORED SIGN-IN IS GONE FROM DISK");
      // The directory is told after the credential goes; given time to be, and then the
      // runner reads what the directory recorded.
      check(wait_for([&] {
              return GetFileAttributesW(store.revoke_path().c_str()) == INVALID_FILE_ATTRIBUTES;
            }, 15000),
            tag + "no sign-out is left owed: the directory was told");
      check(gSessionToken.empty(), tag + "the client holds no session");
    } else if (mode == "sign-out-offline") {
      check(dom(kListShown, 20000), tag + "the PC list is reached without typing");
      { std::ofstream ready(root / "client.ready"); ready << '1'; }
      check(wait_for([&] { return flag_exists(root / "directory.restarted"); }, 60000),
            tag + "(the runner stopped the directory)");
      eval(L"document.getElementById('signOut').click();true");
      check(dom(kFormFree, 10000), tag + "pressing sign out returns to the form at once");
      const login_store::Store store = sign_in_store();
      check(wait_for([&] {
              return GetFileAttributesW(store.credential_path().c_str()) == INVALID_FILE_ATTRIBUTES;
            }, 10000),
            tag + "THE STORED SIGN-IN IS GONE FROM DISK, directory or no directory");
      // The directory cannot be told. Waited for long enough for the attempt to have failed.
      pump(9000);
      check(GetFileAttributesW(store.revoke_path().c_str()) != INVALID_FILE_ATTRIBUTES,
            tag + "the sign-out is written down as owed");
      check(gSessionToken.empty() && eval(kListShown) == "false",
            tag + "the client holds no session and shows no list");
    } else if (mode == "stay-open") {
      check(dom(kListShown, 20000), tag + "the PC list is reached without typing");
      const std::string before = gSessionToken;
      { std::ofstream ready(root / "client.ready"); ready << '1'; }
      check(wait_for([&] { return flag_exists(root / "directory.restarted"); }, 60000),
            tag + "(the runner restarted the directory)");
      eval(L"document.getElementById('refresh').click();true");
      check(wait_for([&] { return !gSessionToken.empty() && gSessionToken != before; }, 20000),
            tag + "THE SESSION THE DIRECTORY FORGOT IS REPLACED, WITHOUT A PASSWORD");
      check(eval(kListShown) == "true" && eval(kFormShown) == "false",
            tag + "the PC list is still what is shown");
      check(typed_nothing(), tag + "the password field was never filled");
    } else if (mode == "retry") {
      // The first attempt gets no answer; the form is given back while more are made.
      check(dom(L"document.getElementById('signInMsg').textContent.includes('다시 시도하는 중')&&"
                L"!document.getElementById('signIn').disabled", 30000),
            tag + "with the directory down the form is usable while attempts go on");
      check(eval(L"document.getElementById('account').value") == "\"" + argument + "\"",
            tag + "the id is in the form");
      check(dom(L"!document.getElementById('retryAuto').classList.contains('hidden')", 60000),
            tag + "after the last attempt a retry is offered");
      check(eval(kFormShown) == "true" && gSessionToken.empty(),
            tag + "it did not fall through to anything else: still the form, no session");
      const login_store::Store store = sign_in_store();
      check(GetFileAttributesW(store.credential_path().c_str()) != INVALID_FILE_ATTRIBUTES,
            tag + "THE STORED SIGN-IN WAS NOT ERASED BY THE DIRECTORY BEING DOWN");
      capture(shot + L".down.png");
      { std::ofstream ready(root / "client.ready"); ready << '1'; }
      check(wait_for([&] { return flag_exists(root / "directory.restarted"); }, 60000),
            tag + "(the runner started the directory)");
      check(eval(L"(function(){var b=document.getElementById('retryAuto'),r=b.getBoundingClientRect();"
                 L"var x=r.left+r.width/2,y=r.top+r.height/2;"
                 L"return r.width>0&&r.height>0&&document.elementFromPoint(x,y)===b;})()") == "true",
            tag + "the retry is on screen and not covered");
      eval(L"document.getElementById('retryAuto').click();true");
      check(dom(kListShown, 20000), tag + "pressing it reaches the PC list, still without a password");
      check(typed_nothing(), tag + "the password field was never filled");
    } else if (mode == "sign-in-then-out") {
      check(dom(kFormFree, 15000), tag + "the sign-in form comes up, free to use");
      type_and_press(account);
      check(dom(kListShown, 15000), tag + "id + password reaches the PC list");
      const login_store::Store store = sign_in_store();
      check(wait_for([&] {
              return GetFileAttributesW(store.credential_path().c_str()) != INVALID_FILE_ATTRIBUTES;
            }, 5000),
            tag + "a sign-in credential is on disk");
      eval(L"document.getElementById('signOut').click();true");
      check(dom(kFormFree, 10000), tag + "pressing sign out returns to the form");
      check(wait_for([&] {
              return GetFileAttributesW(store.credential_path().c_str()) == INVALID_FILE_ATTRIBUTES &&
                     GetFileAttributesW(store.revoke_path().c_str()) == INVALID_FILE_ATTRIBUTES;
            }, 15000),
            tag + "the stored sign-in is gone and the directory was told");
    } else if (mode == "slow-sign-in") {
      check(dom(kFormFree, 30000), tag + "the sign-in form is up");
      type_and_press(account);
      { std::ofstream ready(root / "client.ready"); ready << '1'; }
      // The directory holds its answer until the runner lets it go, which it does after the
      // other window has signed out.
      check(dom(L"document.getElementById('signInMsg').textContent.includes('로그아웃됐습니다')&&"
                L"!document.getElementById('signIn').disabled", 60000),
            tag + "THE ANSWER THAT CAME AFTER THE SIGN-OUT DID NOT SIGN THIS WINDOW IN",
            page_text(L"signInMsg"));
      check(eval(kListShown) == "false" && gSessionToken.empty(),
            tag + "no PC list, no session");
      const login_store::Store store = sign_in_store();
      pump(1500);
      check(GetFileAttributesW(store.credential_path().c_str()) == INVALID_FILE_ATTRIBUTES,
            tag + "AND IT STORED NOTHING");
    } else if (mode == "refused-pending" || mode == "refused-disabled") {
      check(dom(kFormFree, 30000), tag + "the sign-in form comes up, free to use");
      type_and_press(account);
      // The contract's sentences, as the directory sends them.
      const std::wstring want = mode == "refused-pending"
          ? L"승인 대기 중입니다. 관리자 승인 후 사용할 수 있습니다."
          : L"사용이 정지된 계정입니다.";
      check(dom(L"document.getElementById('signInMsg').textContent.includes('" + want + L"')&&"
                L"!document.getElementById('signIn').disabled", 15000),
            tag + "THE FORM SAYS WHAT THE DIRECTORY SAID, and sign in can be pressed again",
            page_text(L"signInMsg"));
      pump(800);
      check(eval(kListShown) == "false" && gSessionToken.empty(), tag + "no PC list, no session");
      const login_store::Store store = sign_in_store();
      check(GetFileAttributesW(store.credential_path().c_str()) == INVALID_FILE_ATTRIBUTES,
            tag + "nothing is stored");
    } else {
      check(false, "unknown mode " + mode);
    }

    check(capture(shot), tag + "the page is photographed");
  } catch (const std::exception& error) {
    check(false, std::string("the run completed: ") + error.what());
  }

  if (gWindow && IsWindow(gWindow)) DestroyWindow(gWindow);
  gClosing = true;
  gWorkers.Shutdown();
  if (gController) gController->Close();
  gWebView.Reset();
  gController.Reset();
  std::printf("client_auto_login_ui_test %s: %s\n", mode.c_str(),
              gFailures == 0 ? "ALL PASS" : "FAIL");
  return gFailures == 0 ? 0 : 1;
}
