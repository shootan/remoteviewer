import re

R = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'


def edit(name, fn):
    p = R + name
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    s2 = fn(s)
    assert s2 != s, name
    assert 'write_injection_off_config' not in s2 and 'host-config.json' not in s2, name
    if crlf:
        s2 = s2.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s2)
    print('ok', name)


def one(s, a, b):
    assert s.count(a) == 1, (a[:90], s.count(a))
    return s.replace(a, b)


def drop_injcfg(s):
    s = re.sub(r'\n[ \t]*(const bool )?injCfg = remote60::native_poc::e2e::e2e_write_injection_off_config\([^)]*\);', '', s)
    s = s.replace('injCfg && ', '')
    s = s.replace('\n  bool injCfg = false;', '')
    return s


for f in ['android_session_reboot_e2e_test.cpp', 'bwe_observe_e2e_test.cpp', 'host_control_deadline_e2e_test.cpp',
          'host_two_dispatcher_e2e_test.cpp']:
    edit(f, drop_injcfg)

edit('host_fatal_exception_test.cpp', lambda s: one(one(one(s,
    '''  // input injection off (e2e_injection_guard.hpp) -- this host dies at a startup step and no client here sends input;
  // there is no wait for its "disabled" line (it may die before saying it)
  remote60::native_poc::e2e::e2e_write_injection_off_config(dir);
''', '''  // input injection off (e2e_injection_guard.hpp) -- this host dies at a startup step and no client here sends input;
  // there is no wait for its "disabled" line (it may die before saying it)
'''),
    '''
  remote60::native_poc::e2e::e2e_write_injection_off_config(dir);  // input injection off; its only client sends a bare Hello''',
    '''  // input injection off; its only client sends a bare Hello'''),
    '''                     L" --seconds 20" + remote60::native_poc::e2e::e2e_injection_off_arg();  // input injection off; its only client sends a bare Hello''',
    '''                     L" --seconds 20" + remote60::native_poc::e2e::e2e_injection_off_arg();  // input injection off; its only client sends a bare Hello'''))

edit('host_reboot_delegate_e2e_test.cpp', lambda s: one(s,
    '''
  if (!remote60::native_poc::e2e::e2e_write_injection_off_config(host->dir)) return false;  // input injection off (e2e_injection_guard.hpp)''',
    '''  // input injection off (e2e_injection_guard.hpp)'''))


def standby_like(s):
    s = one(s, '''// 2026-10-28 (verifier, after worker2's finding): GNLinkStream injects input by default (host_args.hpp
// enableInputInjection = true) -- a test host must not drive THIS PC's input. Every host here gets a config with
// injection off, and a test client connects only after the host itself said "input injection disabled" (StartHost).
std::wstring isolation_args(const std::wstring& stagingDir) {
  return L" --directory-cache \\"" + stagingDir + L"host_cache.json\\"" + L" --config \\"" + stagingDir +
         L"host-config.json\\"";
}

bool write_injection_off_config(const std::wstring& stagingDir) {
  std::ofstream cfg(stagingDir + L"host-config.json", std::ios::binary | std::ios::trunc);
  cfg << "{\\"enableInputInjection\\": false}\\n";
  return static_cast<bool>(cfg);
}''', '''// 2026-10-28 (verifier, after worker2's finding): GNLinkStream injects input by default (host_args.hpp
// enableInputInjection = true) -- a test host must not drive THIS PC's input. Every host here is started with its
// injection off (e2e_injection_guard.hpp), and a test client connects only after the host itself said
// "input injection disabled" (StartHost).
std::wstring isolation_args(const std::wstring& stagingDir) {
  return L" --directory-cache \\"" + stagingDir + L"host_cache.json\\"" +
         remote60::native_poc::e2e::e2e_injection_off_arg();
}''')
    s = re.sub(r'  if \(!write_injection_off_config\(host->dir\)\) \{\n.*?\n    return false;\n  \}\n', '', s, flags=re.S)
    s = one(s, '#include "e2e_isolation.hpp"\n', '#include "e2e_isolation.hpp"\n#include "e2e_injection_guard.hpp"\n')
    return s


edit('host_standby_admission_e2e_test.cpp', standby_like)
edit('host_punch_reply_e2e_test.cpp', standby_like)
