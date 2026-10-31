#!/usr/bin/env bash
set -uo pipefail
PLATFORM=windows
VERSION=0.2.163
DRY_RUN=0
MANIFEST_DIR=D:/remote/remote/.claude/codex_maint_review/same-pair-0d7cc9f12e/published
MANIFEST=C:/work/gnlink-wt/rel-0163/.claude/rel-final/0.2.163/windows.manifest
SIGNATURE=C:/work/gnlink-wt/rel-0163/.claude/rel-final/0.2.163/windows.sig
PATH=D:/remote/remote/.claude/codex_maint_review/same-pair-0d7cc9f12e/shim:$PATH
export PATH
chmod +x D:/remote/remote/.claude/codex_maint_review/same-pair-0d7cc9f12e/shim/mv
step() { :; }
log() { printf '%s\n' "$*"; }
warn() { printf '%s\n' "$*" >&2; }
assert_inside_root() { :; }
remote_put() { cp -- "$1" "$2"; }
remote_sh() { bash -c "$1"; }
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
        printf 'ERROR %s\n' "the swap failed half-way: $PLATFORM.manifest is the NEW one and $PLATFORM.sig the OLD one -- the published pair does not match (clients refuse it); restore the exact backup pair by hand" >&2 ;;
      neither)
        printf 'ERROR %s\n' "the swap failed before either file was replaced: the previous pair should still be published -- check both files against the backup's SHA256SUMS before trusting that" >&2 ;;
      *)
        printf 'ERROR %s\n' "the swap failed and what is published could not be determined: the pair may be partially replaced -- restore the exact backup pair by hand" >&2 ;;
    esac
    printf 'swap-state %s\n' "$swap_state" >&2
    exit 4
  fi
  log "published $VERSION"
  # maint r1: a restart is needed when server.js, a module it requires, or its environment changes
  # (CLAUDE.md "릴리스 배포 (NAS)"); a release changes none of them.
  log "no restart: only the manifest pair changed -- a restart is needed only when server.js, a module it requires, or its env changes"
}

publish_pair
