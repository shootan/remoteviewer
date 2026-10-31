p = r'C:\work\gnlink-wt\maint-r1\automation\gnlink_deploy.sh'
b = open(p, 'rb').read().decode('utf-8')
s = b.replace('\r\n', '\n')

def rep(a, c):
    global s
    assert s.count(a) == 1, (s.count(a), a[:80])
    s = s.replace(a, c)

rep('''    # maint r3 (Codex M2): a state is only said when the query that says it RAN. Each query exits 0 and prints one
    # typed word; a non-zero exit (ssh 255, a remote command that failed) or any other output is "unknown" -- never
    # read as "missing". Likewise only the verifier's own REFUSED (exit 3) is "does-not-verify"; node that cannot
    # run, a file or key it cannot read, anything thrown, is "unknown".
    local swap_state="unknown" answer=""
    answer="$(remote_sh "if test ! -e '$dst_manifest.tmp' && test -e '$dst_sig.tmp'; then echo gnlink-swap-manifest-only; elif test -e '$dst_manifest.tmp' && test -e '$dst_sig.tmp'; then echo gnlink-swap-neither; else echo gnlink-swap-other; fi" 2>/dev/null)" || answer=""
    case "$answer" in
      gnlink-swap-manifest-only) swap_state="manifest-only" ;;
      gnlink-swap-neither)       swap_state="neither" ;;
    esac
''', '''    # maint r3/r4 (Codex M2): a state is only said when the query that says it RAN. Only the verifier's own REFUSED
    # (exit 3) is "does-not-verify"; node that cannot run, a file or key it cannot read, anything thrown, is
    # "unknown". And "missing" is only said when the directory was LISTED: one read-only listing of the manifest
    # directory (name and type of each entry), which must end with its own end mark. `test -f x` cannot tell
    # "x is not there" from "x could not be looked at" (both false), so it is not used for this. A listing that
    # failed anywhere (find exits non-zero on any entry it cannot read, ssh 255, no end mark) is "unknown".
    local swap_state="unknown" listing="" listed=0
    listing="$(remote_sh "find '$MANIFEST_DIR' -mindepth 1 -maxdepth 1 -printf '%y %f\\\\n' && echo gnlink-list-end" 2>/dev/null)" || listing=""
    case "$listing" in *gnlink-list-end) listed=1 ;; esac
    has_entry() { printf '%s\\n' "$listing" | grep -qxF -- "$1"; }
    if [ "$listed" = "1" ]; then
      if ! has_entry "f $PLATFORM.manifest.tmp" && has_entry "f $PLATFORM.sig.tmp"; then
        swap_state="manifest-only"
      elif has_entry "f $PLATFORM.manifest.tmp" && has_entry "f $PLATFORM.sig.tmp"; then
        swap_state="neither"
      fi
    fi
''')
rep('''      answer="$(remote_sh "if test -f '$dst_manifest' && test -f '$dst_sig'; then echo gnlink-pair-present; else echo gnlink-pair-missing; fi" 2>/dev/null)" || answer=""
      if [ "$answer" = "gnlink-pair-missing" ]; then
        pair_state="incomplete"
      elif [ "$answer" = "gnlink-pair-present" ] && remote_sh''', '''      if [ "$listed" != "1" ]; then
        :  # not listed: unknown
      elif ! has_entry "f $PLATFORM.manifest" || ! has_entry "f $PLATFORM.sig"; then
        pair_state="incomplete"  # listed, and one of the two is not there as a regular file
      elif remote_sh''')
open(p, 'wb').write(s.replace('\n', '\r\n').encode('utf-8'))
print('ok')
