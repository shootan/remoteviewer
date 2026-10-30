# old-ref r1 (Codex codex_old_apk_lane_peer_lost_decision_r1.md, task_old_apk_ref_r1): the build folder one reference run
# takes (oa_slot.ps1 -BuildDir) -- the lane's own test tools and ONE chosen test host, copied into a NEW folder under
# <worktree>\.claude\oa_ref_build, every file hashed into MANIFEST.txt with where it came from.
#   the harness (remote60_mobile_abr_session_e2e_test.exe), the job runner (e2e_job_run.exe), the standby holder and its
#   viewer (remote60_ka_standby_holder_tool.exe, GNLinkViewerKaTest.exe): from -ToolsDir (the lane build)
#   GNLinkStreamPinnedSelectTest.exe: from -HostDir -- the old tree's (fae0e1f + safety + diag) or the lane's (53fced4 +
#   diag). The harness stages ONLY that exe (as GNLinkStream.exe in its own staging folder): no companion exe either way.
# Nothing is built or run here; an existing -Out is refused.
param(
  [Parameter(Mandatory = $true)][string]$ToolsDir,
  [Parameter(Mandatory = $true)][string]$HostDir,
  [Parameter(Mandatory = $true)][string]$Name,
  [string]$ToolsCommit = '', [string]$HostCommit = ''
)
$ErrorActionPreference = 'Stop'
$wt = (Resolve-Path "$PSScriptRoot\..\..").Path
$anchor = "$wt\.claude\oa_ref_build"
if ($Name -notmatch '^[A-Za-z0-9_-]{1,40}$') { throw "-Name must be plain ASCII" }
$out = "$anchor\$Name"
if (-not (Test-Path -LiteralPath $anchor)) { New-Item -ItemType Directory $anchor | Out-Null }
New-Item -ItemType Directory $out | Out-Null  # no -Force: an existing folder is refused
$tools = @('remote60_mobile_abr_session_e2e_test.exe', 'e2e_job_run.exe', 'remote60_ka_standby_holder_tool.exe', 'GNLinkViewerKaTest.exe')
$lines = @("old-ref build folder $Name $(Get-Date -Format s)", "  tools from $ToolsDir (commit $ToolsCommit)", "  host from $HostDir (commit $HostCommit)")
foreach ($t in $tools) {
  Copy-Item -LiteralPath "$ToolsDir\$t" -Destination "$out\$t"
  $lines += "$((Get-FileHash "$out\$t").Hash.ToLower())  $t  (tools)"
}
Copy-Item -LiteralPath "$HostDir\GNLinkStreamPinnedSelectTest.exe" -Destination "$out\GNLinkStreamPinnedSelectTest.exe"
$lines += "$((Get-FileHash "$out\GNLinkStreamPinnedSelectTest.exe").Hash.ToLower())  GNLinkStreamPinnedSelectTest.exe  (host)"
# the host carries the test-only close record and the power switch -- by its own bytes (strings), not by its name
$bytes = [IO.File]::ReadAllBytes("$out\GNLinkStreamPinnedSelectTest.exe")
$ascii = [Text.Encoding]::ASCII.GetString($bytes)
foreach ($probe in @('diag{ack seen=', 'host power management=off', 'pinned_test_seam_refused')) {
  $lines += "  host strings: '$probe' x$([regex]::Matches($ascii, [regex]::Escape($probe)).Count)"
}
$lines | Out-File -Encoding utf8 "$out\MANIFEST.txt"
$lines
"build folder: $out"
