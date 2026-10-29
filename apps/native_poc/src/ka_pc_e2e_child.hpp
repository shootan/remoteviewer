// ka-pc e2e (Codex codex_ka_pc_test_src_r1.md N1 / N2): what the two ka-pc e2e tests (managed, shell) share about the
// processes they start and the folder they run in. One copy, so the two cannot drift.
//
// Child -- one process this test starts:
//   * Start: every handle set-up step checked -- the pipe, its read end made non-inheritable, NUL, the attribute list
//     and its HANDLE_LIST whitelist. Any failure: this test's own handles closed, NO process created (so nothing
//     resumed). Then started suspended and resumed only inside a verified kill-on-close job (the shared guard's
//     e2e_adopt_or_invalid). Its output goes, chunk by chunk as it is read, into its OWN new log file (Win32 WriteFile,
//     nothing buffered in the CRT): an INVALID exit anywhere later (std::_Exit) loses none of what was read.
//   * Guard (a test host only): the shared guard's initial OFF gate, then its monitor -- used as it is (ka-r3 b8dcc93).
//   * End -- once (a child never started, or already ended, is left alone): its job ended (confirmed or not), its
//     reader joined (a read still blocked is cancelled, bounded), a trailer beside its log (<log>.end: producer end,
//     read end, error, bytes, log kept) written BEFORE a host's Finalize can end the test, then for EVERY child the
//     generic verdict -- its end confirmed, its output read to a real EOF, its log kept -- a failure counted
//     (child_failures), never only printed. A host's OFF lines are asked of a host only (its monitor).
//   * children_end_all(): every child still running ended and judged -- called before a test's result.
//
// private_temp(): this run's own TEMP/TMP, every step checked, then what GetTempPathW really returns compared with it.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "e2e_injection_guard.hpp"

namespace ka_pc_e2e {

namespace e2e = remote60::native_poc::e2e;

inline std::string narrow_path(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

// ---------------------------------------------------------------- paths
inline std::wstring no_trailing_slash(std::wstring s) {
  while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
  return s;
}
inline std::wstring full_path(const std::wstring& p) {
  wchar_t b[2048] = {};
  const DWORD n = GetFullPathNameW(p.c_str(), 2048, b, nullptr);
  return n && n < 2048 ? std::wstring(b, n) : std::wstring();
}
inline bool same_path(const std::wstring& a, const std::wstring& b) {
  if (a.empty() || b.empty()) return false;
  return _wcsicmp(no_trailing_slash(full_path(a)).c_str(), no_trailing_slash(full_path(b)).c_str()) == 0;
}
// What an existing folder really is once every link in its path is followed ("" = it cannot be opened / named).
inline std::wstring final_path_of(const std::wstring& path) {
  HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (h == INVALID_HANDLE_VALUE) return {};
  wchar_t b[2048] = {};
  const DWORD n = GetFinalPathNameByHandleW(h, b, 2048, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  CloseHandle(h);
  if (!n || n >= 2048) return {};
  std::wstring s(b, n);
  if (s.rfind(L"\\\\?\\", 0) == 0) s = s.substr(4);
  return s;
}
// `path` exists and is itself (no junction / link anywhere on the way to it).
inline bool resolves_to_itself(const std::wstring& path) { return same_path(final_path_of(path), path); }
// `path` lies inside the folder `root` (both compared whole, case-insensitively).
inline bool is_inside(const std::wstring& path, const std::wstring& root) {
  const std::wstring p = no_trailing_slash(full_path(path)), r = no_trailing_slash(full_path(root)) + L"\\";
  return !p.empty() && p.size() > r.size() && _wcsnicmp(p.c_str(), r.c_str(), r.size()) == 0;
}

// The run's own TEMP / TMP (N1): <root>temp made NEW, both variables set (each setter checked), and what GetTempPathW
// -- the call the product makes, with the product's MAX_PATH buffer -- really returns compared with it. *tempDir gets it
// with a trailing backslash. False (with why) on anything not confirmed: the caller ends INVALID before any child.
inline bool private_temp(const std::wstring& root, std::wstring* tempDir, std::string* why) {
  if (!resolves_to_itself(root)) {
    *why = "the run folder does not resolve to itself (" + narrow_path(final_path_of(root)) + ")";
    return false;
  }
  const std::wstring tmp = no_trailing_slash(root) + L"\\temp";
  if (!CreateDirectoryW(tmp.c_str(), nullptr)) {
    *why = "the run's temp folder could not be made new (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  if (!resolves_to_itself(tmp)) {
    *why = "the run's temp folder does not resolve to itself";
    return false;
  }
  if (!SetEnvironmentVariableW(L"TEMP", tmp.c_str()) || !SetEnvironmentVariableW(L"TMP", tmp.c_str())) {
    *why = "TEMP / TMP could not be set (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  wchar_t b[MAX_PATH] = {};
  const DWORD n = GetTempPathW(MAX_PATH, b);
  if (n == 0 || n >= MAX_PATH) {
    *why = "GetTempPathW gave no path that fits MAX_PATH (" + std::to_string(n) + ")";
    return false;
  }
  if (!same_path(std::wstring(b, n), tmp)) {
    *why = "GetTempPathW resolves to " + narrow_path(std::wstring(b, n)) + ", not this run's temp";
    return false;
  }
  *tempDir = tmp + L"\\";
  return true;
}

// ---------------------------------------------------------------- children
inline int& child_failures() {
  static int n = 0;
  return n;
}
inline int& children_started() {
  static int n = 0;
  return n;
}
// ka_pc_e2e_child_test only: the name of one Start set-up step to treat as failed (nullptr in every e2e) -- so the
// "a failed step creates no process" path is provoked without breaking the OS call itself.
inline const char*& start_step_refused_for_contrast() {
  static const char* step = nullptr;
  return step;
}
inline bool start_step_ok(bool ok, const char* step) {
  const char* refused = start_step_refused_for_contrast();
  if (refused && std::strcmp(refused, step) == 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return false;
  }
  return ok;
}
inline void child_fail(const std::string& what) {
  std::printf("FAIL  child %s\n", what.c_str());
  std::fflush(stdout);
  ++child_failures();
}

// The generic end verdict of any child (N2): empty = its end confirmed, its output read to a real EOF and kept in its
// log; otherwise why not. Pure, so its cases are tested without a process (ka_pc_e2e_child_test).
inline std::string child_end_why(bool producerEnded, e2e::E2eReadEnd readEnd, unsigned long readErr, bool sinkFailed,
                                 unsigned long sinkErr) {
  if (!producerEnded) return "its end (every process of its job) is NOT confirmed";
  if (readEnd != e2e::E2eReadEnd::Eof) {
    return std::string("its output was NOT read to its end: ") + e2e::e2e_read_end_name(readEnd) + " (error " +
           std::to_string(readErr) + ")";
  }
  if (sinkFailed) return "its output was NOT kept whole in its log (write error " + std::to_string(sinkErr) + ")";
  return {};
}

// r3 (Codex evidence candidate r1): a test step that ends a child by CLOSING its job itself (the shell's crash, as the
// managed test plays it) records what it saw -- the end is never inferred from a job that is just gone. Only for one
// ready, non-host child with a single process in its job. Every item must hold, else its end is NOT confirmed.
struct CrashClose {
  bool done = false;          // the explicit step ran (Child::CrashCloseJob)
  bool killOnClose = false;   // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE read back from the job, before the close
  bool member = false;        // IsProcessInJob(root, job), before the close
  bool singleRoot = false;    // the job's listed members were the root and only console hosts, before the close
  int consoleHosts = 0;       // how many of those were the OS console host (conhost.exe)
  bool rootAlive = false;     // the root had not ended yet, before the close
  bool closed = false;        // CloseHandle(job) succeeded
  unsigned long closeErr = 0;
  bool rootEnded = false;     // the root's handle signalled within the bound after the close
};
// Empty = the crash-close evidence is whole; otherwise what is missing. Pure (ka_pc_e2e_child_test).
inline std::string crash_close_why(const CrashClose& c) {
  if (!c.done) return "no crash-close record";
  if (!c.killOnClose) return "kill-on-close was not read back from the job";
  if (!c.member) return "the root was not confirmed in the job";
  if (!c.singleRoot) return "the job did not hold the root alone (a process other than the root and its console host)";
  if (!c.rootAlive) return "the root had already ended before the close";
  if (!c.closed) return "the job's handle could not be closed (error " + std::to_string(c.closeErr) + ")";
  if (!c.rootEnded) return "the root did not end after the close";
  return {};
}

struct Child;
inline std::vector<Child*>& started_children() {
  static std::vector<Child*> v;
  return v;
}

struct Child {
  PROCESS_INFORMATION pi{};
  HANDLE job = nullptr;
  HANDLE out = nullptr;
  HANDLE sink = INVALID_HANDLE_VALUE;  // this child's own log, written as its output is read
  std::thread reader;
  std::atomic<bool> reading{false};
  std::atomic<int> readResult{0};
  std::atomic<unsigned long> readErr{0};
  std::atomic<bool> sinkFailed{false};
  std::atomic<unsigned long> sinkErr{0};
  std::mutex mu;
  std::string text;
  std::wstring logPath;  // required: set before Start; made NEW there (an existing file is refused, never overwritten)
  bool started = false;
  bool ended = false;
  std::unique_ptr<e2e::E2eInjectionMonitor> monitor;
  CrashClose crash;  // r3: set only by CrashCloseJob
  std::string endEvidence;  // r3: what End's producer verdict rests on (written to the trailer)

  Child() = default;
  Child(const Child&) = delete;
  Child& operator=(const Child&) = delete;

  std::string Name() const { return logPath.empty() ? std::string("(no log)") : narrow_path(logPath); }

  bool Start(const std::wstring& cmd, const std::wstring& dir, const std::vector<wchar_t>* env,
             std::vector<HANDLE> extraInherit = {}) {
    if (started) {
      child_fail(Name() + ": started twice -- refused");
      return false;
    }
    if (logPath.empty()) {
      child_fail("with no log of its own -- not started");
      return false;
    }
    sink = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (sink == INVALID_HANDLE_VALUE) {
      child_fail(Name() + ": its log could not be made new (error " + std::to_string(GetLastError()) + ") -- not started");
      return false;
    }
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE w = nullptr;
    HANDLE nul = INVALID_HANDLE_VALUE;
    std::vector<char> attrBuf;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = nullptr;
    // N2: a set-up step that fails ends here -- this test's own handles closed, NO process created
    auto refuse = [&](const char* step) {
      const DWORD e = GetLastError();
      if (attrs) DeleteProcThreadAttributeList(attrs);
      if (w) CloseHandle(w);
      if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
      if (out) CloseHandle(out), out = nullptr;
      CloseHandle(sink), sink = INVALID_HANDLE_VALUE;
      child_fail(Name() + ": " + step + " failed (error " + std::to_string(e) + ") -- no process created");
      return false;
    };
    if (!start_step_ok(CreatePipe(&out, &w, &sa, 1 << 20), "pipe")) return refuse("CreatePipe");
    if (!start_step_ok(SetHandleInformation(out, HANDLE_FLAG_INHERIT, 0), "noinherit"))
      return refuse("making the read end non-inheritable");
    nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    if (!start_step_ok(nul != INVALID_HANDLE_VALUE, "nul")) return refuse("opening NUL");
    std::vector<HANDLE> inherit = {w, nul};
    for (HANDLE h : extraInherit) inherit.push_back(h);
    SIZE_T sz = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &sz);  // sizing call: fails by design, gives sz
    if (!start_step_ok(sz != 0, "attrsize")) return refuse("sizing the attribute list");
    attrBuf.resize(sz);
    if (!start_step_ok(InitializeProcThreadAttributeList(reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data()),
                                                         1, 0, &sz),
                       "attrinit"))
      return refuse("InitializeProcThreadAttributeList");
    attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    if (!start_step_ok(UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit.data(),
                                                 inherit.size() * sizeof(HANDLE), nullptr, nullptr),
                       "handlelist"))
      return refuse("installing the HANDLE_LIST whitelist");
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdOutput = w;
    si.StartupInfo.hStdError = w;
    si.StartupInfo.hStdInput = nul;
    si.lpAttributeList = attrs;
    std::vector<wchar_t> c(cmd.begin(), cmd.end());
    c.push_back(0);
    const BOOL ok = CreateProcessW(nullptr, c.data(), nullptr, nullptr, TRUE,
                                   CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT |
                                       (env ? CREATE_UNICODE_ENVIRONMENT : 0),
                                   env ? const_cast<wchar_t*>(env->data()) : nullptr, dir.c_str(), &si.StartupInfo, &pi);
    if (!ok) return refuse("CreateProcessW");
    DeleteProcThreadAttributeList(attrs);
    attrs = nullptr;
    CloseHandle(w);
    CloseHandle(nul);
    job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim{};
      lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      SetInformationJobObject(job, JobObjectExtendedLimitInformation, &lim, sizeof(lim));  // read back by the guard
    }
    e2e::e2e_adopt_or_invalid(job, pi, "ka-pc e2e child");  // resumed only inside a verified kill-on-close job
    started = true;
    ++children_started();
    started_children().push_back(this);
    reading = true;
    reader = std::thread([this] {
      DWORD err = 0;
      const auto end = e2e::e2e_read_to_end(
          out, [this] { return reading.load(); },
          [this](const char* p, size_t n) {
            std::lock_guard<std::mutex> lock(mu);
            text.append(p, n);
            DWORD put = 0;
            if (!sinkFailed.load() && (!WriteFile(sink, p, static_cast<DWORD>(n), &put, nullptr) || put != n)) {
              sinkErr = GetLastError();
              sinkFailed = true;
            }
          },
          &err);
      readErr = err;
      readResult = static_cast<int>(end);
    });
    return true;
  }
  // A test host: the shared guard's initial gate, then its monitor. Fails closed (INVALID, exit 2) before any client --
  // what was read so far is already in its log.
  void Guard(const char* where) {
    std::string why;
    if (!e2e::e2e_wait_injection_off([this] { return Text(); }, 20000, &why)) e2e::e2e_injection_setup_failed(where, why);
    monitor = std::make_unique<e2e::E2eInjectionMonitor>([this] { return Text(); }, job, where);
  }
  std::string Text() {
    std::lock_guard<std::mutex> lock(mu);
    return text;
  }
  bool WaitFor(const std::function<bool(const std::string&)>& pred, int ms) {
    const uint64_t until = GetTickCount64() + ms;
    while (GetTickCount64() < until) {
      if (pred(Text())) return true;
      Sleep(100);
    }
    return pred(Text());
  }
  bool Exited(int ms) { return pi.hProcess && WaitForSingleObject(pi.hProcess, ms) == WAIT_OBJECT_0; }

  // r3: the explicit crash step -- this test closes the child's job itself, recording what it saw before and after
  // (CrashClose). Refused for a host (its end is the guard's Finalize) and for a child not running or already
  // crash-closed. Returns whether the evidence is whole; End judges the same record.
  bool CrashCloseJob(DWORD rootEndMs, std::string* why) {
    if (!started || ended || crash.done || !job || monitor) {
      *why = monitor ? "a host's job is never crash-closed (its end is the guard's)" : "not a running child with its job";
      return false;
    }
    crash.done = true;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim{};
    crash.killOnClose = QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &lim, sizeof(lim), nullptr) &&
                        (lim.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE);
    BOOL in = FALSE;
    crash.member = IsProcessInJob(pi.hProcess, job, &in) && in;
    crash.singleRoot = job_holds_root_alone(&crash.consoleHosts);
    crash.rootAlive = WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT;
    // (the contrast seam is asked BEFORE the close: a refused close leaves the handle really open, never a dead value)
    crash.closed = start_step_ok(true, "crashclose") && CloseHandle(job) != 0;
    if (crash.closed) {
      job = nullptr;  // closed by this step: nothing later may use the value
    } else {
      crash.closeErr = GetLastError();  // kept: End still ends it through the job, but never counts that as the evidence
    }
    crash.rootEnded = crash.closed && WaitForSingleObject(pi.hProcess, rootEndMs) == WAIT_OBJECT_0;
    *why = crash_close_why(crash);
    return why->empty();
  }

  void End() {
    if (!started || ended) return;  // never started (NOT RUN), or already ended and judged
    ended = true;
    // The producer's end, with what it rests on. r3: a crash-closed child is judged on its own record ONLY (a job left
    // open by a failed close is still ended here, but that does not count); a job that is just gone is not an end.
    bool producerEnded = false;
    if (crash.done) {
      if (job) (void)e2e::e2e_end_job_confirmed(job);  // cleanup only
      const std::string cw = crash_close_why(crash);
      producerEnded = cw.empty();
      endEvidence = producerEnded ? "crash-close by this test, single root: kill-on-close read back, the root in the "
                                    "job, its listed members the root + " + std::to_string(crash.consoleHosts) +
                                    " console host(s) only before the close, CloseHandle succeeded, the root's handle "
                                    "signalled after it"
                                  : "crash-close record incomplete: " + cw;
    } else if (job) {
      producerEnded = e2e::e2e_end_job_confirmed(job);  // every process of its job, confirmed
      endEvidence = producerEnded ? "job terminated, its active process count read 0"
                                  : "job terminated, its active process count NOT read 0";
    } else {
      endEvidence = "no job and no crash-close record";
    }
    // its reader: EOF comes once every writer is gone; a read still blocked after that is cancelled (bounded)
    for (int i = 0; i < 100 && readResult.load() == 0; ++i) Sleep(50);
    if (readResult.load() == 0) {
      reading = false;  // a cut read: the output is NOT complete (judged so below)
      for (int i = 0; i < 100 && readResult.load() == 0; ++i) {
        CancelSynchronousIo(reader.native_handle());
        Sleep(20);
      }
    }
    if (readResult.load() == 0) {
      // never a hang, never a pass: the reader is still inside a read of this child's pipe
      WriteTrailer(producerEnded);
      std::printf("FAIL  child %s: its output reader could not be ended -- the run stops\nRESULT: INVALID\n", Name().c_str());
      std::fflush(stdout);
      std::_Exit(2);
    }
    reader.join();
    const auto readEnd = static_cast<e2e::E2eReadEnd>(readResult.load());
    // N2: the evidence first -- a host's Finalize may end the test (INVALID) right after
    const bool trailerKept = WriteTrailer(producerEnded);
    if (monitor) {
      monitor->Finalize(producerEnded, e2e::e2e_final_pipe(readEnd, readErr.load(), Text()));
      monitor.reset();
    }
    std::string why = child_end_why(producerEnded, readEnd, readErr.load(), sinkFailed.load(), sinkErr.load());
    if (why.empty() && !trailerKept) why = "its end record (" + Name() + ".end) could not be written";
    if (!why.empty()) child_fail(Name() + ": " + why);
    if (out) CloseHandle(out), out = nullptr;
    if (sink != INVALID_HANDLE_VALUE) CloseHandle(sink), sink = INVALID_HANDLE_VALUE;
    if (job) CloseHandle(job), job = nullptr;
    if (pi.hThread) CloseHandle(pi.hThread), pi.hThread = nullptr;
    if (pi.hProcess) CloseHandle(pi.hProcess), pi.hProcess = nullptr;
  }
  ~Child() {
    End();
    auto& v = started_children();
    v.erase(std::remove(v.begin(), v.end(), this), v.end());
  }

 private:
  // r3: the job's members, listed (JobObjectBasicProcessIdList): the root is among them, and every other one is the
  // OS console host Windows attaches to a console child (%SystemRoot%\System32\conhost.exe, even with
  // CREATE_NO_WINDOW). Any other process -- a descendant of the root -- refuses. *consoleHosts: how many.
  bool job_holds_root_alone(int* consoleHosts) {
    *consoleHosts = 0;
    struct List {
      JOBOBJECT_BASIC_PROCESS_ID_LIST head;
      ULONG_PTR more[63];
    } list{};
    if (!QueryInformationJobObject(job, JobObjectBasicProcessIdList, &list, sizeof(list), nullptr)) return false;
    if (list.head.NumberOfAssignedProcesses != list.head.NumberOfProcessIdsInList) return false;  // not all listed
    wchar_t sys[MAX_PATH] = {};
    const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    const std::wstring conhost = std::wstring(sys, n) + L"\\conhost.exe";
    bool rootSeen = false;
    for (DWORD i = 0; i < list.head.NumberOfProcessIdsInList; ++i) {
      const DWORD pid = static_cast<DWORD>(list.head.ProcessIdList[i]);
      if (pid == pi.dwProcessId) {
        rootSeen = true;
        continue;
      }
      HANDLE q = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
      if (!q) return false;  // unnamed: refused
      wchar_t image[MAX_PATH] = {};
      DWORD len = MAX_PATH;
      const bool named = QueryFullProcessImageNameW(q, 0, image, &len) != 0;
      CloseHandle(q);
      if (!named || !same_path(std::wstring(image, len), conhost)) return false;  // a descendant of the root
      ++*consoleHosts;
    }
    return rootSeen;
  }

  bool WriteTrailer(bool producerEnded) {
    const auto readEnd = static_cast<e2e::E2eReadEnd>(readResult.load());
    size_t bytes = 0;
    {
      std::lock_guard<std::mutex> lock(mu);
      bytes = text.size();
    }
    const std::string t = std::string("producer end: ") + (producerEnded ? "confirmed" : "NOT confirmed") +
                          "\nproducer end evidence: " + endEvidence +
                          "\nread: " + (readResult.load() == 0 ? "still running" : e2e::e2e_read_end_name(readEnd)) +
                          " (error " + std::to_string(readErr.load()) + ") after " + std::to_string(bytes) + " bytes" +
                          "\nlog: " + (sinkFailed.load() ? "NOT kept whole (error " + std::to_string(sinkErr.load()) + ")"
                                                         : std::string("kept")) +
                          "\n";
    HANDLE h = CreateFileW((logPath + L".end").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const bool ok = WriteFile(h, t.data(), static_cast<DWORD>(t.size()), &put, nullptr) && put == t.size();
    CloseHandle(h);
    return ok;
  }
};

// Every child still running ended and judged (N2) -- before a test prints its result. Returns the failures counted by
// children so far (including those ended earlier).
inline int children_end_all() {
  const std::vector<Child*> live = started_children();
  for (Child* c : live) c->End();
  std::printf("%s  every child this test started (%d): its end confirmed, its output read to EOF and kept in its own log"
              "%s\n",
              child_failures() ? "FAIL" : "PASS", children_started(),
              child_failures() ? (" -- " + std::to_string(child_failures()) + " not (see FAIL child above)").c_str() : "");
  std::fflush(stdout);
  return child_failures();
}

}  // namespace ka_pc_e2e
