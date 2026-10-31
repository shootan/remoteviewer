p = r'C:\work\gnlink-wt\maint-r1\automation\gnlink_deploy.sh'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')
start = s.index('    # maint r1: which of the two renames happened is told apart')
end = s.index('    printf \'swap-state %s\\n\' "$swap_state" >&2\n    exit 4\n  fi')
end_full = end + len('    printf \'swap-state %s\\n\' "$swap_state" >&2\n    exit 4\n  fi')
new = r'''    # maint r1/r2: two separate things are said, and only the second is a statement about what clients get.
    #  - swap-state: which renames PROBABLY happened, read-only, from which .tmp is still there (a rename that
    #    succeeded took its .tmp away). A phase, not a verdict: the manifest renamed and the signature not can
    #    still be a valid pair (the same signed release re-published), or a mismatch (another release before).
    #  - pair-state: what is published NOW, read-only -- both files present? the same bytes as this release?
    #    do they verify together (the same verifier and key as the checks before the swap)?
    # The exit code stays 4 in every case: a failed publish a person has to look at. Nothing is undone or
    # cleaned up here (Codex maint M1: no automatic rollback, no blind cleanup).
    local swap_state="unknown"
    if remote_sh "test ! -e '$dst_manifest.tmp' && test -e '$dst_sig.tmp'"; then
      swap_state="manifest-only"
    elif remote_sh "test -e '$dst_manifest.tmp' && test -e '$dst_sig.tmp'"; then
      swap_state="neither"
    fi
    local pair_state="unknown" pdir=""
    mkdir -p "$REPO_ROOT/.claude/deploy-tmp" 2>/dev/null
    pdir="$(mktemp -d "$REPO_ROOT/.claude/deploy-tmp/pair.XXXXXX" 2>/dev/null)" || pdir=""
    if [ -n "$pdir" ]; then
      if ! remote_sh "test -f '$dst_manifest' && test -f '$dst_sig'"; then
        pair_state="incomplete"
      elif remote_sh "cat -- '$dst_manifest'" > "$pdir/m" 2>/dev/null && remote_sh "cat -- '$dst_sig'" > "$pdir/s" 2>/dev/null; then
        if node "$REPO_ROOT/automation/gnlink_verify_manifest.js" "$pdir/m" "$pdir/s" "$PLATFORM" >/dev/null 2>&1; then
          if cmp -s "$pdir/m" "$MANIFEST" && cmp -s "$pdir/s" "$SIGNATURE"; then
            pair_state="verified-this-release"
          else
            pair_state="verified-another-pair"
          fi
        else
          pair_state="does-not-verify"
        fi
      fi
      rm -f -- "$pdir/m" "$pdir/s"
      rmdir -- "$pdir" 2>/dev/null
    fi
    case "$pair_state" in
      verified-this-release)
        printf 'ERROR %s\n' "the swap reported a failure, but the pair published now is THIS release's and verifies -- confirm both files against this release, then re-run" >&2 ;;
      verified-another-pair)
        printf 'ERROR %s\n' "the swap failed; the pair published now verifies but is NOT this release (the previous pair, intact?) -- check it against the backup's SHA256SUMS" >&2 ;;
      does-not-verify)
        printf 'ERROR %s\n' "the swap failed and the published manifest and signature do NOT verify together -- clients will refuse this update; restore the exact backup pair by hand" >&2 ;;
      incomplete)
        printf 'ERROR %s\n' "the swap failed and the published pair is incomplete (manifest or signature missing) -- restore the exact backup pair by hand" >&2 ;;
      *)
        printf 'ERROR %s\n' "the swap failed and the published pair could not be checked -- it may be partially replaced; check both files and restore the exact backup pair by hand" >&2 ;;
    esac
    printf 'swap-state %s\n' "$swap_state" >&2
    printf 'pair-state %s\n' "$pair_state" >&2
    exit 4
  fi'''
s = s[:start] + new + s[end_full:]
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')
