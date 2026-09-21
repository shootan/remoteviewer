# Runs the update suites for a release candidate and judges them on more than the exit code.
#
# WHY THIS EXISTS. A suite returning 0 does not mean it ran. update_stop_process_test guards three
# checks behind a precondition, and when that precondition was not met those three simply did not
# run -- 108 of 111, exit 0 on an earlier build, and nothing in the output said a group had been
# skipped. Packaging verification that reads only the return code would have accepted it.
#
# So each suite is checked for three things:
#   * it exited 0;
#   * it ran at least the number of checks it is expected to, with nothing skipped;
#   * the specific cases that carry the release claim appear, and appear as PASS.
#
# The counts are MINIMUMS. Adding a check must not fail packaging; losing one must.
#
# Usage:
#   gnlink_update_suites.ps1 [-BuildDir <path>] [-RequireReal] [-Verbose]
#
#   -RequireReal   the service fixture's part 2 must have run (elevation). Without it, part 2
#                  reports BLOCKED and that is tolerated here but never counted as a pass.

param(
  [string]$BuildDir = "D:\remote\remote-worktrees\host-pc-recovery\build-local\apps\native_poc\Release",
  [switch]$RequireReal
)

$ErrorActionPreference = "Stop"

# name -> minimum checks, whether SKIP lines are legitimate, and the cases that must be present.
# A required case is matched as a substring of the check name, and must be on a PASS line.
$suites = @(
  @{ Name = "remote60_update_effects_test"; Min = 302; Skips = $false; Required = @(
       "a hundred attempts abandoned before the second open do not accumulate handles",
       "an unconfirmable second target abandons the attempt") },
  @{ Name = "remote60_update_stop_process_test"; Min = 113; Skips = $false; Required = @(
       "integration: the real attempt abandons on a process that stays",
       "integration: a process that leaves during the settle lets the attempt through",
       "a signal on another block's event does not release this block's child",
       "another owner's note is left alone by the sweep") },
  @{ Name = "remote60_update_identity_match_test"; Min = 66; Skips = $true; Required = @(
       "a handle whose query fails is Unknown -- not Different, not Same",
       "a handle to a process that EXITED is Same, not Unknown",
       'no error but 87 is read as "the pid is not a process"') },
  @{ Name = "remote60_update_service_stop_test"; Min = 30; Skips = $true; Required = @(
       "an accepted stop whose process has not gone does not swap") },
  @{ Name = "remote60_test_scratch_dir_test"; Min = 60; Skips = $true; Required = @(
       "a junction in the MIDDLE of the path is refused",
       "a link above the candidate root is refused before anything is created",
       "two calls to scratch_run_dir() denote one object, not two copies") },
  @{ Name = "remote60_updater_scenarios_test"; Min = 142; Skips = $false; Required = @() },
  @{ Name = "remote60_update_relaunch_test"; Min = 114; Skips = $false; Required = @() },
  @{ Name = "remote60_update_release_test"; Min = 80; Skips = $false; Required = @() },
  @{ Name = "remote60_updater_assembly_test"; Min = 49; Skips = $false; Required = @() },
  @{ Name = "remote60_update_rollback_safety_test"; Min = 18; Skips = $false; Required = @() },
  @{ Name = "remote60_update_readiness_test"; Min = 39; Skips = $false; Required = @() },
  @{ Name = "remote60_update_state_machine_test"; Min = 115; Skips = $false; Required = @() },
  @{ Name = "remote60_host_app_log_test"; Min = 17; Skips = $false; Required = @() },
  @{ Name = "remote60_control_resume_test"; Min = 48; Skips = $false; Required = @() },
  @{ Name = "remote60_host_diag_log_test"; Min = 27; Skips = $false; Required = @() }
)

$problems = @()
$ran = 0

foreach ($suite in $suites) {
  $exe = Join-Path $BuildDir ($suite.Name + ".exe")
  if (-not (Test-Path $exe)) {
    $problems += ("{0}: not built" -f $suite.Name)
    continue
  }
  $hash = (Get-FileHash $exe -Algorithm SHA256).Hash.ToLower()
  $out = & $exe 2>&1
  $code = $LASTEXITCODE
  $ran++

  $pass = @($out | Where-Object { $_ -like 'PASS*' }).Count
  $fail = @($out | Where-Object { $_ -like 'FAIL*' }).Count
  $skip = @($out | Where-Object { $_ -like 'SKIP*' }).Count
  $blocked = @($out | Where-Object { $_ -like 'BLOCKED*' }).Count

  $verdict = @()
  if ($code -ne 0) { $verdict += ("exit=0x{0:X8}" -f $code) }
  if ($fail -ne 0) { $verdict += "$fail failed" }
  # The point of the whole script: fewer checks than expected is a failure even at exit 0.
  if ($pass -lt $suite.Min) { $verdict += ("only $pass checks, expected at least " + $suite.Min) }
  if ($skip -gt 0 -and -not $suite.Skips) { $verdict += "$skip skipped, and this suite has no legitimate skips" }
  # A guarded group that did not run says so itself now -- but only in the SUMMARY line. Matching
  # the word anywhere flagged two suites that simply have checks about incomplete dependency sets
  # and incomplete quiesces, which is the runner inventing a failure rather than finding one.
  if ($out | Where-Object { $_ -match '^\S+:\s+INCOMPLETE' }) { $verdict += "reported INCOMPLETE" }

  foreach ($case in $suite.Required) {
    $seen = $out | Where-Object { $_ -like ('PASS*' + $case + '*') }
    if (-not $seen) { $verdict += ("required case missing or not passing: " + $case) }
  }

  if ($RequireReal -and $blocked -gt 0) { $verdict += "$blocked blocked, and -RequireReal was given" }

  $status = if ($verdict.Count -eq 0) { "ok " } else { "BAD" }
  Write-Output ("{0} {1,-40} pass={2,3} fail={3} skip={4} blocked={5}  {6}" -f
                $status, $suite.Name, $pass, $fail, $skip, $blocked, $hash.Substring(0, 16))
  foreach ($v in $verdict) {
    Write-Output ("      " + $v)
    $problems += ("{0}: {1}" -f $suite.Name, $v)
  }
}

Write-Output ""
Write-Output ("suites run: $ran of " + $suites.Count + ", problems: " + $problems.Count)
if ($problems.Count -ne 0) {
  Write-Output "NOT FIT FOR PACKAGING"
  exit 1
}
Write-Output "all suites ran in full and passed"
exit 0
