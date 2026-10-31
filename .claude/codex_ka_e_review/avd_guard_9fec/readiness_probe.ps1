$pr = [pscustomobject]@{ HasExited = $true; Id = 4242; ExitCode = 2 }
$hid = 'probe-b'; $ReadyTimeoutSec = 5
$ready = 'ready run=probe-b harness_pid=4242 host_pid=4243 host_log=D:\remote\remote\own.log'
  $why = $null
  if (-not $ready -and $pr.HasExited) { $why = "its harness ended before any readiness (exit $($pr.ExitCode))" }
  elseif (-not $ready) { $why = "no readiness within $ReadyTimeoutSec s" }
  elseif ($ready -notmatch "^ready run=$([regex]::Escape($hid)) harness_pid=$($pr.Id) host_pid=\d+ host_log=\S+") {
    $why = "a readiness that is not this run's ready one: $($ready.Trim())"
  }

"ready_valid=True harness_exited=$($pr.HasExited) rejection_set=$([bool]$why)"
if ($why) { exit 1 }
