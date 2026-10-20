#pragma once

// prelogin-field D3 (Codex codex_prelogin_field_d3.md): the pre-login worker's own persistent log.
//
// The worker (GNLinkStream --prelogin, SYSTEM, console session) had no stdout anywhere: the 0.2.160 field run
// died with exit 49 after each viewer connect and nothing said why. The service cannot hand it a log handle
// (a handle is not inherited across sessions -- CreateProcessAsUser; the launch keeps bInheritHandles=FALSE and
// suspended -> job -> resume). So the worker opens the log itself, first thing in main(), and takes its own
// stdout/stderr -- std::cout/cerr and the CRT's printf/fprintf alike -- through a pipe into it.
//
// Where: <ProgramData>\GNLink\logs\prelogin_worker.log, the one before it as .log.1 (ProgramData from the known
// folder, not the environment). How it is trusted -- the G5 rule of the machine record (prelogin_store_chain):
//   * the folders are opened without following reparse points, checked on those handles (ProgramData: its
//     identity and owner; GNLink: nobody else may delete/rename/re-permission it; logs: SYSTEM and
//     Administrators only) and HELD, without delete sharing, for the whole life of the log;
//   * the files: created CREATE_NEW with a protected SYSTEM+Administrators descriptor, or -- when they exist --
//     opened without following a reparse point and checked on that handle (a regular file, trusted owner, a
//     DACL nobody else is in, one hard link) BEFORE anything is written, truncated or renamed. The current file
//     is held without delete sharing; the rotation renames it through that handle and deletes .1 only after
//     .1 itself passed the same check. Anything that fails: the log is refused -- never another path.
//   * size: at most kFileCap per file, so 2 x kFileCap in all, held while the worker runs (a line that would
//     pass the cap rotates first; one line is at most kLineCap). A write that fails is counted and dropped:
//     the pipe is always drained, nothing waits on the disk.
// What it never writes: the command line, the environment, or anything the worker does not print itself (the
// worker prints no secret -- prelogin_worker_log_test and the login e2e look for the test's).
// In the shipped GNLinkStream the sink exists only for a process running as LocalSystem; the test build
// (REMOTE60_STREAM_TEST_SEAM) puts it under GNLINK_STREAM_TEST_LOG_ROOT with the test's own owner rule.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "prelogin_store_chain.hpp"

namespace remote60::native_poc::prelogin {

inline constexpr uint64_t kWorkerLogFileCap = 2ull * 1024 * 1024;
inline constexpr size_t kWorkerLogLineCap = 4096;

// <ProgramData>\GNLink\logs\prelogin_worker.log; empty when ProgramData cannot be resolved.
std::wstring ProductWorkerLogPath();

// The verified, size-bounded, rotating file (one writer). Not thread-safe by itself: the sink serializes.
class WorkerLogFile {
 public:
  WorkerLogFile() = default;
  WorkerLogFile(const WorkerLogFile&) = delete;
  WorkerLogFile& operator=(const WorkerLogFile&) = delete;
  ~WorkerLogFile();

  // Holds the folder chain (creating GNLink\logs as SYSTEM+Administrators only when missing) and opens or
  // creates the current file. 0, or the win32 error; `failedAt` names what failed.
  // `fileSddl`: the descriptor a new file gets -- the product's SYSTEM+Administrators-only when null (a test that
  // cannot run as SYSTEM passes one that also names itself, and an owner rule that trusts itself).
  DWORD Open(const std::wstring& path, const std::function<bool(PSID)>& ownerTrusted, uint64_t fileCap = kWorkerLogFileCap,
             size_t lineCap = kWorkerLogLineCap, const wchar_t* fileSddl = nullptr);
  // One line (a '\n' is added when missing; longer than the line cap: cut, marked). False when it was dropped.
  bool WriteLine(const std::string& line);
  void Flush();
  void Close();
  bool open() const { return file_ != INVALID_HANDLE_VALUE; }
  uint64_t dropped() const { return dropped_; }
  std::wstring failedAt;

 private:
  DWORD OpenCurrent();
  bool Rotate();
  std::wstring path_;
  std::function<bool(PSID)> ownerTrusted_;
  std::wstring fileSddl_;
  uint64_t fileCap_ = kWorkerLogFileCap;
  size_t lineCap_ = kWorkerLogLineCap;
  HeldChain chain_;
  HANDLE file_ = INVALID_HANDLE_VALUE;
  uint64_t size_ = 0;
  uint64_t dropped_ = 0;
  bool broken_ = false;  // a rotation that could not be made safely: nothing more is written
};

// stdout/stderr of this process -> a pipe -> a collector thread -> WorkerLogFile.
class WorkerLogSink {
 public:
  // Opens the file, writes `header`, then points fd 1 / fd 2, the process's std handles and std::cout/cerr
  // (through the CRT) at the pipe. A process started WITH a stdout (a test reading the worker) keeps getting
  // every line there as well; the service's worker has none. 0, or the win32 error (nothing redirected then).
  DWORD Install(const std::wstring& path, const std::function<bool(PSID)>& ownerTrusted, const std::string& header,
                const wchar_t* fileSddl = nullptr);
  // Everything printed so far reaches the disk, or `timeoutMs` passes. Safe from any thread, any number of times.
  bool Drain(uint32_t timeoutMs);
  bool installed() const { return installed_.load(); }
  std::wstring failedAt() const { return file_.failedAt; }

 private:
  void Collect();
  WorkerLogFile file_;
  std::mutex fileMu_;
  HANDLE readEnd_ = nullptr;
  HANDLE forward_ = nullptr;  // the stdout this process was started with, when it had one (tests): lines go on to it too
  HANDLE writeEnd_ = nullptr;
  std::thread collector_;
  std::atomic<bool> installed_{false};
  std::atomic<uint64_t> drainAsked_{0};
  std::atomic<uint64_t> drainDone_{0};
  HANDLE drained_ = nullptr;  // auto-reset; set when a drain marker went through
};

// The worker's one sink (main() installs it; the immediate-exit paths drain it before TerminateProcess).
WorkerLogSink& TheWorkerLogSink();
// Drain the worker's sink if installed (no-op otherwise). For the paths that end the process at once.
void DrainWorkerLogBeforeExit();

// True when this process runs as LocalSystem.
bool ProcessIsLocalSystem();

}  // namespace remote60::native_poc::prelogin
