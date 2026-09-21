#pragma once

// The one writer for host_app.log. (updater-health-gate D1, items 1 and 3)
//
// Role:    open, append, flush and rotate host_app.log, serialised, with failures reported.
// Thread:  any. One mutex covers every operation, including rotation.
// Input:   a line, stamped or verbatim.
// Output:  whether it reached the file, and counters for what did not.
// Callers: append_host_app_log and the supervisor's child-output reader -- which used to be two
//          independent writers on the same file.
//
// There were two, and they could not both write. The child-output reader held
// _wfsopen(path, "ab", _SH_DENYNO) for the whole life of the streaming child; append_host_app_log
// opened _wfopen_s(path, "a") per line, and the CRT's fopen family asks for no sharing, so it opens
// deny-write and cannot open a file another handle already holds open for writing. Measured, not
// inferred (remote60_host_log_writers_probe): with the reader's handle held, every append open
// failed, and the same append through _wfsopen with _SH_DENYNO succeeded.
//
// That mattered beyond the missing lines. The updater's health gate reads this file for
// "[host-app] health version=X directory=ok", and that line is written by append_host_app_log
// while a child is running -- so it could not reach the file in the situation the gate exists for,
// and the gate rolled a good update back (2026-09-21, and the same shape earlier).
//
// Sharing alone would not have been enough. Two handles in append mode both seek to the end and
// then write, which is two steps: they can choose the same offset and overwrite each other. The
// same probe measured that too. So this is one handle, not two that share politely.

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

namespace remote60::native_poc {

struct HostAppLogStats {
  uint64_t written = 0;      // lines that reached the file
  uint64_t failed = 0;       // lines that did not -- never silent again
  uint64_t rotations = 0;
  uint64_t rotationsRefused = 0;  // the rename could not happen; the file runs past the cap
};

class HostAppLog {
 public:
  /** `path` empty means every write fails, which is what an unusable %LOCALAPPDATA% amounts to. */
  explicit HostAppLog(std::wstring path, uint64_t rotateAtBytes = 2ULL * 1024 * 1024,
                      int maxBackups = 10);
  ~HostAppLog();

  HostAppLog(const HostAppLog&) = delete;
  HostAppLog& operator=(const HostAppLog&) = delete;

  /** One line with the supervisor's stamp: "09-21 15:11:53 <line>". */
  bool WriteStamped(const std::string& line);

  /** One line exactly as given -- the child stamps its own output. */
  bool WriteRaw(const std::string& line);

  /** Closes the handle. The next write reopens; used so a test can inspect the bytes. */
  void Close();

  HostAppLogStats stats() const;

 private:
  bool WriteLocked(const std::string& text);
  void RotateLocked();

  mutable std::mutex mu_;
  const std::wstring path_;
  const uint64_t rotateAtBytes_;
  const int maxBackups_;
  std::FILE* file_ = nullptr;
  HostAppLogStats stats_;
};

/** The process-wide log at %LOCALAPPDATA%\GNLink\host_app.log. */
HostAppLog& host_app_log();

}  // namespace remote60::native_poc
