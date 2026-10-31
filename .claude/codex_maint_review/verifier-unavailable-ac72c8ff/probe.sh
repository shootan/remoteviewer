#!/usr/bin/env bash
set -uo pipefail
PLATFORM=windows
VERSION=0.2.163
DRY_RUN=0
REPO_ROOT=/d/remote/remote
MANIFEST_DIR=/d/remote/remote/.claude/codex_maint_review/verifier-unavailable-ac72c8ff/published
MANIFEST=/c/work/gnlink-wt/rel-0163/.claude/rel-final/0.2.163/windows.manifest
SIGNATURE=/c/work/gnlink-wt/rel-0163/.claude/rel-final/0.2.163/windows.sig
PATH=/d/remote/remote/.claude/codex_maint_review/verifier-unavailable-ac72c8ff/shim:$PATH
export PATH
chmod +x /d/remote/remote/.claude/codex_maint_review/verifier-unavailable-ac72c8ff/shim/mv
chmod +x /d/remote/remote/.claude/codex_maint_review/verifier-unavailable-ac72c8ff/shim/node
step() { :; }
log() { printf '%s\n' "$*"; }
warn() { printf '%s\n' "$*" >&2; }
assert_inside_root() { :; }
remote_put() { cp -- "$1" "$2"; }
remote_sh() { :; bash -c "$1"; }
publish_pair() {
  step "publish (the last thing that happens)"
  local dst_manifest="$MANIFEST_DIR/$PLATFORM.manifest" dst_sig="$MANIFEST_DIR/$PLATFORM.sig"
  assert_inside_root "$dst_manifest"
  assert_inside_root "$dst_sig"
  if [ "$DRY_RUN" = "1" ]; then
    log "would publish $VERSION by swapping $PLATFORM.manifest and $PLATFORM.sig (dry-run)"
    return 0
  fi
  # Both to .tmp first, then two renames back to back. rename(2) is atomic per file; what this
  # cannot make atomic is the PAIR, so the window is two renames wide instead of one upload wide.
  remote_put "$MANIFEST"  "$dst_manifest.tmp"
  remote_put "$SIGNATURE" "$dst_sig.tmp"
  remote_sh "chmod 644 -- '$dst_manifest.tmp' '$dst_sig.tmp'"
  if ! remote_sh "mv -f -- '$dst_manifest.tmp' '$dst_manifest' && mv -f -- '$dst_sig.tmp' '$dst_sig'"; then
    warn "temporary files retained for diagnosis; restore BOTH files from the recorded backup"
    # Its own exit code (RV-14): unlike every other failure, this one may have CHANGED what is
    # published -- the manifest new and the signature old -- and needs a person to restore the
    # exact backup pair. The release script passes it through instead of folding it into 1.
    #
    # maint r1/r2: two separate things are said, and only the second is a statement about what clients get.
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
  fi
  log "published $VERSION"
  # maint r1: a restart is needed when server.js, a module it requires, or its environment changes
  # (CLAUDE.md "릴리스 배포 (NAS)"); a release changes none of them.
  log "no restart: only the manifest pair changed -- a restart is needed only when server.js, a module it requires, or its env changes"
}

publish_pair
