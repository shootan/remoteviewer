import subprocess

R = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'
EXE = 'C:/work/gnlink-wt/ka-r3/build/apps/native_poc/Release/remote60_standby_lease_test.exe'
MUTANTS = [
    ('punch_is_stray', 'standby_lease.hpp',
     """    if (kind == static_cast<uint16_t>(UdpPacketKind::Punch)) {
      if (stray) *stray = false;
      if (punch) *punch = true;
      return false;
    }""", """    if (kind == static_cast<uint16_t>(UdpPacketKind::Punch)) {
      if (punch) *punch = true;
      return false;
    }"""),
    ('release_where_all', 'standby_runner.hpp',
     """        if (it->first.compare(0, prefix.size(), prefix) == 0) {""", """        if (true) {"""),
]


def build():
    r = subprocess.run(['cmake', '--build', 'C:/work/gnlink-wt/ka-r3/build', '--config', 'Release', '--target',
                        'remote60_standby_lease_test', '--', '/m', '/v:minimal'], capture_output=True)
    return r.returncode


for name, f, a, b in MUTANTS:
    p = R + f
    orig = open(p, 'rb').read()
    s = orig.decode('utf-8').replace('\r\n', '\n')
    assert s.count(a) == 1, name
    m = s.replace(a, b)
    if b'\r\n' in orig:
        m = m.replace('\n', '\r\n')
    open(p, 'wb').write(m.encode('utf-8'))
    try:
        rc_b = build()
        out = subprocess.run([EXE], capture_output=True, timeout=600)
        lines = out.stdout.decode(errors='replace').splitlines()
        fails = [l for l in lines if l.startswith('FAIL')]
        print(f'{name}: build={rc_b} rc={out.returncode} last={lines[-1] if lines else ""} fails={len(fails)}')
        for l in fails[:4]:
            print('   ', l[:160])
    finally:
        open(p, 'wb').write(orig)
print('restored; rebuild', build())
out = subprocess.run([EXE], capture_output=True, timeout=600)
lines = out.stdout.decode(errors='replace').splitlines()
print('after restore rc', out.returncode, lines[-1], sum(1 for l in lines if l.startswith('PASS')))
