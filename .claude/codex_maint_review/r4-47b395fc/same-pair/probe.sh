#!/usr/bin/env bash
set -uo pipefail
PLATFORM=windows
VERSION=0.2.163
DRY_RUN=0
REPO_ROOT=/d/remote/remote/.claude/codex_maint_review/r4-47b395fc/repo
MANIFEST_DIR=/d/remote/remote/.claude/codex_maint_review/r4-47b395fc/same-pair/published
MANIFEST=/d/remote/remote/.claude/codex_maint_review/r4-47b395fc/windows.manifest
SIGNATURE=/d/remote/remote/.claude/codex_maint_review/r4-47b395fc/windows.sig
PATH=/d/remote/remote/.claude/codex_maint_review/r4-47b395fc/same-pair/shim:$PATH
export PATH
unset GNLINK_PUBLIC_KEY_HEX NODE_OPTIONS
chmod +x /d/remote/remote/.claude/codex_maint_review/r4-47b395fc/same-pair/shim/mv
step() { :; }
log() { printf '%s\n' "$*"; }
warn() { printf '%s\n' "$*" >&2; }
assert_inside_root() { case "$1" in /d/remote/remote/.claude/codex_maint_review/r4-47b395fc/*) ;; *) exit 91 ;; esac; }
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
    # maint r3/r4 (Codex M2): a state is only said when the query that says it RAN. Only the verifier's own REFUSED
    # (exit 3) is "does-not-verify"; node that cannot run, a file or key it cannot read, anything thrown, is
    # "unknown". And "missing" is only said when the directory was LISTED: one read-only listing of the manifest
    # directory (name and type of each entry, a symlink by what it points to), which must end with its own end mark. `test -f x` cannot tell
    # "x is not there" from "x could not be looked at" (both false), so it is not used for this. A listing that
    # failed anywhere (find exits non-zero on any entry it cannot read, ssh 255, no end mark) is "unknown".
    local swap_state="unknown" listing="" listed=0
    listing="$(remote_sh "find '$MANIFEST_DIR' -mindepth 1 -maxdepth 1 -printf '%Y %f\\n' && echo gnlink-list-end" 2>/dev/null)" || listing=""
    case "$listing" in *gnlink-list-end) listed=1 ;; esac
    has_entry() { printf '%s\n' "$listing" | grep -qxF -- "$1"; }
    if [ "$listed" = "1" ]; then
      if ! has_entry "f $PLATFORM.manifest.tmp" && has_entry "f $PLATFORM.sig.tmp"; then
        swap_state="manifest-only"
      elif has_entry "f $PLATFORM.manifest.tmp" && has_entry "f $PLATFORM.sig.tmp"; then
        swap_state="neither"
      fi
    fi
    local pair_state="unknown" pdir="" vrc=0
    mkdir -p "$REPO_ROOT/.claude/deploy-tmp" 2>/dev/null
    pdir="$(mktemp -d "$REPO_ROOT/.claude/deploy-tmp/pair.XXXXXX" 2>/dev/null)" || pdir=""
    if [ -n "$pdir" ]; then
      if [ "$listed" != "1" ]; then
        :  # not listed: unknown
      elif ! has_entry "f $PLATFORM.manifest" || ! has_entry "f $PLATFORM.sig"; then
        pair_state="incomplete"  # listed, and one of the two is not there as a regular file
      elif remote_sh "cat -- '$dst_manifest'" > "$pdir/m" 2>/dev/null && remote_sh "cat -- '$dst_sig'" > "$pdir/s" 2>/dev/null; then
        vrc=0
        node "$REPO_ROOT/automation/gnlink_verify_manifest.js" "$pdir/m" "$pdir/s" "$PLATFORM" >/dev/null 2>&1 || vrc=$?
        if [ "$vrc" -eq 0 ]; then
          if cmp -s "$pdir/m" "$MANIFEST" && cmp -s "$pdir/s" "$SIGNATURE"; then
            pair_state="verified-this-release"
          else
            pair_state="verified-another-pair"
          fi
        elif [ "$vrc" -eq 3 ]; then
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
        printf 'ERROR %s\n' "the swap failed; the pair published now verifies but is NOT this release (the previous pair, intact?) -- check it against the backup's SHA256SUMS; restore the exact backup pair only if it is not the one that should be published" >&2 ;;
      does-not-verify)
        printf 'ERROR %s\n' "the swap failed and the published manifest and signature do NOT verify together -- clients will refuse this update; restore the exact backup pair by hand" >&2 ;;
      incomplete)
        printf 'ERROR %s\n' "the swap failed and the published pair is incomplete (manifest or signature missing) -- restore the exact backup pair by hand" >&2 ;;
      *)
        printf 'ERROR %s\n' "the swap failed and the published pair could not be checked (a query or the verifier did not run) -- nothing is known about it; check both files by hand, and restore the exact backup pair if they are not a verifying pair" >&2 ;;
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
