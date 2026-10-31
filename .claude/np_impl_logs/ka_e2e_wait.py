p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\ka_pc_managed_e2e_test.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c, cnt=1):
    global s
    assert s.count(a) == cnt, (s.count(a), a[:80])
    s = s.replace(a, c)

# a helper: the same PC key again only 31 s after this device's last admission at the Host
rep('''HWND viewer_window(DWORD pid) {''', '''// FINDING (10-28, reported): a NEW lease of the same device for the same Host, started within ~30 s of that device's
// previous admission, got no answer at all (relay "no StandbyAck", nothing in the Host log) and ended "unsupported";
// the next admission came ~30.3 s after the previous one (the Host's kStandbyHelloReplayMs is 30000 -- a coincidence
// or the cause, not settled here). Shared Host / relay code. The cases below that start the same PC key again wait
// that long first, said here and in the report; the immediate case is measured on its own (FINDING line).
void wait_after_admission(const std::string& hostLog, uint64_t* lastAdmitAt) {
  (void)hostLog;
  const uint64_t ready = *lastAdmitAt + 31000;
  if (now_ms() < ready) {
    std::printf("  (waiting %llu ms: 31 s after this device's last admission -- see the FINDING note)\\n",
                static_cast<unsigned long long>(ready - now_ms()));
    Sleep(static_cast<DWORD>(ready - now_ms()));
  }
}

HWND viewer_window(DWORD pid) {''')
# remember when A was admitted (its first ready) -- the module and C start the same key again
rep('''  check("standby: ready (the Host admitted its entry, Ping keeps it)", ready1 >= 0);''',
    '''  check("standby: ready (the Host admitted its entry, Ping keeps it)", ready1 >= 0);
  uint64_t lastAdmitAt = now_ms();''')
rep('''    shell::ManagedLaunch spec;
    spec.exe = gBuild + L"GNLinkViewerKaTest.exe";''', '''    wait_after_admission(hostA.Text(), &lastAdmitAt);
    shell::ManagedLaunch spec;
    spec.exe = gBuild + L"GNLinkViewerKaTest.exe";''')
rep('''    check("...its State reaches the shell: ready", state == "ready", state);''',
    '''    check("...its State reaches the shell: ready", state == "ready", state);
    lastAdmitAt = now_ms();''')
rep('''    Managed c;
    c.key = url + "\\ntester\\n" + idA;''', '''    wait_after_admission(hostA.Text(), &lastAdmitAt);
    Managed c;
    c.key = url + "\\ntester\\n" + idA;''')
rep('''    check("...ready again on the same PC key (its generation store reused, not locked by the ended one)", ready >= 0);''',
    '''    check("...ready again on the same PC key (its generation store reused, not locked by the ended one)", ready >= 0);
    lastAdmitAt = now_ms();''')
rep('''    // FINDING (reported, not counted): right after a crash''', '''    // FINDING (reported, not counted): IMMEDIATELY after a crash''')
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')
