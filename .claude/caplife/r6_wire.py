import re

R = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'
NS = 'remote60::native_poc::e2e::'


def load(name):
    p = R + name
    if 'injMonitor' in open(p, encoding='utf-8').read() or 'injMon;' in open(p, encoding='utf-8').read():
        raise SystemExit('already wired: ' + name) if name not in SKIP else None
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    return p, s.replace('\r\n', '\n'), crlf


def save(p, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', p.rsplit('/', 1)[-1])


def final_checks(s):
    # before every result statement that can say ALL PASS: the last judgement of every host still monitored
    lines = s.split('
')
    starts = []
    for i, line in enumerate(lines):
        if 'ALL PASS' in line and 'RESULT' in line:
            j = i
            while j >= 0 and 'printf' not in lines[j] and 'std::cout' not in lines[j]:
                j -= 1
            assert j >= 0
            if j not in starts:
                starts.append(j)
    assert starts
    for j in sorted(starts, reverse=True):
        indent = lines[j][:len(lines[j]) - len(lines[j].lstrip())]
        lines.insert(j, indent + NS + 'e2e_injection_final_check_all();  // r6 G1: judged again before the result')
    return '
'.join(lines), len(starts)


def struct_member_and_stop(s, job_line_index_hint):
    # the struct's job member is followed by its monitor; its Stop() stops the monitor first
    m = list(re.finditer(r'^(  HANDLE job = nullptr;)\n', s, flags=re.M))
    assert m, 'no job member'
    first = m[0]
    s = s[:first.end()] + '  // r6 G1: this launch\'s monitor (after its initial gate); stopped first in Stop()\n  std::unique_ptr<' + NS + 'E2eInjectionMonitor> injMonitor;\n' + s[first.end():]
    st = s.find('  void Stop() {\n', first.end())
    assert st > 0
    st_end = st + len('  void Stop() {\n')
    s = s[:st_end] + '    if (injMonitor) injMonitor->Stop();  // r6 G1: its last judgement, before it goes\n' + s[st_end:]
    return s


# ---- StartHost-based: standby, punch, migration
for f, anchor in [('host_standby_admission_e2e_test.cpp', '    check("the host said its input injection is OFF (--input-injection-mode none) before any client", true);\n'),
                  ('host_punch_reply_e2e_test.cpp', '    check("the host said its input injection is OFF (--input-injection-mode none) before any client", true);\n')]:
    p, s, crlf = load(f)
    s = struct_member_and_stop(s, 0)
    assert s.count(anchor) == 1, (f, s.count(anchor))
    s = s.replace(anchor, anchor + '    host->injMonitor = std::make_unique<' + NS + 'E2eInjectionMonitor>([host] { return host->log(); }, host->job, tag);\n')
    s, n = final_checks(s)
    if '#include <memory>' not in s:
        s = s.replace('#include <string>\n', '#include <memory>\n#include <string>\n', 1)
    save(p, s, crlf)
    print('  result lines', n)

p, s, crlf = load('host_migration_e2e_test.cpp')
s = struct_member_and_stop(s, 0)
a = '''  check((std::string("[") + tag + "] the host said its input injection is OFF (--input-injection-mode none) before any client").c_str(),
        true);
'''
assert s.count(a) == 1
s = s.replace(a, a + '  host->injMonitor = std::make_unique<' + NS + 'E2eInjectionMonitor>([host] { return host->log(); }, host->job, tag);\n')
s, n = final_checks(s)
if '#include <memory>' not in s:
    s = s.replace('#include <mutex>\n', '#include <memory>\n#include <mutex>\n', 1)
save(p, s, crlf)

# ---- reboot_delegate: its Host struct
p, s, crlf = load('host_reboot_delegate_e2e_test.cpp')
s = struct_member_and_stop(s, 0)
a = '''  check("the host said its input injection is OFF (--input-injection-mode none) before any client", injOff, injWhy);
'''
assert s.count(a) == 1, s.count(a)
s = s.replace(a, a + '  if (injOff) host.injMonitor = std::make_unique<' + NS + 'E2eInjectionMonitor>([&host] { return host.log(); }, host.job, "host_reboot_delegate");\n')
s, n = final_checks(s)
if '#include <memory>' not in s:
    s = s.replace('#include <string>\n', '#include <memory>\n#include <string>\n', 1)
save(p, s, crlf)

# ---- single-host (log file): android, bwe, control_deadline, two_dispatcher
for f, tag in [('android_session_reboot_e2e_test.cpp', 'android_session_reboot'), ('bwe_observe_e2e_test.cpp', 'bwe_observe'),
               ('host_control_deadline_e2e_test.cpp', 'host_control_deadline'), ('host_two_dispatcher_e2e_test.cpp', 'host_two_dispatcher')]:
    p, s, crlf = load(f)
    m = re.search(r'  check\("the host said its input injection is OFF \(--input-injection-mode none\) before any client", injOff, injWhy\);\n', s)
    assert m, f
    s = s[:m.end()] + ('  // r6 G1: the same judgement keeps running after the gate (a contradiction -> INVALID, its job ended)\n'
                       '  std::unique_ptr<' + NS + 'E2eInjectionMonitor> injMon;\n'
                       '  if (launched && injOff) {\n'
                       '    injMon = std::make_unique<' + NS + 'E2eInjectionMonitor>(\n'
                       '        [&] { return ' + NS + 'e2e_read_text_file(hostLogPath); }, job, "' + tag + '");\n'
                       '  }\n') + s[m.end():]
    c = s.count('  CloseHandle(job);\n')
    assert c == 1, (f, c)
    s = s.replace('  CloseHandle(job);\n', '  if (injMon) injMon->Stop();  // r6 G1: its last judgement, before the job closes\n  CloseHandle(job);\n')
    s, n = final_checks(s)
    if '#include <memory>' not in s:
        s = s.replace('#include <string>\n', '#include <memory>\n#include <string>\n', 1)
    save(p, s, crlf)
