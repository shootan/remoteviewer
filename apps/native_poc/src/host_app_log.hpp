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

namespace hostlog_detail {
inline char lower_ascii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
}  // namespace hostlog_detail

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

// hostapp-log-upload r2/r3 (H3): make an arbitrary child-output line safe to embed as the one-line
// "preceding state" field. This field is NOT a trusted summary -- it is raw child stdout -- so it is
// bounded in three ordered steps (Codex H3, order kept): (1) REDACT -- if the line contains ANY
// credential marker (token/password/secret/authorization/bearer/cookie/session/api_key/credential,
// case-insensitive), the WHOLE line is replaced. r2's token-wise redactor left space-separated,
// quoted and JSON values behind ("token= V", `password: V`, `password="a b"`, `{"password":"V"}`),
// so r3 drops it for whole-line redaction (Codex R1); (2) ESCAPE so a non-secret line stays single
// (backslash, quote, CR, LF, TAB, other control bytes); (3) UTF-8-SAFE TRUNCATE to a byte budget
// without cutting a multibyte sequence. Empty/blank -> "none". A safety boundary on a best-effort
// diagnostic, not a claim that child output is secret-free (the child's own lines were already
// uploaded verbatim on the same stream before this).
inline bool hostlog_line_has_credential_marker(const std::string& rawLower) {
  // "auth" (r2 had it, r3 dropped it) catches auth=V / x-auth: V that "authorization" misses; it also
  // subsumes "authorization". Over-matching benign "author"-like words is accepted (Codex: do not
  // loosen the filter). Keep "authorization" listed for readability.
  for (const char* m : {"token", "password", "passwd", "secret", "auth", "authorization", "bearer",
                        "cookie", "session", "apikey", "api_key", "credential"}) {
    if (rawLower.find(m) != std::string::npos) return true;
  }
  return false;
}

inline std::string sanitize_preceding_line(const std::string& raw, size_t maxBytes = 160) {
  // blank -> none
  bool blank = true;
  for (unsigned char c : raw) if (c > ' ') { blank = false; break; }
  if (blank) return "none";
  // (1) redact: whole line if any credential marker appears (checked on the ORIGINAL bytes, before
  // truncation, so a marker past the byte budget still triggers).
  std::string lower;
  lower.reserve(raw.size());
  for (char c : raw) lower += hostlog_detail::lower_ascii(c);
  if (hostlog_line_has_credential_marker(lower)) return "<redacted: credential marker>";
  // (2) escape a non-secret line to one line.
  std::string esc;
  esc.reserve(raw.size() + 8);
  for (unsigned char c : raw) {
    switch (c) {
      case '\\': esc += "\\\\"; break;
      case '"': esc += "\\\""; break;
      case '\r': esc += "\\r"; break;
      case '\n': esc += "\\n"; break;
      case '\t': esc += "\\t"; break;
      default:
        if (c < 0x20 || c == 0x7F) esc += '?';
        else esc += static_cast<char>(c);
    }
  }
  // (3) UTF-8-safe truncate to maxBytes (do not split a multibyte sequence).
  if (esc.size() > maxBytes) {
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(esc[cut]) & 0xC0) == 0x80) --cut;  // back off continuation bytes
    esc.resize(cut);
    esc += "...(cut)";
  }
  return esc;
}

// hostapp-log-upload r2 (H2): the ONE exit-line formatter every exit path shares, so the required
// fields can never drift between the watchdog / short-crash / clean branches. Built with std::string
// (no fixed buffer), required fields FIRST, optional/long fields (dump path, preceding) LAST -- so a
// long dump path can never truncate code/pid/ranMs/restart_planned. `kind` is the leading description
// (e.g. "the streaming host exited"). `codeKnown` distinguishes a real GetExitCodeProcess result from a
// failed query (then queryError is shown and no code is invented). The code is a general Win32 exit
// code (hex + unsigned decimal); exception/NTSTATUS values are preserved as-is but not named NTSTATUS.
// `extra` is the per-path middle text (streak / recoveries5m / ABNORMAL note), already formatted.
// `restartPlanned` is the supervisor's intent AT THIS MOMENT (running_ still set) -- the actual next
// launch is a separate "started pid=.. attempt=.." line, so a Stop racing the exit is not mislabelled
// as a relaunch. dumpNote is "" or " dump=<path>" (path only). preceding must already be sanitized.
struct HostExitLog {
  const char* kind = "the streaming host exited";
  bool codeKnown = true;
  unsigned long code = 0;
  unsigned long queryError = 0;
  unsigned int pid = 0;
  unsigned long long ranMs = 0;
  std::string extra;      // e.g. "streak=2" / "recoveries5m=3" / "(ABNORMAL: nonzero after a long run)"
  bool restartPlanned = false;
  std::string dumpNote;   // "" or " dump=<path>"
  std::string preceding;  // already sanitized (sanitize_preceding_line)
};

inline std::string format_host_exit_log(const HostExitLog& f) {
  std::string s = "[host-app] ";
  s += f.kind;
  char codeBuf[96];
  if (f.codeKnown) {
    std::snprintf(codeBuf, sizeof(codeBuf), " code=0x%08lX (%lu)", f.code, f.code);
  } else {
    std::snprintf(codeBuf, sizeof(codeBuf), " code=unknown queryError=%lu", f.queryError);
  }
  s += codeBuf;
  char mid[96];
  std::snprintf(mid, sizeof(mid), " pid=%u ranMs=%llu", f.pid, f.ranMs);
  s += mid;
  if (!f.extra.empty()) { s += ' '; s += f.extra; }
  s += f.restartPlanned ? " restart_planned=true" : " restart_planned=false";
  s += f.dumpNote;  // "" or " dump=<path>"
  s += " lastLine=\"";
  s += f.preceding;  // sanitized
  s += "\"";
  return s;
}

}  // namespace remote60::native_poc
