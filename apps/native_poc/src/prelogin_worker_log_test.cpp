// prelogin-field D3: the pre-login worker's log (prelogin_worker_log.hpp) -- its trust, its bounds, its drain.
//
// Real folders and files under a test root this process makes. A non-elevated test cannot make SYSTEM-owned
// objects, so -- as in prelogin_store_chain_test -- the test's owner rule also trusts this user, and the folders
// and the file descriptor name this user besides SYSTEM/Administrators. The product's own rule (SYSTEM and
// Administrators only, its SDDL) is NOT exercised here; nor is a SYSTEM worker in the console session.
//
// File (this process): create + header + append across a restart; the new file's descriptor; a line cut at the
// line cap; one run writing past 2 MiB -> rotation, each file <= the cap, the total <= 2 x cap, nothing lost at
// the seam; refused -- current file with a permissive DACL, a current file that is a hard link, a logs folder
// that is a junction, a .1 that is a hard link / permissive at rotation time -- and the objects untouched.
// Sink (a child of this test, its stdout/stderr going into the log): printf, fprintf(stderr), std::cout and
// std::cerr all land; a run past 2 MiB rotates while it runs; a return of 49 after the last line keeps the last
// line (the drain at return); a TerminateProcess right after the hook's drain keeps it too.

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <io.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "prelogin_logon_desktop.hpp"
#include "prelogin_store_chain.hpp"
#include "prelogin_worker_log.hpp"

namespace pl = remote60::native_poc::prelogin;
namespace fs = std::filesystem;

namespace {
int gChecks = 0, gFailures = 0;
void check(const std::string& what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
}
std::wstring self_sid() {
  HANDLE tok = nullptr;
  OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
  BYTE buf[256];
  DWORD n = 0;
  GetTokenInformation(tok, TokenUser, buf, sizeof(buf), &n);
  CloseHandle(tok);
  LPWSTR s = nullptr;
  ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf)->User.Sid, &s);
  std::wstring out = s;
  LocalFree(s);
  return out;
}
const std::wstring kUser = self_sid();
bool trust_me(PSID s) {
  if (pl::OwnerIsSystemOrAdministrators(s)) return true;
  PSID me = nullptr;
  if (!ConvertStringSidToSidW(kUser.c_str(), &me)) return false;
  const bool eq = s && EqualSid(s, me);
  LocalFree(me);
  return eq;
}
const std::wstring kFolderSddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;" + kUser + L")";
const std::wstring kFileSddl = L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;" + kUser + L")";

bool make_dir(const std::wstring& path, const std::wstring& sddl) {
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) return false;
  SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
  const bool ok = CreateDirectoryW(path.c_str(), &sa) != FALSE;
  LocalFree(sd);
  return ok;
}
bool make_file(const std::wstring& path, const std::wstring& sddl, const std::string& content) {
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) return false;
  SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, &sa, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  LocalFree(sd);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD w = 0;
  WriteFile(h, content.data(), static_cast<DWORD>(content.size()), &w, nullptr);
  CloseHandle(h);
  return true;
}
std::string read_all(const std::wstring& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
uint64_t size_of(const std::wstring& p) {
  std::error_code ec;
  const auto s = fs::file_size(p, ec);
  return ec ? 0 : s;
}
// A root with GNLink and GNLink\logs made as the product would (but naming this user).
std::wstring make_root(const std::wstring& base, const std::wstring& name) {
  const std::wstring root = base + L"\\" + name;
  CreateDirectoryW(root.c_str(), nullptr);
  make_dir(root + L"\\GNLink", kFolderSddl);
  make_dir(root + L"\\GNLink\\logs", kFolderSddl);
  return root;
}
std::wstring log_of(const std::wstring& root) { return root + L"\\GNLink\\logs\\prelogin_worker.log"; }

// ---- r3 children (Codex codex_prelogin_field_r2.md R2-1 / R2-2): results go to a side file, not stdout ----
void note_result(const std::wstring& file, const std::string& line) {
  HANDLE h = CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;
  const std::string l = line + "\n";
  DWORD w = 0;
  WriteFile(h, l.data(), static_cast<DWORD>(l.size()), &w, nullptr);
  CloseHandle(h);
}
ULONGLONG ms_since(ULONGLONG t0) { return GetTickCount64() - t0; }
void print_bulk(int lines) {  // ~1 KB a line, half to stdout, half to stderr
  const std::string pad(1000, 'p');
  for (int i = 0; i < lines; ++i) {
    if (i & 1) std::fprintf(stderr, "bulk %06d %s\n", i, pad.c_str());
    else std::printf("bulk %06d %s\n", i, pad.c_str());
  }
}
// R2-1: the writer stops answering (a disk / forward that hangs), or the reader ends; prints past the pipe's
// capacity must go on, Drain must return within its bound, and the exit-15 hook path must end the process.
int r21_child(const std::wstring& root, const std::wstring& results, const std::string& mode) {
  if (pl::TheWorkerLogSink().Install(log_of(root), trust_me, "=== r21 child ===", kFileSddl.c_str()) != 0) return 90;
  pl::PreloginBeforeExitHook() = pl::DrainWorkerLogBeforeExit;
  static HANDLE never = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (mode == "stall") {
    pl::WorkerLogSink::WriterStallForTest() = [] { WaitForSingleObject(never, INFINITE); };  // never released
    std::printf("first line, then the writer hangs\n");
  } else {  // "readerstop": the reader takes one more chunk and ends
    pl::WorkerLogSink::ReaderStopForTest() = true;
    std::printf("the reader ends after this\n");
    Sleep(200);
  }
  ULONGLONG t0 = GetTickCount64();
  print_bulk(4000);  // ~4 MB, 60x the pipe
  note_result(results, "print_ms=" + std::to_string(ms_since(t0)));
  t0 = GetTickCount64();
  const bool drained = pl::TheWorkerLogSink().Drain(1500);
  note_result(results, "drain=" + std::to_string(drained ? 1 : 0) + " drain_ms=" + std::to_string(ms_since(t0)));
  note_result(results, "dropped=" + std::to_string(pl::TheWorkerLogSink().droppedLines()));
  // the immediate-exit path (PreloginStopServing / the desktop leave): hook, then TerminateProcess -- timed by the parent
  note_result(results, "exiting");
  if (pl::PreloginBeforeExitHook()) pl::PreloginBeforeExitHook()();
  TerminateProcess(GetCurrentProcess(), 15);
  return 99;
}
// R2-2: Install fails at a chosen point; afterwards stdout/stderr are what they were, nothing of the sink is left (the
// log can be opened exclusively), printing past the pipe's capacity goes on, and a second Install then works.
int r22_child(const std::wstring& root, const std::wstring& results, const std::string& mode) {
  using FA = pl::WorkerLogSink::FailAt;
  const HANDLE outBefore = GetStdHandle(STD_OUTPUT_HANDLE), errBefore = GetStdHandle(STD_ERROR_HANDLE);
  const intptr_t fd1Before = _get_osfhandle(1), fd2Before = _get_osfhandle(2);
  FA at = FA::None;
  if (mode == "stderr") at = FA::StdoutRedirected;
  if (mode == "event") at = FA::Event;
  if (mode == "thread") at = FA::Thread;
  pl::WorkerLogSink::InstallFailForTest() = at;
  const DWORD e = pl::TheWorkerLogSink().Install(log_of(root), trust_me, "=== r22 child ===", kFileSddl.c_str());
  note_result(results, "install1=" + std::to_string(e) + " installed=" + std::to_string(pl::TheWorkerLogSink().installed() ? 1 : 0));
  // "back as they were": the same kernel objects (a restored fd holds a duplicate of the original handle).
  auto same = [](intptr_t a, HANDLE b) {
    return a == reinterpret_cast<intptr_t>(b) ||
           (a != -1 && a != -2 && b && CompareObjectHandles(reinterpret_cast<HANDLE>(a), b) != FALSE);
  };
  note_result(results, std::string("restored=") +
                           (same(reinterpret_cast<intptr_t>(GetStdHandle(STD_OUTPUT_HANDLE)), outBefore) &&
                                    same(reinterpret_cast<intptr_t>(GetStdHandle(STD_ERROR_HANDLE)), errBefore) &&
                                    same(_get_osfhandle(1), reinterpret_cast<HANDLE>(fd1Before)) &&
                                    same(_get_osfhandle(2), reinterpret_cast<HANDLE>(fd2Before))
                                ? "1"
                                : "0"));
  HANDLE excl = CreateFileW(log_of(root).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  note_result(results, std::string("log_free=") + (excl != INVALID_HANDLE_VALUE ? "1" : "0"));
  if (excl != INVALID_HANDLE_VALUE) CloseHandle(excl);
  const ULONGLONG t0 = GetTickCount64();
  std::printf("AFTER-FAIL to the original stdout\n");
  print_bulk(2000);  // ~2 MB
  std::fflush(stdout);
  note_result(results, "print_ms=" + std::to_string(ms_since(t0)));
  pl::WorkerLogSink::InstallFailForTest() = FA::None;
  const DWORD e2 = pl::TheWorkerLogSink().Install(log_of(root), trust_me, "=== r22 child, second install ===", kFileSddl.c_str());
  std::printf("AFTER-REINSTALL into the log\n");
  const bool drained = pl::TheWorkerLogSink().Drain(2000);
  note_result(results, "install2=" + std::to_string(e2) + " drain2=" + std::to_string(drained ? 1 : 0));
  return 0;
}
// No std handles at all (a detached process, as a service child can be): Install works, prints land in the log.
int nostd_child(const std::wstring& root, const std::wstring& results) {
  note_result(results, std::string("std_out_before=") + (GetStdHandle(STD_OUTPUT_HANDLE) ? "set" : "none") +
                           " fileno=" + std::to_string(_fileno(stdout)));
  const DWORD e = pl::TheWorkerLogSink().Install(log_of(root), trust_me, "=== nostd child ===", kFileSddl.c_str());
  std::printf("NOSTD line via printf\n");
  std::cerr << "NOSTD line via cerr" << std::endl;
  const bool drained = pl::TheWorkerLogSink().Drain(2000);
  note_result(results, "install=" + std::to_string(e) + " drain=" + std::to_string(drained ? 1 : 0));
  return 0;
}

// ---- the sink child ----
int sink_child(const std::wstring& root, const std::string& mode) {
  const DWORD e = pl::TheWorkerLogSink().Install(log_of(root), trust_me, "=== sink child start ===", kFileSddl.c_str());
  if (e != 0) return 90;
  pl::PreloginBeforeExitHook() = pl::DrainWorkerLogBeforeExit;
  std::printf("printf line\n");
  std::fprintf(stderr, "fprintf stderr line\n");
  std::cout << "cout line" << std::endl;
  std::cerr << "cerr line" << std::endl;
  if (mode == "big") {
    const std::string pad(1000, 'x');
    for (int i = 0; i < 6000; ++i) std::printf("big %06d %s\n", i, pad.c_str());  // ~6 MB through the pipe
  }
  if (mode == "terminate") {
    std::cout << "LAST LINE BEFORE TERMINATE" << std::endl;
    if (pl::PreloginBeforeExitHook()) pl::PreloginBeforeExitHook()();
    TerminateProcess(GetCurrentProcess(), 15);
  }
  std::cout << "[native-video-host] FATAL: std::exception \"test\" at step=startup_init_graphics -- exit 49" << std::endl;
  pl::DrainWorkerLogBeforeExit();  // what main()'s guard does at return
  return 49;
}
// Runs a child; *ms = how long until it ended BY ITSELF (0xFFFFFFFF exit + killed when it did not within `limitMs`).
DWORD run_child_ex(const std::wstring& args, DWORD flags, HANDLE stdoutTo, DWORD limitMs, ULONGLONG* ms, bool* killed) {
  std::wstring cmd = args;
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  if (stdoutTo) {
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = stdoutTo;
    si.hStdError = stdoutTo;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  }
  PROCESS_INFORMATION pi{};
  DWORD code = 0xFFFFFFFFu;
  *killed = false;
  const ULONGLONG t0 = GetTickCount64();
  if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, stdoutTo != nullptr, flags, nullptr, nullptr, &si, &pi)) {
    if (WaitForSingleObject(pi.hProcess, limitMs) == WAIT_OBJECT_0) {
      GetExitCodeProcess(pi.hProcess, &code);
    } else {
      *killed = true;  // NOT counted as an ordinary end
      TerminateProcess(pi.hProcess, 0xFFFFFFFFu);
      WaitForSingleObject(pi.hProcess, 5000);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
  *ms = GetTickCount64() - t0;
  return code;
}
std::string result_of(const std::string& results, const std::string& key) {
  const size_t p = results.find(key + "=");
  if (p == std::string::npos) return "";
  const size_t e = results.find_first_of(" \n", p);
  return results.substr(p + key.size() + 1, e == std::string::npos ? std::string::npos : e - p - key.size() - 1);
}
DWORD run_child(const std::wstring& self, const std::wstring& root, const wchar_t* mode) {
  std::wstring cmd = L"\"" + self + L"\" --sink-child \"" + root + L"\" " + mode;
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  DWORD code = 0xFFFFFFFFu;
  if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    if (WaitForSingleObject(pi.hProcess, 60000) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    else TerminateProcess(pi.hProcess, 99);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
  return code;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc >= 5 && std::wstring(argv[1]) == L"--r21-child") {
    const std::wstring m = argv[4];
    return r21_child(argv[2], argv[3], std::string(m.begin(), m.end()));
  }
  if (argc >= 5 && std::wstring(argv[1]) == L"--r22-child") {
    const std::wstring m = argv[4];
    return r22_child(argv[2], argv[3], std::string(m.begin(), m.end()));
  }
  if (argc >= 4 && std::wstring(argv[1]) == L"--nostd-child") return nostd_child(argv[2], argv[3]);
  if (argc >= 4 && std::wstring(argv[1]) == L"--sink-child") {
    const std::wstring m = argv[3];
    return sink_child(argv[2], std::string(m.begin(), m.end()));
  }
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("the pre-login worker's own log: trust, bounds, drain (prelogin-field D3)\n\n");
  wchar_t selfPath[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
  fs::path repo = fs::path(selfPath).parent_path();
  while (!repo.empty() && !fs::exists(repo / L"AGENTS.md")) repo = repo.parent_path();
  const std::wstring base = (repo / L".claude" / L"test-tmp" / (L"worker-log-" + std::to_wstring(GetCurrentProcessId()))).wstring();
  fs::create_directories(base);

  // 1. create, header, append across a restart
  {
    const std::wstring root = make_root(base, L"r1");
    pl::WorkerLogFile f;
    const DWORD a = f.Open(log_of(root), trust_me, pl::kWorkerLogFileCap, pl::kWorkerLogLineCap, kFileSddl.c_str());
    f.WriteLine("=== start 1 ===");
    f.WriteLine("line a");
    f.Close();
    pl::WorkerLogFile g;
    const DWORD b = g.Open(log_of(root), trust_me, pl::kWorkerLogFileCap, pl::kWorkerLogLineCap, kFileSddl.c_str());
    g.WriteLine("=== start 2 ===");
    g.Close();
    const std::string c = read_all(log_of(root));
    check("a new log is created; a second start appends after the first (headers in order)",
          a == 0 && b == 0 && c == "=== start 1 ===\nline a\n=== start 2 ===\n", c);
    // the descriptor it was created with: protected, nobody beyond the trusted principals
    PSECURITY_DESCRIPTOR sd = nullptr;
    GetNamedSecurityInfoW(log_of(root).c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION, nullptr,
                          nullptr, nullptr, nullptr, &sd);
    check("the new file passes the private rule (trusted owner, nobody else in the DACL)",
          sd && pl::SecurityDescriptorAcceptable(sd, pl::ChainLevel::Private, trust_me));
    SECURITY_DESCRIPTOR_CONTROL ctl = 0;
    DWORD rev = 0;
    check("...and its DACL is protected (not inherited from the folder)", sd && GetSecurityDescriptorControl(sd, &ctl, &rev) && (ctl & SE_DACL_PROTECTED));
    if (sd) LocalFree(sd);
  }
  // 2. line cap
  {
    const std::wstring root = make_root(base, L"r2");
    pl::WorkerLogFile f;
    f.Open(log_of(root), trust_me, pl::kWorkerLogFileCap, pl::kWorkerLogLineCap, kFileSddl.c_str());
    f.WriteLine(std::string(10000, 'y'));
    f.Close();
    const std::string c = read_all(log_of(root));
    check("a 10000-byte line is cut to the line cap and marked", c.size() == pl::kWorkerLogLineCap && c.find("...[cut]\n") != std::string::npos,
          std::to_string(c.size()));
  }
  // 3. one run past 2 MiB: rotation, caps, continuity
  {
    const std::wstring root = make_root(base, L"r3");
    pl::WorkerLogFile f;
    f.Open(log_of(root), trust_me, pl::kWorkerLogFileCap, pl::kWorkerLogLineCap, kFileSddl.c_str());
    const std::string pad(990, 'z');
    char line[1100];
    for (int i = 0; i < 6000; ++i) {
      std::snprintf(line, sizeof(line), "n=%06d %s", i, pad.c_str());
      f.WriteLine(line);
    }
    f.Close();
    const uint64_t cur = size_of(log_of(root)), old = size_of(log_of(root) + L".1");
    const std::string c = read_all(log_of(root)), o = read_all(log_of(root) + L".1");
    const bool lastHere = c.find("n=005999 ") != std::string::npos;
    // the line after the last one in .1 is the first line of the current file
    int lastOld = -1, firstCur = -1;
    if (const size_t p = o.rfind("n="); p != std::string::npos) lastOld = std::atoi(o.c_str() + p + 2);
    if (c.rfind("n=", 0) == 0) firstCur = std::atoi(c.c_str() + 2);
    check("one run of ~6 MB: the current file and .1 each <= 2 MiB, together <= 4 MiB",
          cur > 0 && old > 0 && cur <= pl::kWorkerLogFileCap && old <= pl::kWorkerLogFileCap && cur + old <= 2 * pl::kWorkerLogFileCap,
          std::to_string(cur) + " + " + std::to_string(old));
    check("...the newest line is in the current file, and the two meet with nothing lost at the seam",
          lastHere && lastOld >= 0 && firstCur == lastOld + 1, std::to_string(lastOld) + " -> " + std::to_string(firstCur));
  }
  // 4. refused: a current file somebody else can write
  {
    const std::wstring root = make_root(base, L"r4");
    make_file(log_of(root), L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;WD)", "planted\n");
    const std::string before = read_all(log_of(root));
    pl::WorkerLogFile f;
    const DWORD e = f.Open(log_of(root), trust_me, pl::kWorkerLogFileCap, pl::kWorkerLogLineCap, kFileSddl.c_str());
    f.WriteLine("must not land");
    f.Close();
    check("a current file whose DACL lets Everyone write: refused, the file untouched",
          e == ERROR_ACCESS_DENIED && read_all(log_of(root)) == before);
  }
  // 5. refused: the current file is a hard link to something else
  {
    const std::wstring root = make_root(base, L"r5");
    const std::wstring decoy = root + L"\\decoy.txt";
    make_file(decoy, kFileSddl, "decoy\n");
    CreateHardLinkW(log_of(root).c_str(), decoy.c_str(), nullptr);
    pl::WorkerLogFile f;
    const DWORD e = f.Open(log_of(root), trust_me, pl::kWorkerLogFileCap, pl::kWorkerLogLineCap, kFileSddl.c_str());
    f.WriteLine("must not land");
    f.Close();
    check("a current file that is a hard link: refused, the other name's content untouched",
          e == ERROR_ACCESS_DENIED && read_all(decoy) == "decoy\n");
  }
  // 6. refused: the logs folder is a junction
  {
    const std::wstring root = base + L"\\r6";
    CreateDirectoryW(root.c_str(), nullptr);
    make_dir(root + L"\\GNLink", kFolderSddl);
    const std::wstring elsewhere = root + L"\\elsewhere";
    make_dir(elsewhere, kFolderSddl);
    const std::wstring cmd = L"cmd /c mklink /J \"" + root + L"\\GNLink\\logs\" \"" + elsewhere + L"\" >NUL";
    _wsystem(cmd.c_str());
    pl::WorkerLogFile f;
    const DWORD e = f.Open(log_of(root), trust_me, pl::kWorkerLogFileCap, pl::kWorkerLogLineCap, kFileSddl.c_str());
    f.Close();
    check("a logs folder that is a junction: refused, nothing created where it points",
          e != 0 && !fs::exists(elsewhere + L"\\prelogin_worker.log"), "win32 " + std::to_string(e));
    RemoveDirectoryW((root + L"\\GNLink\\logs").c_str());  // the junction itself, not its target
  }
  // 7. rotation target .1 planted as a hard link / with a permissive DACL: no rotation onto it, bounded, untouched
  for (int variant = 0; variant < 2; ++variant) {
    const std::wstring root = make_root(base, variant == 0 ? L"r7a" : L"r7b");
    const std::wstring decoy = root + L"\\decoy.txt";
    if (variant == 0) {
      make_file(decoy, kFileSddl, "decoy\n");
      CreateHardLinkW((log_of(root) + L".1").c_str(), decoy.c_str(), nullptr);
    } else {
      make_file(log_of(root) + L".1", L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;WD)", "planted .1\n");
    }
    const std::string before = read_all(variant == 0 ? decoy : log_of(root) + L".1");
    pl::WorkerLogFile f;
    const DWORD e = f.Open(log_of(root), trust_me, 4096, 512, kFileSddl.c_str());
    int written = 0;
    for (int i = 0; i < 100; ++i) written += f.WriteLine("line " + std::to_string(i) + " " + std::string(100, 'q')) ? 1 : 0;
    f.Close();
    check(std::string(variant == 0 ? "a .1 that is a hard link" : "a .1 that Everyone may write") +
              ": the rotation does not use it -- it stays as it was, the log stops at its cap",
          e == 0 && read_all(variant == 0 ? decoy : log_of(root) + L".1") == before && size_of(log_of(root)) <= 4096 &&
              written < 100 && f.dropped() > 0,
          "written " + std::to_string(written) + ", dropped " + std::to_string(f.dropped()));
  }
  // 8. the sink: stdout/stderr of a process into the log; drain at return 49; drain before TerminateProcess
  {
    const std::wstring root = make_root(base, L"s1");
    const DWORD code = run_child(selfPath, root, L"plain");
    const std::string c = read_all(log_of(root));
    check("sink: printf, fprintf(stderr), std::cout and std::cerr all land, after the header",
          c.find("=== sink child start ===") == 0 && c.find("printf line") != std::string::npos &&
              c.find("fprintf stderr line") != std::string::npos && c.find("cout line") != std::string::npos &&
              c.find("cerr line") != std::string::npos);
    check("sink: the exit-49 line printed last is on disk when the process has returned 49",
          code == 49 && c.find("FATAL: std::exception \"test\" at step=startup_init_graphics -- exit 49") != std::string::npos,
          "exit " + std::to_string(code));
  }
  {
    const std::wstring root = make_root(base, L"s2");
    const DWORD code = run_child(selfPath, root, L"terminate");
    check("sink: TerminateProcess right after the hook's drain keeps the line printed before it",
          code == 15 && read_all(log_of(root)).find("LAST LINE BEFORE TERMINATE") != std::string::npos, "exit " + std::to_string(code));
  }
  {
    const std::wstring root = make_root(base, L"s3");
    const DWORD code = run_child(selfPath, root, L"big");
    const uint64_t cur = size_of(log_of(root)), old = size_of(log_of(root) + L".1");
    const std::string c = read_all(log_of(root));
    check("sink: ~6 MB printed in one run -- rotated while running, each file <= 2 MiB, the last lines kept",
          code == 49 && old > 0 && cur <= pl::kWorkerLogFileCap && old <= pl::kWorkerLogFileCap &&
              c.find("big 005999 ") != std::string::npos && c.find("exit 49") != std::string::npos,
          std::to_string(cur) + " + " + std::to_string(old));
  }

  // ---- r3: R2-1 -- a writer that hangs, a reader that ends ----
  for (const wchar_t* mode : {L"stall", L"readerstop"}) {
    const std::wstring root = make_root(base, std::wstring(L"x1-") + mode);
    const std::wstring results = root + L"\\results.txt";
    ULONGLONG ms = 0;
    bool killed = false;
    const DWORD code = run_child_ex(L"\"" + std::wstring(selfPath) + L"\" --r21-child \"" + root + L"\" \"" + results + L"\" " + mode,
                                    CREATE_NO_WINDOW, nullptr, 20000, &ms, &killed);
    const std::string res = read_all(results);
    const std::string tag = std::string("R2-1 ") + (std::wstring(mode) == L"stall" ? "writer hangs" : "reader ends");
    const unsigned long long printMs = std::strtoull(result_of(res, "print_ms").c_str(), nullptr, 10);
    const unsigned long long drainMs = std::strtoull(result_of(res, "drain_ms").c_str(), nullptr, 10);
    check(tag + ": ~4 MB printed (60x the pipe) without waiting on it", !result_of(res, "print_ms").empty() && printMs < 10000,
          "print " + result_of(res, "print_ms") + " ms");
    check(tag + ": Drain(1500) returns false within its bound (not preserved, said so)",
          result_of(res, "drain") == "0" && drainMs <= 1500 + 300, "drain " + result_of(res, "drain_ms") + " ms");
    check(tag + ": the exit-15 path (hook + TerminateProcess) ends the process by itself, in bounded time",
          !killed && code == 15 && res.find("exiting") != std::string::npos, "exit " + std::to_string(code) + " after " + std::to_string(ms) + " ms");
    if (std::wstring(mode) == L"stall") {
      check(tag + ": the lines it could not keep are counted as dropped", std::strtoull(result_of(res, "dropped").c_str(), nullptr, 10) > 0,
            "dropped " + result_of(res, "dropped"));
    }
  }
  // ---- r3: R2-2 -- Install fails at a point; nothing is left behind ----
  for (const wchar_t* mode : {L"stderr", L"event", L"thread"}) {
    const std::wstring root = make_root(base, std::wstring(L"x2-") + mode);
    const std::wstring results = root + L"\\results.txt";
    const std::wstring outFile = root + L"\\child_stdout.txt";
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE out = CreateFileW(outFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, 0, nullptr);
    ULONGLONG ms = 0;
    bool killed = false;
    const DWORD code = run_child_ex(L"\"" + std::wstring(selfPath) + L"\" --r22-child \"" + root + L"\" \"" + results + L"\" " + mode,
                                    CREATE_NO_WINDOW, out, 30000, &ms, &killed);
    CloseHandle(out);
    const std::string res = read_all(results), childOut = read_all(outFile), logText = read_all(log_of(root));
    const std::string tag = std::string("R2-2 fail at ") + std::string(mode, mode + wcslen(mode));
    check(tag + ": Install reports the failure and is not installed", result_of(res, "install1") != "0" && result_of(res, "installed") == "0",
          "install1 " + result_of(res, "install1"));
    check(tag + ": stdout/stderr (fds and std handles) are back as they were", result_of(res, "restored") == "1");
    check(tag + ": nothing of the sink holds the log (it opens exclusively)", result_of(res, "log_free") == "1");
    check(tag + ": ~2 MB printed afterwards went on to the ORIGINAL stdout, none into the log",
          childOut.find("AFTER-FAIL") != std::string::npos && childOut.find("bulk 001999") != std::string::npos &&
              logText.find("AFTER-FAIL") == std::string::npos);
    check(tag + ": a second Install then works (the line lands in the log), and the child ends by itself",
          result_of(res, "install2") == "0" && result_of(res, "drain2") == "1" && logText.find("AFTER-REINSTALL") != std::string::npos &&
              !killed && code == 0,
          "exit " + std::to_string(code));
  }
  {
    const std::wstring root = make_root(base, L"x3-nostd");
    const std::wstring results = root + L"\\results.txt";
    ULONGLONG ms = 0;
    bool killed = false;
    const DWORD code = run_child_ex(L"\"" + std::wstring(selfPath) + L"\" --nostd-child \"" + root + L"\" \"" + results + L"\"",
                                    DETACHED_PROCESS, nullptr, 20000, &ms, &killed);
    const std::string res = read_all(results), logText = read_all(log_of(root));
    check("no std handles at start (detached): Install works and printf / cerr land in the log",
          result_of(res, "std_out_before") == "none" && result_of(res, "install") == "0" && result_of(res, "drain") == "1" &&
              logText.find("NOSTD line via printf") != std::string::npos && logText.find("NOSTD line via cerr") != std::string::npos &&
              !killed && code == 0,
          res);
  }

  std::error_code ec;
  fs::remove_all(base, ec);
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
