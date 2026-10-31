import re

R = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'
NS = 'remote60::native_poc::e2e::'

# header: the argument writes its config (a failed write -> "failed to load --config" -> rejected by the gate)
h = R + 'e2e_injection_guard.hpp'
s = open(h, encoding='utf-8').read()
a = '''// The argument naming it (append to the host's command line).
inline std::wstring e2e_injection_off_arg(const std::wstring& stagingDir) {
  return L" --config \\"" + stagingDir + L"host-config.json\\"";
}'''
assert s.count(a) == 1
s = s.replace(a, '''// The argument for the host's command line -- it writes that config first. A config that could not be written is
// one the host fails to load ("failed to load --config"): the gate below rejects that, so this cannot fail open.
inline std::wstring e2e_injection_off_arg(const std::wstring& stagingDir) {
  e2e_write_injection_off_config(stagingDir);
  return L" --config \\"" + stagingDir + L"host-config.json\\"";
}''')
open(h, 'w', encoding='utf-8', newline='\n').write(s)
print('ok header')


def edit(name, fn):
    p = R + name
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    s2 = fn(s)
    assert s2 != s, name
    if crlf:
        s2 = s2.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s2)
    print('ok', name)


def one(s, a, b):
    assert s.count(a) == 1, (a[:100], s.count(a))
    return s.replace(a, b)


ARG = NS + 'e2e_injection_off_arg()'


def standby_like(s):
    s = one(s, ARG, NS + 'e2e_injection_off_arg(stagingDir)')
    s = one(s, '''// "input injection disabled" (StartHost).''',
            '''// "input injection disabled (enableInputInjection=false)" -- else the test ends INVALID there (StartHost).''')
    s = re.sub(r'''  const bool injSaid = host->WaitFor\("input injection disabled", 20000\);
  const bool injOn = .*?
  if \(!injSaid \|\| injOn\) \{
    TerminateJobObject\(host->job, 1\);
    return false;
  \}
''', '''  {
    std::string injWhy;
    if (!''' + NS + '''e2e_wait_injection_off([host] { return host->log(); }, 20000, &injWhy)) {
      ''' + NS + '''e2e_injection_setup_failed(tag, injWhy);
    }
    check("the host said its input injection is OFF (enableInputInjection=false) before any client", true);
  }
''', s, count=1, flags=re.S)
    assert 'injSaid' not in s
    return s


edit('host_standby_admission_e2e_test.cpp', standby_like)
edit('host_punch_reply_e2e_test.cpp', standby_like)


def migration(s):
    s = one(s, ARG, NS + 'e2e_injection_off_arg(stagingDir)')
    return one(s, '''  std::string injWhy;
  const bool injOff = remote60::native_poc::e2e::e2e_wait_injection_off([host] { return host->log(); }, 20000, &injWhy);
  check((std::string("[") + tag + "] the host says its input injection is off (before any client connects)").c_str(),
        injOff, injWhy);
  if (!injOff) TerminateJobObject(host->job, 1);
  return usedIt && seam && injOff;''', '''  std::string injWhy;
  if (!remote60::native_poc::e2e::e2e_wait_injection_off([host] { return host->log(); }, 20000, &injWhy)) {
    remote60::native_poc::e2e::e2e_injection_setup_failed(tag, injWhy);
  }
  check((std::string("[") + tag + "] the host said its input injection is OFF (enableInputInjection=false) before any client").c_str(),
        true);
  return usedIt && seam;''')


edit('host_migration_e2e_test.cpp', migration)


def reboot(s):
    s = one(s, ARG, NS + 'e2e_injection_off_arg(host->dir)')
    return one(s, '''  std::string injWhy;
  const bool injOff = started && remote60::native_poc::e2e::e2e_wait_injection_off([&host] { return host.log(); }, 20000, &injWhy);
  check("the host says its input injection is off (before any client connects)", injOff, injWhy);
  if (started && !injOff) TerminateJobObject(host.job, 1);''', '''  std::string injWhy;
  const bool injOff = started && remote60::native_poc::e2e::e2e_wait_injection_off([&host] { return host.log(); }, 20000, &injWhy);
  if (started && !injOff) remote60::native_poc::e2e::e2e_injection_setup_failed("host_reboot_delegate", injWhy);
  check("the host said its input injection is OFF (enableInputInjection=false) before any client", injOff, injWhy);''')


edit('host_reboot_delegate_e2e_test.cpp', reboot)


def file_log(tag, extra_launched_reset):
    def fn(s):
        s = one(s, ARG, NS + 'e2e_injection_off_arg(dir)')
        s = re.sub(r'''  check\("the host says its input injection is off \((before any client connects|before the phone connects)\)", injOff, injWhy\);
(  if \(launched && !injOff\) TerminateJobObject\(job, 1\);  // stopped before anything is sent to it
)?(  if \(!injOff\) launched = false;
)?''', '''  if (launched && !injOff) remote60::native_poc::e2e::e2e_injection_setup_failed("''' + tag + '''", injWhy);
  check("the host said its input injection is OFF (enableInputInjection=false) before any client", injOff, injWhy);
''', s, count=1)
        assert 'injection_setup_failed("' + tag in s, tag
        return s
    return fn


edit('android_session_reboot_e2e_test.cpp', file_log('android_session_reboot', False))
edit('bwe_observe_e2e_test.cpp', file_log('bwe_observe', True))
edit('host_control_deadline_e2e_test.cpp', file_log('host_control_deadline', False))
edit('host_two_dispatcher_e2e_test.cpp', file_log('host_two_dispatcher', True))


def fatal(s):
    s = s.replace(ARG, NS + 'e2e_injection_off_arg(dir)')
    return s


edit('host_fatal_exception_test.cpp', fatal)
