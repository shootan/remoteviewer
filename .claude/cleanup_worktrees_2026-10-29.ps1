# 다 쓴 git worktree 정리 2차 (remote_claude, 2026-10-29) — 사용자 실행용
# - 브랜치는 지우지 않는다(작업 폴더만). 커밋된 내용은 그대로 남는다.
# - 각 폴더는 지우기 전에 junction/reparse point 를 다시 검사하고, 하나라도 있으면 건너뛴다(webview2 SDK 보호).
# - 1차 실행 결과: verify-dirretry-base·base-6e5cd70·base-a95215e·feat-prelogin-service 처리됨.
# 실행: PowerShell 에서  & D:\remote\remote\.claude\cleanup_worktrees_2026-10-29.ps1

Set-Location D:\remote\remote
$env:GIT_ASK_YESNO = 'false'   # "Should I try again? (y/n)" 를 묻지 않는다
$ErrorActionPreference = 'Continue'

# A. 아직 git 에 등록된 다 쓴 worktree — git 으로 지운다(긴 경로 허용)
$registered = @(
  'D:/remote/remote/.claude/worktrees/release-0.2.135',
  'D:/remote/remote/.claude/worktrees/release-0.2.136',
  'D:/remote/remote/.claude/worktrees/release-0.2.137',
  'D:/remote/remote/.claude/worktrees/release-0.2.138',
  'D:/remote/remote/.claude/worktrees/release-0.2.139',
  'D:/remote/remote/.claude/worktrees/release-apk-0.2.23',
  'D:/remote/remote/.claude/worktrees/release-apk-0.2.24',
  'D:/remote/remote/.claude/worktrees/performance-20260922',
  'D:/remote/remote/.claude/worktrees/verify-inj',
  'D:/remote/remote/.claude/worktrees/verify-inj-b8'
)
# B. git 등록은 이미 풀렸는데 폴더가 남은 것 — 링크 검사 뒤 폴더째 지운다(긴 경로는 \\?\ 로)
$orphans = @(
  'D:\remote\remote\.claude\worktrees\feat-prelogin-ui',
  'D:\remote\remote\.claude\worktrees\feat-prelogin-service',
  'D:\remote\remote\.claude\worktrees\verify-c2-ef533f2',
  'C:\work\gnlink-wt\apk-prelogin-select',
  'C:\work\gnlink-wt\fix-capture-lifecycle'
)

function Has-Link($p) { @(Get-ChildItem -LiteralPath $p -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue).Count -gt 0 }

$sdk = 'D:\remote\remote\third_party\webview2'
$sdkBefore = (Get-ChildItem -LiteralPath $sdk -Recurse -File -Force | Measure-Object).Count
$leftover = @()

foreach ($w in $registered) {
  if (-not (Test-Path -LiteralPath $w)) { "already gone  $w"; continue }
  if (Has-Link $w) { "SKIP (link inside)  $w"; $leftover += $w; continue }
  $out = git -c core.longpaths=true worktree remove --force $w 2>&1
  $still = Test-Path -LiteralPath $w
  "rc=$LASTEXITCODE exists=$still  $w"
  if ($still) { $orphans += ($w -replace '/', '\') }
}
git worktree prune

foreach ($o in $orphans) {
  if (-not (Test-Path -LiteralPath $o)) { "already gone  $o"; continue }
  if (Has-Link $o) { "SKIP (link inside)  $o"; $leftover += $o; continue }
  cmd /c rd /s /q "\\?\$o" 2>&1 | Out-Null
  $still = Test-Path -LiteralPath $o
  "rd exists=$still  $o"
  if ($still) { $leftover += $o }
}
git worktree prune

$sdkAfter = (Get-ChildItem -LiteralPath $sdk -Recurse -File -Force | Measure-Object).Count
"webview2 SDK files before=$sdkBefore after=$sdkAfter (must be equal)"
if ($leftover.Count -gt 0) {
  "--- still there (in use by another program -> delete after a reboot):"
  $leftover | ForEach-Object { "    $_" }
}
"C free GB: $([math]::Round((Get-PSDrive C).Free/1GB,1))  D free GB: $([math]::Round((Get-PSDrive D).Free/1GB,1))"
