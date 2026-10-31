#!/usr/bin/env bash
#
# Exercises automation/gnlink_deploy.sh against a directory on this machine.
#
# Every case here runs the REAL script -- the same functions, the same order, the same guards --
# with GNLINK_REMOTE_MODE=local pointing it at a temporary root. A test that re-implemented the
# steps would only prove the test works.
#
# What is deliberately NOT tested here: ssh and curl. Those are the two things a local root cannot
# stand in for, and pretending otherwise would be the more expensive kind of green.
#
# The cases are the ones where a mistake is expensive and invisible:
#   - the manifest is swapped LAST, so a failure leaves the previous release published
#   - a re-run of a deploy that half succeeded is safe
#   - a version path is immutable: same version, different bytes, refused
#   - a document that does not verify never reaches the server
#   - a document that does not describe the files on disk never reaches the server
#   - two deploys cannot interleave

set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPLOY="$REPO/automation/gnlink_deploy.sh"
# 0.2.109-r2, not 0.2.109: the first cut of that release did not name GNLinkSetup.exe, and the
# preflight gate now refuses it -- correctly. A fixture has to be a release that could actually be
# published, or every case downstream is testing the refusal instead of the thing it names.
# maint r2: by default the fixture is MADE here (below): a release with exactly the payload names the product
# replaces today (read from the same source the payload gate reads), signed with a throwaway key generated for
# this run -- never the operational key. The old fixtures (.claude/rel/0.2.109-r2, 0.2.123) are refused by today's
# payload gate (GNLinkClipHelper.exe), which made every case below test that refusal instead of its own subject.
# GNLINK_TEST_RELEASE still names a real signed release to use instead (verified with the product's key).
SOURCE_REL="${GNLINK_TEST_RELEASE:-}"
# That release was signed when the server was called rem.shotan.net, and its artifact urls say
# so. It cannot be edited -- the signature covers them -- so the cases below publish it under
# the base it was made for. What the script does with its DEFAULT base is case 12.
# maint r1: overridable, so a newer fixture (signed for the current server) can be used: GNLINK_TEST_FIXTURE_BASE.
FIXTURE_BASE="${GNLINK_TEST_FIXTURE_BASE:-https://rem.shotan.net}"

failures=0
check() {
  local what="$1" ok="$2" detail="${3:-}"
  if [ "$ok" = "1" ]; then
    printf 'PASS  %s%s\n' "$what" "${detail:+  -- $detail}"
  else
    printf 'FAIL  %s%s\n' "$what" "${detail:+  -- $detail}"
    failures=$(( failures + 1 ))
  fi
}

mkdir -p "$REPO/.claude"
WORK="$(mktemp -d "$REPO/.claude/deploy-test.XXXXXX")"
# Only ever the directory this test created, and only on the way out.
cleanup() { rm -rf -- "$WORK"; }
trap cleanup EXIT

# ---------------------------------------------------------------------------- the fixture (maint r2)
# A throwaway P-256 key, made the way gnlink_release_test.sh makes its own: the private half DPAPI-wrapped in
# this run's directory, the public half (X||Y hex) handed ONLY to the deploy child as GNLINK_PUBLIC_KEY_HEX (the
# verifier's existing override). The product's pinned key and every check stay as they are.
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

# A release directory: payload/ with one small file per name the product replaces (data for this test, never run
# or installed), a manifest naming each with its size, sha256 and url under $FIXTURE_BASE/updates/<version>/, and
# its signature made by the release signing script with the test key (it verifies before it writes).
make_fixture() {
  local dir="$1" version="$2" keydir="$3" names name rel f size sha
  mkdir -p "$dir/payload"
  names="$(python -c "import sys; sys.path.insert(0, r'$(cygpath -w "$REPO/automation")'); import gnlink_check_payload_set as m; print('\\n'.join(sorted(m.expected_names())))" | tr -d '\r')"
  [ -n "$names" ] || return 1
  {
    printf '# GNLink update manifest -- TEST FIXTURE %s, signed with a throwaway test key (never the operational one).\n' "$version"
    printf 'schema=2\nreleaseId=r-%s-fixture\nplatform=windows\narch=x64\nversion=%s\n' "$version" "$version"
    while IFS= read -r name; do
      [ -n "$name" ] || continue
      rel="${name//\\//}"
      f="$dir/payload/$rel"
      mkdir -p "$(dirname "$f")"
      { printf 'GNLink deploy-test fixture %s %s\n' "$version" "$name"; head -c 2048 /dev/zero | tr '\0' 'x'; } > "$f"
      size="$(wc -c < "$f" | tr -d ' ')"
      sha="$(sha256sum -- "$f" | cut -d' ' -f1)"
      printf 'artifact=%s|%s|%s|%s/updates/%s/%s\n' "$name" "$size" "$sha" "$FIXTURE_BASE" "$version" "$rel"
    done <<< "$names"
  } > "$dir/windows.manifest"
  powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "$(cygpath -w "$REPO/automation/gnlink_release_sign.ps1")" \
    -ReleaseDir "$(cygpath -w "$dir")" -KeyDir "$(cygpath -w "$keydir")" \
    -TrustedKeyHex "$(cat "$keydir/public_xy.hex")" >/dev/null 2>&1
  [ -f "$dir/windows.sig" ]
}

TEST_KEY_HEX=""
if [ -z "$SOURCE_REL" ]; then
  KEYDIR="$WORK/testkey"
  SOURCE_REL="$WORK/fixture-0.2.109"
  if make_test_key "$KEYDIR" && make_fixture "$SOURCE_REL" 0.2.109 "$KEYDIR"; then
    TEST_KEY_HEX="$(cat "$KEYDIR/public_xy.hex")"
    printf 'fixture: made (test key, %s artifacts)\n' "$(grep -c '^artifact=' "$SOURCE_REL/windows.manifest")"
  else
    printf 'FAIL  the test fixture could not be made (key or signing)\n'
    printf 'gnlink_deploy_test: FAILED (1)\n'
    exit 1
  fi
fi
if [ ! -f "$SOURCE_REL/windows.manifest" ]; then
  printf 'FAIL  no release to test with at %s\n' "$SOURCE_REL"
  printf 'gnlink_deploy_test: FAILED (1)\n'
  exit 1
fi
# The fixture must pass the payload gate the deploy applies -- otherwise every case tests that refusal.
python "$(cygpath -w "$REPO/automation/gnlink_check_payload_set.py")" "$(cygpath -w "$SOURCE_REL/windows.manifest")" >/dev/null 2>&1
check "the fixture names everything the product replaces (the deploy's payload gate)" "$([ $? -eq 0 ] && echo 1 || echo 0)"

# A private copy of the release, so a case that tampers with it cannot touch the real one.
copy_release() {
  local dest="$1"
  mkdir -p "$dest"
  cp -r -- "$SOURCE_REL/." "$dest/"
}

# Runs the deploy against a fresh root. Echoes the exit code; output goes to $LAST_OUT.
LAST_OUT=""
run_deploy() {
  local root="$1" rel="$2"; shift 2
  LAST_OUT="$WORK/out.$$.txt"
  GNLINK_REMOTE_MODE=local GNLINK_LOCAL_ROOT="$root" GNLINK_VERIFY_PUBLIC=0 \
    GNLINK_PUBLIC_BASE="$FIXTURE_BASE" GNLINK_PUBLIC_KEY_HEX="${GNLINK_PUBLIC_KEY_HEX-$TEST_KEY_HEX}" \
    bash "$DEPLOY" --release-dir "$rel" --platform windows "$@" >"$LAST_OUT" 2>&1
  return $?
}

published_sha() {
  local root="$1" f="$1/update-manifests/windows.manifest"
  [ -f "$f" ] && sha256sum -- "$f" | cut -d' ' -f1 || echo NONE
}

# ---------------------------------------------------------------------------- 1. dry run

printf '\n== a dry run changes nothing\n'
ROOT="$WORK/root1"; mkdir -p "$ROOT"
REL="$WORK/rel1"; copy_release "$REL"
run_deploy "$ROOT" "$REL" --dry-run; rc=$?
check "dry run succeeds" "$([ $rc -eq 0 ] && echo 1 || echo 0)" "exit=$rc"
check "...and publishes nothing" "$([ "$(published_sha "$ROOT")" = "NONE" ] && echo 1 || echo 0)"
check "...and uploads no artifact" \
      "$([ -z "$(find "$ROOT/updates" -type f 2>/dev/null)" ] && echo 1 || echo 0)"
check "...and says so" "$(grep -q 'DRY RUN' "$LAST_OUT" && echo 1 || echo 0)"

# ---------------------------------------------------------------------------- 2. a real publish

printf '\n== a publish puts the artifacts up first and the manifest last\n'
ROOT="$WORK/root2"; mkdir -p "$ROOT"
run_deploy "$ROOT" "$REL"; rc=$?
check "publish succeeds" "$([ $rc -eq 0 ] && echo 1 || echo 0)" "exit=$rc"
# Counted from the manifest, not written down. A hard-coded 9 silently became wrong the moment
# the release gained a tenth artifact, and a fixture that disagrees with its own release tests
# nothing.
expected="$(grep -c '^artifact=' "$REL/windows.manifest" | tr -d ' ')"
count="$(find "$ROOT/updates/0.2.109" -type f 2>/dev/null | wc -l | tr -d ' ')"
check "every artifact is there" "$([ "$count" = "$expected" ] && echo 1 || echo 0)" "$count/$expected"
check "the manifest is published" \
      "$([ "$(published_sha "$ROOT")" = "$(sha256sum -- "$REL/windows.manifest" | cut -d' ' -f1)" ] && echo 1 || echo 0)"
check "the signature went with it" "$([ -f "$ROOT/update-manifests/windows.sig" ] && echo 1 || echo 0)"
check "the ui/ files kept their subdirectory" \
      "$([ -f "$ROOT/updates/0.2.109/ui/shell.html" ] && echo 1 || echo 0)"
check "no .tmp was left behind" \
      "$([ -z "$(find "$ROOT" -name '*.tmp' 2>/dev/null)" ] && echo 1 || echo 0)"
check "the lock was released" "$([ ! -d "$ROOT/.gnlink-deploy.lock" ] && echo 1 || echo 0)"

# ---------------------------------------------------------------------------- 3. idempotence

printf '\n== running it again is safe\n'
before="$(published_sha "$ROOT")"
run_deploy "$ROOT" "$REL"; rc=$?
check "a second run succeeds" "$([ $rc -eq 0 ] && echo 1 || echo 0)" "exit=$rc"
check "...uploading nothing again"       "$(grep -q "uploaded 0, already present $expected" "$LAST_OUT" && echo 1 || echo 0)"
check "...and what is published is unchanged" \
      "$([ "$(published_sha "$ROOT")" = "$before" ] && echo 1 || echo 0)"

# ---------------------------------------------------------------------------- 4. immutability

printf '\n== the same version with different bytes is refused\n'
ROOT4="$WORK/root4"; mkdir -p "$ROOT4/updates/0.2.109"
printf 'not the build that was signed' > "$ROOT4/updates/0.2.109/GNLinkHost.exe"
# Something is already published here, and it must survive the refusal.
mkdir -p "$ROOT4/update-manifests"
printf 'version=0.2.108\n' > "$ROOT4/update-manifests/windows.manifest"
printf 'oldsig\n' > "$ROOT4/update-manifests/windows.sig"
prev="$(published_sha "$ROOT4")"
run_deploy "$ROOT4" "$REL"; rc=$?
check "it refuses" "$([ $rc -ne 0 ] && echo 1 || echo 0)" "exit=$rc"
check "...saying the version is immutable" \
      "$(grep -qi 'immutable' "$LAST_OUT" && echo 1 || echo 0)"
check "...and the previous release is STILL published" \
      "$([ "$(published_sha "$ROOT4")" = "$prev" ] && echo 1 || echo 0)"

# ---------------------------------------------------------------------------- 5. a bad signature

printf '\n== a manifest that does not verify never reaches the server\n'
ROOT5="$WORK/root5"; mkdir -p "$ROOT5"
REL5="$WORK/rel5"; copy_release "$REL5"
# One byte of the signature. The document is untouched, so only the signature check can catch it.
#
# Written as a substitution keyed on the character that is actually there, because the previous
# version replaced a leading '0' -- and the moment a signature began with something else it edited
# nothing at all. The case then "passed" by publishing a perfectly valid release.
sig="$(cat "$REL5/windows.sig")"
first="${sig:0:1}"
if [ "$first" = "a" ]; then rest="b"; else rest="a"; fi
printf '%s%s' "$rest" "${sig:1}" > "$REL5/windows.sig"
[ "$(cat "$REL5/windows.sig")" != "$sig" ] || {
  printf 'FAIL  the signature tamper did not change anything
'; failures=$(( failures + 1 )); }
run_deploy "$ROOT5" "$REL5"; rc=$?
check "it refuses" "$([ $rc -ne 0 ] && echo 1 || echo 0)" "exit=$rc"
check "...before uploading anything" \
      "$([ -z "$(find "$ROOT5/updates" -type f 2>/dev/null)" ] && echo 1 || echo 0)"
check "...and nothing is published" "$([ "$(published_sha "$ROOT5")" = "NONE" ] && echo 1 || echo 0)"

# ---------------------------------------------------------------------------- 6. a manifest that lies

printf '\n== a manifest that does not describe the files on disk never reaches the server\n'
ROOT6="$WORK/root6"; mkdir -p "$ROOT6"
REL6="$WORK/rel6"; copy_release "$REL6"
# The signature still verifies -- the DOCUMENT is untouched. What changed is the file it names.
# This is the 0.2.108 failure exactly: everything signed, everything verified, and the hash
# belonged to a build that no longer existed.
# One byte, IN PLACE. Appending would change the size too, and the size check fires first --
# the assertion below would then pass while the hash comparison it names had never run.
printf 'X' | dd of="$REL6/payload/GNLinkHost.exe" bs=1 seek=1000 conv=notrunc status=none
run_deploy "$ROOT6" "$REL6"; rc=$?
check "it refuses" "$([ $rc -ne 0 ] && echo 1 || echo 0)" "exit=$rc"
check "...naming the hash mismatch, not the signature" \
      "$(grep -q 'manifest hash and file hash differ' "$LAST_OUT" && echo 1 || echo 0)"
check "...before uploading anything" \
      "$([ -z "$(find "$ROOT6/updates" -type f 2>/dev/null)" ] && echo 1 || echo 0)"

# ---------------------------------------------------------------------------- 7. a missing file

printf '\n== a manifest naming a file that is not there is refused\n'
ROOT7="$WORK/root7"; mkdir -p "$ROOT7"
REL7="$WORK/rel7"; copy_release "$REL7"
rm -f -- "$REL7/payload/ui/macro.html"
run_deploy "$ROOT7" "$REL7"; rc=$?
check "it refuses" "$([ $rc -ne 0 ] && echo 1 || echo 0)" "exit=$rc"
check "...naming the missing file" "$(grep -q 'macro.html' "$LAST_OUT" && echo 1 || echo 0)"

# ---------------------------------------------------------------------------- 8. the lock

printf '\n== two deploys cannot interleave\n'
ROOT8="$WORK/root8"; mkdir -p "$ROOT8/.gnlink-deploy.lock"
printf 'pid 1 on somewhere\n' > "$ROOT8/.gnlink-deploy.lock/owner"
run_deploy "$ROOT8" "$REL"; rc=$?
check "a second deploy refuses to start" "$([ $rc -ne 0 ] && echo 1 || echo 0)" "exit=$rc"
check "...naming the holder" "$(grep -q 'another deploy holds the lock' "$LAST_OUT" && echo 1 || echo 0)"
check "...and does not take the other one's lock away" \
      "$([ -f "$ROOT8/.gnlink-deploy.lock/owner" ] && echo 1 || echo 0)"
check "...and uploads nothing" \
      "$([ -z "$(find "$ROOT8/updates" -type f 2>/dev/null)" ] && echo 1 || echo 0)"

# ---------------------------------------------------------------------------- 9. verify-only

printf '\n== verify-only reports and changes nothing\n'
before="$(published_sha "$ROOT")"
run_deploy "$ROOT" "$REL" --verify-only; rc=$?
check "verify-only succeeds against a matching server" "$([ $rc -eq 0 ] && echo 1 || echo 0)" "exit=$rc"
check "...and says the release is already published" \
      "$(grep -q 'IDENTICAL' "$LAST_OUT" && echo 1 || echo 0)"
check "...and changed nothing" "$([ "$(published_sha "$ROOT")" = "$before" ] && echo 1 || echo 0)"
check "...and never claims an external check it did not run" \
      "$(grep -q 'SKIPPED -- not attempted, so not verified' "$LAST_OUT" && echo 1 || echo 0)"

# ---------------------------------------------------------------------------- 10. the backup

printf '\n== publishing over an existing release backs up the pair it replaces\n'
ROOT10="$WORK/root10"; mkdir -p "$ROOT10/update-manifests"
printf 'version=0.2.108\nschema=2\n' > "$ROOT10/update-manifests/windows.manifest"
printf 'previous-signature\n' > "$ROOT10/update-manifests/windows.sig"
run_deploy "$ROOT10" "$REL"; rc=$?
check "publish succeeds" "$([ $rc -eq 0 ] && echo 1 || echo 0)" "exit=$rc"
check "the replaced manifest is backed up under its own version" \
      "$([ -f "$ROOT10/manifest-backups/0.2.108/windows.manifest" ] && echo 1 || echo 0)"
check "...with its signature, as a pair" \
      "$([ -f "$ROOT10/manifest-backups/0.2.108/windows.sig" ] && echo 1 || echo 0)"
check "...and the hashes recorded" \
      "$([ -s "$ROOT10/manifest-backups/0.2.108/SHA256SUMS" ] && echo 1 || echo 0)"

# ---------------------------------------------------------------------------- 10b. a revision

printf '
== publishing a revision of the SAME version does not eat its own rollback point
'
# The recovery case: 0.2.109 is already published and a corrected manifest for 0.2.109 goes out.
# The backup is named after the published version, so a naive second copy would overwrite the
# original pair with the new one -- leaving nothing to roll back TO.
ROOT10B="$WORK/root10b"; mkdir -p "$ROOT10B/update-manifests"
printf 'version=0.2.109
schema=2
' > "$ROOT10B/update-manifests/windows.manifest"
printf 'the-original-signature
' > "$ROOT10B/update-manifests/windows.sig"
original="$(sha256sum -- "$ROOT10B/update-manifests/windows.manifest" | cut -d' ' -f1)"
run_deploy "$ROOT10B" "$REL"; rc=$?
check "the first publish succeeds" "$([ $rc -eq 0 ] && echo 1 || echo 0)" "exit=$rc"
check "...backing the original up under its version"       "$([ -f "$ROOT10B/manifest-backups/0.2.109/windows.manifest" ] && echo 1 || echo 0)"
# Now publish again, exactly as a retry or a further revision would.
run_deploy "$ROOT10B" "$REL"; rc=$?
check "a second publish of the same version succeeds" "$([ $rc -eq 0 ] && echo 1 || echo 0)" "exit=$rc"
kept="$(sha256sum -- "$ROOT10B/manifest-backups/0.2.109/windows.manifest" 2>/dev/null | cut -d' ' -f1)"
check "...and the ORIGINAL backup is still the original"       "$([ "$kept" = "$original" ] && echo 1 || echo 0)" "${kept:-missing}"
extra="$(find "$ROOT10B/manifest-backups" -maxdepth 1 -name '0.2.109[-+]*' -type d 2>/dev/null | wc -l | tr -d ' ')"
check "...and the second one went somewhere of its own"       "$([ "$extra" -ge 1 ] && echo 1 || echo 0)" "$extra"

# ---------------------------------------------------------------------------- 10c. which document

printf '\n== the backup name says WHICH document of a version it holds\n'
# Ledger 0.0.7: manifest-backups/0.2.109 held the broken r-0.2.109 (no Setup) and the good
# r-0.2.109-2 only had a timestamp -- a name that picked by version restored the outage. The
# releaseId is now part of the name, and BACKUP_INFO says how many artifacts the document lists.
ROOT10C="$WORK/root10c"; mkdir -p "$ROOT10C/update-manifests"
printf 'schema=2\nreleaseId=r-0.2.109-2\nversion=0.2.109\nartifact=A|1|x\nartifact=B|1|y\nartifact=C|1|z\n' \
  > "$ROOT10C/update-manifests/windows.manifest"
printf 'sig-of-r-0.2.109-2\n' > "$ROOT10C/update-manifests/windows.sig"
run_deploy "$ROOT10C" "$REL"; rc=$?
check "publish succeeds" "$([ $rc -eq 0 ] && echo 1 || echo 0)" "exit=$rc"
check "the backup is named version+releaseId" \
      "$([ -f "$ROOT10C/manifest-backups/0.2.109+r-0.2.109-2/windows.manifest" ] && echo 1 || echo 0)" \
      "$(ls "$ROOT10C/manifest-backups" 2>/dev/null | tr '\n' ' ')"
info="$(cat "$ROOT10C/manifest-backups/0.2.109+r-0.2.109-2/BACKUP_INFO" 2>/dev/null)"
check "...and BACKUP_INFO records the releaseId" \
      "$(printf '%s\n' "$info" | grep -qx 'releaseId=r-0.2.109-2' && echo 1 || echo 0)" "$info"
check "...and the artifact count of the kept document" \
      "$(printf '%s\n' "$info" | grep -qx 'artifacts=3' && echo 1 || echo 0)" "$info"

printf '\n== a releaseId cannot steer the backup out of its directory\n'
ROOT10D="$WORK/root10d"; mkdir -p "$ROOT10D/update-manifests"
printf 'schema=2\nreleaseId=../../escape\nversion=0.2.110\n' > "$ROOT10D/update-manifests/windows.manifest"
printf 'sig\n' > "$ROOT10D/update-manifests/windows.sig"
run_deploy "$ROOT10D" "$REL"; rc=$?
check "publish succeeds" "$([ $rc -eq 0 ] && echo 1 || echo 0)" "exit=$rc"
check "the unsafe characters are replaced, the backup stays inside" \
      "$([ -f "$ROOT10D/manifest-backups/0.2.110+.._.._escape/windows.manifest" ] && echo 1 || echo 0)" \
      "$(ls "$ROOT10D/manifest-backups" 2>/dev/null | tr '\n' ' ')"
check "...and nothing was written outside it" \
      "$([ ! -e "$WORK/escape" ] && [ ! -e "$ROOT10D/escape" ] && echo 1 || echo 0)"

# ---------------------------------------------------------------------------- 11. path containment

printf '\n== an artifact url outside the public base is refused\n'
ROOT11="$WORK/root11"; mkdir -p "$ROOT11"
REL11="$WORK/rel11"; copy_release "$REL11"
sed -i "s#$FIXTURE_BASE/updates/#https://elsewhere.example/updates/#" "$REL11/windows.manifest"
# The substitution is the whole case. A pattern that matched nothing leaves the manifest as it
# was, the deploy refuses it for a different reason (or not at all), and "it refuses" is then a
# statement about something else.
moved="$(grep -c 'https://elsewhere.example/updates/' "$REL11/windows.manifest" || true)"
left="$(grep -c "$FIXTURE_BASE/updates/" "$REL11/windows.manifest" || true)"
check "the fixture's artifact urls really were moved elsewhere" \
      "$([ "${moved:-0}" -gt 0 ] && [ "${left:-0}" -eq 0 ] && echo 1 || echo 0)" \
      "moved=$moved left=$left"
GNLINK_PUBLIC_KEY_HEX="" run_deploy "$ROOT11" "$REL11"; rc=$?
check "it refuses" "$([ $rc -ne 0 ] && echo 1 || echo 0)" "exit=$rc"
# Why it refuses, said out loud, because it is not why the title suggests: the urls are inside
# the signed document, so moving them breaks the signature and the deploy stops there -- before
# it has compared any url with the public base. That is the right outcome, and it means this
# case cannot show the prefix rule. Case 12 does, with a document whose signature is intact.
check "...at the signature, before any url is compared with the base" \
      "$(grep -q 'does not verify' "$LAST_OUT" && ! grep -q 'is outside' "$LAST_OUT" && echo 1 || echo 0)" \
      "$(grep -E 'does not verify|is outside' "$LAST_OUT" | head -1)"
check "...and uploads nothing" \
      "$([ -z "$(find "$ROOT11/updates" -type f 2>/dev/null)" ] && echo 1 || echo 0)"

# ---------------------------------------------------------------------------- 12. the default base

printf '\n== with nothing set, the public base is the one server, and the prefix rule is applied to it\n'
default_base="$(env -u GNLINK_PUBLIC_BASE bash -c 'set -uo pipefail; eval "$(grep -E "^GNLINK_PUBLIC_BASE=" "$1")"; printf %s "$GNLINK_PUBLIC_BASE"' _ "$DEPLOY")"
check "the default is https://gnlink.shotan.net" \
      "$([ "$default_base" = "https://gnlink.shotan.net" ] && echo 1 || echo 0)" "$default_base"
# And it is what the script acts on: the fixture names the former address, so under the default
# base every one of its artifacts is outside the public base and the deploy must refuse.
ROOT12="$WORK/root12"; mkdir -p "$ROOT12"
REL12="$WORK/rel12"; copy_release "$REL12"
run_deploy_default() {
  local root="$1" rel="$2"
  LAST_OUT="$WORK/out.$$.txt"
  env -u GNLINK_PUBLIC_BASE GNLINK_REMOTE_MODE=local GNLINK_LOCAL_ROOT="$root" \
    GNLINK_VERIFY_PUBLIC=0 GNLINK_PUBLIC_KEY_HEX="${GNLINK_PUBLIC_KEY_HEX-$TEST_KEY_HEX}" \
    bash "$DEPLOY" --release-dir "$rel" --platform windows >"$LAST_OUT" 2>&1
  return $?
}
run_deploy_default "$ROOT12" "$REL12"; rc=$?
check "a release signed for the former address is refused under the default" \
      "$([ $rc -ne 0 ] && echo 1 || echo 0)" "exit=$rc"
check "...because of where its artifacts are" \
      "$(grep -qi 'rem.shotan.net' "$LAST_OUT" && echo 1 || echo 0)" \
      "$(grep -i 'rem.shotan.net' "$LAST_OUT" | head -1)"
check "...and nothing is uploaded or published" \
      "$([ -z "$(find "$ROOT12" -type f 2>/dev/null)" ] && echo 1 || echo 0)"

# ---------------------------------------------------------------------------- 13. a swap that fails half-way
# maint r1/r2: when one of the two renames fails, the script says the PHASE (swap-state: which .tmp is left) and,
# separately, what is published NOW (pair-state: present? this release's bytes? verifies?). Only the second is a
# statement about what clients get (Codex maint M1). The failure is made the only way a local root can make one
# rename fail and not the other: an `mv` earlier on the PATH that refuses one name; a `cat` that refuses one path
# makes the read-back fail (the "could not be checked" case). Everything else is the real script.
printf '\n13. a swap that fails half-way\n'
SHIM13="$WORK/shim13"; mkdir -p "$SHIM13"
REAL_MV="$(command -v mv)"
REAL_CAT="$(command -v cat)"
cat > "$SHIM13/mv" <<SHIM
#!/usr/bin/env bash
for a in "\$@"; do
  case "\$a" in *"\$GNLINK_TEST_FAIL_MV_SUFFIX") echo "mv: refused by the test (\$a)" >&2; exit 1 ;; esac
done
exec "$REAL_MV" "\$@"
SHIM
cat > "$SHIM13/cat" <<SHIM
#!/usr/bin/env bash
for a in "\$@"; do
  case "\$a" in *"\${GNLINK_TEST_FAIL_CAT_SUFFIX:-@none@}") echo "cat: refused by the test (\$a)" >&2; exit 1 ;; esac
done
exec "$REAL_CAT" "\$@"
SHIM
chmod +x "$SHIM13/mv" "$SHIM13/cat"
run_deploy_failing() {
  local root="$1" rel="$2" mvSuffix="$3" catSuffix="${4:-}"
  LAST_OUT="$WORK/out.$$.txt"
  PATH="$SHIM13:$PATH" GNLINK_TEST_FAIL_MV_SUFFIX="$mvSuffix" GNLINK_TEST_FAIL_CAT_SUFFIX="$catSuffix" \
    GNLINK_REMOTE_MODE=local GNLINK_LOCAL_ROOT="$root" GNLINK_VERIFY_PUBLIC=0 \
    GNLINK_PUBLIC_BASE="$FIXTURE_BASE" GNLINK_PUBLIC_KEY_HEX="${GNLINK_PUBLIC_KEY_HEX-$TEST_KEY_HEX}" \
    bash "$DEPLOY" --release-dir "$rel" --platform windows >"$LAST_OUT" 2>&1
  return $?
}
state_line() { grep "^$1 " "$LAST_OUT" | head -1 | cut -d' ' -f2; }
pub="$WORK/rel13-pub"
REL13="$WORK/rel13"; copy_release "$REL13"
# 13a. nothing published before; the signature rename fails
ROOT13A="$WORK/root13a"; mkdir -p "$ROOT13A"
run_deploy_failing "$ROOT13A" "$REL13" ".sig.tmp"; rc=$?
check "13a. no pair before, the signature rename fails: exit 4" "$([ $rc -eq 4 ] && echo 1 || echo 0)" "exit=$rc"
check "13a. ...phase: manifest-only" "$([ "$(state_line swap-state)" = manifest-only ] && echo 1 || echo 0)" "$(state_line swap-state)"
check "13a. ...published: incomplete (no signature at all)" "$([ "$(state_line pair-state)" = incomplete ] && echo 1 || echo 0)" "$(state_line pair-state)"
check "13a. ...and it is true: the new manifest is there, the signature only as .tmp" \
      "$([ "$(published_sha "$ROOT13A")" = "$(sha256sum -- "$REL13/windows.manifest" | cut -d' ' -f1)" ] && \
          [ -f "$ROOT13A/update-manifests/windows.sig.tmp" ] && [ ! -e "$ROOT13A/update-manifests/windows.sig" ] && echo 1 || echo 0)"
# 13b. ANOTHER signed release published before (0.2.110, same test key); the signature rename fails
if [ -n "$TEST_KEY_HEX" ]; then
  RELO="$WORK/rel-0.2.110"
  make_fixture "$RELO" 0.2.110 "$KEYDIR" >/dev/null 2>&1
  ROOT13B="$WORK/root13b"; mkdir -p "$ROOT13B"
  run_deploy "$ROOT13B" "$RELO" >/dev/null 2>&1
  oldsig="$(sha256sum -- "$ROOT13B/update-manifests/windows.sig" 2>/dev/null | cut -d' ' -f1)"
  run_deploy_failing "$ROOT13B" "$REL13" ".sig.tmp"; rc=$?
  check "13b. another pair before, the signature rename fails: exit 4" "$([ $rc -eq 4 ] && echo 1 || echo 0)" "exit=$rc"
  check "13b. ...phase: manifest-only" "$([ "$(state_line swap-state)" = manifest-only ] && echo 1 || echo 0)"
  check "13b. ...published: does-not-verify (the new manifest beside the old signature)" \
        "$([ "$(state_line pair-state)" = does-not-verify ] && echo 1 || echo 0)" "$(state_line pair-state)"
  check "13b. ...and it is true: the old signature is still the published one" \
        "$([ "$(sha256sum -- "$ROOT13B/update-manifests/windows.sig" | cut -d' ' -f1)" = "$oldsig" ] && echo 1 || echo 0)"
  check "13b. ...the old pair was backed up first" \
        "$([ -n "$(find "$ROOT13B/manifest-backups" -name windows.sig 2>/dev/null | head -1)" ] && echo 1 || echo 0)"
else
  printf 'SKIP  13b/13c/13d need the made fixture (GNLINK_TEST_RELEASE names an external release)\n'
fi
# 13c. the SAME release published before; re-publishing it, the signature rename fails (Codex M1's case)
if [ -n "$TEST_KEY_HEX" ]; then
  ROOT13C="$WORK/root13c"; mkdir -p "$ROOT13C"
  run_deploy "$ROOT13C" "$REL13" >/dev/null 2>&1
  run_deploy_failing "$ROOT13C" "$REL13" ".sig.tmp"; rc=$?
  check "13c. the same pair before, the signature rename fails: exit 4 (still a failed publish)" "$([ $rc -eq 4 ] && echo 1 || echo 0)" "exit=$rc"
  check "13c. ...phase: manifest-only" "$([ "$(state_line swap-state)" = manifest-only ] && echo 1 || echo 0)"
  check "13c. ...published: verified-this-release (NOT a mismatch)" \
        "$([ "$(state_line pair-state)" = verified-this-release ] && echo 1 || echo 0)" "$(state_line pair-state)"
  check "13c. ...and its ERROR line does not claim clients refuse it" "$(grep '^ERROR the swap' "$LAST_OUT" | grep -q 'refuse' && echo 0 || echo 1)" "$(grep '^ERROR the swap' "$LAST_OUT" | head -1 | cut -c1-120)"
  check "13c. ...and it is true: the published pair verifies (the verifier, the test key)" \
        "$(GNLINK_PUBLIC_KEY_HEX="$TEST_KEY_HEX" node "$(cygpath -w "$REPO/automation/gnlink_verify_manifest.js")" \
             "$(cygpath -w "$ROOT13C/update-manifests/windows.manifest")" "$(cygpath -w "$ROOT13C/update-manifests/windows.sig")" windows >/dev/null 2>&1 && echo 1 || echo 0)"
fi
# 13d. another pair before; the MANIFEST rename fails (nothing replaced)
if [ -n "$TEST_KEY_HEX" ]; then
  ROOT13D="$WORK/root13d"; mkdir -p "$ROOT13D"
  run_deploy "$ROOT13D" "$RELO" >/dev/null 2>&1
  run_deploy_failing "$ROOT13D" "$REL13" ".manifest.tmp"; rc=$?
  check "13d. the manifest rename fails: exit 4" "$([ $rc -eq 4 ] && echo 1 || echo 0)" "exit=$rc"
  check "13d. ...phase: neither" "$([ "$(state_line swap-state)" = neither ] && echo 1 || echo 0)"
  check "13d. ...published: verified-another-pair (the previous one, intact)" \
        "$([ "$(state_line pair-state)" = verified-another-pair ] && echo 1 || echo 0)" "$(state_line pair-state)"
  check "13d. ...both .tmp kept for diagnosis" \
        "$([ -f "$ROOT13D/update-manifests/windows.manifest.tmp" ] && [ -f "$ROOT13D/update-manifests/windows.sig.tmp" ] && echo 1 || echo 0)"
fi
# 13e. the read-back itself fails: nothing is claimed
ROOT13E="$WORK/root13e"; mkdir -p "$ROOT13E"
run_deploy "$ROOT13E" "$REL13" >/dev/null 2>&1
run_deploy_failing "$ROOT13E" "$REL13" ".sig.tmp" "update-manifests/windows.sig"; rc=$?
check "13e. the published pair cannot be read back: exit 4" "$([ $rc -eq 4 ] && echo 1 || echo 0)" "exit=$rc"
check "13e. ...published: unknown, and no claim either way" \
      "$([ "$(state_line pair-state)" = unknown ] && ! grep '^ERROR the swap' "$LAST_OUT" | grep -q 'refuse\|verifies\|is THIS' && echo 1 || echo 0)" "$(state_line pair-state)"
# 13f. a normal publish has neither line, and names the restart rule
ROOT13F="$WORK/root13f"; mkdir -p "$ROOT13F"
run_deploy "$ROOT13F" "$REL13"; rc=$?
check "13f. a normal publish: exit 0, no swap-state / pair-state" \
      "$([ $rc -eq 0 ] && ! grep -q '^swap-state\|^pair-state' "$LAST_OUT" && echo 1 || echo 0)" "exit=$rc"
check "13f. ...and the restart line names code, modules and env" \
      "$(grep -q 'a module it requires, or its env' "$LAST_OUT" && echo 1 || echo 0)"

# ----------------------------------------------------------------------------

printf '\n'
if [ "$failures" = "0" ]; then
  printf 'gnlink_deploy_test: PASS\n'
  exit 0
fi
printf 'gnlink_deploy_test: FAILED (%d)\n' "$failures"
exit 1
