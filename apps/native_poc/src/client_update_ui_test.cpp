// Renders the production page in a real WebView2 and presses the button.
//
// This test exists because of what the previous round got wrong. `client_update_flow_test` read
// the source, found `start_update_check()` called from two places, and that was accepted as the
// Client's update path being wired. It was not. The notice reached the screen and there was
// nothing on the screen to press: `shell.html` had seven buttons and none of them was an update,
// while `client_shell_main.cpp` had handled a {"type":"update"} message all along that no page
// ever sent. A source grep cannot see that gap, because both halves are present -- what is
// missing is only the fact that nothing joins them.
//
// So nothing here is read from source. The real `ui/shell.html` is loaded into a real browser
// engine, the real message builders from `client_shell_bridge.cpp` are what gets posted to it,
// the click is a DOM click on the rendered element, and the assertion is that a message comes
// back out. A screenshot is written next to the results so a person can see what the user sees.
//
// Evidence is graded here, and the grades are not interchangeable:
//
//   (1) the page posts a WebMessage           <- what this test observes
//   (2) the native side receives it and acts  <- NOT observed here
//   (3) a real updater starts                 <- NOT observed here
//
// (1) is never quoted as evidence of (2) or (3). Nor does any of this show that a live check
// against the real server produces the message in the first place: that needs an account and a
// network and is a separate step. What is shown is that the HTML which will be published can put
// a pressable button on screen and emit the message the native side has always been waiting for.
//
// "Pressable" is meant literally. `disabled === false` was the first version of that check and it
// is not enough: a button can be enabled and off-screen, transparent, or underneath something
// else. The three negative controls below cover exactly those, and if they ever stop failing the
// check has become decoration.

#include <windows.h>
#include <shlwapi.h>
#include <wrl.h>

#include <atomic>
#include <memory>
#include <cstdio>
#include <bcrypt.h>

#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

#include "WebView2.h"
#include "client_shell_bridge.hpp"
#include "update_manifest.hpp"
#include "update_signature.hpp"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using remote60::native_poc::shell_message_type;
using remote60::native_poc::shell_update_available_json;
using remote60::native_poc::shell_update_busy_json;
using remote60::native_poc::shell_update_cleared_json;

namespace {

int gPass = 0;
int gFail = 0;

void ok(bool cond, const std::string& what, const std::string& detail = {}) {
  if (cond) {
    ++gPass;
    std::printf("PASS  %s%s%s\n", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  } else {
    ++gFail;
    std::printf("FAIL  %s%s%s\n", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  }
}

std::string narrow(const std::wstring& text) {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                       nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], size,
                      nullptr, nullptr);
  return out;
}

std::wstring widen(const std::string& text) {
  if (text.empty()) return {};
  const int size =
      MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], size);
  return out;
}

std::wstring executable_dir() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring full(path);
  const size_t slash = full.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : full.substr(0, slash);
}

/**
 * The page under test, which must be the one that ships.
 *
 * Looked for beside the executable first (that is the installed layout) and then up the build
 * tree at the repository copy. A test that fabricated its own HTML would pass forever while the
 * shipped page had no button on it -- which is exactly the failure this is here to catch.
 */
std::wstring production_page_path() {
  // The repository copy first, deliberately. The copy beside the executable is written by the
  // build and can lag the source by a whole edit -- it did on the first run of this test, which
  // would have meant asserting against a page nobody was shipping.
  const wchar_t* candidates[] = {
      L"\\..\\..\\..\\..\\apps\\native_poc\\ui\\shell.html",
      L"\\..\\..\\..\\apps\\native_poc\\ui\\shell.html",
  };
  for (const wchar_t* suffix : candidates) {
    wchar_t full[MAX_PATH]{};
    const std::wstring joined = executable_dir() + suffix;
    if (PathCanonicalizeW(full, joined.c_str()) && PathFileExistsW(full)) return full;
  }
  const std::wstring beside = executable_dir() + L"\\ui\\shell.html";
  if (PathFileExistsW(beside.c_str())) return beside;
  return {};
}

/**
 * The newest signed release staged in this working copy.
 *
 * Deliberately not a fixed version: the pin has to follow the release being prepared, and a
 * version typed in here would rot into pinning against something that shipped months ago.
 */
std::wstring release_manifest_path() {
  const wchar_t* roots[] = {
      L"\\..\\..\\..\\..\\.claude\\rel",
      L"\\..\\..\\..\\.claude\\rel",
  };
  std::wstring best;
  for (const wchar_t* suffix : roots) {
    wchar_t root[MAX_PATH]{};
    if (!PathCanonicalizeW(root, (executable_dir() + suffix).c_str())) continue;
    WIN32_FIND_DATAW found{};
    const HANDLE search = FindFirstFileW((std::wstring(root) + L"\\*").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) continue;
    do {
      if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
      const std::wstring candidate =
          std::wstring(root) + L"\\" + found.cFileName + L"\\windows.manifest";
      if (!PathFileExistsW(candidate.c_str())) continue;
      // Lexical order is enough for 0.2.NNN and keeps this from needing a version parser.
      if (candidate > best) best = candidate;
    } while (FindNextFileW(search, &found));
    FindClose(search);
    if (!best.empty()) return best;
  }
  return best;
}

std::wstring manifest_sig_path(const std::wstring& manifestPath) {
  return manifestPath.substr(0, manifestPath.size() - 8) + L"sig";
}

// ---------------------------------------------------------------------------- the harness

ComPtr<ICoreWebView2Controller> gController;
ComPtr<ICoreWebView2> gWebView;
HWND gWindow = nullptr;
std::vector<std::string> gFromPage;
std::atomic<bool> gReady{false};
std::atomic<bool> gFatal{false};

/** Runs the message loop until `done` or the budget runs out. Returns whether `done` happened. */
bool pump_until(const std::function<bool()>& done, DWORD budgetMs) {
  const DWORD deadline = GetTickCount() + budgetMs;
  while (!done()) {
    if (gFatal.load()) return false;
    if (GetTickCount() > deadline) return false;
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    Sleep(10);
  }
  return true;
}

/**
 * Evaluates script in the page and returns its JSON result. Empty when it did not answer.
 *
 * The answer is held in a shared block rather than captured by reference. The first version
 * captured two locals of this function, which is fine right up until a call times out: this
 * returns, the frame goes away, and the completion handler arrives later and writes through two
 * dangling references. That crashed the run at the first script that was slow to answer, which
 * looked like a fault in the page and was a fault in this harness.
 */
std::string eval(const std::wstring& script, DWORD budgetMs = 5000) {
  struct Answer {
    std::string text;
    std::atomic<bool> done{false};
  };
  auto answer = std::make_shared<Answer>();
  gWebView->ExecuteScript(script.c_str(),
                          Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                              [answer](HRESULT, LPCWSTR result) -> HRESULT {
                                if (result) answer->text = narrow(result);
                                answer->done = true;
                                return S_OK;
                              })
                              .Get());
  if (!pump_until([answer]() { return answer->done.load(); }, budgetMs)) {
    std::printf("      (script did not answer within %lums)\n",
                static_cast<unsigned long>(budgetMs));
    return {};
  }
  return answer->text;
}

/**
 * Whether a person could actually press this, expressed as one script.
 *
 * Every clause is here because it can be true while the others are false: an element can be
 * enabled and `display:none`; visible and scrolled out of the viewport; on screen and fully
 * transparent; opaque and covered by something with a higher z-index. `elementFromPoint` is the
 * one that catches the last case, because it answers what the mouse would actually hit.
 */
const wchar_t* kPressableFn =
    L"(function(){"
    L"  var b=document.getElementById('updateNow');"
    L"  if(!b) return 'no element';"
    L"  if(b.disabled) return 'disabled';"
    L"  if(!b.offsetParent) return 'not laid out';"
    L"  var s=getComputedStyle(b);"
    L"  if(s.visibility!=='visible') return 'visibility:'+s.visibility;"
    L"  if(parseFloat(s.opacity)<0.99) return 'opacity:'+s.opacity;"
    L"  var r=b.getBoundingClientRect();"
    L"  if(r.width<1||r.height<1) return 'zero size';"
    L"  if(r.bottom<=0||r.right<=0||r.top>=innerHeight||r.left>=innerWidth) return 'off viewport';"
    L"  var hit=document.elementFromPoint(r.left+r.width/2, r.top+r.height/2);"
    L"  if(!hit||!(hit===b||b.contains(hit))) return 'covered by '+(hit?hit.id||hit.tagName:'nothing');"
    L"  b.focus();"
    L"  if(document.activeElement!==b) return 'cannot take focus';"
    L"  return 'pressable';"
    L"})";

/** The same predicate as a standalone script, for when it is asked on its own. */
std::wstring pressable_script() { return std::wstring(kPressableFn) + L"()"; }

/**
 * Whether the offer is actually on the screen.
 *
 * Asked of the rendered result, not of the class list. `classList.contains('hidden')` was true
 * for the entire life of the bug in 2026-09-11: the class was applied exactly as the script
 * intended and `#updateBar { display: flex }` (an id, 0-1-0-0) beat `.hidden { display: none }`
 * (a class, 0-0-1-0), so the bar was painted from the first frame. A test that reads the class
 * is reading the intention; this reads the outcome.
 */
const wchar_t* kVisibleFn =
    L"(function(){"
    L"  var b=document.getElementById('updateBar');"
    L"  if(!b) return 'no element';"
    L"  var s=getComputedStyle(b);"
    L"  if(s.display==='none') return 'hidden';"
    L"  if(s.visibility!=='visible') return 'visibility:'+s.visibility;"
    L"  if(parseFloat(s.opacity)<0.01) return 'transparent';"
    L"  var r=b.getBoundingClientRect();"
    L"  if(r.width<1||r.height<1) return 'zero size';"
    L"  if(r.bottom<=0||r.top>=innerHeight) return 'off viewport';"
    L"  var hit=document.elementFromPoint(r.left+r.width/2, r.top+r.height/2);"
    L"  if(!hit||!(hit===b||b.contains(hit))) return 'covered';"
    L"  return 'shown';"
    L"})";

std::wstring visible_script() { return std::wstring(kVisibleFn) + L"()"; }

std::string sha256_hex_of_file(const std::wstring& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return {};
  const std::string bytes((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
  unsigned char digest[32]{};
  const NTSTATUS hashed =
      BCryptHash(alg, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                 static_cast<ULONG>(bytes.size()), digest, sizeof(digest));
  BCryptCloseAlgorithmProvider(alg, 0);
  if (hashed != 0) return {};
  static const char* kHex = "0123456789abcdef";
  std::string out;
  for (unsigned char byte : digest) {
    out += kHex[byte >> 4];
    out += kHex[byte & 0x0f];
  }
  return out;
}

std::string read_file(const std::wstring& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return {};
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

/**
 * Waits for a script to answer with `expected`, without nesting one pump inside another.
 *
 * `eval` already runs a message loop, so using it inside a `pump_until` predicate meant two loops
 * pumping the same queue, one from inside the other. That arrangement ran fine for a while and
 * then fell over part-way through a run, differently each time -- which is what re-entrant
 * dispatch looks like. Here the loop is on the outside and `eval` is called once per turn.
 */
bool wait_for(const std::wstring& script, const std::string& expected, DWORD budgetMs) {
  const DWORD deadline = GetTickCount() + budgetMs;
  std::string seen;
  for (;;) {
    seen = eval(script);
    if (seen == expected) return true;
    if (GetTickCount() > deadline) {
      // Says what it actually saw. A bare "timed out" sent the last diagnosis chasing the page
      // when the page was right and the comparison was wrong.
      std::printf("      (waited %lums for [%s], last saw [%s])\n",
                  static_cast<unsigned long>(budgetMs), expected.c_str(), seen.c_str());
      return false;
    }
    Sleep(50);
  }
}

bool page_sent(const std::string& type) {
  for (const std::string& message : gFromPage) {
    if (shell_message_type(message) == type) return true;
  }
  return false;
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg == WM_SIZE && gController) {
    RECT bounds{};
    GetClientRect(hwnd, &bounds);
    gController->put_Bounds(bounds);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void write_screenshot(const std::wstring& path) {
  ComPtr<IStream> stream;
  if (FAILED(SHCreateStreamOnFileEx(path.c_str(), STGM_CREATE | STGM_READWRITE, FILE_ATTRIBUTE_NORMAL,
                                    TRUE, nullptr, &stream))) {
    return;
  }
  // Shared, not captured by reference. Every crash in this file's history happened after this
  // function had returned: the capture completed late, and the handler wrote `done` on a frame
  // that no longer existed. The same mistake was in eval(); this was the other half of it.
  auto done = std::make_shared<std::atomic<bool>>(false);
  gWebView->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream.Get(),
                           Callback<ICoreWebView2CapturePreviewCompletedHandler>(
                               [done](HRESULT) -> HRESULT {
                                 *done = true;
                                 return S_OK;
                               })
                               .Get());
  pump_until([done]() { return done->load(); }, 8000);
}

}  // namespace

int wmain() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  // WebView2 refuses to build an environment without an apartment: the first run of this test
  // came back CO_E_NOTINITIALIZED (0x800401f0). Single-threaded, because the page and the
  // callbacks below all run on this one thread. S_FALSE still owes an uninitialise.
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
    std::printf("FAIL  CoInitializeEx  hr=0x%08lx\n", static_cast<unsigned long>(com));
    return 1;
  }

  const std::wstring page = production_page_path();
  if (page.empty()) {
    std::printf("FAIL  the production ui/shell.html could not be found\n");
    std::printf("client_update_ui_test: FAIL (1 failed)\n");
    return 1;
  }
  std::printf("page  %s\n", narrow(page).c_str());

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"Remote60UpdateUiTest";
  RegisterClassExW(&wc);
  // Off screen and never shown: this must not steal focus from whoever is at the machine.
  gWindow = CreateWindowExW(0, wc.lpszClassName, L"update ui test", WS_OVERLAPPEDWINDOW, -3000,
                            -3000, 520, 760, nullptr, nullptr, wc.hInstance, nullptr);
  if (!gWindow) {
    std::printf("FAIL  could not create the host window\n");
    return 1;
  }

  std::atomic<bool> created{false};
  // Its own user-data folder, under TEMP. Sharing the client's would have this test writing into
  // the profile of the program a person may be using at the time.
  wchar_t tempDir[MAX_PATH]{};
  GetTempPathW(MAX_PATH, tempDir);
  const std::wstring userData = std::wstring(tempDir) + L"gnlink-update-ui-test";
  CreateDirectoryW(userData.c_str(), nullptr);

  const HRESULT env = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, userData.c_str(), nullptr,
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [&created](HRESULT hr, ICoreWebView2Environment* environment) -> HRESULT {
            if (FAILED(hr) || !environment) {
              std::printf("      environment hr=0x%08lx\n", static_cast<unsigned long>(hr));
              gFatal = true;
              return S_OK;
            }
            environment->CreateCoreWebView2Controller(
                gWindow,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [&created](HRESULT hr2, ICoreWebView2Controller* controller) -> HRESULT {
                      if (FAILED(hr2) || !controller) {
                        std::printf("      controller hr=0x%08lx\n",
                                    static_cast<unsigned long>(hr2));
                        gFatal = true;
                        return S_OK;
                      }
                      gController = controller;
                      gController->get_CoreWebView2(&gWebView);
                      RECT bounds{};
                      GetClientRect(gWindow, &bounds);
                      gController->put_Bounds(bounds);
                      EventRegistrationToken token;
                      gWebView->add_WebMessageReceived(
                          Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                              [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args)
                                  -> HRESULT {
                                LPWSTR raw = nullptr;
                                if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
                                  const std::string text = narrow(raw);
                                  gFromPage.push_back(text);
                                  if (shell_message_type(text) == "ready") gReady = true;
                                  CoTaskMemFree(raw);
                                }
                                return S_OK;
                              })
                              .Get(),
                          &token);
                      created = true;
                      return S_OK;
                    })
                    .Get());
            return S_OK;
          })
          .Get());
  if (FAILED(env)) {
    std::printf("FAIL  the WebView2 runtime is not available here  hr=0x%08lx\n",
                static_cast<unsigned long>(env));
    std::printf("client_update_ui_test: FAIL (1 failed)\n");
    return 1;
  }
  if (!pump_until([&created]() { return created.load(); }, 30000) || !gWebView) {
    std::printf("FAIL  the WebView2 controller never came up\n");
    std::printf("client_update_ui_test: FAIL (1 failed)\n");
    return 1;
  }

  // ------------------------------------------------------------------ the page must be THE page
  //
  // Not "the repository copy", which is only the candidate until something says so. The signed
  // manifest names a sha256 for ui\\shell.html; the file loaded below has to be that file. Same
  // commit and same toolchain do not make two builds identical, so the hash is the only fixed
  // point there is.
  const std::string pageSha = sha256_hex_of_file(page);
  std::printf("sha   %s\n", pageSha.c_str());
  const std::wstring manifestPath = release_manifest_path();
  if (manifestPath.empty()) {
    ok(false, "a signed manifest to pin against was not found");
  } else {
    // The production verifier, with the key compiled into this build. A manifest that did not
    // verify is not allowed to say what the page should be -- otherwise the pin could be moved
    // by editing a text file.
    namespace upd = remote60::native_poc::update;
    const upd::ManifestResult loaded =
        upd::load_manifest(read_file(manifestPath), read_file(manifest_sig_path(manifestPath)),
                           "windows", upd::default_verifier());
    if (loaded.status != upd::ManifestStatus::Ok || !loaded.manifest.has_value()) {
      ok(false, "the manifest pinned against did not verify", loaded.detail);
    } else {
      std::string named;
      for (const upd::ManifestArtifact& artifact : loaded.manifest->fields().artifacts) {
        if (artifact.name == "ui\\shell.html") named = artifact.sha256;
      }
      std::printf("pin   %s  version=%s\n", narrow(manifestPath).c_str(),
                  loaded.manifest->fields().version.c_str());
      ok(!named.empty() && named == pageSha,
         "the page under test is the one the signed manifest names",
         named.empty() ? std::string("the manifest names no ui shell.html")
                       : named.substr(0, 16) + " vs " + pageSha.substr(0, 16));
    }
  }

  gWebView->Navigate(page.c_str());
  ok(pump_until([]() { return gReady.load(); }, 30000),
     "the shipped page loads and reports ready");
  if (!gReady.load()) {
    std::printf("client_update_ui_test: FAIL (%d failed)\n", gFail);
    return 1;
  }

  // ------------------------------------------------------------------ negative: nothing offered yet
  // Read from the rendering, not the class list -- that distinction is the whole point of this
  // block now.
  const std::string atStart = eval(visible_script());
  ok(atStart == "\"hidden\"", "before any check, there is no offer on screen", atStart);
  gFromPage.clear();
  eval(L"document.getElementById('updateNow').click()");
  ok(!page_sent("update"),
     "clicking the hidden button asks for nothing",
     "no update message was sent");

  // ------------------------------------------------------------------ the production message
  // The sentence comes from the production notice builder rather than being typed here. A test
  // that supplied its own wording would render something the user never sees.
  const remote60::native_poc::ShellUpdateNotice notice =
      remote60::native_poc::shell_update_notice("UpdateAvailable", "0.2.115", "");
  ok(notice.show, "the production notice builder says this outcome is worth showing");
  const std::string available = shell_update_available_json("0.2.115", notice.text);
  ok(available.find("\"type\":\"updateAvailable\"") != std::string::npos &&
         available.find("\"version\":\"0.2.115\"") != std::string::npos,
     "the version travels in a field of its own", available);

  gWebView->PostWebMessageAsString(widen(available).c_str());
  ok(wait_for(visible_script(), "\"shown\"", 8000), "the offer appears on the shipped page");
  ok(eval(L"document.getElementById('updateText').textContent").find("0.2.115") !=
         std::string::npos,
     "and it names the version");
  {
    const std::string state = eval(pressable_script());
    ok(state == "\"pressable\"", "the install button is actually pressable", state);
  }
  std::fflush(stdout);

  write_screenshot(executable_dir() + L"\\client_update_ui.png");
  std::printf("shot  %s\n", narrow(executable_dir() + L"\\client_update_ui.png").c_str());

  // ------------------------------------------------------------------ the controls that must fail
  //
  // Three ways a button can be present, enabled, and still unpressable. If any of these comes
  // back "pressable", the check above is measuring nothing and every PASS beside it is worthless.
  //
  // Run before the click, while the offer is up and untouched. Doing it afterwards meant putting
  // the offer back first, and that extra round trip was state this does not need.
  //
  // Read once per assertion: calling eval() twice inside one ok() would pump the message loop
  // twice from within a single expression, and the two answers could describe different moments.
  //
  // All three run inside one script and report together. Doing them as seven separate round
  // trips left the page mutated between calls, and each round trip is another chance for the
  // harness rather than the page to decide the answer.
  const std::string controls = eval(
      L"(function(){"
      L"  var b=document.getElementById('updateNow');"
      L"  var check=" + std::wstring(kPressableFn) +
      L";"
      L"  var out={};"
      L"  b.style.display='none';        out.hidden=check();"
      L"  b.style.display='';            out.restored=check();"
      L"  var o=document.createElement('div'); o.id='blocker';"
      L"  o.style.cssText='position:fixed;left:0;top:0;right:0;bottom:0;z-index:99999;"
      L"background:rgba(0,0,0,0)';"
      L"  document.body.appendChild(o);  out.covered=check();"
      L"  o.remove();                    out.uncovered=check();"
      L"  return JSON.stringify(out);"
      L"})()");
  ok(controls.find("\\\"hidden\\\":\\\"not laid out\\\"") != std::string::npos,
     "negative control: display:none is not pressable", controls);
  ok(controls.find("\\\"covered\\\":\\\"covered by blocker\\\"") != std::string::npos,
     "negative control: a transparent cover is not pressable", controls);
  ok(controls.find("\\\"restored\\\":\\\"pressable\\\"") != std::string::npos &&
         controls.find("\\\"uncovered\\\":\\\"pressable\\\"") != std::string::npos,
     "and pressable again once each obstruction is removed", controls);

  // ------------------------------------------------------------------ the CSS that shipped broken
  //
  // 0.2.116 put the bar on screen before sign-in and kept it there, because `.hidden` is a class
  // and `#updateBar` is an id. Removing the `#updateBar.hidden` rule puts the page back in that
  // state; the class still gets applied and the bar still shows. If this control ever reports
  // "hidden", the fix has been lost and nothing else here would notice.
  const std::string reverted = eval(
      L"(function(){"
      L"  var removed=0;"
      L"  for (var i=0;i<document.styleSheets.length;i++){"
      L"    var rules=document.styleSheets[i].cssRules;"
      L"    for (var j=rules.length-1;j>=0;j--){"
      L"      if (rules[j].selectorText==='#updateBar.hidden'){"
      L"        document.styleSheets[i].deleteRule(j); removed++;"
      L"      }"
      L"    }"
      L"  }"
      L"  document.getElementById('updateBar').classList.add('hidden');"
      L"  var b=document.getElementById('updateBar');"
      L"  return removed + ':' + getComputedStyle(b).display;"
      L"})()");
  ok(reverted == "\"1:flex\"",
     "negative control: without the id-scoped rule the bar shows despite the hidden class",
     reverted);
  // Put it back, so the rest of the run tests the page as it ships.
  eval(L"document.styleSheets[0].insertRule('#updateBar.hidden{display:none}',"
       L"document.styleSheets[0].cssRules.length)");
  const std::string restored = eval(visible_script());
  ok(restored == "\"hidden\"", "and the shipped rule hides it again", restored);
  gWebView->PostWebMessageAsString(widen(available).c_str());
  wait_for(visible_script(), "\"shown\"", 8000);

  // ------------------------------------------------------------------ the click that was missing
  gFromPage.clear();
  eval(L"document.getElementById('updateNow').click()");
  ok(pump_until([]() { return page_sent("update"); }, 5000),
     "pressing it sends {\"type\":\"update\"} -- the message the native side has been waiting for");
  ok(eval(L"document.getElementById('updateNow').disabled") == "true",
     "and the button goes dead so one press is one updater");

  // ------------------------------------------------------------------ busy, then released
  gWebView->PostWebMessageAsString(widen(shell_update_busy_json(true)).c_str());
  ok(wait_for(L"document.getElementById('updateNow').disabled", "true", 5000),
     "while an install is starting the button stays disabled");
  gWebView->PostWebMessageAsString(widen(shell_update_busy_json(false)).c_str());
  // One eval per statement, never two inside one expression. Two pumping calls interleaved in a
  // single expression is what made earlier runs die part-way through, in a different place each
  // time; the message loop cannot be re-entered from both halves of one argument list.
  const bool released = wait_for(L"document.getElementById('updateNow').disabled", "false", 5000);
  const std::string releasedState =
      eval(L"JSON.stringify({disabled:document.getElementById('updateNow').disabled,"
           L"label:document.getElementById('updateNow').textContent})");
  ok(released, "a failed start hands the button back rather than leaving a dead end",
     releasedState);

  // ------------------------------------------------------------------ 나중에 defers, nothing more
  eval(L"document.getElementById('updateLater').click()");
  // ------------------------------------------------------------------ keyboard focus is defined
  //
  // A screenshot cannot carry focus state, and neither can this window: it is off screen and never
  // activated, so the engine does not treat a programmatic focus() as keyboard-driven and
  // `:focus-visible` does not match. Measured, not assumed -- the first version of this asserted
  // the ring was drawn and got ring=false with outline=none.
  //
  // So the claim is narrowed to what is actually checkable here: focus moves to the control, and
  // the stylesheet defines a visible ring for `:focus-visible`. That the ring PAINTS for a real
  // keyboard user is NOT shown by this and is listed as unverified.
  const std::string focused = eval(
      L"(function(){"
      L"  var el=document.getElementById('server');"
      L"  el.focus();"
      L"  return document.activeElement ? document.activeElement.id : 'none';"
      L"})()");
  ok(focused == "\"server\"", "focus lands on the control that was asked for", focused);

  const std::string ringRule = eval(
      L"(function(){"
      L"  for (var i=0;i<document.styleSheets.length;i++){"
      L"    var rules=document.styleSheets[i].cssRules;"
      L"    for (var j=0;j<rules.length;j++){"
      L"      var sel=rules[j].selectorText||'';"
      L"      if (sel.indexOf(':focus-visible')<0) continue;"
      L"      var o=rules[j].style.outline||rules[j].style.outlineStyle||'';"
      L"      if (o && o!=='none') return 'defined:'+o;"
      L"    }"
      L"  }"
      L"  return 'missing';"
      L"})()");
  ok(ringRule.find("defined:") != std::string::npos,
     "and the stylesheet defines a visible ring for keyboard focus", ringRule);

  const std::string afterLater = eval(visible_script());
  ok(afterLater == "\"hidden\"", "나중에 takes the offer down", afterLater);
  ok(eval(L"document.getElementById('signInCard').classList.contains('hidden')") == "false",
     "and leaves signing in exactly where it was");

  // ------------------------------------------------------------------ withdrawn
  gWebView->PostWebMessageAsString(widen(available).c_str());
  wait_for(visible_script(), "\"shown\"", 8000);
  gWebView->PostWebMessageAsString(widen(shell_update_cleared_json()).c_str());
  ok(wait_for(visible_script(), "\"hidden\"", 5000),
     "a check that finds nothing takes the offer back down");

  gWebView->PostWebMessageAsString(widen(available).c_str());
  wait_for(visible_script(), "\"shown\"", 8000);
  gWebView->PostWebMessageAsString(L"{\"type\":\"signedOut\"}");
  ok(wait_for(visible_script(), "\"hidden\"", 5000),
     "signing out withdraws an offer found for that session");

  gFromPage.clear();
  eval(L"document.getElementById('updateNow').click()");
  ok(!page_sent("update"), "and the withdrawn button asks for nothing");

  // ------------------------------------------------------------------ negative control: the old page
  //
  // The page as it shipped in 0.2.115 -- notice, no button. Loading it has to fail the same
  // assertions, because the whole point of this file is that it would have caught that release.
  const std::wstring stalePath = std::wstring(tempDir) + L"gnlink-stale-shell.html";
  {
    std::string stale = read_file(page);
    const std::string barStart = "<div id=\"updateBar\"";
    const size_t from = stale.find(barStart);
    const size_t to = stale.find("</div>", stale.find("id=\"updateLater\""));
    if (from != std::string::npos && to != std::string::npos) {
      stale.erase(from, (to + 6) - from);
    }
    std::ofstream out(stalePath, std::ios::binary);
    out.write(stale.data(), static_cast<std::streamsize>(stale.size()));
  }
  gReady = false;
  gFromPage.clear();
  gWebView->Navigate(stalePath.c_str());
  pump_until([]() { return gReady.load(); }, 20000);
  gWebView->PostWebMessageAsString(widen(available).c_str());
  Sleep(500);
  const std::string staleState = eval(pressable_script());
  ok(staleState != "\"pressable\"",
     "negative control: a page with no update button fails this test", staleState);
  gFromPage.clear();
  eval(L"var b=document.getElementById('updateNow'); if(b) b.click();");
  Sleep(500);
  ok(!page_sent("update"), "and it cannot send the message either");
  DeleteFileW(stalePath.c_str());

  if (gController) gController->Close();
  DestroyWindow(gWindow);

  std::printf("client_update_ui_test: %s (%d passed, %d failed)\n", gFail == 0 ? "PASS" : "FAIL",
              gPass, gFail);
  return gFail == 0 ? 0 : 1;
}
