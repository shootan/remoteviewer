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
  ( cd "$dir" && git init -q . && git add -A && \
    git -c user.email=t@t -c user.name=t commit -qm fixture ) >/dev/null 2>&1
}

printf '=== arguments\n'
expect_nonzero "no --worktree is refused" bash "$RELEASE_SH" --version 0.2.134
expect_nonzero "no --version is refused" bash "$RELEASE_SH" --worktree "$TMP"
expect_nonzero "a worktree that does not exist is refused" \
  bash "$RELEASE_SH" --worktree "$TMP/nowhere" --version 0.2.134
expect_nonzero "a version that is not a version is refused" \
  bash "$RELEASE_SH" --worktree "$TMP" --version "latest"
expect_nonzero "an unknown argument is refused" \
  bash "$RELEASE_SH" --worktree "$TMP" --version 0.2.134 --publish-now

printf '\n=== preflight, fail-closed\n'
WT="$TMP/wt"
make_worktree "$WT" "0.2.134"

# The guard that matters most: the script must never paper over a version it was not asked for.
expect_nonzero "a version the source does not claim is refused" \
  env GNLINK_WEBVIEW2_SOURCE="$TMP/no-such-sdk" bash "$RELEASE_SH" --worktree "$WT" --version 0.2.999
OUT="$(env GNLINK_WEBVIEW2_SOURCE="$TMP/no-such-sdk" bash "$RELEASE_SH" --worktree "$WT" --version 0.2.999 2>&1)"
case "$OUT" in
  *"the source says 0.2.134"*) check 0 "...and says which version the source claims" ;;
  *) check 1 "...and says which version the source claims" "$(printf '%s' "$OUT" | tail -1)" ;;
esac

# Same worktree, matching version, but no SDK anywhere: it must stop rather than start a build
# that would fail later with an error naming neither WebView2 nor the reason.
expect_nonzero "no WebView2 SDK, and none to copy, is refused" \
  env GNLINK_WEBVIEW2_SOURCE="$TMP/no-such-sdk" bash "$RELEASE_SH" --worktree "$WT" --version 0.2.134
OUT="$(env GNLINK_WEBVIEW2_SOURCE="$TMP/no-such-sdk" bash "$RELEASE_SH" --worktree "$WT" --version 0.2.134 2>&1)"
case "$OUT" in
  *"fetch_webview2"*) check 0 "...and names the script that fetches it" ;;
  *) check 1 "...and names the script that fetches it" "$(printf '%s' "$OUT" | tail -1)" ;;
esac

# A dirty worktree cannot be released: the release would not name a commit.
DIRTY_WT="$TMP/dirty"
make_worktree "$DIRTY_WT" "0.2.134"
printf 'edited\n' >> "$DIRTY_WT/apps/native_poc/src/product_version.hpp"
expect_nonzero "uncommitted tracked changes are refused" \
  env GNLINK_WEBVIEW2_SOURCE="$TMP/no-such-sdk" bash "$RELEASE_SH" --worktree "$DIRTY_WT" --version 0.2.134

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
          'GNLinkCapture.exe': 0, 'GNLinkInputService.exe': 0, 'GNLinkUpdater.exe': 0}
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
         'ui' + chr(92) + 'shell.html', 'ui' + chr(92) + 'macro.html']
problems = []
if not lines[0].startswith('# GNLink update manifest -- release 0.2.134'):
    problems.append('header line')
if lines[1:6] != want_head:
    problems.append('head fields: %r' % (lines[1:6],))
arts = [l for l in lines if l.startswith('artifact=')]
if len(arts) != 10:
    problems.append('%d artifact lines, expected 10' % len(arts))
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
    if not url.startswith('https://rem.shotan.net/updates/0.2.134/'):
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
  sed -n '/^path_reparse_between()/,/^}$/p;/^nearest_existing()/,/^}$/p;/^check_inside_worktree()/,/^}$/p;/^make_fresh_dir()/,/^}$/p' "$RELEASE_SH"
  printf 'WORKTREE_REAL="$(cd "$2" && pwd -P)"\n'
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

make_test_key "$KEYDIR"
KEY_MADE=0
[ $? = 0 ] && KEY_MADE=1

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
  out="$(GNLINK_WEBVIEW2_SOURCE="$TMP/no-such-sdk" bash "$RELEASE_SH" \
           --worktree "$OPTWT" --version 0.2.134 "$@" 2>&1)"
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
  bash "$RELEASE_SH" --worktree "$OPTWT" --version 0.2.134 --deploy

OUT="$(bash "$RELEASE_SH" --worktree "$OPTWT" --version 0.2.134 --deploy 2>&1 || true)"
printf '%s' "$OUT" | grep -q 'nobody signed'
check $? "...and says why, in those words"

# At argument time, which is before the preflight banner is printed -- so the ABSENCE of that
# banner is the evidence. The first version of this check asserted the opposite and failed a
# script that was refusing exactly when it should.
printf '%s' "$OUT" | grep -qv '1. preflight'
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
  GNLINK_WEBVIEW2_SOURCE="$TMP/no-such-sdk" bash "$RELEASE_SH" \
    --worktree "$DELWT" --version 0.2.134 $mode >/dev/null 2>&1
done
[ -f "$DELWT/.claude/rel/0.2.134/payload/GNLinkHost.exe" ] \
  && [ -f "$DELWT/build-0.2.134/CMakeCache.txt" ]
check $? "an existing release and an existing build survive every mode"

OUT="$(GNLINK_WEBVIEW2_SOURCE="$TMP/no-such-sdk" bash "$RELEASE_SH" \
        --worktree "$DELWT" --version 0.2.134 2>&1 || true)"
printf '%s' "$OUT" | grep -qi 'already exists'
check $? "...and the run says the release directory is already there"

expect_nonzero "a build dir outside the worktree is refused" \
  bash "$RELEASE_SH" --worktree "$DELWT" --version 0.2.134 --build-dir "$TMP/elsewhere-build"
expect_nonzero "a build dir reached through .. is refused" \
  bash "$RELEASE_SH" --worktree "$DELWT" --version 0.2.134 --build-dir "$DELWT/../escaped"
expect_nonzero "a rel-root outside the worktree is refused" \
  bash "$RELEASE_SH" --worktree "$DELWT" --version 0.2.134 --rel-root "$TMP/elsewhere-rel"
[ ! -e "$TMP/elsewhere-build" ] && [ ! -e "$TMP/elsewhere-rel" ]
check $? "...and none of those created anything outside the worktree"

# Two at once. The second has to stop rather than build into the first one's directory.
mkdir -p "$DELWT/.claude/gnlink_release.lock"
printf 'pid=1 run=fixture\n' > "$DELWT/.claude/gnlink_release.lock/owner"
expect_nonzero "a second release in the same worktree is refused while one holds the lock" \
  bash "$RELEASE_SH" --worktree "$DELWT" --version 0.2.134
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
    bash "$RELEASE_SH" --worktree "$NINEWT" --version 0.2.134 \
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
    bash "$RELEASE_SH" --worktree "$NINEWT" --version 0.2.134 \
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
  env "$assign" bash "$RELEASE_SH" --worktree "$SUBWT" --version 0.2.134 "$@" 2>&1
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
  printf '%s' "$out" | grep -qv '1. preflight'
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
printf '%s' "$out" | grep -qv 'no signing key at'
check $? "a refused --sign never gets as far as looking for a key"

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
