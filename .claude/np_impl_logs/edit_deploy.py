import sys
p = sys.argv[1]
s = open(p, 'rb').read().decode('utf-8')
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('\r\n', '\n')

old = """  if ! remote_sh "mv -f -- '$dst_manifest.tmp' '$dst_manifest' && mv -f -- '$dst_sig.tmp' '$dst_sig'"; then
    warn "temporary files retained for diagnosis; restore BOTH files from the recorded backup"
    # Its own exit code (RV-14): unlike every other failure, this one may have CHANGED what is
    # published -- the manifest new and the signature old -- and needs a person to restore the
    # exact backup pair. The release script passes it through instead of folding it into 1.
    printf 'ERROR %s\\n' "the swap failed; the published pair may be partially replaced -- restore the backup pair" >&2
    exit 4
  fi"""
new = """  if ! remote_sh "mv -f -- '$dst_manifest.tmp' '$dst_manifest' && mv -f -- '$dst_sig.tmp' '$dst_sig'"; then
    warn "temporary files retained for diagnosis; restore BOTH files from the recorded backup"
    # Its own exit code (RV-14): unlike every other failure, this one may have CHANGED what is
    # published -- the manifest new and the signature old -- and needs a person to restore the
    # exact backup pair. The release script passes it through instead of folding it into 1.
    #
    # maint r1: which of the two renames happened is told apart -- read-only, by which .tmp is still
    # there (a rename that succeeded took its .tmp away). The exit code stays 4 in every case: even
    # "neither" is a failed publish a person has to look at, and the release script's handling of 4
    # does not change. Nothing here is undone automatically.
    local swap_state="unknown"
    if remote_sh "test ! -e '$dst_manifest.tmp' && test -e '$dst_sig.tmp'"; then
      swap_state="manifest-only"
    elif remote_sh "test -e '$dst_manifest.tmp' && test -e '$dst_sig.tmp'"; then
      swap_state="neither"
    fi
    case "$swap_state" in
      manifest-only)
        printf 'ERROR %s\\n' "the swap failed half-way: $PLATFORM.manifest is the NEW one and $PLATFORM.sig the OLD one -- the published pair does not match (clients refuse it); restore the exact backup pair by hand" >&2 ;;
      neither)
        printf 'ERROR %s\\n' "the swap failed before either file was replaced: the previous pair should still be published -- check both files against the backup's SHA256SUMS before trusting that" >&2 ;;
      *)
        printf 'ERROR %s\\n' "the swap failed and what is published could not be determined: the pair may be partially replaced -- restore the exact backup pair by hand" >&2 ;;
    esac
    printf 'swap-state %s\\n' "$swap_state" >&2
    exit 4
  fi"""
assert s.count(old) == 1, 'swap block'
s = s.replace(old, new)

old2 = '  log "no restart: only manifest files changed; server code and environment are unchanged"'
new2 = ('  # maint r1: a restart is needed when server.js, a module it requires, or its environment changes\n'
        '  # (CLAUDE.md "릴리스 배포 (NAS)"); a release changes none of them.\n'
        '  log "no restart: only the manifest pair changed -- a restart is needed only when server.js, a module it requires, or its env changes"')
assert s.count(old2) == 1, 'restart log'
s = s.replace(old2, new2)
open(p, 'wb').write(s.replace('\n', nl).encode('utf-8'))
print('ok', repr(nl))
