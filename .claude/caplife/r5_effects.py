import re

R = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'


def load(name):
    p = R + name
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    return p, s.replace('\r\n', '\n'), crlf


def save(p, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', p.rsplit('/', 1)[-1])


def one(s, a, b):
    assert s.count(a) == 1, (a[:90], s.count(a))
    return s.replace(a, b)


# ---------------------------------------------------------------- os_input_effects.hpp
p, s, crlf = load('os_input_effects.hpp')
s = one(s, '#include <windows.h>\n', '#include <windows.h>\n#include <imm.h>\n')
# the dispatch context + record prefix
s = one(s, '''inline void record(const std::string& line) {
  std::lock_guard<std::mutex> lock(record_mu());
  recorded().push_back(line);''', '''// The dispatch this effect belongs to (r5, Codex T1): the product's dispatch sets it around one input / text / release
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
  recorded().push_back(line);''')
s = one(s, '''// SendInput: recorded, none sent (record-only)''', '''// The dispatch around one product input (record-only: tags the effects it causes; product build: nothing at all).
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

// SendInput: recorded, none sent (record-only)''')
# the new wrappers, before the broker
s = one(s, '''// SetThreadExecutionState (HostPowerKeepalive's only power API''', '''// AttachThreadInput (r5, Codex N1-2: it joins two threads' input state -- not a query): recorded, not joined
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

// SetThreadExecutionState (HostPowerKeepalive's only power API''')
save(p, s, crlf)

# ---------------------------------------------------------------- host_input_inject.cpp
p, s, crlf = load('host_input_inject.cpp')
n_a = len(re.findall(r'(?<![\w:])AttachThreadInput\(', s))
n_o = len(re.findall(r'(?<![\w:])ImmSetOpenStatus\(', s))
n_c = len(re.findall(r'(?<![\w:])ImmSetConversionStatus\(', s))
print('  attach', n_a, 'open', n_o, 'conv', n_c)
assert (n_a, n_o, n_c) == (6, 1, 2)
s = re.sub(r'(?<![\w:])AttachThreadInput\(', 'os_input::attach_thread_input(', s)
s = re.sub(r'(?<![\w:])ImmSetOpenStatus\(', 'os_input::imm_set_open_status(', s)
s = re.sub(r'(?<![\w:])ImmSetConversionStatus\(', 'os_input::imm_set_conversion_status(', s)
save(p, s, crlf)

# ---------------------------------------------------------------- logon_ui_target.cpp
p, s, crlf = load('logon_ui_target.cpp')
s = one(s, 'SUCCEEDED(value->SetValue(empty))', 'SUCCEEDED(os_input::uia_set_value(value, empty))')
save(p, s, crlf)

# ---------------------------------------------------------------- host_control_session.cpp: the dispatch contexts
p, s, crlf = load('host_control_session.cpp')
s = one(s, '''      std::string target;
      InputFailStage stage = InputFailStage::None;
      DWORD error = 0;
      const InputInjectResult result =
          desktopMode && !targetMonitor''', '''      std::string target;
      InputFailStage stage = InputFailStage::None;
      DWORD error = 0;
      // r5 (Codex T1): the effects of this release, tagged with it (record-only test build; nothing in the product)
      const os_input::DispatchContext dispatchCtx("release", 0, servedEpoch);
      const InputInjectResult result =
          desktopMode && !targetMonitor''')
s = one(s, '''          injectResult =
              desktopMode && !targetMonitor ? InputInjectResult::NoTarget :
              inject_background_input_event(input, inputRouter.targetCriteria, capture.targetHwnd,''', '''          // r5 (Codex T1): the effects of THIS input, tagged with its seq and session (record-only test build only)
          const os_input::DispatchContext dispatchCtx("input", input.seq, servedEpoch);
          injectResult =
              desktopMode && !targetMonitor ? InputInjectResult::NoTarget :
              inject_background_input_event(input, inputRouter.targetCriteria, capture.targetHwnd,''')
s = one(s, '''        } else {
          injectResult = apply_input_text_message(text, capture.targetHwnd, desktopMode,''', '''        } else {
          // r5 (Codex T1): the effects of THIS text, tagged with its seq and session (record-only test build only)
          const os_input::DispatchContext dispatchCtx("text", text.seq, servedEpoch);
          injectResult = apply_input_text_message(text, capture.targetHwnd, desktopMode,''')
if '#include "os_input_effects.hpp"' not in s:
    last = list(re.finditer(r'^#include "[^"]+"\n', s, flags=re.M))[-1]
    s = s[:last.end()] + '#include "os_input_effects.hpp"\n' + s[last.end():]
save(p, s, crlf)
