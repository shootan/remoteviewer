bs = chr(92)
q = bs + '"'
for name in ['ka_pc_shell_e2e_test.cpp', 'ka_pc_managed_e2e_test.cpp']:
    p = 'C:\\work\\gnlink-wt\\ka-pc\\apps\\native_poc\\src\\' + name
    b = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in b
    s = b.replace('\r\n', '\n')
    # the note
    i = s.index('// r4: input injection OFF for every test host.')
    j = s.index('(forceEncodedExperiment).', i) + len('(forceEncodedExperiment).')
    indent = s[s.rfind('\n', 0, i) + 1:i]
    s = s[:i] + ('// r4: input injection OFF for every test host -- the standard the verifier set (Codex): an explicit\n'
                 + indent + '// --input-injection-mode none on the argv, and no viewer connects before THIS launch says "input injection\n'
                 + indent + '// disabled". GNLinkStream injects by default (host_args.hpp enableInputInjection = true); before this, each\n'
                 + indent + '// session start\'s modifier key-ups reached this PC\'s input (2026-10-28, reported). The host still receives,\n'
                 + indent + '// logs ([input-timing] ... injectionEnabled=0) and acknowledges every input event. (Not --config: a config\n'
                 + indent + '// file clears the host\'s environment overrides, the h264 switch among them.)') + s[j:]
    if name == 'ka_pc_shell_e2e_test.cpp':
        a = ('    {\n      std::ofstream cfg(dir + L"host-config.json", std::ios::binary | std::ios::trunc);\n'
             '      cfg << "{' + q + 'enableInputInjection' + q + ': false, ' + q + 'forceEncodedExperiment' + q + ': true}' + bs + 'n";\n    }\n')
        assert s.count(a) == 1, ('cfg block', s.count(a))
        s = s.replace(a, '')
        a = (' --seconds 1800" +\n            L" --config ' + q + '" + dir + L"host-config.json' + q + '",')
        assert s.count(a) == 1, ('argv', s.count(a))
        s = s.replace(a, ' --seconds 1800" +\n            L" --input-injection-mode none",')
    else:
        a = ('    {\n      HANDLE f = CreateFileW((dir + L"host-config.json").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);\n'
             '      const char cfg[] = "{' + q + 'enableInputInjection' + q + ': false, ' + q + 'forceEncodedExperiment' + q + ': true}' + bs + 'n";\n'
             '      DWORD put = 0;\n'
             '      if (f != INVALID_HANDLE_VALUE) { WriteFile(f, cfg, sizeof(cfg) - 1, &put, nullptr); CloseHandle(f); }\n    }')
        assert s.count(a) == 1, ('cfg block m', s.count(a))
        s = s.replace(a, '')
        a = ' --seconds 900 --config ' + q + '" + dir + L"host-config.json' + q + '",'
        assert s.count(a) == 1, ('argv m', s.count(a))
        s = s.replace(a, ' --seconds 900 --input-injection-mode none",')
    open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
    print(name, 'ok')
