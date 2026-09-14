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
SOURCE_REL="${GNLINK_TEST_RELEASE:-$REPO/.claude/rel/0.2.109-r2}"

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

if [ ! -f "$SOURCE_REL/windows.manifest" ]; then
  printf 'FAIL  no release to test with at %s\n' "$SOURCE_REL"
  printf 'gnlink_deploy_test: FAILED (1)\n'
  exit 1
fi

mkdir -p "$REPO/.claude"
WORK="$(mktemp -d "$REPO/.claude/deploy-test.XXXXXX")"
# Only ever the directory this test created, and only on the way out.
cleanup() { rm -rf -- "$WORK"; }
trap cleanup EXIT

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
extra="$(find "$ROOT10B/manifest-backups" -maxdepth 1 -name '0.2.109-*' -type d 2>/dev/null | wc -l | tr -d ' ')"
check "...and the second one went somewhere of its own"       "$([ "$extra" -ge 1 ] && echo 1 || echo 0)" "$extra"

# ---------------------------------------------------------------------------- 11. path containment

printf '\n== an artifact url outside the public base is refused\n'
ROOT11="$WORK/root11"; mkdir -p "$ROOT11"
REL11="$WORK/rel11"; copy_release "$REL11"
sed -i 's#https://rem.shotan.net/updates/#https://elsewhere.example/updates/#' "$REL11/windows.manifest"
GNLINK_PUBLIC_KEY_HEX="" run_deploy "$ROOT11" "$REL11"; rc=$?
check "it refuses" "$([ $rc -ne 0 ] && echo 1 || echo 0)" "exit=$rc"
check "...and uploads nothing" \
      "$([ -z "$(find "$ROOT11/updates" -type f 2>/dev/null)" ] && echo 1 || echo 0)"

# ----------------------------------------------------------------------------

printf '\n'
if [ "$failures" = "0" ]; then
  printf 'gnlink_deploy_test: PASS\n'
  exit 0
fi
printf 'gnlink_deploy_test: FAILED (%d)\n' "$failures"
exit 1
