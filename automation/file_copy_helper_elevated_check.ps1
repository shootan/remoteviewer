# The one elevated run of the clipboard helper's launch path (file-copy-helper r1, plan §5).
#
# Everything else about the helper is verified without elevation (remote60_file_copy_helper_e2e_test
# runs it as this user on a private window station). What only an elevated process can show is
# the product's own launch: the current shell's primary token accepted as the interactive user
# (helper-shell-token r1 -- 0.2.146 used TokenLinkedToken and failed with 1346), the pipe made by a
# High process with a Medium label, CreateProcessWithTokenW under SeImpersonate, the Job, the
# handshake, one StatFiles, Shutdown. This script asks for that elevation ONCE, runs the check, and
# prints its log. It runs the test build's binaries only: no installed GNLink file or process.
#
# It puts nothing on the clipboard and touches no user file: the check stats the test executable
# itself and tells the helper to shut down.
#
#   powershell -ExecutionPolicy Bypass -File automation\file_copy_helper_elevated_check.ps1 `
#       -BuildDir D:\remote\remote\.claude\worktrees\feat-helper-shell-token\build-local
param(
  [string]$BuildDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build-local'),
  [string]$Config = 'Release'
)
$bin = Join-Path $BuildDir "apps\native_poc\$Config"
$exe = Join-Path $bin 'remote60_file_copy_helper_e2e_test.exe'
$helper = Join-Path $bin 'GNLinkClipHelper.exe'
foreach ($f in @($exe, $helper)) {
  if (-not (Test-Path $f)) { Write-Error "missing: $f (build remote60_file_copy_helper_e2e_test first)"; exit 2 }
}
$log = Join-Path $bin 'file_copy_helper_elevated_check.log'
if (Test-Path $log) { Remove-Item $log }
Write-Host "UAC prompt follows: the check runs elevated, once. Log: $log"
$p = Start-Process -FilePath $exe -ArgumentList @('--elevated-check', '--log', "`"$log`"") -Verb RunAs -PassThru -Wait
Write-Host "exit code: $($p.ExitCode)"
if (Test-Path $log) { Get-Content $log } else { Write-Host '(no log written)' }
exit $p.ExitCode
