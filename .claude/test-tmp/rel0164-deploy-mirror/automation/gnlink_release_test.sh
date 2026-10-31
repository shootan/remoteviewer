#!/usr/bin/env bash
#
# Regression for automation/gnlink_release.sh and its two helpers.
#
# Everything here runs against fixtures: no compiler, no signing key, no network. What is checked
# is the part that decides whether a release is allowed to proceed -- argument handling, the
# preflight refusals, the shape of the manifest, and each gate's ability to say no. A release
# script that only ever gets exercised by successful releases has never been tested at all.
#
# Every guard is checked twice: once passing, and once with the thing it guards against actually
# present. A check that cannot fail is decoration.
#
#   automation/gnlink_release_test.sh
#
# Exit 0 = all checks passed.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RELEASE_SH="$SCRIPT_DIR/gnlink_release.sh"
MANIFEST_PY="$SCRIPT_DIR/gnlink_release_manifest.py"
PAYLOAD_SET_PY="$SCRIPT_DIR/gnlink_check_payload_set.py"

CHECKS=0
FAILURES=0

check() {
  CHECKS=$((CHECKS + 1))
  if [ "$1" = "0" ]; then
    printf 'PASS  %s\n' "$2"
  else
    FAILURES=$((FAILURES + 1))
    printf 'FAIL  %s%s\n' "$2" "${3:+  $3}"
  fi
}

expect_nonzero() {
  # $1 description, rest: command. Passes when the command REFUSES.
  local what="$1"; shift
  local out rc
  out="$("$@" 2>&1)"; rc=$?
  if [ "$rc" -ne 0 ]; then
    check 0 "$what"
  else
    check 1 "$what" "it returned 0 and should not have"
  fi
}

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Every fixture run goes through the CANDIDATE'S OWN copy of the release script, which
# make_worktree commits into the fixture. The script refuses to run from outside the worktree
# it releases (RV-13), and that refusal is tested below with the checkout's own copy. Runs with
# no usable --worktree (the argument checks) use the checkout's copy; they refuse before the
# worktree is looked at.
RUN_RELEASE="$TMP/run_release.sh"
{
  printf '#!/usr/bin/env bash\n'
  printf 'wt=""; prev=""\n'
  printf 'for a in "$@"; do [ "$prev" = "--worktree" ] && wt="$a"; prev="$a"; done\n'
  printf 'if [ -n "$wt" ] && [ -f "$wt/automation/gnlink_release.sh" ]; then\n'
  printf '  exec bash "$wt/automation/gnlink_release.sh" "$@"\n'
  printf 'fi\n'
  printf 'exec bash "%s" "$@"\n' "$RELEASE_SH"
} > "$RUN_RELEASE"

# ---------------------------------------------------------------- a worktree that looks real
# Enough of one for the preflight: a git repo, a version header, a clean tree.
make_worktree() {
  local dir="$1" version="$2"
  mkdir -p "$dir/apps/native_poc/src" "$dir/apps/native_poc/ui"
  cat > "$dir/apps/native_poc/src/product_version.hpp" <<EOF
#pragma once
namespace remote60::native_poc {
constexpr wchar_t kProductVersion[] = L"$version";
}
EOF
  # The tools the release script calls, from where it calls them: its own directory, and the
  # one source file the payload-set gate reads relative to it. A candidate carries its own.
  mkdir -p "$dir/automation"
  cp "$SCRIPT_DIR"/gnlink_release.sh "$SCRIPT_DIR"/gnlink_release_manifest.py \
     "$SCRIPT_DIR"/gnlink_check_payload_set.py "$SCRIPT_DIR"/gnlink_check_installer_payload.ps1 \
     "$SCRIPT_DIR"/gnlink_path_probe.ps1 "$SCRIPT_DIR"/gnlink_release_sign.ps1 \
     "$SCRIPT_DIR"/gnlink_verify_manifest.js "$SCRIPT_DIR"/gnlink_deploy.sh "$dir/automation/"
  cp "$SCRIPT_DIR/../apps/native_poc/src/update_process_targets.cpp" "$dir/apps/native_poc/src/"
  ( cd "$dir" && git init -q . && git add -A && \
    git -c user.email=t@t -c user.name=t commit -qm fixture ) >/dev/null 2>&1
}

printf '=== arguments\n'
expect_nonzero "no --worktree is refused" bash "$RUN_RELEASE" --version 0.2.134
expect_nonzero "no --version is refused" bash "$RUN_RELEASE" --worktree "$TMP"
expect_nonzero "a worktree that does not exist is refused" \
  bash "$RUN_RELEASE" --worktree "$TMP/nowhere" --version 0.2.134
expect_nonzero "a version that is not a version is refused" \
  bash "$RUN_RELEASE" --worktree "$TMP" --version "latest"
expect_nonzero "an unknown argument is refused" \
  bash "$RUN_RELEASE" --worktree "$TMP" --version 0.2.134 --publish-now

printf '\n=== preflight, fail-closed\n'
WT="$TMP/wt"
make_worktree "$WT" "0.2.134"

# The guard that matters most: the script must never paper over a version it was not asked for.
expect_nonzero "a version the source does not claim is refused" \
  env GNLINK_WEBVIEW2_SOURCE="$TMP/no-such-sdk" bash "$RUN_RELEASE" --worktree "$WT" --version 0.2.999
OUT="$(env GNLINK_WEBVIEW2_SOURCE="$TMP/no-such-sdk" bash "$RUN_RELEASE" --worktree "$WT" --version 0.2.999 2>&1)"
case "$OUT" in
  *"the source says 0.2.134"*) check 0 "...and says which version the source claims" ;;
  *) check 1 "...and says which version the source claims" "$(printf '%s' "$OUT" | tail -1)" ;;
esac

# Same worktree, matching version, but no SDK anywhere: it must stop rather than start a build
# that would fail later with an error naming neither WebView2 nor the reason.
expect_nonzero "no WebView2 SDK, and none to copy, is refused" \
  env GNLINK_WEBVIEW2_SOURCE="$TMP/no-such-sdk" bash "$RUN_RELEASE" --worktree "$WT" --version 0.2.134
OUT="$(env GNLINK_WEBVIEW2_SOURCE="$TMP/no-such-sdk" bash "$RUN_RELEASE" --worktree "$WT" --version 0.2.134 2>&1)"
case "$OUT" in
  *"fetch_webview2"*) check 0 "...and names the script that fetches it" ;;
  *) check 1 "...and names the script that fetches it" "$(printf '%s' "$OUT" | tail -1)" ;;
esac

# A dirty worktree cannot be released: the release would not name a commit.
DIRTY_WT="$TMP/dirty"
make_worktree "$DIRTY_WT" "0.2.134"
printf 'edited\n' >> "$DIRTY_WT/apps/native_poc/src/product_version.hpp"
expect_nonzero "uncommitted tracked changes are refused" \
  env GNLINK_WEBVIEW2_SOURCE="$TMP/no-such-sdk" bash "$RUN_RELEASE" --worktree "$DIRTY_WT" --version 0.2.134

printf '\n=== the manifest, against the shape 0.2.134 published\n'

# A payload of the right names with the right version strings in them. Not executables -- the
# manifest writer only reads bytes, and the parity checker only looks for UTF-16 version strings.
FAKE="$TMP/payload"
mkdir -p "$FAKE/ui"
python - "$FAKE" "0.2.134" <<'PYEOF'
import io, os, sys
payload, version = sys.argv[1], sys.argv[2]
counts = {'GNLinkHost.exe': 1, 'GNLinkClient.exe': 1, 'GNLinkViewer.exe': 1,
          'GNLinkStream.exe': 1, 'GNLinkSetup.exe': 5,
          'GNLinkCapture.exe': 0, 'GNLinkInputService.exe': 0, 'GNLinkUpdater.exe': 0,
          'GNLinkClipHelper.exe': 0}
for name, n in counts.items():
    # An EVEN number of filler bytes: UTF-16 is two bytes per character, and an odd prefix
    # shifts every string after it out of alignment.
    body = b'MZfixture' + b'!' + (version.encode('utf-16-le') + b'\x00\x00') * n
    io.open(os.path.join(payload, name), 'wb').write(body)
for html in ('shell.html', 'macro.html'):
    io.open(os.path.join(payload, 'ui', html), 'wb').write(b'<html>fixture</html>')
PYEOF

python "$MANIFEST_PY" write --payload "$FAKE" --version 0.2.134 --out "$TMP/windows.manifest" \
  >/dev/null 2>&1
check $? "the manifest writer produces a file"

python - "$TMP/windows.manifest" <<'PYEOF'
import io, re, sys
lines = io.open(sys.argv[1], encoding='utf-8').read().split('\n')
lines = [l for l in lines if l != '']
want_head = ['schema=2', 'releaseId=r-0.2.134', 'platform=windows', 'arch=x64', 'version=0.2.134']
order = ['GNLinkHost.exe', 'GNLinkStream.exe', 'GNLinkCapture.exe', 'GNLinkInputService.exe',
         'GNLinkClient.exe', 'GNLinkViewer.exe', 'GNLinkSetup.exe', 'GNLinkUpdater.exe',
         'GNLinkClipHelper.exe', 'ui' + chr(92) + 'shell.html', 'ui' + chr(92) + 'macro.html']
problems = []
if not lines[0].startswith('# GNLink update manifest -- release 0.2.134'):
    problems.append('header line')
if lines[1:6] != want_head:
    problems.append('head fields: %r' % (lines[1:6],))
arts = [l for l in lines if l.startswith('artifact=')]
if len(arts) != 11:
    problems.append('%d artifact lines, expected 11' % len(arts))
for i, line in enumerate(arts):
    fields = line[len('artifact='):].split('|')
    if len(fields) != 4:
        problems.append('line %d has %d fields' % (i, len(fields)))
        continue
    name, size, digest, url = fields
    if i < len(order) and name != order[i]:
        problems.append('position %d is %s, expected %s' % (i, name, order[i]))
    if not size.isdigit():
        problems.append('%s size is not a number' % name)
    if not re.fullmatch(r'[0-9a-f]{64}', digest):
        problems.append('%s digest is not a sha256' % name)
    if not url.startswith('https://gnlink.shotan.net/updates/0.2.134/'):
        problems.append('%s url is %s' % (name, url))
if problems:
    for p in problems:
        sys.stderr.write('  %s\n' % p)
    sys.exit(1)
PYEOF
check $? "...with 0.2.134's head fields, artifact order, four fields and https urls"

# And against the real thing, when a previous release is still on disk to compare with.
REAL_134=""
for candidate in \
  "D:/remote/remote/.claude/worktrees/main-stability-integration/.claude/rel/0.2.134/windows.manifest" \
  "D:/remote/remote-worktrees/_archive-2026-09-22/host-pc-recovery/.claude/rel/0.2.133/windows.manifest"; do
  [ -f "$candidate" ] && { REAL_134="$candidate"; break; }
done
if [ -n "$REAL_134" ]; then
  python - "$TMP/windows.manifest" "$REAL_134" <<'PYEOF'
import io, sys


def shape(path):
    out = []
    for line in io.open(path, encoding='utf-8').read().split('\n'):
        if not line:
            continue
        if line.startswith('artifact='):
            out.append('artifact=' + line[len('artifact='):].split('|')[0])
        elif line.startswith('#'):
            out.append('#')
        else:
            out.append(line.split('=')[0] + '=')
    return out


mine, real = shape(sys.argv[1]), shape(sys.argv[2])
# The one line added since 0.2.134 (file copy), and only that one: everything else must still
# line up with what was published.
if 'artifact=GNLinkClipHelper.exe' not in real:
    mine = [l for l in mine if l != 'artifact=GNLinkClipHelper.exe']
if mine != real:
    sys.stderr.write('  generated: %r\n  published: %r\n' % (mine, real))
    sys.exit(1)
PYEOF
  check $? "...and the same line structure as the last published manifest"
else
  printf 'SKIP  no published manifest on disk to compare against\n'
fi

printf '\n=== the gates say no when they should\n'

python "$MANIFEST_PY" parity --payload "$FAKE" --version 0.2.134 >/dev/null 2>&1
check $? "parity passes on a payload that claims the right version"

# Mutation: one binary still carries the previous release's number.
python - "$FAKE" <<'PYEOF'
import io, os, sys
p = os.path.join(sys.argv[1], 'GNLinkHost.exe')
io.open(p, 'ab').write('0.2.133'.encode('utf-16-le'))
PYEOF
expect_nonzero "parity refuses a binary carrying an older version" \
  python "$MANIFEST_PY" parity --payload "$FAKE" --version 0.2.134
python - "$FAKE" <<'PYEOF'
import io, os, sys
p = os.path.join(sys.argv[1], 'GNLinkHost.exe')
data = io.open(p, 'rb').read()
io.open(p, 'wb').write(data[:-len('0.2.133'.encode('utf-16-le'))])
PYEOF

# Mutation: a carrier that does not claim the version at all.
python - "$FAKE" <<'PYEOF'
import io, os, sys
io.open(os.path.join(sys.argv[1], 'GNLinkViewer.exe'), 'wb').write(b'MZ fixture with no version')
PYEOF
expect_nonzero "parity refuses a carrier with no version string" \
  python "$MANIFEST_PY" parity --payload "$FAKE" --version 0.2.134

# Mutation: the manifest is missing one of the names the update replaces.
python - "$TMP/windows.manifest" "$TMP/short.manifest" <<'PYEOF'
import io, sys
lines = [l for l in io.open(sys.argv[1], encoding='utf-8').read().split('\n')
         if not l.startswith('artifact=GNLinkSetup.exe')]
io.open(sys.argv[2], 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
PYEOF
expect_nonzero "the payload-set gate refuses a manifest missing GNLinkSetup.exe" \
  python "$PAYLOAD_SET_PY" "$TMP/short.manifest"

# Mutation: a packaged file changed after the checksums were written.
SUMDIR="$TMP/sums"
mkdir -p "$SUMDIR"
printf 'original\n' > "$SUMDIR/thing.bin"
( cd "$SUMDIR" && sha256sum ./thing.bin > SHA256SUMS.txt )
( cd "$SUMDIR" && sha256sum -c SHA256SUMS.txt ) >/dev/null 2>&1
check $? "checksums verify an untouched file"
printf 'tampered\n' > "$SUMDIR/thing.bin"
expect_nonzero "checksums refuse a file that changed after packaging" \
  bash -c "cd '$SUMDIR' && sha256sum -c SHA256SUMS.txt"

printf '\n=== deleting a tree: the boundary checks\n'
# The path functions are lifted out of the release script and driven directly. Sourcing the whole
# script would run a release; this runs those functions and nothing else. $2 stands in for the
# worktree, which is the only root they allow anything under.
RMDRIVE="$TMP/rmdrive.sh"
{
  printf 'set -u\n'
  printf 'die() { printf "FAIL  %%s\\n" "$*" >&2; exit 1; }\n'
  printf 'say() { printf "%%s\\n" "$*"; }\n'
  printf 'SCRIPT_DIR="%s"\n' "$SCRIPT_DIR"
  sed -n '/^PATH_PROBE=/p;/^canon_path()/,/^}$/p;/^normalise_path()/,/^}$/p;/^path_reparse_between()/,/^}$/p;/^path_real()/,/^}$/p;/^nearest_existing()/,/^}$/p;/^check_inside_worktree()/,/^}$/p;/^make_fresh_dir()/,/^}$/p' "$RELEASE_SH"
  printf 'WORKTREE_REAL="$(canon_path "$(cd "$2" && pwd -P)")"\n'
  printf 'check_inside_worktree "$1" "target"\n'
} > "$RMDRIVE"

RMROOT="$TMP/rmroot"
mkdir -p "$RMROOT/root/victim" "$RMROOT/outside"
printf 'do not delete me\n' > "$RMROOT/outside/precious.txt"

expect_nonzero "an empty path is refused"               bash "$RMDRIVE" "" "$RMROOT/root"
expect_nonzero "a relative path is refused"             bash "$RMDRIVE" "root/victim" "$RMROOT/root"
expect_nonzero "a filesystem root is refused"           bash "$RMDRIVE" "/" "$RMROOT/root"
expect_nonzero "the worktree itself is refused"         bash "$RMDRIVE" "$RMROOT/root" "$RMROOT/root"
expect_nonzero "a path outside the worktree is refused" bash "$RMDRIVE" "$RMROOT/outside" "$RMROOT/root"
expect_nonzero "an escape through .. is refused"        bash "$RMDRIVE" "$RMROOT/root/../outside" "$RMROOT/root"

[ -f "$RMROOT/outside/precious.txt" ]
check $? "...and nothing outside the worktree was touched by any of that"

# A path that does not exist yet is the normal case: these directories are about to be created.
# The check has to reach through to the nearest ancestor that does exist.
bash "$RMDRIVE" "$RMROOT/root/not/here/yet" "$RMROOT/root" >/dev/null 2>&1
check $? "a path that does not exist yet is allowed, if its ancestor is inside"

bash "$RMDRIVE" "$RMROOT/root/victim" "$RMROOT/root" >/dev/null 2>&1
rc=$?
[ "$rc" = "0" ] && [ -d "$RMROOT/root/victim" ]
check $? "a real directory inside the worktree is allowed, and still there afterwards"

# A junction is not a symlink as far as `test -L` goes, which is the whole reason the check asks
# the OS. Skipped rather than faked where one cannot be made.
JUNCTION_MADE=0
if powershell.exe -NoProfile -NonInteractive -Command \
     "New-Item -ItemType Junction -Path '$(cygpath -w "$RMROOT/root")\link' -Target '$(cygpath -w "$RMROOT/outside")' | Out-Null" >/dev/null 2>&1; then
  JUNCTION_MADE=1
fi
if [ "$JUNCTION_MADE" = "1" ]; then
  expect_nonzero "a junction pointing out of the worktree is refused" \
    bash "$RMDRIVE" "$RMROOT/root/link" "$RMROOT/root"
  [ -f "$RMROOT/outside/precious.txt" ]
  check $? "...and what it pointed at is still there"
  powershell.exe -NoProfile -NonInteractive -Command \
    "Remove-Item -LiteralPath '$(cygpath -w "$RMROOT/root")\link' -Force" >/dev/null 2>&1
else
  printf 'SKIP  junction check (could not create a junction here)\n'
fi

printf '\n=== signing: verify first, write once, dispose always\n'
# Against a THROWAWAY key generated for this run, in a directory of its own. The operational key
# is never read here, and nothing below prints key material of either kind.
SIGNDIR="$TMP/signing"
KEYDIR="$TMP/testkey"
mkdir -p "$SIGNDIR" "$KEYDIR"
printf 'version=0.2.134\nartifact=GNLinkHost.exe\n' > "$SIGNDIR/windows.manifest"

# CngKey::Create with an export policy is refused for an ephemeral key on this machine, so the
# key is made by the ordinary ECDsa API and the CNG private blob assembled from its parameters:
# BCRYPT_ECDSA_PRIVATE_P256_MAGIC, the key size, then X, Y and D. That is exactly the shape the
# signing script imports.
make_test_key() {
  local dir="$1"
  mkdir -p "$dir"
  powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "
  \$ErrorActionPreference = 'Stop'
  Add-Type -AssemblyName System.Security
  \$e = [System.Security.Cryptography.ECDsa]::Create(
    [System.Security.Cryptography.ECCurve]::CreateFromFriendlyName('nistP256'))
  \$k = \$e.ExportParameters(\$true)
  \$ms = New-Object System.IO.MemoryStream
  \$bw = New-Object System.IO.BinaryWriter(\$ms)
  \$bw.Write([int]0x32534345); \$bw.Write([int]32)
  \$bw.Write(\$k.Q.X); \$bw.Write(\$k.Q.Y); \$bw.Write(\$k.D)
  \$bw.Flush()
  \$prot = [System.Security.Cryptography.ProtectedData]::Protect(
    \$ms.ToArray(), \$null, [System.Security.Cryptography.DataProtectionScope]::CurrentUser)
  [System.IO.File]::WriteAllBytes((Join-Path '$(cygpath -w "$dir")' 'private.ecc.dpapi'), \$prot)
  \$hex = ([System.BitConverter]::ToString(\$k.Q.X + \$k.Q.Y) -replace '-','').ToLowerInvariant()
  [System.IO.File]::WriteAllText((Join-Path '$(cygpath -w "$dir")' 'public_xy.hex'), \$hex)
  \$bw.Dispose(); \$e.Dispose()" >/dev/null 2>&1
  [ -f "$dir/private.ecc.dpapi" ] && [ -f "$dir/public_xy.hex" ]
}

# `[ $? = 0 ]` after `KEY_MADE=0` read the assignment's status, which is always 0 -- so a key that
# could not be made still ran every signing check against nothing. (RV-14)
if make_test_key "$KEYDIR"; then KEY_MADE=1; else KEY_MADE=0; fi

# A second, unrelated key. Its public half is a valid point on the curve, which the earlier
# "corrupt two characters of the hex" trick was not -- that made ECDsa::Create throw before any
# signing happened, so the test passed without ever reaching the code it was aiming at. A mutant
# that wrote the .sig before verifying went undetected until this was fixed.
OTHERKEY="$TMP/otherkey"
make_test_key "$OTHERKEY" || KEY_MADE=0

# Every call pins the key the product trusts. For this test that is the throwaway key itself --
# which is the point: the pin is not optional, so a test that wants signing to succeed has to
# name the key doing the signing.
sign_run() {
  powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "$(cygpath -w "$SCRIPT_DIR/gnlink_release_sign.ps1")" \
    -ReleaseDir "$(cygpath -w "$SIGNDIR")" -KeyDir "$(cygpath -w "$KEYDIR")" \
    -TrustedKeyHex "$1" >/dev/null 2>&1
}

if [ "$KEY_MADE" = "1" ]; then
  sign_run "$(cat "$KEYDIR/public_xy.hex")"
  rc=$?
  [ "$rc" = "0" ] && [ -f "$SIGNDIR/windows.sig" ]
  check $? "a good key signs the manifest"

  [ "$(tr -d ' \r\n' < "$SIGNDIR/windows.sig" | wc -c)" = "128" ]
  check $? "...to 128 hex characters, a P-256 r||s"

  [ ! -f "$SIGNDIR/windows.sig.tmp" ]
  check $? "...and leaves no temporary behind"

  GOOD_SIG="$(cat "$SIGNDIR/windows.sig")"

  # The failure this reordering exists for. A public key that is perfectly valid but belongs to
  # a different key makes the verification return false; the old order had already written the
  # .sig by then, and the caller decides whether to publish by asking whether that file exists.
  cp "$KEYDIR/public_xy.hex" "$KEYDIR/public_xy.hex.bak"
  cp "$OTHERKEY/public_xy.hex" "$KEYDIR/public_xy.hex"
  rm -f "$SIGNDIR/windows.sig"
  sign_run "$(cat "$KEYDIR/public_xy.hex")"
  rc=$?
  [ "$rc" != "0" ]
  check $? "a signature that does not verify fails the run"

  [ ! -f "$SIGNDIR/windows.sig" ]
  check $? "...and writes no .sig at all, so --deploy cannot pick one up"

  [ ! -f "$SIGNDIR/windows.sig.tmp" ]
  check $? "...and leaves no temporary either"

  # An existing signature must survive a failed re-sign rather than being half-replaced.
  printf '%s' "$GOOD_SIG" > "$SIGNDIR/windows.sig"
  sign_run "$(cat "$KEYDIR/public_xy.hex")"
  [ "$(cat "$SIGNDIR/windows.sig")" = "$GOOD_SIG" ]
  check $? "a failed re-sign leaves the previous signature untouched"

  mv -f "$KEYDIR/public_xy.hex.bak" "$KEYDIR/public_xy.hex"

  # A key the product does not trust. The release would sign, self-verify and publish, and fail
  # on every machine that tried to install it -- so this has to stop before the key is used.
  rm -f "$SIGNDIR/windows.sig"
  sign_run "$(printf 'a%.0s' $(seq 128))"
  rc=$?
  [ "$rc" != "0" ]
  check $? "a signing key the product does not trust is refused"
  [ ! -f "$SIGNDIR/windows.sig" ]
  check $? "...and nothing is written"

  expect_nonzero "a trusted key of the wrong length is refused" \
    powershell.exe -NoProfile -ExecutionPolicy Bypass \
      -File "$(cygpath -w "$SCRIPT_DIR/gnlink_release_sign.ps1")" \
      -ReleaseDir "$(cygpath -w "$SIGNDIR")" -KeyDir "$(cygpath -w "$KEYDIR")" \
      -TrustedKeyHex "deadbeef"

  # The signature is over the manifest's BYTES. A manifest edited after signing must stop
  # verifying -- that is the whole guarantee, and it is worth pinning rather than assuming.
  rm -f "$SIGNDIR/windows.sig"
  printf 'version=0.2.134\nartifact=GNLinkHost.exe\n' > "$SIGNDIR/windows.manifest"
  sign_run "$(cat "$KEYDIR/public_xy.hex")"
  [ -f "$SIGNDIR/windows.sig" ]
  check $? "the manifest on disk is what gets signed"

  verify_sig() {
    # Independent of the signing script: the public key from the file, the manifest from disk.
    powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "
      \$hex = ([System.IO.File]::ReadAllText('$(cygpath -w "$KEYDIR")\public_xy.hex')).Trim()
      \$xy = New-Object byte[] 64
      for (\$i = 0; \$i -lt 64; \$i++) { \$xy[\$i] = [Convert]::ToByte(\$hex.Substring(\$i*2,2),16) }
      \$pr = New-Object System.Security.Cryptography.ECParameters
      \$pr.Curve = [System.Security.Cryptography.ECCurve]::CreateFromFriendlyName('nistP256')
      \$pt = New-Object System.Security.Cryptography.ECPoint
      \$pt.X = \$xy[0..31]; \$pt.Y = \$xy[32..63]
      \$pr.Q = \$pt
      \$v = [System.Security.Cryptography.ECDsa]::Create(\$pr)
      \$doc = [System.IO.File]::ReadAllBytes('$(cygpath -w "$SIGNDIR")\windows.manifest')
      \$sh = ([System.IO.File]::ReadAllText('$(cygpath -w "$SIGNDIR")\windows.sig')).Trim()
      \$sg = New-Object byte[] 64
      for (\$i = 0; \$i -lt 64; \$i++) { \$sg[\$i] = [Convert]::ToByte(\$sh.Substring(\$i*2,2),16) }
      \$ok = \$v.VerifyData(\$doc, \$sg, [System.Security.Cryptography.HashAlgorithmName]::SHA256)
      \$v.Dispose()
      if (\$ok) { exit 0 } else { exit 1 }" >/dev/null 2>&1
  }

  verify_sig
  check $? "...and the signature it wrote verifies against it"

  printf 'version=0.2.134\nartifact=GNLinkHost.exe.tampered\n' > "$SIGNDIR/windows.manifest"
  expect_nonzero "...and stops verifying the moment the manifest is edited" verify_sig

  # The trusted key the release script will actually pass: read from the product source the same
  # way gnlink_verify_manifest.js reads it. If that stops matching, signing stops working.
  PRODUCT_KEY="$(sed -n 's/.*return[[:space:]]*"\([0-9a-f]\{128\}\)"[[:space:]]*;.*/\1/p' \
    "$SCRIPT_DIR/../apps/native_poc/src/update_manifest.cpp" | head -1)"
  [ "${#PRODUCT_KEY}" = "128" ]
  check $? "the trusted key reads out of update_manifest.cpp as 128 hex characters" \
    "got ${#PRODUCT_KEY}"
else
  printf 'SKIP  signing checks (could not create a throwaway key here)\n'
fi

# The three objects that hold a CNG handle have to be released in finally, or a throw on the way
# -- a short signature, a key the product does not trust, a verification that failed -- skips it.
# Named individually rather than matching every Dispose: a local hash disposed on its own error
# path is not this defect.
FINALLY_LINE="$(grep -n '^finally {' "$SCRIPT_DIR/gnlink_release_sign.ps1" | head -1 | cut -d: -f1)"
EARLY="$(grep -nE '\$(signer|key|verifier)\.Dispose\(\)' "$SCRIPT_DIR/gnlink_release_sign.ps1" \
         | cut -d: -f1 | awk -v f="$FINALLY_LINE" '$1 < f')"
[ -z "$EARLY" ]
check $? "signer, key and verifier are disposed in finally, not on the success path" \
  "${EARLY:+lines $(printf '%s' "$EARLY" | tr '\n' ' ')}"

for obj in signer key verifier; do
  awk -v f="$FINALLY_LINE" -v o="$obj" 'NR > f && $0 ~ ("\\$" o "\\.Dispose\\(\\)") { found=1 }
                                        END { exit found ? 0 : 1 }' \
    "$SCRIPT_DIR/gnlink_release_sign.ps1"
  check $? "...and \$$obj is actually disposed there"
done

printf '\n=== the option table: eight combinations, no contradictions\n'
# Only one of the eight is refused, and it is refused at argument time rather than nine stages
# later. The rest are driven far enough to see that the arguments are accepted -- a real run
# needs a compiler, which this test does not have and does not want.
OPTWT="$TMP/optwt"
make_worktree "$OPTWT" "0.2.134"

opt_reaches_preflight() {
  # Accepted arguments get as far as the preflight and stop there on something else (no
  # WebView2, no compiler). Refused arguments never print the preflight banner.
  local out
  # Stopped by a version the source does not claim, which the preflight checks first. It used
  # to be stopped by GNLINK_WEBVIEW2_SOURCE pointing nowhere -- which is now itself a setting
  # that refuses --sign and --deploy (RV-11), so it can no longer stand in for "nothing else".
  out="$(bash "$RUN_RELEASE" --worktree "$OPTWT" --version 0.2.999 "$@" 2>&1)"
  printf '%s' "$out" | grep -q '1. preflight'
}

opt_reaches_preflight                             ; check $? "sign=no  deploy=no  dry=no   is accepted"
opt_reaches_preflight --dry-run                   ; check $? "sign=no  deploy=no  dry=yes  is accepted"
opt_reaches_preflight --deploy --dry-run          ; check $? "sign=no  deploy=yes dry=yes  is accepted"
opt_reaches_preflight --sign                      ; check $? "sign=yes deploy=no  dry=no   is accepted"
opt_reaches_preflight --sign --dry-run            ; check $? "sign=yes deploy=no  dry=yes  is accepted"
opt_reaches_preflight --sign --deploy             ; check $? "sign=yes deploy=yes dry=no   is accepted"
opt_reaches_preflight --sign --deploy --dry-run   ; check $? "sign=yes deploy=yes dry=yes  is accepted (it used to self-contradict)"

expect_nonzero "sign=no  deploy=yes dry=no   is refused, publishing something unsigned" \
  bash "$RUN_RELEASE" --worktree "$OPTWT" --version 0.2.134 --deploy

OUT="$(bash "$RUN_RELEASE" --worktree "$OPTWT" --version 0.2.134 --deploy 2>&1 || true)"
printf '%s' "$OUT" | grep -q 'nobody signed'
check $? "...and says why, in those words"

# At argument time, which is before the preflight banner is printed -- so the ABSENCE of that
# banner is the evidence. The first version of this check asserted the opposite and failed a
# script that was refusing exactly when it should.
! printf '%s' "$OUT" | grep -q '1. preflight'
check $? "...at argument time, before the preflight even starts"

printf '\n=== nothing is deleted, in any mode\n'
# The release directory holds the bytes a published manifest is signed over. An earlier version
# cleared it with rm -rf -- in --dry-run too.
DELWT="$TMP/delwt"
make_worktree "$DELWT" "0.2.134"
mkdir -p "$DELWT/.claude/rel/0.2.134/payload"
printf 'the published candidate\n' > "$DELWT/.claude/rel/0.2.134/payload/GNLinkHost.exe"
mkdir -p "$DELWT/build-0.2.134"
printf 'an earlier build\n' > "$DELWT/build-0.2.134/CMakeCache.txt"

for mode in "" "--dry-run" "--sign --dry-run" "--deploy --dry-run"; do
  # shellcheck disable=SC2086
  GNLINK_WEBVIEW2_SOURCE="$TMP/no-such-sdk" bash "$RUN_RELEASE" \
    --worktree "$DELWT" --version 0.2.134 $mode >/dev/null 2>&1
done
[ -f "$DELWT/.claude/rel/0.2.134/payload/GNLinkHost.exe" ] \
  && [ -f "$DELWT/build-0.2.134/CMakeCache.txt" ]
check $? "an existing release and an existing build survive every mode"

# The name is reserved AFTER the preflight, so this needs a worktree whose preflight passes --
# otherwise the run stops at the missing WebView2 SDK and never reaches the reservation. The
# loop above deliberately does the opposite: it fails early, which is what makes it a test that
# nothing is deleted before a build even starts.
mkdir -p "$DELWT/third_party/webview2/build/native"
printf 'stub
' > "$DELWT/third_party/webview2/build/native/WebView2Loader.dll"
OUT="$(GNLINK_CMAKE=/usr/bin/true bash "$RUN_RELEASE" \
        --worktree "$DELWT" --version 0.2.134 2>&1 || true)"
printf '%s' "$OUT" | grep -qi 'already exists'
check $? "...and the run says the release directory is already there"

# The reservation is a directory, and a run that fails before filling it gives it back. Without
# that, every failed attempt would leave debris the next one refuses to build into.
rm -rf "$DELWT/.claude/rel/0.2.134" "$DELWT/build-0.2.134"
GNLINK_CMAKE=/usr/bin/true bash "$RUN_RELEASE" \
  --worktree "$DELWT" --version 0.2.134 >/dev/null 2>&1
[ ! -e "$DELWT/.claude/rel/0.2.134" ]
check $? "a run that fails after reserving the name gives the name back" \
  "$(ls -A "$DELWT/.claude/rel/0.2.134" 2>/dev/null | tr '
' ' ')"

expect_nonzero "a build dir outside the worktree is refused" \
  bash "$RUN_RELEASE" --worktree "$DELWT" --version 0.2.134 --build-dir "$TMP/elsewhere-build"
expect_nonzero "a build dir reached through .. is refused" \
  bash "$RUN_RELEASE" --worktree "$DELWT" --version 0.2.134 --build-dir "$DELWT/../escaped"
expect_nonzero "a rel-root outside the worktree is refused" \
  bash "$RUN_RELEASE" --worktree "$DELWT" --version 0.2.134 --rel-root "$TMP/elsewhere-rel"
[ ! -e "$TMP/elsewhere-build" ] && [ ! -e "$TMP/elsewhere-rel" ]
check $? "...and none of those created anything outside the worktree"

# Two at once. The second has to stop rather than build into the first one's directory.
mkdir -p "$DELWT/.claude/gnlink_release.lock"
printf 'pid=1 run=fixture\n' > "$DELWT/.claude/gnlink_release.lock/owner"
expect_nonzero "a second release in the same worktree is refused while one holds the lock" \
  bash "$RUN_RELEASE" --worktree "$DELWT" --version 0.2.134
rm -rf "$DELWT/.claude/gnlink_release.lock"

printf '\n=== all nine stages, every option combination\n'
# The option table at the top of the release script says what stage 8 and stage 9 do for each of
# the eight combinations. Checking that the ARGUMENTS are accepted -- which is all the block
# above does -- does not check the table: the two contradictions this table has now had were both
# at stage 9, reached only after a build. So this runs the whole thing.
#
# Four tools are substituted to make that possible, through the seams the release script provides
# and announces: a cmake that stages files instead of compiling, an installer-payload check that
# returns 0 instead of reading a PE, a deploy script that records its arguments instead of
# reaching the network, and a throwaway signing key instead of the operational one.
#
# WHAT THIS DOES NOT SHOW, and is not claimed: that the real build produces those files, that the
# installer really carries the payload (the real check does that, against a real build -- see the
# release-script r2 notes), or that publication works. What it does show is that every one of the
# eight combinations reaches stage 9 and ends the way the table says.
NINE="$TMP/nine"
mkdir -p "$NINE/bin"

# The stub cmake. Two invocations matter: `-S <src> -B <build>` and `--build <build> ...`. The
# first makes the directory, the second stages eight executables carrying the version string the
# parity gate looks for, plus the two ui files the packager copies from the worktree.
cat > "$NINE/bin/cmake" <<'STUBCMAKE'
#!/usr/bin/env bash
set -u
BUILD=""
MODE="configure"
while [ $# -gt 0 ]; do
  case "$1" in
    --build) MODE="build"; BUILD="${2:-}"; shift 2 ;;
    -B) BUILD="${2:-}"; shift 2 ;;
    *) shift ;;
  esac
done
[ -n "$BUILD" ] || exit 1
mkdir -p "$BUILD"
if [ "$MODE" = "build" ]; then
  OUT="$BUILD/apps/native_poc/Release"
  mkdir -p "$OUT"
  # The parity gate does not just look for the version: it counts occurrences, and the count
  # differs per binary (CARRIERS in gnlink_release_manifest.py). Three carry none at all and
  # are failed if they do. A stub that ignored that produced eight identical files and a gate
  # that refused them -- correctly, which is how the first version of this fixture found out.
  emit() {
    # $1 name, $2 how many times the version should appear.
    local out="$OUT/$1.exe" n="$2" i=0
    printf 'MZ stub %s\n' "$1" > "$out"
    while [ "$i" -lt "$n" ]; do
      # UTF-16LE, at whatever offset it lands on, which is how it sits in a real binary.
      printf '0\x00.\x002\x00.\x001\x003\x004\x00' >> "$out"
      printf ' padding\n' >> "$out"
      i=$((i + 1))
    done
  }
  emit GNLinkHost 1
  emit GNLinkClient 1
  emit GNLinkViewer 1
  emit GNLinkStream 1
  emit GNLinkSetup 5
  emit GNLinkCapture 0
  emit GNLinkInputService 0
  emit GNLinkUpdater 0
  emit GNLinkClipHelper 0
  # The verifier is a payload-stage target now, so the stub has to produce it or stage 8
  # fails on a file the real cmake would have made.
  printf 'MZ stub verifier\n' > "$OUT/remote60_verify_release.exe"
fi
exit 0
STUBCMAKE
chmod +x "$NINE/bin/cmake"

# The installer-payload check, which in a release reads nine RCDATA resources out of a real PE.
cat > "$NINE/bin/installer_check.ps1" <<'STUBCHECK'
param([string]$Setup, [string]$Payload)
Write-Output "STUB: not reading resources from $Setup"
Write-Output "9/9 embedded payloads match the release payload"
exit 0
STUBCHECK

# The deploy script, which in a release reaches the NAS. This records what it was asked and
# answers the way the real one would, so the release script's handling of it is what is tested.
cat > "$NINE/bin/deploy.sh" <<'STUBDEPLOY'
#!/usr/bin/env bash
set -u
REL=""
MODE="publish"
while [ $# -gt 0 ]; do
  case "$1" in
    --release-dir) REL="${2:-}"; shift 2 ;;
    --dry-run) MODE="dry-run"; shift ;;
    --verify-only) MODE="verify-only"; shift ;;
    *) shift ;;
  esac
done
printf '%s %s\n' "$MODE" "$REL" >> "$GNLINK_STUB_DEPLOY_LOG"
# The real script refuses before it reads a flag when there is no signature. Kept, because that
# is the behaviour the release script has to be correct about.
[ -f "$REL/windows.sig" ] || { printf 'no signature at %s/windows.sig\n' "$REL"; exit 1; }
printf 'published %s https://example.invalid/%s sha256 deadbeef\n' "$MODE" "$(basename "$REL")"
exit 0
STUBDEPLOY
chmod +x "$NINE/bin/deploy.sh"

# A throwaway signing key, and a worktree whose update_manifest.cpp names its public half -- the
# release script reads the trusted key from there and the signing script refuses a mismatch.
NINEKEY="$TMP/ninekey"
NINE_KEY_MADE=0
if make_test_key "$NINEKEY"; then NINE_KEY_MADE=1; fi

NINEWT="$NINE/wt"
make_worktree "$NINEWT" "0.2.134"
mkdir -p "$NINEWT/apps/native_poc/ui" "$NINEWT/third_party/webview2/build/native"
printf '<html>shell</html>\n' > "$NINEWT/apps/native_poc/ui/shell.html"
printf '<html>macro</html>\n' > "$NINEWT/apps/native_poc/ui/macro.html"
printf 'WebView2Loader stub\n' > "$NINEWT/third_party/webview2/build/native/WebView2Loader.dll"
if [ "$NINE_KEY_MADE" = "1" ]; then
  printf 'const char* trusted_key_hex() {\n  return "%s";\n}\n' \
    "$(cat "$NINEKEY/public_xy.hex")" > "$NINEWT/apps/native_poc/src/update_manifest.cpp"
  ( cd "$NINEWT" && git add -A >/dev/null 2>&1 && \
    git -c user.email=t@t -c user.name=t commit -q -m "fixture" >/dev/null 2>&1 )
fi

run_nine() {
  # $1 label, rest: release-script arguments. Echoes the exit code.
  local label="$1"; shift
  rm -rf "$NINE/build-$label" "$NINE/rel-$label"
  GNLINK_STUB_DEPLOY_LOG="$NINE/deploy-$label.log" \
  GNLINK_CMAKE="$NINE/bin/cmake" \
  GNLINK_INSTALLER_PAYLOAD_CHECK="$NINE/bin/installer_check.ps1" \
  GNLINK_DEPLOY_SCRIPT="$NINE/bin/deploy.sh" \
  GNLINK_SIGN_KEYDIR="$NINEKEY" \
  GNLINK_PUBLIC_KEY_HEX="$(cat "$NINEKEY/public_xy.hex")" \
    bash "$RUN_RELEASE" --worktree "$NINEWT" --version 0.2.134 \
      --build-dir "$NINEWT/build-$label" --rel-root "$NINEWT/rel-$label" "$@" \
      > "$NINE/out-$label.log" 2>&1
  printf '%s' "$?"
}

if [ "$NINE_KEY_MADE" = "1" ]; then
  rc="$(run_nine plain)"
  [ "$rc" = "0" ]
  check $? "sign=no  deploy=no  dry=no   runs all nine stages" "exit $rc"

  grep -q 'SUBSTITUTED' "$NINE/out-plain.log" && grep -q 'NOT A RELEASE' "$NINE/out-plain.log"
  check $? "...and says loudly that its tools were substituted"

  rc="$(run_nine dry --dry-run)"
  [ "$rc" = "0" ]
  check $? "sign=no  deploy=no  dry=yes  runs all nine stages" "exit $rc"

  # Signing for real is refused while any tool is substituted, so this row no longer reaches
  # stage 8 -- see the "substituted tools" block below, which is where it is checked now. What is
  # lost here is stub coverage of the signing row; what covers signing is the isolated block
  # above, which runs the real signing script against a throwaway key.
  rc="$(run_nine sign --sign)"
  [ "$rc" != "0" ] && [ ! -f "$NINEWT/rel-sign/0.2.134/windows.sig" ]
  check $? "sign=yes deploy=no  dry=no   is refused while tools are substituted" "exit $rc"

  rc="$(run_nine signdry --sign --dry-run)"
  [ "$rc" = "0" ] && [ ! -f "$NINEWT/rel-signdry/0.2.134/windows.sig" ]
  check $? "sign=yes deploy=no  dry=yes  describes signing and writes no signature" "exit $rc"

  # THE case. It signed nothing, so it must not require a signature -- and it must not call the
  # deploy script either, because that script refuses before it reads a flag when there is none.
  rc="$(run_nine signdeploydry --sign --deploy --dry-run)"
  [ "$rc" = "0" ]
  check $? "sign=yes deploy=yes dry=yes  exits 0" "exit $rc"

  [ ! -s "$NINE/deploy-signdeploydry.log" ]
  check $? "...without calling the deploy script at all" \
    "$(cat "$NINE/deploy-signdeploydry.log" 2>/dev/null | tr '\n' ' ')"

  grep -q 'would deploy' "$NINE/out-signdeploydry.log"
  check $? "...and says what it would have published"

  rc="$(run_nine deploydry --deploy --dry-run)"
  [ "$rc" = "0" ] && [ ! -s "$NINE/deploy-deploydry.log" ]
  check $? "sign=no  deploy=yes dry=yes  exits 0, deploy script not called" "exit $rc"

  rc="$(run_nine signdeploy --sign --deploy)"
  [ "$rc" != "0" ] && [ ! -s "$NINE/deploy-signdeploy.log" ]
  check $? "sign=yes deploy=yes dry=no   is refused, and the deploy script is not called" \
    "exit $rc"

  # A signed release rehearsed end to end: the deploy script IS called, with --dry-run, because
  # this time there is a signature for its preflight to find.
  cp -r "$NINEWT/rel-plain/0.2.134" "$NINEWT/rel-signed-rehearse-0.2.134" 2>/dev/null
  mkdir -p "$NINEWT/rel-rehearse"
  cp -r "$NINEWT/rel-signed-rehearse-0.2.134" "$NINEWT/rel-rehearse/0.2.134"
  GNLINK_STUB_DEPLOY_LOG="$NINE/deploy-rehearse.log" \
  GNLINK_CMAKE="$NINE/bin/cmake" \
  GNLINK_INSTALLER_PAYLOAD_CHECK="$NINE/bin/installer_check.ps1" \
  GNLINK_DEPLOY_SCRIPT="$NINE/bin/deploy.sh" \
  GNLINK_SIGN_KEYDIR="$NINEKEY" \
  GNLINK_PUBLIC_KEY_HEX="$(cat "$NINEKEY/public_xy.hex")" \
    bash "$RUN_RELEASE" --worktree "$NINEWT" --version 0.2.134 \
      --build-dir "$NINEWT/build-rehearse" --rel-root "$NINEWT/rel-rehearse" \
      --deploy --dry-run > "$NINE/out-rehearse.log" 2>&1
  # It refuses, because rel-rehearse/0.2.134 already exists -- which is the r2 rule, working.
  grep -qi 'already exists' "$NINE/out-rehearse.log"
  check $? "an existing release directory still stops a run, even in this mode"

  # The eighth combination is the refusal, checked at argument time above; assert it here too so
  # the table is covered in one place.
  rc="$(run_nine deployonly --deploy)"
  [ "$rc" != "0" ]
  check $? "sign=no  deploy=yes dry=no   is refused" "exit $rc"
else
  printf 'SKIP  nine-stage combinations (could not create a throwaway key here)\n'
fi

printf '\n=== a path that does not exist yet is still normalised\n'
# The escape this closes: the old check resolved only as far as the deepest EXISTING ancestor and
# then appended the rest of the path as text. `<worktree>/new/../../outside/evil` has no existing
# ancestor past the worktree, so it was compared as a string starting with `<worktree>/` and
# ACCEPTED -- and would then have been created outside. Measured before it was fixed.
NORMWT="$TMP/normwt"
mkdir -p "$NORMWT/wt" "$NORMWT/outside"

expect_nonzero "a .. in a not-yet-existing suffix cannot escape" \
  bash "$RMDRIVE" "$NORMWT/wt/new/../../outside/evil" "$NORMWT/wt"
expect_nonzero "...nor one whose whole path is invented" \
  bash "$RMDRIVE" "$NORMWT/wt/a/b/../../../outside" "$NORMWT/wt"
[ ! -e "$NORMWT/outside/evil" ]
check $? "...and nothing was created out there while refusing"

expect_nonzero "a UNC path is refused" bash "$RMDRIVE" "//server/share/x" "$NORMWT/wt"
expect_nonzero "a drive-relative path is refused" bash "$RMDRIVE" "C:relative" "$NORMWT/wt"

bash "$RMDRIVE" "$NORMWT/wt/./sub/../ok" "$NORMWT/wt" >/dev/null 2>&1
check $? "a . and a .. that stay inside are folded and allowed"

# Paths a -Command string would have broken on. The probe takes them as arguments now.
mkdir -p "$NORMWT/wt/with space" "$NORMWT/wt/with\$dollar"
bash "$RMDRIVE" "$NORMWT/wt/with space/x" "$NORMWT/wt" >/dev/null 2>&1
check $? "a path with a space is handled"
bash "$RMDRIVE" "$NORMWT/wt/with\$dollar/x" "$NORMWT/wt" >/dev/null 2>&1
check $? "a path with a dollar sign is handled"

printf '\n=== the published name is reserved, not checked and hoped for\n'
# `mv -T` was doing the taking, on the belief that it fails when the target exists. It does not:
# an existing EMPTY directory is replaced without a word. Shown here against mv itself, so the
# claim is about the tool rather than about the script's opinion of the tool.
MVT="$TMP/mvt"
mkdir -p "$MVT/src" "$MVT/dstEmpty"
printf 'payload\n' > "$MVT/src/file.txt"
mv -T "$MVT/src" "$MVT/dstEmpty" 2>/dev/null
[ -f "$MVT/dstEmpty/file.txt" ]
check $? "mv -T does replace an existing empty directory -- which is why reserving is needed"

RESWT="$TMP/reswt"
make_worktree "$RESWT" "0.2.134"
mkdir -p "$RESWT/third_party/webview2/build/native"
printf 'stub\n' > "$RESWT/third_party/webview2/build/native/WebView2Loader.dll"
# An EMPTY release directory: the case mv -T would have swallowed.
mkdir -p "$RESWT/.claude/rel/0.2.134"
OUT="$(GNLINK_CMAKE=/usr/bin/true bash "$RUN_RELEASE" \
        --worktree "$RESWT" --version 0.2.134 2>&1 || true)"
printf '%s' "$OUT" | grep -qi 'already exists'
check $? "an EMPTY release directory stops the run rather than being replaced"
[ -d "$RESWT/.claude/rel/0.2.134" ]
check $? "...and is still there afterwards"
rmdir "$RESWT/.claude/rel/0.2.134"

# Two runs at once: exactly one may hold the name. Run in the background against the same
# worktree, which is what a second person on the same machine would do.
GNLINK_CMAKE=/usr/bin/true bash "$RUN_RELEASE" --worktree "$RESWT" --version 0.2.134 \
  > "$TMP/race-a.log" 2>&1 &
RACE_A=$!
GNLINK_CMAKE=/usr/bin/true bash "$RUN_RELEASE" --worktree "$RESWT" --version 0.2.134 \
  > "$TMP/race-b.log" 2>&1 &
RACE_B=$!
wait $RACE_A; wait $RACE_B
# WHICH refusal, and that exactly one run got it. "at least one turned away, for any reason with
# 'already exists' in it" was also satisfied by the build directory being there -- which says
# nothing about the lock or the reservation. (RV-14)
race_turned_away() {
  grep -qi 'another release is running\|already exists and is not empty\|another run may hold it' "$1"
}
TURNED=0; RAN=0
for race_log in "$TMP/race-a.log" "$TMP/race-b.log"; do
  if race_turned_away "$race_log"; then TURNED=$((TURNED + 1));
  elif grep -q '2. fresh configure' "$race_log"; then RAN=$((RAN + 1)); fi
done
[ "$TURNED" = "1" ] && [ "$RAN" = "1" ]
check $? "two runs in one worktree: exactly one is turned away by the lock or the reservation, the other builds" \
  "turned=$TURNED built=$RAN"

printf '\n=== backslashes and globs are refused, not interpreted\n'
# Two characters the fold step used to mishandle, both measured on the previous version:
#
#   <wt>/new\..\..\escape   folded to nothing -- a backslash was not a separator here, so the
#                           path stayed "inside" as a string and walked out the moment CMake or
#                           Windows read it
#   <wt>/build-*            the fold loop splits with an unquoted expansion, so this was globbed
#                           against the filesystem: with two matches it came out as
#                           build-aaa/build-bbb, a path nobody named
#
# Refused rather than translated now. A release directory is not a place to guess.
GLOBWT="$TMP/globwt"
mkdir -p "$GLOBWT/wt"
touch "$GLOBWT/wt/build-aaa" "$GLOBWT/wt/build-bbb"

# The REASON matters here, not just the exit code. Without the string-level rejection this path
# is still refused -- by the downstream real-path check, which happens to resolve it outside the
# worktree. That is an accident of this fixture, not the guard doing its job, and a check that
# accepted any non-zero exit passed with the rejection removed. So it asserts which refusal.
OUT="$(bash "$RMDRIVE" "$GLOBWT/wt/new\\..\\..\\escape" "$GLOBWT/wt" 2>&1 || true)"
printf '%s' "$OUT" | grep -q 'error:backslash'
check $? "a backslash is refused at the string stage, by name" "$(printf '%s' "$OUT" | tail -1)"
OUT="$(bash "$RMDRIVE" "$GLOBWT/wt/build-*" "$GLOBWT/wt" 2>&1 || true)"
printf '%s' "$OUT" | grep -q 'error:glob'
check $? "a * is refused at the string stage, by name" "$(printf '%s' "$OUT" | tail -1)"
expect_nonzero "a [ ] in the path is refused" \
  bash "$RMDRIVE" "$GLOBWT/wt/build-[a]" "$GLOBWT/wt"
expect_nonzero "a ? in the path is refused" \
  bash "$RMDRIVE" "$GLOBWT/wt/build-?" "$GLOBWT/wt"

[ ! -e "$GLOBWT/wt/build-aaa/build-bbb" ] && [ ! -e "$GLOBWT/escape" ]
check $? "...and none of them created anything"

bash "$RMDRIVE" "$GLOBWT/wt/ordinary/path" "$GLOBWT/wt" >/dev/null 2>&1
check $? "an ordinary path still folds and passes"

# The whole run, not just the helper: the refusal has to happen before anything is built.
GLOBRUN="$TMP/globrun"
make_worktree "$GLOBRUN" "0.2.134"
mkdir -p "$GLOBRUN/third_party/webview2/build/native"
printf 'stub\n' > "$GLOBRUN/third_party/webview2/build/native/WebView2Loader.dll"
OUT="$(GNLINK_CMAKE=/usr/bin/true bash "$RUN_RELEASE" --worktree "$GLOBRUN" --version 0.2.134 \
        --build-dir "$GLOBRUN/build-*" 2>&1 || true)"
! printf '%s' "$OUT" | grep -q '2. configure'
check $? "a globbed --build-dir never reaches the build"
[ ! -e "$GLOBRUN/build-aaa" ]
check $? "...and created nothing"

printf '\n=== the verifier is built with the payload, not after the drift check\n'
# Built at stage 8 it would be compiled AFTER the stage 7 drift check -- the one binary whose job
# is to say "this candidate is what it claims" would itself come from a tree nothing had checked
# since. It is a payload-stage target now.
grep -q 'remote60_verify_release' <<< "$(grep '^TARGETS=' "$RELEASE_SH")"
check $? "remote60_verify_release is one of the payload targets"

! grep -q -- '--target remote60_verify_release' "$RELEASE_SH"
check $? "...and stage 8 does not build it, only runs it"

printf '\n=== a refusal inside a substitution still stops the run\n'
# `die` called inside $( ) kills the SUBSHELL. The caller carries on with an empty string, and
# the empty string becomes `mkdir ""` -- which failed, and reported a lock held at "()" . Found
# because four different mutations produced identical results, which is a harness telling you it
# is not measuring anything.
#
# The lock path is where it showed, so that is where it is pinned: a lock directory the boundary
# check refuses must stop the run, not hand back nothing and continue.
SUBWT2="$TMP/subshellwt"
make_worktree "$SUBWT2" "0.2.134"
mkdir -p "$SUBWT2/third_party/webview2/build/native"
printf 'stub\n' > "$SUBWT2/third_party/webview2/build/native/WebView2Loader.dll"

# A junction at .claude, so the lock path resolves outside the worktree and the check refuses it.
mkdir -p "$TMP/elsewhere-claude"
JUNCTION_OK=0
if powershell.exe -NoProfile -NonInteractive -Command \
     "New-Item -ItemType Junction -Path '$(cygpath -w "$SUBWT2")\.claude' -Target '$(cygpath -w "$TMP/elsewhere-claude")' | Out-Null" >/dev/null 2>&1; then
  JUNCTION_OK=1
fi

if [ "$JUNCTION_OK" = "1" ]; then
  OUT="$(GNLINK_CMAKE=/usr/bin/true bash "$RUN_RELEASE" \
          --worktree "$SUBWT2" --version 0.2.134 2>&1 || true)"
  printf '%s' "$OUT" | grep -qi 'refusing to take the lock\|refusing to use the lock'
  check $? "a lock path the boundary check refuses stops the run" \
    "$(printf '%s' "$OUT" | tail -1)"

  # The symptom of the bug, which must not come back: an empty path reported as a held lock.
  ! printf '%s' "$OUT" | grep -q 'another release is running in this worktree ()'
  check $? "...rather than continuing with an empty path"

  ! printf '%s' "$OUT" | grep -q '2. configure'
  check $? "...and never reaches the build"

  powershell.exe -NoProfile -NonInteractive -Command \
    "Remove-Item -LiteralPath '$(cygpath -w "$SUBWT2")\.claude' -Force" >/dev/null 2>&1
else
  printf 'SKIP  subshell refusal check (could not create a junction here)\n'
fi

printf '\n=== the lock belongs to whoever took it\n'
LOCKWT="$TMP/lockwt"
make_worktree "$LOCKWT" "0.2.134"
mkdir -p "$LOCKWT/.claude/gnlink_release.lock"
printf 'pid=999999 run=someone-else version=0.2.134\n' > "$LOCKWT/.claude/gnlink_release.lock/owner"
OUT="$(GNLINK_WEBVIEW2_SOURCE="$TMP/no-such-sdk" bash "$RUN_RELEASE" \
        --worktree "$LOCKWT" --version 0.2.134 2>&1 || true)"
printf '%s' "$OUT" | grep -qi 'another release is running'
check $? "a lock somebody else holds turns a run away"
[ -d "$LOCKWT/.claude/gnlink_release.lock" ] && \
  grep -q 'someone-else' "$LOCKWT/.claude/gnlink_release.lock/owner"
check $? "...and the run it turned away did not delete their lock on its way out"

printf '\n=== substituted tools cannot reach the key or the server\n'
# Announcing a substitution is not a gate. The first version of the seams printed "NOT A RELEASE"
# at the END and carried on, so a run with a stub compiler could still hand its staged files to
# the operational key, and a stub deploy script could still print "published". Test scaffolding
# with a path to the real key is not scaffolding.
#
# So each seam, on its own, has to be enough to stop --sign and --deploy -- and to stop them at
# argument time, before the worktree is entered or the key is looked for.
SUBWT="$TMP/subwt"
make_worktree "$SUBWT" "0.2.134"

sub_run() {
  # $1 = the environment assignment to make, rest = release-script arguments.
  local assign="$1"; shift
  env "$assign" bash "$RUN_RELEASE" --worktree "$SUBWT" --version 0.2.134 "$@" 2>&1
}

SEAMS=(
  "GNLINK_CMAKE=$TMP/stub-cmake"
  "GNLINK_INSTALLER_PAYLOAD_CHECK=$TMP/stub-check.ps1"
  "GNLINK_DEPLOY_SCRIPT=$TMP/stub-deploy.sh"
  "GNLINK_SIGN_KEYDIR=$TMP/stub-keydir"
  "GNLINK_PUBLIC_KEY_HEX=$(printf 'a%.0s' $(seq 128))"
)

for seam in "${SEAMS[@]}"; do
  name="${seam%%=*}"

  out="$(sub_run "$seam" --sign)"; rc=$?
  [ "$rc" != "0" ]
  check $? "$name alone refuses --sign" "exit $rc"

  printf '%s' "$out" | grep -q 'refusing --sign'
  check $? "...saying which gate refused it"

  # Before the preflight banner: nothing was read, nothing was entered, no key was looked for.
  ! printf '%s' "$out" | grep -q '1. preflight'
  check $? "...at argument time, before anything is read"

  out="$(sub_run "$seam" --sign --deploy)"; rc=$?
  [ "$rc" != "0" ] && printf '%s' "$out" | grep -q 'refusing --'
  check $? "$name alone refuses a real deploy" "exit $rc"
done

# A dry run is what a substituted run is for, and it still works. The worktree has no WebView2,
# so this gets as far as the preflight and stops there -- which is past the gate, which is the
# point.
out="$(GNLINK_WEBVIEW2_SOURCE="$TMP/no-such-sdk" \
       sub_run "GNLINK_CMAKE=$TMP/stub-cmake" --sign --deploy --dry-run)"
printf '%s' "$out" | grep -q '1. preflight'
check $? "...but a dry run with the same seam is allowed through"

printf '%s' "$out" | grep -q 'SUBSTITUTED  cmake'
check $? "...and is still told, loudly, what it is running with"

# The operational key is never consulted on the refused path. Pointed at a directory that does
# not exist: if the run had reached stage 8 it would say so in its own words, and it does not.
out="$(sub_run "GNLINK_SIGN_KEYDIR=$TMP/definitely-not-a-keydir" --sign)"
! printf '%s' "$out" | grep -q 'no signing key at'
check $? "a refused --sign never gets as far as looking for a key"

printf '\n=== RV-13: the gates come from the candidate itself\n'
FOREIGNWT="$TMP/foreignwt"
make_worktree "$FOREIGNWT" "0.2.134"
# The checkout's copy, pointed at a different worktree: the 0.2.109 shape.
OUT="$(bash "$RELEASE_SH" --worktree "$FOREIGNWT" --version 0.2.134 --dry-run 2>&1)"; rc=$?
[ "$rc" != "0" ] && printf '%s' "$OUT" | grep -q 'not inside --worktree'
check $? "a release script outside --worktree refuses to run" "exit $rc: $(printf '%s' "$OUT" | tail -1)"
! printf '%s' "$OUT" | grep -q '2. fresh configure'
check $? "...before anything is built"
OUT="$(GNLINK_ALLOW_FOREIGN_SCRIPT_DIR=1 bash "$RELEASE_SH" --worktree "$FOREIGNWT" --version 0.2.134 --sign 2>&1)"; rc=$?
[ "$rc" != "0" ] && printf '%s' "$OUT" | grep -q 'refusing --sign'
check $? "...and the override that lets the regression do it cannot sign" "exit $rc"

printf '\n=== RV-11: settings the tools read on their own refuse a real sign or deploy\n'
ENVWT="$TMP/envwt"
make_worktree "$ENVWT" "0.2.134"
for env_seam in GNLINK_REMOTE_MODE=local GNLINK_LOCAL_ROOT=/tmp/x GNLINK_VERIFY_PUBLIC=0 \
                GNLINK_DEPLOY_HOST=nobody@127.0.0.1 GNLINK_DEPLOY_KEY=/tmp/nokey GNLINK_REMOTE_ROOT=/tmp/r \
                GNLINK_PUBLIC_BASE=https://example.invalid GNLINK_UPDATES_SUBDIR=u \
                GNLINK_MANIFEST_SUBDIR=m GNLINK_BACKUP_SUBDIR=b GNLINK_SOMETHING_NEW=1 \
                _CL_=/DSTUB CL=/DSTUB LINK=/STUB CMAKE_TOOLCHAIN_FILE=/tmp/t.cmake \
                CMAKE_GENERATOR=Ninja GNLINK_WEBVIEW2_SOURCE=/tmp/nosdk; do
  OUT="$(env "$env_seam" bash "$RUN_RELEASE" --worktree "$ENVWT" --version 0.2.134 --sign --deploy 2>&1)"; rc=$?
  [ "$rc" != "0" ] && printf '%s' "$OUT" | grep -q 'refusing --sign' \
    && ! printf '%s' "$OUT" | grep -q '1. preflight'
  check $? "${env_seam%%=*} alone refuses --sign --deploy at argument time" "exit $rc"
done
# The control: none of them set, the same arguments get past the argument gate.
OUT="$(bash "$RUN_RELEASE" --worktree "$ENVWT" --version 0.2.999 --sign --deploy 2>&1)"
printf '%s' "$OUT" | grep -q '1. preflight'
check $? "...and with none of them set the same arguments reach the preflight"

printf '\n=== RV-11/12: what a substituted run leaves, and what its summary says\n'
if [ "$NINE_KEY_MADE" = "1" ]; then
  [ -f "$NINEWT/rel-plain/0.2.134/NOT_A_RELEASE.txt" ]
  check $? "a substituted run marks its release directory NOT_A_RELEASE.txt"
  mkdir -p "$TMP/deploymark/payload"
  cp "$NINEWT/rel-plain/0.2.134/NOT_A_RELEASE.txt" "$TMP/deploymark/"
  printf 'm\n' > "$TMP/deploymark/windows.manifest"; printf 's\n' > "$TMP/deploymark/windows.sig"
  OUT="$(GNLINK_REMOTE_MODE=local GNLINK_LOCAL_ROOT="$TMP/deploymark-root" GNLINK_VERIFY_PUBLIC=0 \
        bash "$SCRIPT_DIR/gnlink_deploy.sh" --release-dir "$TMP/deploymark" --dry-run 2>&1)"; rc=$?
  [ "$rc" != "0" ] && printf '%s' "$OUT" | grep -q 'NOT_A_RELEASE'
  check $? "...and the deploy script refuses a directory carrying it, even signed" "exit $rc"
  [ ! -e "$TMP/deploymark-root" ]
  check $? "...before touching its target"
  grep -q 'installer payload 9/9 (as reported by installer_check.ps1)' "$NINE/out-plain.log"
  check $? "the installer line in the summary is the check's own count, naming the check"
  grep -q 'deployed  : no (dry run; the deploy script was not called)' "$NINE/out-deploydry.log"
  check $? "a dry run's summary says it deployed nothing, not 'dry run only'"
  # The per-tool lines of the scripts hash, not the SUBSTITUTED banner that also names them.
  grep -qE '^ +[0-9a-f]{16}  .*/bin/installer_check\.ps1$' "$NINE/out-plain.log" \
    && grep -qE '^ +[0-9a-f]{16}  .*/bin/deploy\.sh$' "$NINE/out-plain.log" \
    && grep -qE '^ +[0-9a-f]{16}  .*/gnlink_check_payload_set\.py$' "$NINE/out-plain.log"
  check $? "the scripts hash lists the tools actually used, substituted ones included"

  # An installer check that exits 0 without a count is not a pass.
  cat > "$NINE/bin/installer_quiet.ps1" <<'STUBQUIET'
param([string]$Setup, [string]$Payload)
Write-Output "STUB: says nothing about a count"
exit 0
STUBQUIET
  GNLINK_CMAKE="$NINE/bin/cmake" GNLINK_INSTALLER_PAYLOAD_CHECK="$NINE/bin/installer_quiet.ps1" \
    bash "$RUN_RELEASE" --worktree "$NINEWT" --version 0.2.134 \
      --build-dir "$NINEWT/build-quiet" --rel-root "$NINEWT/rel-quiet" > "$NINE/out-quiet.log" 2>&1; rc=$?
  [ "$rc" != "0" ] && grep -q 'did not report a match count' "$NINE/out-quiet.log"
  check $? "an installer check that reports no count fails the run" "exit $rc"

  # The manifest is pinned at stage 5: an installer check that rewrites it is caught at 7b.
  cat > "$NINE/bin/installer_tamper.ps1" <<'STUBTAMPER'
param([string]$Setup, [string]$Payload)
Add-Content -LiteralPath (Join-Path (Split-Path -Parent $Payload) "windows.manifest") -Value "tampered=1"
Write-Output "9/9 embedded payloads match the release payload"
exit 0
STUBTAMPER
  GNLINK_CMAKE="$NINE/bin/cmake" GNLINK_INSTALLER_PAYLOAD_CHECK="$NINE/bin/installer_tamper.ps1" \
    bash "$RUN_RELEASE" --worktree "$NINEWT" --version 0.2.134 \
      --build-dir "$NINEWT/build-tamper" --rel-root "$NINEWT/rel-tamper" > "$NINE/out-tamper.log" 2>&1; rc=$?
  [ "$rc" != "0" ] && grep -q 'windows.manifest changed since stage 5' "$NINE/out-tamper.log"
  check $? "a manifest changed after stage 5 stops the run" "exit $rc"
else
  printf 'SKIP  marker and summary checks (no throwaway key, so no nine-stage runs)\n'
fi

printf '\n=== RV-14: small ones\n'
SMALLWT="$TMP/smallwt"
make_worktree "$SMALLWT" "0.2.134"
OUT="$(timeout 20 bash "$RUN_RELEASE" --version 0.2.134 --worktree 2>&1)"; rc=$?
[ "$rc" != "0" ] && [ "$rc" != "124" ] && printf '%s' "$OUT" | grep -q 'needs a value'
check $? "an option given last without its value is refused, not looped on" "exit $rc"

# The D:/ form the usage allows, for a path under %TEMP% -- which the other form spells /tmp.
mkdir -p "$SMALLWT/third_party/webview2/build/native"
printf 'stub\n' > "$SMALLWT/third_party/webview2/build/native/WebView2Loader.dll"
MIXED_BUILD="$(cygpath -m "$SMALLWT")/build-mixed"
OUT="$(GNLINK_CMAKE=/usr/bin/true bash "$RUN_RELEASE" --worktree "$SMALLWT" --version 0.2.134 \
        --build-dir "$MIXED_BUILD" 2>&1)"
printf '%s' "$OUT" | grep -q 'build dir: created'
check $? "a build dir in D:/ form under the worktree is accepted" "$(printf '%s' "$OUT" | grep -m1 FAIL)"
rm -rf "$SMALLWT/build-mixed" "$SMALLWT/.claude/rel"

# An earlier run's log beside the build dir is evidence; the run stops instead of replacing it.
printf 'an earlier log\n' > "$SMALLWT/build-logkeep.configure.log"
OUT="$(GNLINK_CMAKE=/usr/bin/true bash "$RUN_RELEASE" --worktree "$SMALLWT" --version 0.2.134 \
        --build-dir "$SMALLWT/build-logkeep" 2>&1)"; rc=$?
[ "$rc" != "0" ] && grep -q 'an earlier log' "$SMALLWT/build-logkeep.configure.log"
check $? "an existing build log is not overwritten" "exit $rc"

printf '\n=== the script does not bump versions\n'
# Reading the header is exactly what the preflight does, so the pattern has to name WRITING:
# an in-place edit, or a redirect into the file. The first version of this check matched the
# preflight's own sed that reads the version out, and failed a script that was behaving.
if grep -qE "sed -i|(>|>>|tee)[^|]*product_version\.hpp" "$RELEASE_SH"; then
  check 1 "the release script never writes product_version.hpp" "it appears to modify it"
else
  check 0 "the release script never writes product_version.hpp"
fi

printf '\n%s  (%d checks, %d failed)\n' \
  "$([ "$FAILURES" = "0" ] && echo 'RESULT: ALL PASS' || echo 'RESULT: FAILED')" \
  "$CHECKS" "$FAILURES"
[ "$FAILURES" = "0" ]
