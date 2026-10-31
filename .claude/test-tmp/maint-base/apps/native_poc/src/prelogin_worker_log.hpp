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
#include <condition_variable>
#include <cstdint>
#include <deque>
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

// Bounds of the queue between the pipe reader and the disk writer.
inline constexpr size_t kWorkerLogQueueLines = 4096;
inline constexpr size_t kWorkerLogQueueBytes = 1024 * 1024;

// stdout/stderr of this process -> a pipe -> a READER thread -> a bounded queue -> a WRITER thread -> WorkerLogFile
// (and the original stdout, when there was one).
//
// prelogin-field r3 (Codex codex_prelogin_field_r2.md R2-1): the reader does nothing but empty the pipe into the
// queue; when the queue is full a line is dropped and counted (said in the log once there is room). So a disk or a
// forward that stops answering stops the WRITER only -- the pipe keeps emptying and whatever prints goes on. If the
// reader itself ends, it closes the pipe's read end: a later print fails at once instead of waiting.
// Drain's deadline starts when it is called; nothing in it waits on the disk except the bounded wait at its end.
// Install (R2-2) is all or nothing: on any failure, or exception, every fd, std handle, thread, pipe, event and
// file it touched is put back or released, and the caller's stdout/stderr are what they were (a stream that had no
// handle at all stays on NUL).
class WorkerLogSink {
 public:
  // 0, or the win32 error -- and then nothing is redirected. Never throws. A process started WITH a stdout (a test
  // reading the worker) keeps getting every line there as well; the service's worker has none.
  DWORD Install(const std::wstring& path, const std::function<bool(PSID)>& ownerTrusted, const std::string& header,
                const wchar_t* fileSddl = nullptr) noexcept;
  // Everything printed before the call is on disk -> true; `timeoutMs` (counted from the call) passed, the writer is
  // stuck or gone -> false. Never waits longer. Safe from any thread, any number of times.
  bool Drain(uint32_t timeoutMs) noexcept;
  bool installed() const { return installed_.load(); }
  std::wstring failedAt() const { return failedAt_; }
  uint64_t droppedLines() const { return queueDropped_.load(); }

  // TEST SEAMS (null in the product; prelogin_worker_log_test sets them): where Install fails, the writer's stall
  // before each item, the reader's stop.
  enum class FailAt { None, Event, StdoutRedirected /* the stderr step, after stdout already went to the pipe */, Thread };
  static FailAt& InstallFailForTest();
  static std::function<void()>& WriterStallForTest();
  static std::atomic<bool>& ReaderStopForTest();

 private:
  struct Item {
    std::string line;
    uint64_t drainId = 0;  // != 0: a drain marker, not a line
  };
  void ReadLoop();
  void WriteLoop();
  bool Enqueue(Item&& item, bool force);
  void Rollback() noexcept;
  WorkerLogFile file_;
  std::wstring failedAt_;
  HANDLE readEnd_ = nullptr;
  HANDLE writeEnd_ = nullptr;
  HANDLE forward_ = nullptr;  // the stdout this process was started with, when it had one (tests): lines go on to it too
  HANDLE drained_ = nullptr;  // manual-reset; pulsed when a drain marker went through
  // what Install changed, to put back
  int savedFd_[2] = {-1, -1};
  HANDLE savedStd_[2] = {nullptr, nullptr};
  bool redirected_[2] = {false, false};
  std::thread reader_, writer_;
  std::mutex qMu_;
  std::condition_variable qCv_;
  std::deque<Item> queue_;
  size_t queueBytes_ = 0;
  bool stopping_ = false;
  std::atomic<uint64_t> queueDropped_{0};
  uint64_t reportedDropped_ = 0;
  std::atomic<bool> readerAlive_{false};
  std::atomic<bool> writerAlive_{false};
  std::atomic<bool> installed_{false};
  std::atomic<uint64_t> drainAsked_{0};
  std::atomic<uint64_t> drainDone_{0};
};

// The worker's one sink (main() installs it; the immediate-exit paths drain it before TerminateProcess).
WorkerLogSink& TheWorkerLogSink();
// Drain the worker's sink if installed (no-op otherwise). For the paths that end the process at once.
void DrainWorkerLogBeforeExit();

// True when this process runs as LocalSystem.
bool ProcessIsLocalSystem();

}  // namespace remote60::native_poc::prelogin
