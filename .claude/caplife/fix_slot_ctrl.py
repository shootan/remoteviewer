p = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/ke_slot.ps1'
b = open(p, 'rb').read()
BS = bytes([92])
b = b.replace(b'$root\x07vd', b'$root' + BS + b'avd')
b = b.replace(b'$root\x0cixture_failed', b'$root' + BS + b'fixture_failed')
open(p, 'wb').write(b)
print('bel', b.count(b'\x07'), 'ff', b.count(b'\x0c'))
