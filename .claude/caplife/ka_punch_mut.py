import sys, shutil, subprocess
p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/directory_client.cpp'
bak = 'D:/remote/remote/.claude/caplife/ka_punch_dc.bak'
MUT = {
    'stopNoClear': ("""  std::lock_guard<std::mutex> lock(punchMu_);
  punchJobs_.clear();
}""", """  // MUTANT: Stop keeps the queue
}"""),
    'punchNoGuard': ("""    if (!running_.load()) return;  // r2 N1: stopping -- nothing new for a thread that is leaving (or gone)
""", """    // MUTANT: queues while stopped
"""),
    'startNoClear': ("""    punchJobs_.clear();  // r2 N1: nothing of an earlier Start..Stop (Stop already cleared it; this is the belt)
""", """    // MUTANT: Start keeps what is there
"""),
}
exe = 'C:/work/gnlink-wt/ka-r3/build/apps/native_poc/Release/remote60_host_agent_punch_test.exe'
for m in sys.argv[1:]:
    shutil.copyfile(p, bak)
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    a, b = MUT[m]
    assert s.count(a) == 1, m
    s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))
    b_ = subprocess.run(['cmake', '--build', 'C:/work/gnlink-wt/ka-r3/build', '--config', 'Release', '--target',
                         'remote60_host_agent_punch_test', '--', '/m', '/v:q', '/nologo'], capture_output=True)
    r = subprocess.run([exe], capture_output=True, timeout=120)
    out = r.stdout.decode(errors='replace')
    fails = [l for l in out.splitlines() if l.startswith('FAIL')]
    print(f'{m}: build={b_.returncode} exit={r.returncode} FAIL={len(fails)} first={fails[0][:120] if fails else "-"}')
    shutil.copyfile(bak, p)
    import os, time
    os.utime(p, None)
subprocess.run(['cmake', '--build', 'C:/work/gnlink-wt/ka-r3/build', '--config', 'Release', '--target',
                'remote60_host_agent_punch_test', '--', '/m', '/v:q', '/nologo'], capture_output=True)
r = subprocess.run([exe], capture_output=True, timeout=120)
print('restored exit', r.returncode, [l for l in r.stdout.decode(errors='replace').splitlines() if 'checks,' in l])
