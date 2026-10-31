base = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane'


def edit(name, reps):
    p = base + '\\' + name
    raw = open(p, 'rb').read()
    crlf = b'\r\n' in raw
    bom = raw.startswith(b'\xef\xbb\xbf')
    s = raw.decode('utf-8-sig').replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, s.count(a), a[:80])
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write((b'\xef\xbb\xbf' if bom else b'') + s.encode('utf-8'))


edit('oa_fixture.ps1', [
("""    $hc = if ($HolderCmd.Count) { $HolderCmd } else { @("$b\\remote60_ka_standby_holder_tool.exe") }""",
 """    # r4 (verifier oa-s/oa-p 2026-10-29 23:56): @( ) around the if -- an if EXPRESSION goes through the pipeline, so
    # a one-element array came out as a bare string and $hc[0] was its first letter ('D' -> a relative path)
    $hc = @(if ($HolderCmd.Count) { $HolderCmd } else { "$b\\remote60_ka_standby_holder_tool.exe" })"""),
("""  $b = if ($BuildDir) { $BuildDir } else { "$wt\\build\\apps\\native_poc\\Release" }""",
 """  $b = if ($BuildDir) { $BuildDir } else { "$wt\\build\\apps\\native_poc\\Release" }
  if (-not [IO.Path]::IsPathRooted($b)) { throw "the build folder must be an absolute path ($b)" }  # r4"""),
])

edit('oa_slot.ps1', [
("""$wt = (Resolve-Path "$PSScriptRoot\\..\\..").Path""",
 """# r4: every path argument absolute -- a relative one would be read against whichever cwd a child happens to have
foreach ($pv in @(@('Apk', $Apk), @('BuildDir', $BuildDir), @('ServerJs', $ServerJs), @('JobRun', $JobRun))) {
  if ($pv[1] -and -not [IO.Path]::IsPathRooted($pv[1])) { "FAIL  SETUP slot: -$($pv[0]) must be an absolute path ($($pv[1]))`nRESULT: INVALID"; exit 2 }
}
$wt = (Resolve-Path "$PSScriptRoot\\..\\..").Path"""),
])

edit('oa_orchestration_test.ps1', [
("""function Run-Case([string]$tag, [string]$plan, [hashtable]$extraEnv, [string[]]$slotArgs) {""",
 """function Run-Case([string]$tag, [string]$plan, [hashtable]$extraEnv, [string[]]$slotArgs, [bool]$defaultHolder = $false) {"""),
("""  $p = Start-Process -FilePath powershell -ArgumentList (@('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$e\\oa_slot.ps1",
    '-Tag', $tag, '-ReadyTimeoutSec', '5', '-FixtureReadyTimeoutSec', '90', '-StopWaitSec', '5',
    '-HarnessCmd', "$py,-I,$e\\stubs\\stub_harness.py", '-NodeCmd', "$py,-I,$e\\stubs\\stub_node.py",
    '-HolderCmd', "$py,-I,$e\\stubs\\stub_holder.py", '-AvdCmd', "$py,-I,$e\\stubs\\stub_avd.py") + $slotArgs) `""",
 """  # r4: $defaultHolder -- no -HolderCmd: the fixture runs <BuildDir>\\remote60_ka_standby_holder_tool.exe itself
  $holderArgs = if ($defaultHolder) { @() } else { @('-HolderCmd', "$py,-I,$e\\stubs\\stub_holder.py") }
  $p = Start-Process -FilePath powershell -ArgumentList (@('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$e\\oa_slot.ps1",
    '-Tag', $tag, '-ReadyTimeoutSec', '5', '-FixtureReadyTimeoutSec', '90', '-StopWaitSec', '5',
    '-HarnessCmd', "$py,-I,$e\\stubs\\stub_harness.py", '-NodeCmd', "$py,-I,$e\\stubs\\stub_node.py",
    '-AvdCmd', "$py,-I,$e\\stubs\\stub_avd.py") + $holderArgs + $slotArgs) `"""),
("""    left = @(Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -like "*$e\\stubs\\stub_*" -or $_.CommandLine -like "*oa_fixture.ps1*" }).Count""",
 """    left = @(Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -like "*$e\\stubs\\stub_*" -or $_.CommandLine -like "*oa_fixture.ps1*" -or
      $_.CommandLine -like '*_stubbuild-*remote60_ka_standby_holder_tool.exe*' }).Count"""),
("""\"--- r3 N2: the holder was ready, but its end was not a clean Release (exit 5): the bundle fails\"""",
 """\"--- r4: the DEFAULT holder path (no -HolderCmd): <BuildDir>\\remote60_ka_standby_holder_tool.exe, one element\"
# a stand-in build folder of this run: the holder is remote60_oa_stub_holder.exe (no viewer, no network) under the
# real name; the viewer exe is only hashed by the fixture
$stubBuild = "$runs\\_stubbuild-$PID"
New-Item -ItemType Directory $stubBuild | Out-Null
$stubExe = "$wt\\build\\apps\\native_poc\\Release\\remote60_oa_stub_holder.exe"
Copy-Item $stubExe "$stubBuild\\remote60_ka_standby_holder_tool.exe"
Copy-Item $stubExe "$stubBuild\\GNLinkViewerKaTest.exe"
$r = Run-Case 'orch-sb-default' 'a=ok' @{} @('-WithStandby', '-BuildDir', $stubBuild) $true
Check 'default holder path: the fixture ran <BuildDir>\\remote60_ka_standby_holder_tool.exe (hashed, started, ready), rc 0, nothing left' (
  $r.avd -and $r.rc -eq 0 -and $r.holderStarted -and $r.fx -match 'standby holder [0-9a-f]{16} viewer [0-9a-f]{16}' -and
  $r.holderArgs -match [regex]::Escape("--root $($r.root)\\standby") -and $r.result -match '  standby exit 0' -and $r.left -eq 0) "rc=$($r.rc) left=$($r.left)"
\"--- r4: a relative -BuildDir is refused before anything starts\"
$r = Run-Case 'orch-relbuild' 'a=ok' @{} @('-WithStandby', '-BuildDir', 'relative\\build')
Check 'relative -BuildDir: refused (exit 2), no fixture started' ($r.rc -eq 2 -and -not $r.hostStarted -and -not $r.avd) "rc=$($r.rc)"

\"--- r3 N2: the holder was ready, but its end was not a clean Release (exit 5): the bundle fails\""""),
])
print('ok')
