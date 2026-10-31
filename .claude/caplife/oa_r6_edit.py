p = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane\oa_fixture.ps1'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
bom = raw.startswith(b'\xef\xbb\xbf')
s = raw.decode('utf-8-sig').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (s.count(a), a[:90])
    s = s.replace(a, b)


rep("""  [int]$ControlPort = 18460
)""", """  [int]$ControlPort = 18460,
  # r6: the relay address the directory hands its clients (empty: this PC's LAN IPv4, see below)
  [string]$RelayIp = ''
)""")
rep("""  $dirEnv = @{ REMOTE60_DIR_DATA = "$Root\\dir\\directory-data.json"; REMOTE60_DIR_PORT = '18410'; REMOTE60_DIR_UDP_PORT = '18411';
    REMOTE60_LOG_DIR = "$Root\\dir\\logs"; REMOTE60_RELAY_ENABLED = '1'; REMOTE60_RELAY_IP = '10.0.2.2';""",
    """  # r6 (verifier oa-s / oa-p 2026-10-30 00:56, standby "unsupported"): the relay is dialled by TWO clients here -- the
  # phone (in the emulator) AND the PC-side standby holder's viewer. A standby lease aims at the relay address the
  # capability names (standby_lease.hpp:9; server.js:1310 RELAY_IP), and 10.0.2.2 -- the emulator's alias for this PC
  # -- does not exist on the PC: the viewer's StandbyHellos went nowhere and it ended `unsupported`. This PC's LAN IPv4
  # (the interface with the default route) is reachable from both: the emulator's NAT forwards to it, the PC delivers
  # it to itself. The relay socket was bound to every interface already (server.js sock.bind(port)): what it listens
  # on is unchanged -- only the address it hands out. The hosts still reach it at 127.0.0.1 (RELAY_HOST_FACING_IP).
  if (-not $RelayIp) {
    $RelayIp = @(Get-NetIPConfiguration -ErrorAction SilentlyContinue | Where-Object { $_.IPv4DefaultGateway -and
      $_.NetAdapter.Status -eq 'Up' } | ForEach-Object { $_.IPv4Address.IPAddress } | Where-Object { $_ -match '^\\d+\\.\\d+\\.\\d+\\.\\d+$' }) |
      Select-Object -First 1
  }
  if (-not $RelayIp -or $RelayIp -notmatch '^\\d+\\.\\d+\\.\\d+\\.\\d+$' -or $RelayIp -like '127.*' -or $RelayIp -eq '10.0.2.2') {
    throw "no relay address both the emulator and this PC reach (got '$RelayIp')"
  }
  Say "  relay address handed out: $RelayIp (this PC's LAN IPv4; hosts dial 127.0.0.1)"
  $dirEnv = @{ REMOTE60_DIR_DATA = "$Root\\dir\\directory-data.json"; REMOTE60_DIR_PORT = '18410'; REMOTE60_DIR_UDP_PORT = '18411';
    REMOTE60_LOG_DIR = "$Root\\dir\\logs"; REMOTE60_RELAY_ENABLED = '1'; REMOTE60_RELAY_IP = $RelayIp;""")
rep("""        $result[$name] = "exit $($pr.ExitCode)\"""", """        $result[$name] = "exit $($pr.ExitCode)"
        # r6: a host's own log, kept whatever the harness did -- its copy (directoryhost_host.log) is made only at its
        # normal end; if it is missing, the log its readiness named is copied here (while it still exists)
        if ($name -ne 'node' -and $name -ne 'standby' -and -not (Test-Path -LiteralPath "$Root\\$name\\directoryhost_host.log")) {
          $rd = "$Root\\$name\\directoryhost_ready.txt"
          $hl = if (Test-Path -LiteralPath $rd) { [regex]::Match((Get-Content -LiteralPath $rd -Raw), 'host_log=(\\S+)').Groups[1].Value } else { '' }
          if ($hl -and (Test-Path -LiteralPath $hl)) { Copy-Item -LiteralPath $hl -Destination "$Root\\$name\\host.log.kept" }
          $result[$name] += if ($hl -and (Test-Path -LiteralPath "$Root\\$name\\host.log.kept")) { '; host log kept by the fixture' } else { '; NO host log (harness copy missing, source gone)' }
        }""")
rep("""  "ready run=$RunId hosts=a standby=$sb controlPort=$cp" | Out-File "$Root\\fixture_ready.tmp" -Encoding ascii""",
    """  "ready run=$RunId hosts=a standby=$sb controlPort=$cp relay=$RelayIp" | Out-File "$Root\\fixture_ready.tmp" -Encoding ascii""")
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write((b'\xef\xbb\xbf' if bom else b'') + s.encode('utf-8'))
print('ok')
