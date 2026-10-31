p = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane\oa_orchestration_test.ps1'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
bom = raw.startswith(b'\xef\xbb\xbf')
s = raw.decode('utf-8-sig').replace('\r\n', '\n')


def rep(a, b, cnt=1):
    global s
    assert s.count(a) == cnt, (s.count(a), a[:90])
    s = s.replace(a, b)


# the first case: P + standby (case 7 runs with arm P since r6); its relay is 127.0.0.1
rep('''"--- arm S + standby: host ready, standby ready, the AVD passes"
$r = Run-Case 'orch-s-sb' 'a=ok' @{} @('-Arm', 'S', '-WithStandby')
Check 'S+standby: host and holder started, AVD saw "standby=ready controlPort=0", rc 0, nothing left' ($r.avd -and $r.hostStarted -and
  $r.holderStarted -and $r.avdSaw -match 'hosts=a standby=ready controlPort=0' -and $r.rc -eq 0 -and $r.pidFiles.Count -eq 0 -and
  $r.left -eq 0) "rc=$($r.rc) avd=$($r.avd) saw=$($r.avdSaw.Trim())"
Check 'S: the harness got no --host-control-port; the holder got this run''s standby root, stop file, oa-pc-a, the account file' (
  $r.harnessArgs -match '--host-name oa-pc-a' -and $r.harnessArgs -notmatch '--host-control-port' -and''',
    '''"--- arm P + standby (r6: case 7 runs with arm P): host ready, standby ready, the AVD passes; relay 127.0.0.1"
$r = Run-Case 'orch-p-sb' 'a=ok' @{} @('-Arm', 'P', '-WithStandby')
Check 'P+standby: host and holder started, AVD saw "standby=ready controlPort=18460 relay=127.0.0.1", rc 0, nothing left' ($r.avd -and
  $r.hostStarted -and $r.holderStarted -and $r.avdSaw -match 'hosts=a standby=ready controlPort=18460 relay=127\\.0\\.0\\.1' -and
  $r.rc -eq 0 -and $r.pidFiles.Count -eq 0 -and $r.left -eq 0) "rc=$($r.rc) avd=$($r.avd) saw=$($r.avdSaw.Trim())"
Check 'P+standby: the holder got this run''s standby root, stop file, oa-pc-a, the account file' (
  $r.harnessArgs -match '--host-name oa-pc-a' -and''')
# the old "arm P, no standby" case keeps its check; add arm S (no standby: relay 10.0.2.2) and S+standby refused
rep('''"--- standby never ready (5 s)"''', '''"--- arm S, no standby: no --host-control-port, relay 10.0.2.2 (the phone's)"
$r = Run-Case 'orch-s' 'a=ok' @{} @('-Arm', 'S')
Check 'S: harness got no --host-control-port, no holder, AVD saw "standby=none controlPort=0 relay=10.0.2.2", rc 0' ($r.avd -and
  $r.harnessArgs -notmatch '--host-control-port' -and -not $r.holderStarted -and
  $r.avdSaw -match 'standby=none controlPort=0 relay=10\\.0\\.0\\.2' -and $r.rc -eq 0 -and $r.left -eq 0) "rc=$($r.rc) saw=$($r.avdSaw.Trim())"

"--- r6: arm S + standby is refused before anything starts (the PC cannot reach the phone's relay address)"
$r = Run-Case 'orch-s-sb-refused' 'a=ok' @{} @('-Arm', 'S', '-WithStandby')
Check 'S+standby: refused (rc 2), no node / host / holder started, no AVD, the reason said' (-not $r.avd -and $r.rc -eq 2 -and
  -not $r.hostStarted -and -not $r.holderStarted -and $r.fx -notmatch 'started node' -and
  $r.fx -match 'arm S cannot hold a PC-side standby lease') "rc=$($r.rc)"

"--- standby never ready (5 s)"''')
# every other standby case: arm P
s = s.replace("@('-WithStandby')", "@('-Arm', 'P', '-WithStandby')")
s = s.replace("@('-WithStandby', '-BuildDir', $stubBuild)", "@('-Arm', 'P', '-WithStandby', '-BuildDir', $stubBuild)")
s = s.replace("@('-WithStandby', '-BuildDir', 'relative\\build')", "@('-Arm', 'P', '-WithStandby', '-BuildDir', 'relative\\build')")
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write((b'\xef\xbb\xbf' if bom else b'') + s.encode('utf-8'))
print('ok', s.count("'-Arm', 'P', '-WithStandby'"))
