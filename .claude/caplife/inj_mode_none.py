import re

R = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'

# the helper: mode none (no file) -- `--config` turns h264 off in this host build (probe 2026-10-28)
h = R + 'e2e_injection_guard.hpp'
s = open(h, encoding='utf-8').read()
a = s[s.index('// The config file in the host'):s.index('// What a host wrote to a log file')]
b = '''// The argument that turns it off: `--input-injection-mode none` (host_input_inject.cpp parse_input_injection_mode ->
// Disabled; the host then says "input injection disabled (unsupported mode) mode=none"). Not `--config` with
// {"enableInputInjection": false}: with a --config file this host build refuses h264 ("unsupported codec", seen
// 2026-10-28), so such a host never served a session at all.
inline std::wstring e2e_injection_off_arg() { return L" --input-injection-mode none"; }

'''
s = s.replace(a, b)
s = s.replace('''// 2026-10-28 (verifier, after worker2's finding in the ka-pc e2e): GNLinkStream injects input by default
// (host_args.hpp enableInputInjection = true). A test host must never drive THIS PC's input, so every test host gets
// a config that turns injection off, and the test goes on only after the host ITSELF said''',
              '''// 2026-10-28 (verifier, after worker2's finding in the ka-pc e2e): GNLinkStream injects input by default
// (host_args.hpp enableInputInjection = true). A test host must never drive THIS PC's input, so every test host is
// started with its injection off, and the test goes on only after the host ITSELF said''')
s = s.replace('#include <fstream>\n', '')
open(h, 'w', encoding='utf-8', newline='\n').write(s)
print('ok header')

files = ['host_standby_admission_e2e_test.cpp', 'host_punch_reply_e2e_test.cpp', 'android_session_reboot_e2e_test.cpp',
         'bwe_observe_e2e_test.cpp', 'host_control_deadline_e2e_test.cpp', 'host_fatal_exception_test.cpp',
         'host_reboot_delegate_e2e_test.cpp', 'host_two_dispatcher_e2e_test.cpp']
for f in files:
    p = R + f
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    n0 = s
    # arg calls: e2e_injection_off_arg(<anything>) -> e2e_injection_off_arg()
    s = re.sub(r'e2e_injection_off_arg\([^()]*\)', 'e2e_injection_off_arg()', s)
    print(f, 'arg calls', len(re.findall(r'e2e_injection_off_arg\(\)', s)))
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
