p = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane\oa_fixture.ps1'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
bom = raw.startswith(b'\xef\xbb\xbf')
s = raw.decode('utf-8-sig').replace('\r\n', '\n')
start = s.index("  # r6 (verifier oa-s / oa-p 2026-10-30 00:56, standby \"unsupported\")")
end = s.index("  Say \"  relay address handed out: $RelayIp (this PC's LAN IPv4; hosts dial 127.0.0.1)\"\n")
end = end + len("  Say \"  relay address handed out: $RelayIp (this PC's LAN IPv4; hosts dial 127.0.0.1)\"\n")
new = '''  # r6 (verifier oa-s / oa-p 2026-10-30 00:56: the PC-side standby lease ended "unsupported"; the fixture-only checks
  # 01:15): a standby lease aims at the relay address its capability names (standby_lease.hpp:9; server.js:1310
  # RELAY_IP), and the relay binds the lease to the wire the client OBSERVED from (server: "standby hello rejected
  # ... reason=wire-mismatch expectedWire=127.0.0.1"). So one relay address serves only clients that reach it from the
  # same wire as their observation:
  #   * the phone (emulator) observes via 10.0.2.2 -> wire 127.0.0.1, and reaches the relay as 127.0.0.1 only through
  #     10.0.2.2 (ke-r26: standby relay=reserved worked);
  #   * the PC-side holder's viewer observes via 127.0.0.1 -> wire 127.0.0.1, and cannot reach 10.0.2.2 at all (00:56:
  #     no hello reached the relay); through this PC's LAN address its datagrams leave from that address (01:15:
  #     wire-mismatch). Only 127.0.0.1 works for it.
  # Hence by arm: S (the phone uses the directory and the relay) -> 10.0.2.2, and NO PC-side standby lease (refused
  # here, not discovered later); P (the phone connects directly, no relay) -> 127.0.0.1, where the holder's lease works.
  if (-not $RelayIp) { $RelayIp = if ($Arm -eq 'P') { '127.0.0.1' } else { '10.0.2.2' } }
  if ($Arm -eq 'S' -and $WithStandby) {
    throw "arm S cannot hold a PC-side standby lease in this fixture: the phone needs the relay at 10.0.2.2, which the PC cannot reach (see oa_fixture.ps1 r6) -- run case 7 with arm P"
  }
  if ($RelayIp -notin @('10.0.2.2', '127.0.0.1')) { throw "-RelayIp must be 10.0.2.2 (arm S) or 127.0.0.1 (arm P): $RelayIp" }
  Say "  relay address handed out: $RelayIp (arm $Arm; hosts dial 127.0.0.1)"
'''
s = s[:start] + new + s[end:]
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write((b'\xef\xbb\xbf' if bom else b'') + s.encode('utf-8'))
print('ok')
