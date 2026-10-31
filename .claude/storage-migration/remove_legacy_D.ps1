param([switch]$Execute)
$ErrorActionPreference = 'Stop'
$scope = [IO.Path]::GetFullPath('D:\remote').TrimEnd('\')
$expectedScope = 'D:\remote'
if (-not $scope.Equals($expectedScope, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected deletion scope' }
$targets = @('build', 'download', 'remote-worktrees', 'remote')
$reportPath = 'E:\remote-output\migration\20261031-storage\deletion_result.json'
$preflight = Get-Content -LiteralPath 'E:\remote-output\migration\20261031-storage\predelete_check.json' -Raw | ConvertFrom-Json
if ($preflight.predelete_check -ne 'PASS') { throw 'Missing successful predelete verification' }
if (-not (Test-Path -LiteralPath 'C:\work\remote\.git' -PathType Container)) { throw 'C repository is missing' }
if (-not (Test-Path -LiteralPath $preflight.archive -PathType Leaf)) { throw 'E backup is missing' }
if ((Get-Item -LiteralPath $scope -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Deletion scope is a reparse point' }
$children = @(Get-ChildItem -LiteralPath $scope -Force | Select-Object -ExpandProperty Name)
if (@(Compare-Object ($targets | Sort-Object) ($children | Sort-Object)).Count -ne 0) { throw 'Unexpected top-level contents; stop' }
foreach ($name in $targets) {
    $candidate = [IO.Path]::GetFullPath((Join-Path $scope $name))
    if (-not $candidate.StartsWith($scope + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Target escaped named scope' }
    $item = Get-Item -LiteralPath $candidate -Force
    if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unexpected target type' }
}
$users = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -notmatch '^(pwsh|powershell)\.exe$' -and
    ($_.ExecutablePath + ' ' + $_.CommandLine) -match '(?i)D:[/\\]remote(?:[/\\"\s]|$)'
})
if ($users.Count -ne 0) { throw 'A process still references the old location' }
if (-not $Execute) { Write-Output 'Validated named absolute targets; pass -Execute to delete.'; exit 0 }

$script:files = 0L
$script:directories = 0L
$script:links = 0L
$script:bytes = 0L
$result = [ordered]@{ scope=$scope; started=(Get-Date).ToString('o'); deleted_roots=@(); files=0; directories=0; links=0; bytes=0; product_work_still_stopped=$true; push_executed=$false; success=$false }

function Remove-EntryWithoutFollowingLinks([string]$path) {
    $absolute = [IO.Path]::GetFullPath($path)
    if (-not $absolute.StartsWith($scope + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Entry escaped named scope' }
    $attributes = [IO.File]::GetAttributes($absolute)
    $isDirectory = [bool]($attributes -band [IO.FileAttributes]::Directory)
    if ($attributes -band [IO.FileAttributes]::ReparsePoint) {
        # Delete only the directory entry. Never enumerate or resolve its target.
        if ($isDirectory) { [IO.Directory]::Delete($absolute, $false) }
        else { [IO.File]::Delete($absolute) }
        $script:links++
    } elseif ($isDirectory) {
        foreach ($child in [IO.Directory]::EnumerateFileSystemEntries($absolute)) {
            Remove-EntryWithoutFollowingLinks $child
        }
        if ($attributes -band [IO.FileAttributes]::ReadOnly) {
            [IO.File]::SetAttributes($absolute, ($attributes -band (-bnot [IO.FileAttributes]::ReadOnly)))
        }
        [IO.Directory]::Delete($absolute, $false)
        $script:directories++
    } else {
        $length = (New-Object IO.FileInfo($absolute)).Length
        if ($attributes -band [IO.FileAttributes]::ReadOnly) {
            [IO.File]::SetAttributes($absolute, ($attributes -band (-bnot [IO.FileAttributes]::ReadOnly)))
        }
        [IO.File]::Delete($absolute)
        $script:files++
        $script:bytes += $length
        if (($script:files % 10000) -eq 0) { Write-Output ('Deleted files: ' + $script:files) }
    }
}

try {
    foreach ($name in $targets) {
        $path = Join-Path $scope $name
        Remove-EntryWithoutFollowingLinks $path
        if (Test-Path -LiteralPath $path) { throw ('Target still exists: ' + $path) }
        $result.deleted_roots += $path
        Write-Output ('Removed named target: ' + $path)
    }
    if (@([IO.Directory]::EnumerateFileSystemEntries($scope)).Count -ne 0) { throw 'Parent is not empty' }
    [IO.Directory]::Delete($scope, $false)
    $result.success = $true
} catch {
    $result.error = $_.Exception.Message
    throw
} finally {
    $result.files = $script:files
    $result.directories = $script:directories
    $result.links = $script:links
    $result.bytes = $script:bytes
    $result.finished = (Get-Date).ToString('o')
    $json = $result | ConvertTo-Json -Depth 5
    [IO.File]::WriteAllText($reportPath, $json, (New-Object Text.UTF8Encoding($false)))
    Write-Output $json
}
