$ErrorActionPreference='Stop'
$base='D:\remote\remote\.claude\codex_ka_e_review\avd_r5_script'
$stub='D:\remote\remote\.claude\caplife\avd_guard_r5_snapshot\stubs\stub_avd.py'
$py=(Get-Command python).Source
foreach($case in 'stderr','missing'){
 $root=Join-Path $base ($case+'-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $root|Out-Null
 $env:KE_STUB_AVD_STDERR='1';$env:KE_STUB_AVD_SEC='0'
 [string[]]$AvdCmd=if($case -eq 'stderr'){@($py,'-I',$stub)}else{@('D:\remote\remote\.claude\codex_ka_e_review\avd_r5_script\no-such-exe.exe')}
 $LASTEXITCODE=0;$avdExit=99
 $caught=$false
 try {
  $savedPref = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    if ($AvdCmd.Count -eq 0) {
      $env:KA_SERIAL = 'emulator-5554'
      & python -I "$e\ke_avd.py" $root "$e\ke-off.apk" "$e\ke-on.apk" "$root\avd" > "$root\avd_stdout.txt" 2> "$root\avd_stderr.txt"
    } else {
      $avdArgs = @($AvdCmd | Select-Object -Skip 1) + @($root)
      & $AvdCmd[0] @avdArgs > "$root\avd_stdout.txt" 2> "$root\avd_stderr.txt"
    }
    $avdExit = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $savedPref
  }

 } catch { $caught=$true }
 $stdoutExists=Test-Path -LiteralPath "$root\avd_stdout.txt"
 $stderrExists=Test-Path -LiteralPath "$root\avd_stderr.txt"
 $stderr=if($stderrExists){Get-Content -LiteralPath "$root\avd_stderr.txt" -Raw}else{''}
 "case=$case rc=$avdExit launchCaught=$caught preference=$ErrorActionPreference stdout=$stdoutExists stderr=$stderrExists traceStart=$($stderr -match 'Traceback') traceEnd=$($stderr -match 'RuntimeError: stub:')"
}
