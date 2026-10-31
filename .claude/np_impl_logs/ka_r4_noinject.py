import os
base = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src' + os.sep

def edit(name, pairs):
    p = base + name
    b = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in b
    s = b.replace('\r\n', '\n')
    for a, c in pairs:
        assert s.count(a) == 1, (name, s.count(a), a[:90])
        s = s.replace(a, c)
    open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))

note = '''// r4: input injection OFF for every test host. GNLinkStream injects by default (host_args.hpp enableInputInjection =
// true); before this, each session start's modifier key-ups reached THIS PC's desktop input (2026-10-28, reported).
// The host still receives, logs ([input-timing] ... injectionEnabled=0) and acknowledges every input event.
'''
edit('ka_pc_shell_e2e_test.cpp', [(
'''    const auto env = env_with(dir + L"lad", {});
    const uint16_t media = e2e::e2e_pick_free_udp_port(), control = e2e::e2e_pick_free_tcp_port();
    h->proc.logPath = dir + L"host.log";''', note.replace('// ', '    // ').replace('\n    //', '\n    //') + '''    {
      std::ofstream cfg(dir + L"host-config.json", std::ios::binary | std::ios::trunc);
      cfg << "{\\"enableInputInjection\\": false}\\n";
    }
    const auto env = env_with(dir + L"lad", {});
    const uint16_t media = e2e::e2e_pick_free_udp_port(), control = e2e::e2e_pick_free_tcp_port();
    h->proc.logPath = dir + L"host.log";'''), (
'''            L" --directory-id tester --host-name " + widen(name) + L" --directory-cache \\"" + dir + L"cache.json\\" --seconds 1800",''',
'''            L" --directory-id tester --host-name " + widen(name) + L" --directory-cache \\"" + dir + L"cache.json\\" --seconds 1800" +
            L" --config \\"" + dir + L"host-config.json\\"",''')])
edit('ka_pc_managed_e2e_test.cpp', [(
'''                       L" --directory-cache \\"" + dir + L"cache.json\\" --seconds 900",''',
'''                       L" --directory-cache \\"" + dir + L"cache.json\\" --seconds 900 --config \\"" + dir + L"host-config.json\\"",'''), (
'''    CopyFileW(exe.c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE);''', '''    CopyFileW(exe.c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE);
''' + note.replace('// ', '    // ').replace('\n    //', '\n    //') + '''    {
      HANDLE f = CreateFileW((dir + L"host-config.json").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
      const char cfg[] = "{\\"enableInputInjection\\": false}\\n";
      DWORD put = 0;
      if (f != INVALID_HANDLE_VALUE) { WriteFile(f, cfg, sizeof(cfg) - 1, &put, nullptr); CloseHandle(f); }
    }''')])
print('ok')
