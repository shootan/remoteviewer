// The host's OS input effects in one place (2026-10-28, Codex codex_test_injection_finding.md Finding-2): every
// SendInput / input PostMessage / SetCursorPos of the streaming host goes through these. In the product they ARE the
// Win32 calls, nothing else. In a test build compiled with REMOTE60_OS_INPUT_RECORD_ONLY (its own target, never
// packaged; automation/gnlink_check_fixed_server.py refuses a shipped GNLinkStream.exe carrying "os-input-record")
// each call is RECORDED ("[os-input-record] ..." on the host's stdout) and has no effect at all: no input reaches any
// desktop, no window gets a message, the cursor does not move. Everything before them -- receipt, authentication,
// peer / epoch, the prelogin and lock policies, routing, target choice -- is the product's code, unchanged.
#ifndef REMOTE60_NATIVE_POC_OS_INPUT_EFFECTS_HPP
#define REMOTE60_NATIVE_POC_OS_INPUT_EFFECTS_HPP

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace remote60::native_poc::os_input {

#if defined(REMOTE60_OS_INPUT_RECORD_ONLY)
inline constexpr bool kRecordOnly = true;
#else
inline constexpr bool kRecordOnly = false;
#endif

#if defined(REMOTE60_OS_INPUT_RECORD_ONLY)
// One line per call, written whole (the host's stdout is shared by its threads).
// How many effects were recorded, by API prefix -- for the record-only tests (never in a product build).
inline std::mutex& record_mu() {
  static std::mutex mu;
  return mu;
}
inline std::vector<std::string>& recorded() {
  static std::vector<std::string> v;
  return v;
}
inline void record(const std::string& line) {
  std::lock_guard<std::mutex> lock(record_mu());
  recorded().push_back(line);
  std::fputs(("[os-input-record] " + line + "\n").c_str(), stdout);
  std::fflush(stdout);
}
// A Unicode run is recorded by its length and an FNV-1a of its UTF-16 units -- never the characters themselves
// (a test compares the hash of its own test string; nothing typed is ever printed).
inline uint32_t fnv1a(const uint16_t* u, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; ++i) {
    h = (h ^ (u[i] & 0xFF)) * 16777619u;
    h = (h ^ (u[i] >> 8)) * 16777619u;
  }
  return h;
}
inline std::string describe(const INPUT* in, UINT n) {
  std::string out;
  std::vector<uint16_t> units;
  for (UINT i = 0; i < n; ++i) {
    const INPUT& e = in[i];
    if (e.type == INPUT_KEYBOARD && (e.ki.dwFlags & KEYEVENTF_UNICODE)) {
      if (!(e.ki.dwFlags & KEYEVENTF_KEYUP)) units.push_back(static_cast<uint16_t>(e.ki.wScan));
      continue;
    }
    char b[96];
    if (e.type == INPUT_KEYBOARD) {
      std::snprintf(b, sizeof(b), " key vk=%u %s", static_cast<unsigned>(e.ki.wVk),
                    (e.ki.dwFlags & KEYEVENTF_KEYUP) ? "up" : "down");
    } else if (e.type == INPUT_MOUSE) {
      std::snprintf(b, sizeof(b), " mouse flags=0x%lx dx=%ld dy=%ld data=%ld", static_cast<unsigned long>(e.mi.dwFlags),
                    static_cast<long>(e.mi.dx), static_cast<long>(e.mi.dy), static_cast<long>(e.mi.mouseData));
    } else {
      std::snprintf(b, sizeof(b), " type=%lu", static_cast<unsigned long>(e.type));
    }
    out += b;
  }
  if (!units.empty()) {
    char b[64];
    std::snprintf(b, sizeof(b), " unicode units=%zu fnv=%08x", units.size(), fnv1a(units.data(), units.size()));
    out += b;
  }
  return out;
}
#endif

// SendInput: recorded, none sent (record-only) -- reported as all of them sent, as the call would.
inline UINT send_input(UINT n, LPINPUT in, int cb) {
#if defined(REMOTE60_OS_INPUT_RECORD_ONLY)
  (void)cb;
  record("api=SendInput n=" + std::to_string(n) + describe(in, n));
  return n;
#else
  return SendInput(n, in, cb);
#endif
}

// PostMessage carrying input (mouse / key / char / the display-wake broadcast): recorded, none posted (record-only).
inline BOOL post_message(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
#if defined(REMOTE60_OS_INPUT_RECORD_ONLY)
  char b[128];
  std::snprintf(b, sizeof(b), "api=PostMessage hwnd=%p msg=0x%x wp=0x%llx lp=0x%llx", static_cast<void*>(h), msg,
                static_cast<unsigned long long>(wp), static_cast<unsigned long long>(lp));
  record(b);
  return TRUE;
#else
  return PostMessageW(h, msg, wp, lp);
#endif
}

// SetCursorPos: recorded, the cursor never moves (record-only).
inline BOOL set_cursor_pos(int x, int y) {
#if defined(REMOTE60_OS_INPUT_RECORD_ONLY)
  record("api=SetCursorPos x=" + std::to_string(x) + " y=" + std::to_string(y));
  return TRUE;
#else
  return SetCursorPos(x, y);
#endif
}

// SetThreadExecutionState (HostPowerKeepalive's only power API; Finding-3): recorded, the system's idle timers never
// touched (record-only) -- reported as the call would (a non-zero previous state).
inline EXECUTION_STATE set_thread_execution_state(EXECUTION_STATE flags) {
#if defined(REMOTE60_OS_INPUT_RECORD_ONLY)
  char b[64];
  std::snprintf(b, sizeof(b), "api=SetThreadExecutionState flags=0x%lx", static_cast<unsigned long>(flags));
  record(b);
  return ES_CONTINUOUS;
#else
  return SetThreadExecutionState(flags);
#endif
}

// The secure-desktop broker (a SYSTEM service the host hands input to): in record-only no service is installed,
// connected or written to -- every hand-off is recorded and refused.
inline bool broker_refused(const char* what) {
#if defined(REMOTE60_OS_INPUT_RECORD_ONLY)
  record(std::string("api=SecureInputBroker refused=") + what);
  return true;
#else
  (void)what;
  return false;
#endif
}

}  // namespace remote60::native_poc::os_input

#endif
