R = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'


def edit(name, reps):
    p = R + name
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, a[:80], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', name)


BACKSTOP_A = '''    wchar_t oldBin[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"REMOTE60_E2E_OLD_BIN", oldBin, MAX_PATH) != 0 && oldBin[0] != L'\\0') {
      streamSrc = oldBin;'''
BACKSTOP_B = '''    wchar_t oldBin[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"REMOTE60_E2E_OLD_BIN", oldBin, MAX_PATH) != 0 && oldBin[0] != L'\\0') {
      // 2026-10-28 (Codex Finding-3 (4)): an older build has no --host-power-management -- it would ignore the
      // argument and keep its power effects on. Never started here to "check" that; this case is unverified.
      std::printf("NOT RUN  %s: an older binary (no --host-power-management) is not started on this PC -- unverified "
                  "(Codex Finding-3 (4))\\n", tag);
      return false;
      streamSrc = oldBin;'''

edit('host_standby_admission_e2e_test.cpp', [
    (BACKSTOP_A, BACKSTOP_B),
    ('''    if (i + 1 < argc && std::wstring(argv[i]) == L"--old-host") oldHost = argv[i + 1];''',
     '''    if (i + 1 < argc && std::wstring(argv[i]) == L"--old-host") {
      // 2026-10-28 (Codex Finding-3 (4)): an older build cannot turn its power effects (or, before 0.2.x, maybe
      // more) off -- not started on this PC. Its cases are UNVERIFIED here (an approved isolated machine, or a
      // reviewed source-equivalent test build, would be the place).
      std::printf("NOT RUN  --old-host %ls: an older binary has no --host-power-management; its cases are unverified "
                  "(Codex Finding-3 (4))\\n", argv[i + 1]);
    }'''),
])
edit('host_punch_reply_e2e_test.cpp', [(BACKSTOP_A, BACKSTOP_B)])
