import sys
p = sys.argv[1]
s = open(p, 'rb').read().decode('utf-8')
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('\r\n', '\n')
anchor = """# ----------------------------------------------------------------------------

printf '\\n'
if [ "$failures" = "0" ]; then"""
assert s.count(anchor) == 1
case = r'''# ---------------------------------------------------------------------------- 13. a swap that fails half-way
# maint r1: the two renames of the publish step are told apart when they fail. The failure is made the only way a
# local root can make one rename fail and not the other: an `mv` earlier on the PATH that refuses one file name and
# hands everything else to the real one. Everything else is the real script.
printf '\n13. a swap that fails half-way\n'
SHIM13="$WORK/shim13"; mkdir -p "$SHIM13"
REAL_MV="$(command -v mv)"
cat > "$SHIM13/mv" <<SHIM
#!/usr/bin/env bash
for a in "\$@"; do
  case "\$a" in *"\$GNLINK_TEST_FAIL_MV_SUFFIX") echo "mv: refused by the test (\$a)" >&2; exit 1 ;; esac
done
exec "$REAL_MV" "\$@"
SHIM
chmod +x "$SHIM13/mv"
run_deploy_failing_mv() {
  local root="$1" rel="$2" suffix="$3"
  LAST_OUT="$WORK/out.$$.txt"
  PATH="$SHIM13:$PATH" GNLINK_TEST_FAIL_MV_SUFFIX="$suffix" \
    GNLINK_REMOTE_MODE=local GNLINK_LOCAL_ROOT="$root" GNLINK_VERIFY_PUBLIC=0 \
    GNLINK_PUBLIC_BASE="$FIXTURE_BASE" \
    bash "$DEPLOY" --release-dir "$rel" --platform windows >"$LAST_OUT" 2>&1
  return $?
}
# 13a. the manifest is renamed, the signature is not: the pair is mismatched now
ROOT13A="$WORK/root13a"; mkdir -p "$ROOT13A"
REL13="$WORK/rel13"; copy_release "$REL13"
run_deploy_failing_mv "$ROOT13A" "$REL13" ".sig.tmp"; rc=$?
check "13a. the signature rename fails: exit 4" "$([ $rc -eq 4 ] && echo 1 || echo 0)" "exit=$rc"
check "13a. ...and it says which: manifest-only" \
      "$(grep -q '^swap-state manifest-only' "$LAST_OUT" && echo 1 || echo 0)" "$(grep '^swap-state' "$LAST_OUT")"
check "13a. ...in words: the published pair does not match" \
      "$(grep -q 'does not match' "$LAST_OUT" && echo 1 || echo 0)"
check "13a. ...and it is true: the NEW manifest is published, the signature is still only a .tmp" \
      "$([ "$(published_sha "$ROOT13A")" = "$(sha256sum -- "$REL13/windows.manifest" | cut -d' ' -f1)" ] && \
          [ -f "$ROOT13A/update-manifests/windows.sig.tmp" ] && [ ! -e "$ROOT13A/update-manifests/windows.sig" ] && echo 1 || echo 0)"
check "13a. ...and the old wording that would mislead here is gone" \
      "$(grep -q 'previous release is still published' "$LAST_OUT" && echo 0 || echo 1)"
# 13b. the first rename fails: nothing was replaced
ROOT13B="$WORK/root13b"; mkdir -p "$ROOT13B"
run_deploy_failing_mv "$ROOT13B" "$REL13" ".manifest.tmp"; rc=$?
check "13b. the manifest rename fails: exit 4" "$([ $rc -eq 4 ] && echo 1 || echo 0)" "exit=$rc"
check "13b. ...and it says which: neither" \
      "$(grep -q '^swap-state neither' "$LAST_OUT" && echo 1 || echo 0)" "$(grep '^swap-state' "$LAST_OUT")"
check "13b. ...and it is true: nothing new is published, both .tmp are still there" \
      "$([ "$(published_sha "$ROOT13B")" = "NONE" ] && [ -f "$ROOT13B/update-manifests/windows.manifest.tmp" ] && \
          [ -f "$ROOT13B/update-manifests/windows.sig.tmp" ] && echo 1 || echo 0)"
# 13c. a publish that does not fail still says nothing about a swap state, and names the restart rule
ROOT13C="$WORK/root13c"; mkdir -p "$ROOT13C"
run_deploy "$ROOT13C" "$REL13"; rc=$?
check "13c. a normal publish: exit 0, no swap-state line" \
      "$([ $rc -eq 0 ] && ! grep -q '^swap-state' "$LAST_OUT" && echo 1 || echo 0)" "exit=$rc"
check "13c. ...and the restart line names code, modules and env" \
      "$(grep -q 'a module it requires, or its env' "$LAST_OUT" && echo 1 || echo 0)"

'''
s = s.replace(anchor, case + anchor)
open(p, 'wb').write(s.replace('\n', nl).encode('utf-8'))
print('ok')
