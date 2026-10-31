# rel-0164 old-APK lane r6: the REAL fixture (server.js + the guarded test host oa-pc-a + the standby holder with the
# real managed viewer), NO emulator: oa_slot.ps1's AVD step is a stand-in that only waits (stubs\stub_avd.py, 20 s),
# so the bundle runs its whole course -- fixture readiness (hosts=a, standby=ready), the hold, the end -- with no device
# touched. Run under sh_run.ps1 -NativeExe (the slot, CreateNew). Called WITHOUT redirection (the fixture's children).
$e = 'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane'
$b = 'C:\work\gnlink-wt\old-apk-lane\build\apps\native_poc\Release'
$py = (Get-Command python).Source
$env:OA_STUB_AVD_SEC = '20'
"fixture-only check start $((Get-Date).ToString('s'))"
& powershell -NoProfile -ExecutionPolicy Bypass -File "$e\oa_slot.ps1" -Tag oa-fxonly -Arm P -WithStandby `
  -BuildDir $b -JobRun "$b\e2e_job_run.exe" -ServerJs 'C:\work\gnlink-wt\old-apk-lane\apps\directory\server.js' `
  -AvdCmd "$py,-I,$e\stubs\stub_avd.py"
"oa_slot rc=$LASTEXITCODE end $((Get-Date).ToString('s'))"
Remove-Item env:OA_STUB_AVD_SEC -ErrorAction SilentlyContinue
exit 0
