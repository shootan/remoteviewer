# Deploys the remote60 directory service to a Linux host over SSH.
#
# The service is tiny and stateless apart from one JSON file, so deployment is: copy the
# sources, make sure a Node runtime exists, and register a systemd unit that restarts it.
# Nothing here needs the repository, so the same script re-runs safely to update.
#
# Requires key-based SSH access; see docs for the one-time key install.

param(
  [string]$ServerHost = "223.130.132.180",
  [int]$Port = 2244,
  [string]$User = "shotan",
  [string]$KeyPath = "$env:USERPROFILE\.ssh\remote60_deploy",
  [int]$HttpPort = 8080,
  [int]$UdpPort = 8081,
  [string]$InstallDir = "/opt/remote60-directory",
  [switch]$SkipService
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$sourceDir = Join-Path $repoRoot "apps\directory"
if (-not (Test-Path (Join-Path $sourceDir "server.js"))) {
  throw "directory sources not found at $sourceDir"
}

$sshArgs = @("-i", $KeyPath, "-p", "$Port", "-o", "StrictHostKeyChecking=accept-new", "-o", "BatchMode=yes")
$target = "$User@$ServerHost"

# stderr is deliberately not merged into the pipeline: Windows PowerShell turns a native
# command's stderr into error records, so a harmless banner like "your password will expire"
# would abort the deployment. The exit code is the only signal that matters here.
#
# Scripts travel as a file rather than as an ssh argument. Windows PowerShell rewrites quoting
# when it builds a native command line -- an empty `""` disappears entirely -- which silently
# corrupted every multi-line script sent this way.
$scpArgs = @("-i", $KeyPath, "-P", "$Port", "-o", "StrictHostKeyChecking=accept-new", "-o", "BatchMode=yes")
$stepScript = Join-Path ([System.IO.Path]::GetTempPath()) "remote60-deploy-step.sh"

function Invoke-Remote([string]$Command) {
  $body = ($Command -replace "`r`n", "`n")
  if (-not $body.EndsWith("`n")) { $body += "`n" }
  # No BOM and LF endings: bash treats either as part of the first command.
  [System.IO.File]::WriteAllText($stepScript, $body, (New-Object System.Text.UTF8Encoding($false)))

  & scp @scpArgs $stepScript "${target}:.remote60-deploy-step.sh"
  if ($LASTEXITCODE -ne 0) { throw "failed to upload deploy step" }

  $output = & ssh @sshArgs $target "bash .remote60-deploy-step.sh"
  if ($LASTEXITCODE -ne 0) {
    throw "remote command failed ($LASTEXITCODE):`n$Command`n---`n$output"
  }
  return $output
}

Write-Host "[deploy] checking connectivity to $target"
$whoami = Invoke-Remote "id -un && uname -sr"
Write-Host "[deploy] connected as: $($whoami -join ' / ')"

# --- node runtime -----------------------------------------------------------------
# A distro package would need sudo and drags in a random version; the official tarball
# unpacked into the user's home works everywhere and needs no privileges.
$nodeCheck = & ssh @sshArgs $target "command -v node >/dev/null 2>&1 && node -v || echo MISSING"
$nodeVersion = ($nodeCheck | Select-Object -Last 1).ToString().Trim()
if ($nodeVersion -eq "MISSING" -or $nodeVersion -notmatch '^v(1[89]|2[0-9])') {
  Write-Host "[deploy] installing a local node runtime (found: $nodeVersion)"
  $installNode = @'
set -e
ARCH=$(uname -m)
case "$ARCH" in
  x86_64) NODE_ARCH=x64 ;;
  aarch64) NODE_ARCH=arm64 ;;
  *) echo "unsupported arch $ARCH" >&2; exit 1 ;;
esac
NODE_VER=v20.18.1
cd "$HOME"
if [ ! -d "$HOME/node-$NODE_VER" ]; then
  curl -fsSL "https://nodejs.org/dist/$NODE_VER/node-$NODE_VER-linux-$NODE_ARCH.tar.xz" -o /tmp/node.tar.xz
  tar -xf /tmp/node.tar.xz -C "$HOME"
  mv "$HOME/node-$NODE_VER-linux-$NODE_ARCH" "$HOME/node-$NODE_VER"
  rm -f /tmp/node.tar.xz
fi
"$HOME/node-$NODE_VER/bin/node" -v
'@
  $installNode = $installNode -replace "`r`n", "`n"
  $result = Invoke-Remote $installNode
  Write-Host "[deploy] node ready: $($result | Select-Object -Last 1)"
  $nodeBin = "`$HOME/node-v20.18.1/bin/node"
} else {
  Write-Host "[deploy] using existing node $nodeVersion"
  $nodeBin = (& ssh @sshArgs $target "command -v node" | Select-Object -Last 1).ToString().Trim()
}

# --- sources ----------------------------------------------------------------------
# Staged under the home directory first: the install dir may be root-owned.
Write-Host "[deploy] copying sources"
Invoke-Remote "rm -rf ~/remote60-directory-stage && mkdir -p ~/remote60-directory-stage"
& scp @scpArgs (Join-Path $sourceDir "server.js") (Join-Path $sourceDir "package.json") (Join-Path $sourceDir "README.md") "${target}:~/remote60-directory-stage/"
if ($LASTEXITCODE -ne 0) { throw "scp failed" }

$canSudo = $false
& ssh @sshArgs $target "sudo -n true" | Out-Null
if ($LASTEXITCODE -eq 0) { $canSudo = $true }
Write-Host "[deploy] passwordless sudo: $canSudo"

if (-not $canSudo) {
  # Without sudo, everything lives under the user's home and runs from a user service.
  $InstallDir = "`$HOME/remote60-directory"
}

$deployScript = @"
set -e
INSTALL_DIR="$InstallDir"
SUDO=""
if [ "$($canSudo.ToString().ToLower())" = "true" ]; then SUDO="sudo"; fi
`$SUDO mkdir -p "`$INSTALL_DIR"
`$SUDO cp ~/remote60-directory-stage/server.js ~/remote60-directory-stage/package.json ~/remote60-directory-stage/README.md "`$INSTALL_DIR/"
`$SUDO chown -R "`$(id -un):`$(id -gn)" "`$INSTALL_DIR" 2>/dev/null || true
rm -rf ~/remote60-directory-stage
echo "installed to `$INSTALL_DIR"
"@
Invoke-Remote ($deployScript -replace "`r`n", "`n")

if ($SkipService) {
  Write-Host "[deploy] service registration skipped"
  exit 0
}

# --- service ----------------------------------------------------------------------
$unitBody = @"
[Unit]
Description=remote60 directory service
After=network-online.target

[Service]
Environment=REMOTE60_DIR_PORT=$HttpPort
Environment=REMOTE60_DIR_UDP_PORT=$UdpPort
Environment=REMOTE60_DIR_DATA=DATA_PATH_PLACEHOLDER
ExecStart=NODE_PLACEHOLDER INSTALL_PLACEHOLDER/server.js
Restart=always
RestartSec=3
WorkingDirectory=INSTALL_PLACEHOLDER

[Install]
WantedBy=WANTED_PLACEHOLDER
"@

if ($canSudo) {
  $serviceScript = @"
set -e
INSTALL_DIR="$InstallDir"
cat > /tmp/remote60-directory.service <<'UNIT'
$unitBody
UNIT
sed -i "s#NODE_PLACEHOLDER#$nodeBin#g; s#INSTALL_PLACEHOLDER#`$INSTALL_DIR#g; s#DATA_PATH_PLACEHOLDER#`$INSTALL_DIR/directory-data.json#g; s#WANTED_PLACEHOLDER#multi-user.target#g" /tmp/remote60-directory.service
sudo mv /tmp/remote60-directory.service /etc/systemd/system/remote60-directory.service
sudo systemctl daemon-reload
sudo systemctl enable remote60-directory
sudo systemctl restart remote60-directory
sleep 2
sudo systemctl is-active remote60-directory
"@
} else {
  $serviceScript = @"
set -e
INSTALL_DIR="`$HOME/remote60-directory"
mkdir -p "`$HOME/.config/systemd/user"
cat > "`$HOME/.config/systemd/user/remote60-directory.service" <<'UNIT'
$unitBody
UNIT
sed -i "s#NODE_PLACEHOLDER#$nodeBin#g; s#INSTALL_PLACEHOLDER#`$INSTALL_DIR#g; s#DATA_PATH_PLACEHOLDER#`$INSTALL_DIR/directory-data.json#g; s#WANTED_PLACEHOLDER#default.target#g" "`$HOME/.config/systemd/user/remote60-directory.service"
# Without this the user service dies at logout, which would stop the host from being findable.
loginctl enable-linger "`$(id -un)" 2>/dev/null || true
systemctl --user daemon-reload
systemctl --user enable remote60-directory
# restart, not `enable --now`: the latter leaves an already-running old build serving.
systemctl --user restart remote60-directory
sleep 2
systemctl --user is-active remote60-directory
"@
}

Write-Host "[deploy] registering service"
$state = Invoke-Remote ($serviceScript -replace "`r`n", "`n")
Write-Host "[deploy] service state: $($state | Select-Object -Last 1)"

$health = & ssh @sshArgs $target "curl -fsS --max-time 5 http://127.0.0.1:$HttpPort/healthz || echo UNREACHABLE"
Write-Host "[deploy] healthz: $($health | Select-Object -Last 1)"
Write-Host "[deploy] done. http=$HttpPort udp=$UdpPort"
Write-Host "[deploy] the cloud firewall (ACG) must allow inbound TCP $HttpPort and UDP $UdpPort"
