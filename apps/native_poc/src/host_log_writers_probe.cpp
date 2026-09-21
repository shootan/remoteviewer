#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

// Do the supervisor's two log writers lose lines when they overlap? (updater-health-gate Phase 1)
//
// host_app.log has two writers in one process. The child-output reader holds one handle open for
// the child's lifetime and appends every line the streaming host prints (AppendLogLine, "ab",
// _SH_DENYNO, flushed per line). Everything else -- including the health report the updater's gate
// reads -- goes through append_host_app_log, which opens "a", writes one line, and closes.
//
// On 2026-09-21 the health gate timed out on a host that was demonstrably online: the child's
// "directory online" line is in host_app.log, and the two lines note_child_log_line writes in
// response to it -- "directory-online observed" and "health version=0.2.131 directory=ok" -- are
// not, anywhere. Both of those go through append_host_app_log. The one that did survive that
// minute, "health version=0.2.131 directory=pending", was written a second earlier, before the
// child started producing output.
//
// This measures the mechanism directly rather than arguing from the log. It reproduces both
// writers against a scratch file at two child-output rates, and counts how many of the
// append_host_app_log-style lines survive.
//
// It asserts nothing about the product. It prints a measurement, and the Phase 1 conclusion is
// drawn from the number.

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <share.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::wstring scratch_path(const wchar_t* tag) {
  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  return std::wstring(temp) + L"gnlink-log-writers-" + tag + L"-" +
         std::to_wstring(GetCurrentProcessId()) + L".log";
}

/** Exactly what the child-output reader does: one handle, held open, flushed per line. */
void reader_writer(const std::wstring& path, int lines, int gapUs, std::atomic<bool>* go) {
  FILE* log = _wfsopen(path.c_str(), L"ab", _SH_DENYNO);
  if (!log) return;
  while (!go->load()) Sleep(0);
  for (int i = 0; i < lines; ++i) {
    // A child line is long: the real ones run 80-200 bytes.
    std::string line = "09-21 15:11:54.282 [native-video-host] child output line " +
                       std::to_string(i) + " padding-to-a-realistic-length-0123456789012345678901";
    std::fputs(line.c_str(), log);
    std::fputc('\n', log);
    std::fflush(log);
    if (gapUs > 0) {
      LARGE_INTEGER freq{}, start{}, now{};
      QueryPerformanceFrequency(&freq);
      QueryPerformanceCounter(&start);
      const double want = gapUs / 1e6;
      do { QueryPerformanceCounter(&now); } while ((now.QuadPart - start.QuadPart) / double(freq.QuadPart) < want);
    }
  }
  std::fclose(log);
}

/** Exactly what append_host_app_log does: open "a", one line, close. */
bool append_once(const std::wstring& path, const std::string& line) {
  FILE* file = nullptr;
  if (_wfopen_s(&file, path.c_str(), L"a") != 0 || !file) return false;
  std::fprintf(file, "%s\n", line.c_str());
  std::fclose(file);
  return true;
}

int count_present(const std::wstring& path, const std::vector<std::string>& wanted) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string all = ss.str();
  int found = 0;
  for (const std::string& w : wanted) if (all.find(w) != std::string::npos) ++found;
  return found;
}

struct Result {
  int written = 0;
  int openFailed = 0;
  int survived = 0;
};

Result run(const wchar_t* tag, int readerLines, int readerGapUs, int appends) {
  const std::wstring path = scratch_path(tag);
  DeleteFileW(path.c_str());
  { FILE* f = nullptr; if (_wfopen_s(&f, path.c_str(), L"w") == 0 && f) std::fclose(f); }

  std::atomic<bool> go{false};
  std::thread reader(reader_writer, path, readerLines, readerGapUs, &go);
  std::vector<std::string> wanted;
  go.store(true);

  Result r;
  for (int i = 0; i < appends; ++i) {
    const std::string line = "09-21 15:11:54 [host-app][lifecycle] directory-online observed marker=" +
                             std::to_string(i);
    wanted.push_back("marker=" + std::to_string(i) + "\n");
    if (append_once(path, line)) ++r.written; else ++r.openFailed;
    Sleep(1);
  }
  reader.join();
  r.survived = count_present(path, wanted);
  DeleteFileW(path.c_str());
  return r;
}

}  // namespace

/**
 * The sharing question on its own: a handle is held open the way the reader holds it, and nothing
 * is written through it at all. If the append writer cannot open the file in that state, the
 * overwriting above is a second problem and not the first one.
 */
void probe_sharing_only() {
  const std::wstring path = scratch_path(L"share");
  DeleteFileW(path.c_str());
  { FILE* f = nullptr; if (_wfopen_s(&f, path.c_str(), L"w") == 0 && f) std::fclose(f); }

  std::cout << "  the append writer, with nobody holding the file:      "
            << (append_once(path, "before") ? "opened" : "OPEN FAILED") << "\n";

  FILE* held = _wfsopen(path.c_str(), L"ab", _SH_DENYNO);
  std::cout << "  ...the reader's handle is open now (ab, _SH_DENYNO)\n";
  std::cout << "  the append writer, while that handle is held:         "
            << (append_once(path, "during") ? "opened" : "OPEN FAILED") << "\n";
  if (held) std::fclose(held);
  std::cout << "  ...the reader's handle is closed again\n";
  std::cout << "  the append writer, after it was released:             "
            << (append_once(path, "after") ? "opened" : "OPEN FAILED") << "\n";

  // And the same thing the way the reader opens it, to show the mode is what differs.
  FILE* held2 = _wfsopen(path.c_str(), L"ab", _SH_DENYNO);
  FILE* second = _wfsopen(path.c_str(), L"a", _SH_DENYNO);
  std::cout << "  an append open that ASKS to share, while held:        "
            << (second ? "opened" : "OPEN FAILED") << "\n";
  if (second) std::fclose(second);
  if (held2) std::fclose(held2);
  DeleteFileW(path.c_str());
}

int main() {
  std::cout << "host_log_writers_probe\n";
  std::cout << "  Two writers on one file, as host_app_main has them. The reader holds a handle\n"
               "  open and flushes each line; the append writer opens, writes one line, closes.\n\n";

  probe_sharing_only();
  std::cout << "\n";

  struct Case { const wchar_t* tag; const char* name; int lines; int gapUs; };
  const Case cases[] = {
      {L"quiet", "quiet child (0.2.130-like: a line every 20ms)", 120, 20000},
      {L"busy", "busy child (0.2.131-like: a line every 200us)", 12000, 200},
      {L"flood", "flood (no gap at all)", 40000, 0},
  };

  for (const Case& c : cases) {
    const Result r = run(c.tag, c.lines, c.gapUs, 40);
    std::cout << "  " << c.name << "\n"
              << "    append lines attempted=40 opened=" << r.written
              << " openFailed=" << r.openFailed << " survivedInFile=" << r.survived
              << "  LOST=" << (r.written - r.survived) << "\n";
  }

  std::cout << "\n  A line that was opened and written but is not in the file was overwritten by\n"
               "  the other writer. Both use append mode, which the CRT implements as seek-to-end\n"
               "  then write -- two steps, not one -- so two handles can choose the same offset.\n";
  return 0;
}
