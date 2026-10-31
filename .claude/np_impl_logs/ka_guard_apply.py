B = chr(92)
NL = B + 'n'
base = 'C:' + B + 'work' + B + 'gnlink-wt' + B + 'ka-pc' + B + 'apps' + B + 'native_poc' + B + 'src' + B
child = open('D:' + B + 'remote' + B + 'remote' + B + '.claude' + B + 'np_impl_logs' + B + 'ka_guard_child.cpp.txt',
             encoding='utf-8').read().replace('\r\n', '\n')

run_block = ('  // ka-pc r7+ (the shared guard: a repository fixture this run OWNS): a NEW folder of this run inside the root --\n'
             '  // made new here (an existing one is refused, never reused) -- and everything of the run goes in it.\n'
             '  const std::wstring run = std::wstring(full) + L"' + B + B + 'run-" + std::to_wstring(GetCurrentProcessId()) + L"-" +\n'
             '                           std::to_wstring(GetTickCount64());\n'
             '  if (!CreateDirectoryW(run.c_str(), nullptr)) {\n'
             '    *why = "this run' + "'" + 's own folder could not be made new";\n'
             '    return false;\n'
             '  }\n'
             '  gRoot = run + L"' + B + B + '";\n')
old_root = '  gRoot = std::wstring(full) + L"' + B + B + '";\n'

temp_block = ('  // TEMP / TMP of this process (and so of every child it starts) inside this run: nothing of the test -- the shell\'s\n'
              '  // WebView profile (GetTempPath + "GNLinkClient") among them -- lands in the user\'s %TEMP%.\n'
              '  {\n'
              '    const std::wstring tmp = gRoot + L"temp";\n'
              '    CreateDirectoryW(tmp.c_str(), nullptr);\n'
              '    SetEnvironmentVariableW(L"TEMP", tmp.c_str());\n'
              '    SetEnvironmentVariableW(L"TMP", tmp.c_str());\n'
              '  }\n')

def apply(name, extra):
    p = base + name
    b = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in b
    s = b.replace('\r\n', '\n')
    i = s.index('struct Child {')
    # the old comment line(s) just above stay; the struct is replaced whole
    j = s.index('  ~Child() { End(); }\n};', i) + len('  ~Child() { End(); }\n};')
    s = s[:i] + child.rstrip('\n').split('struct Child {', 1)[0] + 'struct Child {' + child.split('struct Child {', 1)[1].rstrip('\n') + s[j:]
    assert s.count(old_root) == 1, (name, 'root')
    s = s.replace(old_root, run_block)
    anchor = ('  if (!init_root(&why)) {\n    std::printf("FAIL  no test root: %s' + NL + '", why.c_str());\n    return 2;\n  }\n')
    assert s.count(anchor) == 1, (name, 'anchor')
    s = s.replace(anchor, anchor + temp_block)
    for a, c in extra:
        assert s.count(a) == 1, (name, s.count(a), a[:80])
        s = s.replace(a, c)
    open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
    print(name, 'ok')

apply('ka_pc_managed_e2e_test.cpp', [
('#include <functional>\n', '#include <fstream>\n#include <functional>\n#include <memory>\n'),
('''  // The shared test-host guard (ka-r3 6b9cf79, e2e_injection_guard.hpp): THIS launch must have said input injection
  // OFF and host power management OFF, both whole lines, before any viewer exists -- else INVALID, exit 2.
  {
    std::string why;
    if (!e2e::e2e_wait_injection_off([&hostA] { return hostA.Text(); }, 20000, &why))
      e2e::e2e_injection_setup_failed("host KA-A", why);
  }''', '''  // The shared test-host guard (ka-r3 b8dcc93): THIS launch said input injection OFF and power management OFF, both
  // whole lines, before any viewer exists -- else INVALID, exit 2; then its monitor; its end is the final judgement.
  hostA.Guard("host KA-A");'''),
('''  // rule 4 of the guard: a host that took input with injection on makes the run INVALID
  if (e2e::e2e_injection_contradicted(hostA.Text())) {
    std::printf("FAIL  host KA-A logged input with injection on''' + NL + '''RESULT: INVALID''' + NL + '''");
    return 2;
  }
''', '''  // the shared guard: every launch of the run judged before the result (a finalized one on its complete output)
  e2e::e2e_injection_final_check_all();
'''),
])
apply('ka_pc_shell_e2e_test.cpp', [
('''    // The shared test-host guard (ka-r3 6b9cf79): THIS launch said input injection OFF and host power management
    // OFF, both whole lines, before any viewer exists -- else INVALID, exit 2 (its hosts die with their jobs).
    {
      std::string why;
      TestHost* th = h.get();
      if (!e2e::e2e_wait_injection_off([th] { return th->proc.Text(); }, 20000, &why))
        e2e::e2e_injection_setup_failed(name, why);
    }''', '''    // The shared test-host guard (ka-r3 b8dcc93): THIS launch said input injection OFF and power management OFF,
    // both whole lines, before any viewer exists -- else INVALID, exit 2; then its monitor; its end is judged final.
    h->proc.Guard(name);'''),
('''  // rule 4 of the guard: a host that took input with injection on makes the run INVALID
  for (auto& h : hosts) {
    if (e2e::e2e_injection_contradicted(h->proc.Text())) {
      std::printf("FAIL  host %s logged input with injection on''' + NL + '''RESULT: INVALID''' + NL + '''", h->name.c_str());
      for (auto& k : hosts) k->proc.End();
      server.End();
      return 2;
    }
  }
  for (auto& h : hosts) h->proc.End();
  server.End();''', '''  for (auto& h : hosts) h->proc.End();  // each host's end is the guard's final judgement (Finalize)
  server.End();
  // the shared guard: every launch of the run judged before the result
  e2e::e2e_injection_final_check_all();'''),
])
