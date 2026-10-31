#pragma once

// Host process-level logging and power/display keepalive helpers.
//
// Role:    (1) TimestampPrefixBuf -- streambuf that stamps every std::cout/std::cerr line with
//              wall-clock time; (2) HostPowerKeepalive -- SetThreadExecutionState wrapper that keeps
//              the machine awake while the host runs and the display on while streaming;
//              (3) wake_display_for_remote_session -- one-shot display wake on the stream edge.
// Thread:  TimestampPrefixBuf is called from every logging thread (per-thread line buffer + one
//          locked write). HostPowerKeepalive is main-thread only (SetThreadExecutionState is
//          per-thread state and must be applied from the thread that owns it).
// Input:   destination streambuf / streaming on-off edges.
// Output:  timestamped log lines on the wrapped stream / OS execution-state flags.
// Callers: native_video_host_main.cpp (main() prologue installs the buffers and owns the keepalive).
//
// Extracted verbatim from native_video_host_main.cpp (host split refactor Phase 0-8). Header-only
// so no new translation unit is added and behavior is byte-identical.

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <streambuf>
#include <string>

namespace remote60::native_poc {

// Every host log line is prefixed with a wall-clock timestamp so a capture can be lined up against
// the client's own timestamped log (which uses the same MM-DD HH:MM:SS form). Rather than touch the
// hundreds of std::cout/std::cerr sites, this filtering streambuf is slipped under both streams: it
// buffers each line per-thread and, on the terminating newline, emits "timestamp + line" as one
// locked write so concurrent log threads can never split a line or interleave a stamp mid-line.
class TimestampPrefixBuf : public std::streambuf {
 public:
  explicit TimestampPrefixBuf(std::streambuf* dest) : dest_(dest) {}

 protected:
  int_type overflow(int_type ch) override {
    if (traits_type::eq_int_type(ch, traits_type::eof())) return traits_type::not_eof(ch);
    const char c = traits_type::to_char_type(ch);
    std::string& line = tls_line();
    line.push_back(c);
    if (c == '\n') flush_line(line);
    return ch;
  }
  std::streamsize xsputn(const char* s, std::streamsize n) override {
    std::string& line = tls_line();
    for (std::streamsize i = 0; i < n; ++i) {
      line.push_back(s[i]);
      if (s[i] == '\n') flush_line(line);
    }
    return n;
  }
  int sync() override {
    std::lock_guard<std::mutex> lk(mu_);
    return dest_->pubsync();
  }

 private:
  static std::string& tls_line() {
    static thread_local std::string line;
    return line;
  }
  static std::string timestamp_now() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d-%02d %02d:%02d:%02d.%03d ", tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));
    return std::string(buf);
  }
  void flush_line(std::string& line) {
    const std::string ts = timestamp_now();
    std::lock_guard<std::mutex> lk(mu_);
    dest_->sputn(ts.data(), static_cast<std::streamsize>(ts.size()));
    dest_->sputn(line.data(), static_cast<std::streamsize>(line.size()));
    line.clear();
  }

  std::streambuf* dest_;
  std::mutex mu_;
};

inline void wake_display_for_remote_session() {
  // ES_DISPLAY_REQUIRED resets the idle timer, but a monitor that has already powered down
  // is not guaranteed to light immediately on every display driver. Mirror a real local
  // wake without leaving the pointer displaced: the paired relative moves cancel out.
  INPUT wake[2]{};
  wake[0].type = INPUT_MOUSE;
  wake[0].mi.dx = 1;
  wake[0].mi.dwFlags = MOUSEEVENTF_MOVE;
  wake[1].type = INPUT_MOUSE;
  wake[1].mi.dx = -1;
  wake[1].mi.dwFlags = MOUSEEVENTF_MOVE;
  (void)SendInput(2, wake, sizeof(INPUT));
  (void)PostMessageW(HWND_BROADCAST, WM_SYSCOMMAND,
                     static_cast<WPARAM>(SC_MONITORPOWER), static_cast<LPARAM>(-1));
}

class HostPowerKeepalive {
 public:
  HostPowerKeepalive() {
    Apply(false);
  }

  ~HostPowerKeepalive() {
    (void)SetThreadExecutionState(ES_CONTINUOUS);
  }

  void SetStreaming(bool streaming, bool wakeDisplay = false) {
    // Only a real not-streaming -> streaming edge may wake the display. The wake injects
    // actual mouse motion, and the previous condition re-ran it for any call that passed
    // wakeDisplay while already streaming -- including the capture-fallback retry loops,
    // which re-arm themselves every 100ms and so jittered the cursor continuously.
    const bool startedStreaming = streaming && !streaming_;
    if (streaming_ == streaming) return;
    streaming_ = streaming;
    Apply(streaming);
    if (startedStreaming && wakeDisplay) wake_display_for_remote_session();
  }

 private:
  static void Apply(bool streaming) {
    EXECUTION_STATE flags = ES_CONTINUOUS | ES_SYSTEM_REQUIRED;
    if (streaming) flags = static_cast<EXECUTION_STATE>(flags | ES_DISPLAY_REQUIRED);
    (void)SetThreadExecutionState(flags);
  }

  bool streaming_ = false;
};

}  // namespace remote60::native_poc
