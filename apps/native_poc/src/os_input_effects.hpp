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
#include <imm.h>

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
// The dispatch this effect belongs to (r5, Codex T1): the product's dispatch sets it around one input / text / release
// (DispatchContext below) -- every effect recorded meanwhile carries "ctx what=.. seq=.. epoch=.. frag=..", in
// order; an effect with no dispatch behind it (the display wake, an IME query) carries "ctx none".
struct DispatchState {
  bool active = false;
  const char* what = "";
  uint64_t seq = 0;
  uint64_t epoch = 0;
  int frag = 0;
};
inline DispatchState& dispatch_state() {
  thread_local DispatchState st;
  return st;
}
inline std::string context_prefix() {
  DispatchState& st = dispatch_state();
  if (!st.active) return "ctx none ";
  char b[96];
  std::snprintf(b, sizeof(b), "ctx what=%s seq=%llu epoch=%llu frag=%d ", st.what,
                static_cast<unsigned long long>(st.seq), static_cast<unsigned long long>(st.epoch), st.frag++);
  return b;
}
inline void record(const std::string& raw) {
  const std::string line = context_prefix() + raw;
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

// The dispatch around one product input (record-only: tags the effects it causes; product build: nothing at all).
class DispatchContext {
 public:
  DispatchContext(const char* what, uint64_t seq, uint64_t epoch) {
#if defined(REMOTE60_OS_INPUT_RECORD_ONLY)
    DispatchState& st = dispatch_state();
    prev_ = st;
    st = DispatchState{true, what, seq, epoch, 0};
#else
    (void)what;
    (void)seq;
    (void)epoch;
#endif
  }
  ~DispatchContext() {
#if defined(REMOTE60_OS_INPUT_RECORD_ONLY)
    dispatch_state() = prev_;
#endif
  }
  DispatchContext(const DispatchContext&) = delete;
  DispatchContext& operator=(const DispatchContext&) = delete;

 private:
#if defined(REMOTE60_OS_INPUT_RECORD_ONLY)
  DispatchState prev_;
#endif
};

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

// AttachThreadInput (r5, Codex N1-2: it joins two threads' input state -- not a query): recorded, not joined
// (record-only) -- reported as joined, so the product's matching detach is recorded too.
inline BOOL attach_thread_input(DWORD from, DWORD to, BOOL attach) {
#if defined(REMOTE60_OS_INPUT_RECORD_ONLY)
  record("api=AttachThreadInput from=" + std::to_string(from) + " to=" + std::to_string(to) +
         (attach ? " attach" : " detach"));
  return TRUE;
#else
  return AttachThreadInput(from, to, attach);
#endif
}

// ImmSetOpenStatus / ImmSetConversionStatus (r5, Codex N1-2: they change the foreground IME): recorded, unchanged
// (record-only). The reads before them (ImmGetContext, ImmGetConversionStatus) stay real: observation, no effect.
inline BOOL imm_set_open_status(HIMC imc, BOOL open) {
#if defined(REMOTE60_OS_INPUT_RECORD_ONLY)
  (void)imc;
  record(std::string("api=ImmSetOpenStatus open=") + (open ? "1" : "0"));
  return TRUE;
#else
  return ImmSetOpenStatus(imc, open);
#endif
}
inline BOOL imm_set_conversion_status(HIMC imc, DWORD conversion, DWORD sentence) {
#if defined(REMOTE60_OS_INPUT_RECORD_ONLY)
  (void)imc;
  char b[96];
  std::snprintf(b, sizeof(b), "api=ImmSetConversionStatus conv=0x%lx sentence=0x%lx", static_cast<unsigned long>(conversion),
                static_cast<unsigned long>(sentence));
  record(b);
  return TRUE;
#else
  return ImmSetConversionStatus(imc, conversion, sentence);
#endif
}

// UI Automation ValuePattern::SetValue (r5, Codex N1-3: the credential field's clear is a real write): recorded, the
// field untouched (record-only) -- the value by its length only. A template so this header needs no UIA include.
template <class ValuePattern>
inline HRESULT uia_set_value(ValuePattern* pattern, BSTR value) {
#if defined(REMOTE60_OS_INPUT_RECORD_ONLY)
  (void)pattern;
  record("api=UIA.SetValue units=" + std::to_string(value ? SysStringLen(value) : 0u));
  return S_OK;
#else
  return pattern->SetValue(value);
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
