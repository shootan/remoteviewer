p = r'C:\work\gnlink-wt\maint-r1\automation\gnlink_deploy_test.sh'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c, cnt=1):
    global s
    assert s.count(a) == cnt, (s.count(a), a[:80])
    s = s.replace(a, c)

rep('''SOURCE_REL="${GNLINK_TEST_RELEASE:-$REPO/.claude/rel/0.2.109-r2}"''',
    '''# maint r2: by default the fixture is MADE here (below): a release with exactly the payload names the product
# replaces today (read from the same source the payload gate reads), signed with a throwaway key generated for
# this run -- never the operational key. The old fixtures (.claude/rel/0.2.109-r2, 0.2.123) are refused by today's
# payload gate (GNLinkClipHelper.exe), which made every case below test that refusal instead of its own subject.
# GNLINK_TEST_RELEASE still names a real signed release to use instead (verified with the product's key).
SOURCE_REL="${GNLINK_TEST_RELEASE:-}"''')
rep('''if [ ! -f "$SOURCE_REL/windows.manifest" ]; then
  printf 'FAIL  no release to test with at %s\\n' "$SOURCE_REL"
  printf 'gnlink_deploy_test: FAILED (1)\\n'
  exit 1
fi

mkdir -p "$REPO/.claude"
WORK="$(mktemp -d "$REPO/.claude/deploy-test.XXXXXX")"
# Only ever the directory this test created, and only on the way out.
cleanup() { rm -rf -- "$WORK"; }
trap cleanup EXIT
''', '''mkdir -p "$REPO/.claude"
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
  \\$ErrorActionPreference = 'Stop'
  Add-Type -AssemblyName System.Security
  \\$e = [System.Security.Cryptography.ECDsa]::Create(
    [System.Security.Cryptography.ECCurve]::CreateFromFriendlyName('nistP256'))
  \\$k = \\$e.ExportParameters(\\$true)
  \\$ms = New-Object System.IO.MemoryStream
  \\$bw = New-Object System.IO.BinaryWriter(\\$ms)
  \\$bw.Write([int]0x32534345); \\$bw.Write([int]32)
  \\$bw.Write(\\$k.Q.X); \\$bw.Write(\\$k.Q.Y); \\$bw.Write(\\$k.D)
  \\$bw.Flush()
  \\$prot = [System.Security.Cryptography.ProtectedData]::Protect(
    \\$ms.ToArray(), \\$null, [System.Security.Cryptography.DataProtectionScope]::CurrentUser)
  [System.IO.File]::WriteAllBytes((Join-Path '$(cygpath -w "$dir")' 'private.ecc.dpapi'), \\$prot)
  \\$hex = ([System.BitConverter]::ToString(\\$k.Q.X + \\$k.Q.Y) -replace '-','').ToLowerInvariant()
  [System.IO.File]::WriteAllText((Join-Path '$(cygpath -w "$dir")' 'public_xy.hex'), \\$hex)
  \\$bw.Dispose(); \\$e.Dispose()" >/dev/null 2>&1
  [ -f "$dir/private.ecc.dpapi" ] && [ -f "$dir/public_xy.hex" ]
}

# A release directory: payload/ with one small file per name the product replaces (data for this test, never run
# or installed), a manifest naming each with its size, sha256 and url under $FIXTURE_BASE/updates/<version>/, and
# its signature made by the release signing script with the test key (it verifies before it writes).
make_fixture() {
  local dir="$1" version="$2" keydir="$3" names name rel f size sha
  mkdir -p "$dir/payload"
  names="$(python -c "import sys; sys.path.insert(0, r'$(cygpath -w "$REPO/automation")'); import gnlink_check_payload_set as m; print('\\\\n'.join(sorted(m.expected_names())))" | tr -d '\\r')"
  [ -n "$names" ] || return 1
  {
    printf '# GNLink update manifest -- TEST FIXTURE %s, signed with a throwaway test key (never the operational one).\\n' "$version"
    printf 'schema=2\\nreleaseId=r-%s-fixture\\nplatform=windows\\narch=x64\\nversion=%s\\n' "$version" "$version"
    while IFS= read -r name; do
      [ -n "$name" ] || continue
      rel="${name//\\\\//}"
      f="$dir/payload/$rel"
      mkdir -p "$(dirname "$f")"
      { printf 'GNLink deploy-test fixture %s %s\\n' "$version" "$name"; head -c 2048 /dev/zero | tr '\\0' 'x'; } > "$f"
      size="$(wc -c < "$f" | tr -d ' ')"
      sha="$(sha256sum -- "$f" | cut -d' ' -f1)"
      printf 'artifact=%s|%s|%s|%s/updates/%s/%s\\n' "$name" "$size" "$sha" "$FIXTURE_BASE" "$version" "$rel"
    done <<< "$names"
  } > "$dir/windows.manifest"
  powershell.exe -NoProfile -ExecutionPolicy Bypass \\
    -File "$(cygpath -w "$REPO/automation/gnlink_release_sign.ps1")" \\
    -ReleaseDir "$(cygpath -w "$dir")" -KeyDir "$(cygpath -w "$keydir")" \\
    -TrustedKeyHex "$(cat "$keydir/public_xy.hex")" >/dev/null 2>&1
  [ -f "$dir/windows.sig" ]
}

TEST_KEY_HEX=""
if [ -z "$SOURCE_REL" ]; then
  KEYDIR="$WORK/testkey"
  SOURCE_REL="$WORK/fixture-0.2.109"
  if make_test_key "$KEYDIR" && make_fixture "$SOURCE_REL" 0.2.109 "$KEYDIR"; then
    TEST_KEY_HEX="$(cat "$KEYDIR/public_xy.hex")"
    printf 'fixture: made (test key, %s artifacts)\\n' "$(grep -c '^artifact=' "$SOURCE_REL/windows.manifest")"
  else
    printf 'FAIL  the test fixture could not be made (key or signing)\\n'
    printf 'gnlink_deploy_test: FAILED (1)\\n'
    exit 1
  fi
fi
if [ ! -f "$SOURCE_REL/windows.manifest" ]; then
  printf 'FAIL  no release to test with at %s\\n' "$SOURCE_REL"
  printf 'gnlink_deploy_test: FAILED (1)\\n'
  exit 1
fi
# The fixture must pass the payload gate the deploy applies -- otherwise every case tests that refusal.
python "$(cygpath -w "$REPO/automation/gnlink_check_payload_set.py")" "$(cygpath -w "$SOURCE_REL/windows.manifest")" >/dev/null 2>&1
check "the fixture names everything the product replaces (the deploy's payload gate)" "$([ $? -eq 0 ] && echo 1 || echo 0)"
''')
# the key goes to every deploy child (unless a case sets the variable itself, as case 11 does)
rep('''  GNLINK_REMOTE_MODE=local GNLINK_LOCAL_ROOT="$root" GNLINK_VERIFY_PUBLIC=0 \\
    GNLINK_PUBLIC_BASE="$FIXTURE_BASE" \\
    bash "$DEPLOY" --release-dir "$rel" --platform windows "$@" >"$LAST_OUT" 2>&1
  return $?
}''', '''  GNLINK_REMOTE_MODE=local GNLINK_LOCAL_ROOT="$root" GNLINK_VERIFY_PUBLIC=0 \\
    GNLINK_PUBLIC_BASE="$FIXTURE_BASE" GNLINK_PUBLIC_KEY_HEX="${GNLINK_PUBLIC_KEY_HEX-$TEST_KEY_HEX}" \\
    bash "$DEPLOY" --release-dir "$rel" --platform windows "$@" >"$LAST_OUT" 2>&1
  return $?
}''')
rep('''  env -u GNLINK_PUBLIC_BASE GNLINK_REMOTE_MODE=local GNLINK_LOCAL_ROOT="$root" \\
    GNLINK_VERIFY_PUBLIC=0 \\''', '''  env -u GNLINK_PUBLIC_BASE GNLINK_REMOTE_MODE=local GNLINK_LOCAL_ROOT="$root" \\
    GNLINK_VERIFY_PUBLIC=0 GNLINK_PUBLIC_KEY_HEX="${GNLINK_PUBLIC_KEY_HEX-$TEST_KEY_HEX}" \\''')

# ---------------------------------------------------------------- case 13, rewritten
i = s.index('# ---------------------------------------------------------------------------- 13. a swap that fails half-way')
j = s.index('# ----------------------------------------------------------------------------\n\nprintf \'\\n\'\nif [ "$failures" = "0" ]; then')
case13 = r'''# ---------------------------------------------------------------------------- 13. a swap that fails half-way
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
  check "13c. ...and it does not claim clients refuse it" "$(grep -q 'refuse' "$LAST_OUT" && echo 0 || echo 1)"
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
      "$([ "$(state_line pair-state)" = unknown ] && ! grep -q 'refuse\|verifies' "$LAST_OUT" && echo 1 || echo 0)" "$(state_line pair-state)"
# 13f. a normal publish has neither line, and names the restart rule
ROOT13F="$WORK/root13f"; mkdir -p "$ROOT13F"
run_deploy "$ROOT13F" "$REL13"; rc=$?
check "13f. a normal publish: exit 0, no swap-state / pair-state" \
      "$([ $rc -eq 0 ] && ! grep -q '^swap-state\|^pair-state' "$LAST_OUT" && echo 1 || echo 0)" "exit=$rc"
check "13f. ...and the restart line names code, modules and env" \
      "$(grep -q 'a module it requires, or its env' "$LAST_OUT" && echo 1 || echo 0)"

'''
s = s[:i] + case13 + s[j:]
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')
