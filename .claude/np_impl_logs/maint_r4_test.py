p = r'C:\work\gnlink-wt\maint-r1\automation\gnlink_deploy_test.sh'
s = open(p, 'rb').read().decode('utf-8').replace('\r\n', '\n')
i = s.index('# maint r3 (Codex M2): a check that did not RUN')
j = s.index('# 13f. a normal publish has neither line')
block = r'''# maint r3/r4 (Codex M2): a check that did not RUN is "unknown", never "missing" or "refused". Shims in their own
# directory so the cases above are untouched; each one leaves a mark when it injects, so a case can show its fault
# was reached (a fault that never fired proves nothing):
#  - `bash`: fails (255, as ssh does) the one remote command whose text carries $GNLINK_TEST_FAIL_SH_MATCH -- the
#    local root runs every remote command through `bash -c`
#  - `find`: with GNLINK_TEST_FIND_FAULT=1, lists the manifest directory as the real one does and then exits 1, the
#    way find ends when one entry could not be read (the listing looks complete; only its exit says otherwise)
#  - `node`: for the read-back pair only (deploy-tmp/pair.*), cannot run (127) or dies with node's own exit 1
SHIM13M="$WORK/shim13m"; mkdir -p "$SHIM13M"
HIT13M="$WORK/hit13m"; mkdir -p "$HIT13M"
REAL_BASH="$(command -v bash)"
REAL_NODE="$(command -v node)"
REAL_FIND="$(command -v find)"
cat > "$SHIM13M/bash" <<SHIM
#!$REAL_BASH
if [ "\${1:-}" = "-c" ] && [ -n "\${GNLINK_TEST_FAIL_SH_MATCH:-}" ]; then
  case "\${2:-}" in *"\$GNLINK_TEST_FAIL_SH_MATCH"*) : > "$HIT13M/bash"; echo "ssh: the test dropped this query" >&2; exit 255 ;; esac
fi
exec "$REAL_BASH" "\$@"
SHIM
cat > "$SHIM13M/find" <<SHIM
#!$REAL_BASH
if [ "\${GNLINK_TEST_FIND_FAULT:-}" = "1" ]; then
  case " \$* " in *update-manifests*)
    "$REAL_FIND" "\$@"
    : > "$HIT13M/find"; echo "find: an entry could not be read (the test)" >&2; exit 1 ;;
  esac
fi
exec "$REAL_FIND" "\$@"
SHIM
cat > "$SHIM13M/node" <<SHIM
#!$REAL_BASH
for a in "\$@"; do
  case "\$a" in *deploy-tmp/pair.*)
    case "\${GNLINK_TEST_NODE_FAULT:-}" in
      missing) : > "$HIT13M/node"; echo "node: command not found (the test)" >&2; exit 127 ;;
      throw)   : > "$HIT13M/node"; echo "Error: thrown by the test" >&2; exit 1 ;;
    esac ;;
  esac
done
exec "$REAL_NODE" "\$@"
SHIM
chmod +x "$SHIM13M/bash" "$SHIM13M/find" "$SHIM13M/node"
run_deploy_m2() {
  local root="$1" rel="$2" shMatch="$3" nodeFault="$4" findFault="${5:-}"
  rm -f "$HIT13M"/*
  PATH="$SHIM13M:$PATH" GNLINK_TEST_FAIL_SH_MATCH="$shMatch" GNLINK_TEST_NODE_FAULT="$nodeFault" \
    GNLINK_TEST_FIND_FAULT="$findFault" run_deploy_failing "$root" "$rel" ".sig.tmp"
}
hit() { [ -e "$HIT13M/$1" ]; }
pair_sha() { printf '%s %s' "$(sha256sum -- "$1/update-manifests/windows.manifest" 2>/dev/null | cut -d' ' -f1)" \
                            "$(sha256sum -- "$1/update-manifests/windows.sig" 2>/dev/null | cut -d' ' -f1)"; }
no_claim() { ! grep '^ERROR the swap' "$LAST_OUT" | grep -q 'refuse\|incomplete\|missing\|verifies\|is THIS'; }
# 13g. the directory listing does not run (255): pair and phase unknown -- before r3 this said "incomplete"
ROOT13G="$WORK/root13g"; mkdir -p "$ROOT13G"
run_deploy "$ROOT13G" "$REL13" >/dev/null 2>&1
before13g="$(pair_sha "$ROOT13G")"
run_deploy_m2 "$ROOT13G" "$REL13" "gnlink-list-end" ""; rc=$?
check "13g. the listing query fails (255): exit 4, and the fault was reached" "$([ $rc -eq 4 ] && hit bash && echo 1 || echo 0)" "exit=$rc"
check "13g. ...published: unknown (not incomplete), phase unknown, and no claim either way" \
      "$([ "$(state_line pair-state)" = unknown ] && [ "$(state_line swap-state)" = unknown ] && no_claim && echo 1 || echo 0)" \
      "$(state_line pair-state) $(state_line swap-state)"
check "13g. ...and it is true that the pair is whole: the same bytes as before" \
      "$([ "$(pair_sha "$ROOT13G")" = "$before13g" ] && echo 1 || echo 0)"
# 13h. (r4, Codex M2-b) the listing RAN but an entry could not be read inside it (find exit 1, full output): unknown
ROOT13H="$WORK/root13h"; mkdir -p "$ROOT13H"
run_deploy "$ROOT13H" "$REL13" >/dev/null 2>&1
before13h="$(pair_sha "$ROOT13H")"
run_deploy_m2 "$ROOT13H" "$REL13" "" "" "1"; rc=$?
check "13h. an entry cannot be read inside the listing: exit 4, and the fault was reached" "$([ $rc -eq 4 ] && hit find && echo 1 || echo 0)" "exit=$rc"
check "13h. ...published: unknown (not incomplete), phase unknown, and no claim either way" \
      "$([ "$(state_line pair-state)" = unknown ] && [ "$(state_line swap-state)" = unknown ] && no_claim && echo 1 || echo 0)" \
      "$(state_line pair-state) $(state_line swap-state)"
check "13h. ...and it is true that the pair is whole: the same bytes as before" \
      "$([ "$(pair_sha "$ROOT13H")" = "$before13h" ] && echo 1 || echo 0)"
# 13i. the verifier cannot run (127): unknown -- before r3 this said "does-not-verify, clients will refuse"
ROOT13I="$WORK/root13i"; mkdir -p "$ROOT13I"
run_deploy "$ROOT13I" "$REL13" >/dev/null 2>&1
before13i="$(pair_sha "$ROOT13I")"
run_deploy_m2 "$ROOT13I" "$REL13" "" "missing"; rc=$?
check "13i. the verifier cannot run (127): exit 4, and the fault was reached" "$([ $rc -eq 4 ] && hit node && echo 1 || echo 0)" "exit=$rc"
check "13i. ...published: unknown (not does-not-verify), and no claim either way" \
      "$([ "$(state_line pair-state)" = unknown ] && no_claim && echo 1 || echo 0)" "$(state_line pair-state)"
check "13i. ...and it is true that the pair verifies: the same bytes as before" \
      "$([ "$(pair_sha "$ROOT13I")" = "$before13i" ] && echo 1 || echo 0)"
# 13j. the verifier dies (node's own exit 1, an uncaught error): unknown
ROOT13J="$WORK/root13j"; mkdir -p "$ROOT13J"
run_deploy "$ROOT13J" "$REL13" >/dev/null 2>&1
run_deploy_m2 "$ROOT13J" "$REL13" "" "throw"; rc=$?
check "13j. the verifier dies (exit 1): exit 4, published: unknown, the fault reached" \
      "$([ $rc -eq 4 ] && [ "$(state_line pair-state)" = unknown ] && no_claim && hit node && echo 1 || echo 0)" "exit=$rc $(state_line pair-state)"
# 13k. the verifier's own codes, run for real: a refusal is 3 and only a refusal; a check that cannot be done is 4.
# (r4, Codex M2-a) crypto.verify made to throw inside the real CLI and the real shared module (a --require preload
# that replaces it, leaving a mark when it runs): the shared module folds that into SignatureInvalid; the CLI must
# not call it a refusal.
VJS="$(cygpath -w "$REPO/automation/gnlink_verify_manifest.js")"
vcode() { local rc=0; node "$VJS" "$@" >/dev/null 2>&1 || rc=$?; echo "$rc"; }
THROW13K="$WORK/crypto-verify-throws.js"
HIT13K="$WORK/hit13k-crypto"
cat > "$THROW13K" <<JS
const crypto = require('crypto');
const fs = require('fs');
crypto.verify = function () { fs.writeFileSync(process.env.GNLINK_TEST_HIT, 'x'); throw new Error('provider failure (the test)'); };
JS
vcode_throwing() {
  rm -f "$HIT13K"
  GNLINK_TEST_HIT="$(cygpath -w "$HIT13K")" NODE_OPTIONS="--require $(cygpath -w "$THROW13K")" vcode "$@"
}
if [ -n "$TEST_KEY_HEX" ] && [ -n "${RELO:-}" ]; then
  M13="$(cygpath -w "$REL13/windows.manifest")"; S13="$(cygpath -w "$REL13/windows.sig")"; SO="$(cygpath -w "$RELO/windows.sig")"
  # a real refusal: this release's manifest with another release's signature (both made with the test key)
  c="$(GNLINK_PUBLIC_KEY_HEX="$TEST_KEY_HEX" vcode "$M13" "$SO" windows)"
  check "13k. verifier: a document that does not verify -> 3 (REFUSED)" "$([ "$c" = 3 ] && echo 1 || echo 0)" "exit=$c"
  c="$(GNLINK_PUBLIC_KEY_HEX="$TEST_KEY_HEX" vcode "$M13" "$S13" windows)"
  check "13k. ...a pair that verifies -> 0" "$([ "$c" = 0 ] && echo 1 || echo 0)" "exit=$c"
  c="$(GNLINK_PUBLIC_KEY_HEX="$TEST_KEY_HEX" vcode_throwing "$M13" "$S13" windows)"
  check "13k. ...a valid pair while crypto.verify throws -> 4 (not 3), and the throw was reached" \
        "$([ "$c" = 4 ] && [ -e "$HIT13K" ] && echo 1 || echo 0)" "exit=$c"
  c="$(GNLINK_PUBLIC_KEY_HEX="$TEST_KEY_HEX" vcode_throwing "$M13" "$SO" windows)"
  check "13k. ...a mismatched pair while crypto.verify throws -> 4 (it was not observed)" \
        "$([ "$c" = 4 ] && [ -e "$HIT13K" ] && echo 1 || echo 0)" "exit=$c"
  c="$(printf 'zz' > "$WORK/bad.sig"; GNLINK_PUBLIC_KEY_HEX="$TEST_KEY_HEX" vcode "$M13" "$(cygpath -w "$WORK/bad.sig")" windows)"
  check "13k. ...a signature that is not a signature -> 3" "$([ "$c" = 3 ] && echo 1 || echo 0)" "exit=$c"
fi
c="$(GNLINK_PUBLIC_KEY_HEX="${TEST_KEY_HEX:-}" vcode "$(cygpath -w "$WORK/no-such.manifest")" "$(cygpath -w "$REL13/windows.sig")" windows)"
check "13k. ...a file it cannot read -> 4 (not 3)" "$([ "$c" = 4 ] && echo 1 || echo 0)" "exit=$c"
c="$(GNLINK_PUBLIC_KEY_HEX="zz" vcode "$(cygpath -w "$REL13/windows.manifest")" "$(cygpath -w "$REL13/windows.sig")" windows)"
check "13k. ...a key it cannot use -> 4 (not 3)" "$([ "$c" = 4 ] && echo 1 || echo 0)" "exit=$c"

'''
s = s[:i] + block + s[j:]
open(p, 'wb').write(s.replace('\n', '\r\n').encode('utf-8'))
print('ok')
