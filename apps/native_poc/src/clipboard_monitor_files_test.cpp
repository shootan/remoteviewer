// The host's clipboard monitor reports copies of files (t-zdmsd4gb step 2: R->P starts here), run
// where the clipboard is not the user's.
//
// clipboard_monitor_test touches the real session clipboard and is skipped unless allowed. This one
// never does: it starts itself again on a private window station (its own clipboard) and runs the
// product ClipboardMonitor there -- the same clipboard_monitor.cpp the host links. On that station it
// puts a CF_HDROP (path strings; no file behind them is opened by anyone), then text, then an
// over-long list, and checks what SetOnFiles reported each time: the paths, nothing, one over the
// offer limit.
//
// Tags: session-privilege (window station), pure-logic otherwise.

#include <windows.h>
#include <shlobj.h>

#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "clipboard_monitor.hpp"
#include "e2e_station_lock.hpp"

using namespace remote60::native_poc;

namespace {

int gChecks = 0, gFailures = 0;
void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
}

bool put_hdrop(const std::vector<std::wstring>& paths) {
  size_t chars = 1;
  for (const auto& p : paths) chars += p.size() + 1;
  HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + chars * sizeof(wchar_t));
  if (!g) return false;
  auto* df = static_cast<DROPFILES*>(GlobalLock(g));
  df->pFiles = sizeof(DROPFILES);
  df->fWide = TRUE;
  auto* out = reinterpret_cast<wchar_t*>(reinterpret_cast<uint8_t*>(df) + sizeof(DROPFILES));
  for (const auto& p : paths) {
    std::memcpy(out, p.c_str(), (p.size() + 1) * sizeof(wchar_t));
    out += p.size() + 1;
  }
  GlobalUnlock(g);
  if (!OpenClipboard(nullptr)) {
    GlobalFree(g);
    return false;
  }
  EmptyClipboard();
  const bool ok = SetClipboardData(CF_HDROP, g) != nullptr;
  if (!ok) GlobalFree(g);
  CloseClipboard();
  return ok;
}

bool put_text(const std::wstring& text) {
  HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
  if (!g) return false;
  std::memcpy(GlobalLock(g), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
  GlobalUnlock(g);
  if (!OpenClipboard(nullptr)) {
    GlobalFree(g);
    return false;
  }
  EmptyClipboard();
  const bool ok = SetClipboardData(CF_UNICODETEXT, g) != nullptr;
  if (!ok) GlobalFree(g);
  CloseClipboard();
  return ok;
}

int child() {
  std::mutex mu;
  std::condition_variable cv;
  std::vector<std::pair<uint64_t, std::vector<std::wstring>>> reports;
  ClipboardMonitor monitor;
  check("the product monitor starts on this station", monitor.Start([](const std::u16string&) {}));
  put_text(L"before");
  Sleep(200);
  monitor.SetOnFiles([&](uint64_t seq, std::vector<std::wstring> paths) {
    std::lock_guard<std::mutex> lock(mu);
    reports.emplace_back(seq, std::move(paths));
    cv.notify_all();
  });
  auto wait_for = [&](size_t n) {
    std::unique_lock<std::mutex> lock(mu);
    return cv.wait_for(lock, std::chrono::seconds(5), [&] { return reports.size() >= n; });
  };
  check("setting the listener reports the current content once (no files)", wait_for(1) && reports[0].second.empty());

  const std::vector<std::wstring> two = {L"C:\\no such dir\\a.txt", L"C:\\no such dir\\사진 b.bin"};
  check("a CF_HDROP is put on this station's clipboard", put_hdrop(two));
  check("...and reported: exactly its path strings, in order", wait_for(2) && reports[1].second == two);
  check("...with the clipboard's sequence number", reports.size() >= 2 && reports[1].first > reports[0].first,
        reports.size() >= 2 ? std::to_string(reports[0].first) + " -> " + std::to_string(reports[1].first) : "");

  check("text replaces it", put_text(L"not files"));
  check("...and is reported as no files", wait_for(3) && reports[2].second.empty());

  std::vector<std::wstring> many;
  for (int i = 0; i < 105; ++i) many.push_back(L"C:\\x\\f" + std::to_wstring(i));
  check("a copy of 105 files is put on the clipboard", put_hdrop(many));
  check("...and reported cut at one over the offer limit (101), so the rules say \"too many\"",
        wait_for(4) && reports[3].second.size() == 101, reports.size() >= 4 ? std::to_string(reports[3].second.size()) : "");

  monitor.SetOnFiles(nullptr);
  const size_t before = reports.size();
  put_text(L"after");
  Sleep(500);
  check("after SetOnFiles(nullptr) nothing more is reported", reports.size() == before);
  monitor.Stop();
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "PASSED", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc >= 2 && std::wstring(argv[1]) == L"--child") return child();
  remote60::native_poc::e2e::StationLock stationLock;  // TEST ONLY: queue behind any other clipboard e2e (e2e_station_lock.hpp)
  if (!stationLock.Acquire("clipboard_monitor_files_test")) return remote60::native_poc::e2e::StationLock::Busy("clipboard_monitor_files_test");

  const DWORD userSeq = GetClipboardSequenceNumber();
  // A private window station: its clipboard is not the user's.
  HWINSTA ws = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
  wchar_t name[256] = L"";
  DWORD len = 0;
  if (ws) GetUserObjectInformationW(ws, UOI_NAME, name, sizeof(name), &len);
  HWINSTA orig = GetProcessWindowStation();
  HDESK dk = nullptr;
  if (ws) {
    SetProcessWindowStation(ws);
    dk = CreateDesktopW(L"Default", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    SetProcessWindowStation(orig);
  }
  check("a private window station for the monitor", ws && dk);
  if (!ws || !dk) return 1;
  std::wstring desktop = std::wstring(name) + L"\\Default";

  // The child's output comes back through a pipe.
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE rd = nullptr, wr = nullptr;
  CreatePipe(&rd, &wr, &sa, 0);
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
  wchar_t self[MAX_PATH] = L"";
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  std::wstring cmd = L"\"" + std::wstring(self) + L"\" --child";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.lpDesktop = desktop.data();
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = wr;
  si.hStdError = wr;
  PROCESS_INFORMATION pi{};
  const bool started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != 0;
  CloseHandle(wr);
  check("the monitor runs in a child on that station", started);
  std::string out;
  char buf[4096];
  DWORD n = 0;
  while (started && ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n > 0) out.append(buf, n);
  CloseHandle(rd);
  DWORD code = 1;
  if (started) {
    WaitForSingleObject(pi.hProcess, 30000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
  std::printf("--- child (private station) ---\n%s--- end child ---\n", out.c_str());
  check("the child's checks all passed", code == 0 && out.find("RESULT: PASSED") != std::string::npos,
        "exit=" + std::to_string(code));
  check("the user's clipboard (WinSta0) did not move", GetClipboardSequenceNumber() == userSeq);
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "PASSED", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
