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
