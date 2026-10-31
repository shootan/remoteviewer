# Verifier: independent run of os_input_record_e2e (ka-r3 ba2eaf2, Codex SOURCE OK for this boundary).
# Slot: CreateNew C:\work\gnlink-wt\SLOT_REQUEST.txt; delete only the file this run created.
param([int]$Run = 1)
$ErrorActionPreference = 'Stop'
$bin = 'D:\remote\remote\.claude\worktrees\verify-inj\build-verify\apps\native_poc\Release'
$out = "D:\remote\remote\.claude\verify_inj\r1"
$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
New-Item -ItemType Directory -Force $out | Out-Null
$tmp = 'D:\remote\remote\.claude\test-tmp\verify-inj'
New-Item -ItemType Directory -Force $tmp | Out-Null

$fs = $null
try { $fs = [System.IO.File]::Open($slot, 'CreateNew', 'Write', 'None') } catch { "SLOT BUSY: $slot exists -- not run"; exit 4 }
$fs.Write([byte[]][char[]]"verifier remote#7fsfy22z os_input_record_e2e $(Get-Date -Format o)", 0, 60); $fs.Close()
try {
  $others = Get-Process GNLinkStream* -ErrorAction SilentlyContinue | Select-Object Id, Path
  "other GNLinkStream before run: $($others | Out-String)" | Out-File "$out\pre_$Run.txt" -Encoding utf8
  qwinsta | Out-File "$out\qwinsta_$Run.txt" -Encoding utf8
  (Get-FileHash "$bin\GNLinkStreamInputRecordTest.exe").Hash, (Get-FileHash "$bin\remote60_os_input_record_e2e_test.exe").Hash |
    Out-File "$out\hash_$Run.txt" -Encoding utf8
  $env:REMOTE60_ALLOW_HOST_E2E = '1'
  $env:REMOTE60_TEST_TMP = $tmp
  & "$bin\remote60_os_input_record_e2e_test.exe" > "$out\e2e_$Run.log" 2>&1
  "rc=$LASTEXITCODE" | Out-File "$out\rc_$Run.txt" -Encoding utf8
  "rc=$LASTEXITCODE"
} finally {
  Remove-Item $slot -Confirm:$false
}
