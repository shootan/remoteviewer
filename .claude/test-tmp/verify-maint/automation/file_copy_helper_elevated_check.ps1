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
# r3: the 14:20 field run reached CreateProcessWithTokenW and failed err=5 (ACCESS_DENIED) with the
# minimal dup-token mask. The check now first runs a MATRIX -- variant=a..f -- each the same product
# launch function with one parameter changed (dup mask, create flags, lpDesktop, work dir), so this
# one run says which difference from the updater's launch_as_shell_user matters. Then it runs the
# product default (MAXIMUM_ALLOWED mask, CREATE_SUSPENDED kept) end to end. Expect every variant line
# and then PASS for the full launch; variant=a (the field combo) is expected to still show err=5.
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
