R = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'

h = R + 'e2e_injection_guard.hpp'
s = open(h, encoding='utf-8').read()
a = '}  // namespace remote60::native_poc::e2e'
assert s.count(a) == 1
s = s.replace(a, '''// The tests whose very subject is an injection the OS performs (2026-10-28): a host that injects into a window of
// the test's own -- but in window mode a KEY goes through SendInput whenever that window holds the focus
// (host_input_inject.cpp, the kind 5/6 branch), and the pre-login credential typing is SendInput of its own (it does
// not follow the injection switch at all). That is this PC's real input path. Such a test refuses to run unless the
// person starting it says so for this run: REMOTE60_ALLOW_REAL_INPUT_TEST=1. Refused = exit 3, "RESULT: REFUSED" --
// never a pass. Whether it becomes a receipt-only test (injection off) is its owner's decision.
inline bool e2e_real_input_refused(const char* test, const char* why) {
  wchar_t v[8] = {};
  if (GetEnvironmentVariableW(L"REMOTE60_ALLOW_REAL_INPUT_TEST", v, 8) > 0 && wcscmp(v, L"1") == 0) return false;
  std::printf("REFUSED  %s: %s -- this PC's real input path. Not run without REMOTE60_ALLOW_REAL_INPUT_TEST=1 "
              "(e2e_injection_guard.hpp)\\nRESULT: REFUSED\\n", test, why);
  return true;
}

}  // namespace remote60::native_poc::e2e''')
s = s.replace('#include <chrono>\n', '#include <chrono>\n#include <cstdio>\n#include <cwchar>\n')
open(h, 'w', encoding='utf-8', newline='\n').write(s)
print('ok header')

GATES = {
    'control_resume_e2e_test.cpp': ('control_resume_e2e', 'a key-up is injected into a window of this test (SendInput when it has the focus)'),
    'viewer_mouse_xbutton_e2e_test.cpp': ('viewer_mouse_xbutton_e2e', 'the product viewer session start sends key releases the host injects (SendInput when its window has the focus)'),
    'viewer_paste_e2e_test.cpp': ('viewer_paste_e2e', 'Ctrl+V / Shift+Insert keys are injected by the host (SendInput when its window has the focus)'),
    'viewer_release_all_e2e_test.cpp': ('viewer_release_all_e2e', 'held keys and their releases are injected by the host (SendInput when its window has the focus)'),
    'viewer_prelogin_login_e2e_test.cpp': ('viewer_prelogin_login_e2e', 'the pre-login worker types the credential with SendInput on this desktop, and navigation keys / the pointer are injected'),
}
for f, (name, why) in GATES.items():
    p = R + f
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    a = 'int wmain(int argc, wchar_t** argv) {\n  std::setvbuf(stdout, nullptr, _IONBF, 0);\n'
    assert s.count(a) == 1, f
    s = s.replace(a, a + f'  // 2026-10-28: injection into this PC\'s real input path -- refused unless asked for (e2e_injection_guard.hpp)\n'
                         f'  if (remote60::native_poc::e2e::e2e_real_input_refused("{name}", "{why}")) return 3;\n')
    inc = '#include "e2e_isolation.hpp"\n'
    if '#include "e2e_injection_guard.hpp"' not in s:
        assert s.count(inc) == 1, (f, 'include')
        s = s.replace(inc, inc + '#include "e2e_injection_guard.hpp"\n')
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', f)
