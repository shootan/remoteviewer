// clip-image: the isolation the image tests stand on (plan r1 ⑧, Codex 5).
//
// Measured, and pinned here: a medium process cannot create a NAMED window station (err 5), and an
// UNNAMED one is named after the logon session (Service-0x0-<luid>$) -- so asking twice returns the
// SAME station, with ONE clipboard. Two independent clipboards (sender + receiver) are therefore not
// available to a test in one logon session; the two-sided image e2e injects the clipboard at the OS
// boundary instead, and the real clipboard is exercised one side at a time on the private station.
//
// What this test holds to: the private station's clipboard is a real clipboard that its processes
// share, and the interactive clipboard (WinSta0) does not move at all while it is used.
//
//   remote60_clip_image_winsta_test            (parent)
//   remote60_clip_image_winsta_test --child TAG RESULTFILE
#include "e2e_isolation.hpp"  // first: it brings winsock2.h, which must precede windows.h

#include <windows.h>

#include <cstdio>
#include <string>
#include "e2e_station_lock.hpp"

namespace {
int g_failed = 0;
int g_checks = 0;
void check(const std::string& what, bool ok) {
  ++g_checks;
  if (!ok) ++g_failed;
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
}

// The sibling may hold the clipboard for a moment: retry the open, as clipboard_win32.hpp does.
bool open_with_retry() {
  for (int i = 0; i < 50; ++i) {
    if (OpenClipboard(nullptr)) return true;
    Sleep(20);
  }
  return false;
}

int run_child(const wchar_t* tag, const wchar_t* resultFile) {
  // Put TAG on this station's clipboard, then read what is there.
  const DWORD seqBefore = GetClipboardSequenceNumber();
  const bool readOnly = wcscmp(tag, L"-") == 0;
  bool setOk = readOnly;
  if (!readOnly && open_with_retry()) {
    EmptyClipboard();
    const size_t bytes = (wcslen(tag) + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    std::memcpy(GlobalLock(h), tag, bytes);
    GlobalUnlock(h);
    setOk = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
    CloseClipboard();
  }
  std::wstring read;
  if (open_with_retry()) {
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
      read = static_cast<const wchar_t*>(GlobalLock(h));
      GlobalUnlock(h);
    }
    CloseClipboard();
  }
  const DWORD seqAfter = GetClipboardSequenceNumber();
  FILE* f = _wfopen(resultFile, L"w, ccs=UTF-8");
  if (!f) return 3;
  fwprintf(f, L"set=%d read=%ls seqBefore=%lu seqAfter=%lu\n", setOk ? 1 : 0, read.c_str(), seqBefore, seqAfter);
  fclose(f);
  return 0;
}

struct Station {
  HWINSTA ws = nullptr;
  HDESK dk = nullptr;
  std::wstring desktop;
};

Station make_station() {
  Station s;
  s.ws = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);  // unnamed: a medium process may
  if (!s.ws) return s;                                                  // not create a named one (err 5)
  wchar_t name[256] = L"";
  DWORD len = 0;
  GetUserObjectInformationW(s.ws, UOI_NAME, name, sizeof(name), &len);
  HWINSTA orig = GetProcessWindowStation();
  SetProcessWindowStation(s.ws);
  s.dk = CreateDesktopW(L"Default", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
  SetProcessWindowStation(orig);
  s.desktop = std::wstring(name) + L"\\Default";
  return s;
}

bool read_result(const std::wstring& path, std::wstring* line) {
  FILE* f = _wfopen(path.c_str(), L"r, ccs=UTF-8");
  if (!f) return false;
  wchar_t buf[512] = L"";
  fgetws(buf, 512, f);
  fclose(f);
  *line = buf;
  return true;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc >= 4 && wcscmp(argv[1], L"--child") == 0) return run_child(argv[2], argv[3]);
  remote60::native_poc::e2e::StationLock stationLock;  // TEST ONLY: queue behind any other clipboard e2e (e2e_station_lock.hpp)
  if (!stationLock.Acquire("clip_image_winsta_test")) return remote60::native_poc::e2e::StationLock::Busy("clip_image_winsta_test");

  const DWORD interactiveBefore = GetClipboardSequenceNumber();
  wchar_t self[MAX_PATH];
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  // The children's result files: in the repository's test scratch (a directory this run creates),
  // never %TEMP%.
  remote60::native_poc::e2e::StagingDir staging;
  if (!staging.Create(L"clip_winsta")) {
    std::printf("FAIL  %s\n", staging.why().c_str());
    return 1;
  }
  const std::wstring resA = staging.path() + L"winsta_a.txt";
  const std::wstring resB = staging.path() + L"winsta_b.txt";

  Station a = make_station(), b = make_station();
  check("a private window station was created", a.ws && a.dk && b.ws && b.dk);
  check("a second unnamed station is the SAME station (named after the logon session): one clipboard",
        a.desktop == b.desktop);
  auto start = [&](Station& s, const wchar_t* tag, const std::wstring& res, PROCESS_INFORMATION* pi) {
    std::wstring cmd = L"\"" + std::wstring(self) + L"\" --child " + tag + L" \"" + res + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.lpDesktop = s.desktop.data();
    return CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, pi) != FALSE;
  };
  // Sequential, so the outcome does not depend on scheduling: A writes and EXITS, then B (on the
  // "second" station -- the same one) only reads.
  PROCESS_INFORMATION pa{}, pb{};
  bool started = start(a, L"alpha-station", resA, &pa);
  if (started) WaitForSingleObject(pa.hProcess, 15000);
  started = started && start(b, L"-", resB, &pb);
  if (started) WaitForSingleObject(pb.hProcess, 15000);
  check("a writer and then a reader ran on the private station", started);
  std::wstring la, lb;
  const bool gotA = read_result(resA, &la), gotB = read_result(resB, &lb);
  std::wprintf(L"  station A: %ls  station B: %ls", la.c_str(), lb.c_str());
  check("the writer set its clipboard and read it back", gotA && la.find(L"set=1 read=alpha-station ") != std::wstring::npos);
  // The reader started after the writer exited: the data outlives its writer (HGLOBAL, no owner
  // window), and the "second" station is the same clipboard.
  check("a later process on the second unnamed station reads the first one's text (one clipboard)",
        gotB && lb.find(L"read=alpha-station ") != std::wstring::npos);
  const DWORD interactiveAfter = GetClipboardSequenceNumber();
  check("the interactive clipboard (WinSta0) did not move: sequence " + std::to_string(interactiveBefore) + " -> " +
            std::to_string(interactiveAfter),
        interactiveBefore == interactiveAfter);
  const bool stagingRemoved = staging.Remove();  // before the check: argument order is unspecified
  check("the staging directory is removed" + (stagingRemoved ? std::string() : ": " + staging.why()), stagingRemoved);
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
