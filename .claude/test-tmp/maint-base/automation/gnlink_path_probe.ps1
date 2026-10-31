# Answers two questions about a path, for gnlink_release.sh, without being handed a command line.
#
# It exists because the shell script used to build a PowerShell -Command string with the path
# interpolated into single quotes. A path containing a quote, a dollar sign or a newline is then
# not data any more -- it is script. These are the paths a release deletes nothing at and creates
# everything under, so that is not a theoretical objection.
#
# Called with -File and real arguments, which PowerShell passes as values.
#
#   gnlink_path_probe.ps1 -Mode reparse -Root <dir> -Path <dir>
#       Walks from <Path> up to <Root> looking for a reparse point (a junction or a symlink).
#       Prints: none | reparse:<path> | error:<why>
#
#   gnlink_path_probe.ps1 -Mode real -Path <dir>
#       Prints the fully resolved filesystem path, or error:<why>.
#
# "error" is a REFUSAL, not a shrug. The earlier version used -ErrorAction SilentlyContinue and
# treated anything it could not read as safe, which is the wrong direction for a check whose whole
# job is to stop a delete or a create from landing somewhere unexpected.

param(
  [Parameter(Mandatory = $true)][ValidateSet('reparse', 'real')][string]$Mode,
  [string]$Root,
  [Parameter(Mandatory = $true)][string]$Path
)

$ErrorActionPreference = 'Stop'

function Resolve-Full {
  param([string]$p)
  # GetFullPath folds . and .. textually. It does not touch the disk, which is what we want here:
  # a path that does not exist yet still has to be judged.
  return [System.IO.Path]::GetFullPath($p)
}

try {
  if ($Mode -eq 'real') {
    $item = Get-Item -LiteralPath $Path -Force
    # The resolved target, so a junction reports where it actually goes.
    if ($item.PSObject.Properties['Target'] -and $item.Target) {
      Write-Output ([System.IO.Path]::GetFullPath($item.Target))
    } else {
      Write-Output $item.FullName
    }
    exit 0
  }

  if (-not $Root) { Write-Output 'error:no root given'; exit 1 }
  $rootFull = Resolve-Full $Root
  $p = Resolve-Full $Path

  while ($p -and $p.Length -ge $rootFull.Length) {
    if (Test-Path -LiteralPath $p) {
      # Get-Item without SilentlyContinue: a path that exists but cannot be read is an answer
      # this script must not give, so it throws and the catch below reports the refusal.
      $item = Get-Item -LiteralPath $p -Force
      if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
        Write-Output ("reparse:" + $p)
        exit 0
      }
    }
    $parent = [System.IO.Path]::GetDirectoryName($p)
    if ($parent -eq $p -or -not $parent) { break }
    $p = $parent
  }
  Write-Output 'none'
  exit 0
} catch {
  Write-Output ("error:" + $_.Exception.Message.Replace("`r", ' ').Replace("`n", ' '))
  exit 1
}
