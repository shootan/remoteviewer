#!/usr/bin/env bash
#
# Publishes a signed GNLink release to the NAS as the `gnlink` account.
#
# This encodes a procedure that had only ever been carried out by hand, one release at a time.
# Doing it by hand is how 0.2.108 shipped a manifest that verified while naming an APK that no
# longer existed, and how the same release spent an afternoon returning 403 because two files
# landed with the wrong mode. Neither was a thinking error; both were steps that are easy to skip
# and impossible to see afterwards.
#
# The order below is the whole point and is not negotiable:
#
#   preflight -> upload artifacts -> verify them ON THE SERVER -> back up the old pair
#             -> replace manifest+sig with two renames, LAST -> verify outside over https
#
# The manifest is what makes a release public. Publishing it before its artifacts are in place
# opens a window in which the update endpoint hands out a document pointing at files that 404,
# and a host that reads it during that window reports a failed download rather than "not yet".
#
# What this script may not do, by design:
#   - rebuild anything (it publishes an existing build, at a fixed commit)
#   - touch nginx, systemd, env or any other service (the gnlink account cannot, and should not)
#   - delete anything outside the release paths it manages
#   - overwrite a version directory whose bytes differ from what is being published
#   - carry a password anywhere: no argv, no environment echo, no log, no repository
#
# Usage:
#   automation/gnlink_deploy.sh --release-dir .claude/rel/0.2.109 --dry-run
#   automation/gnlink_deploy.sh --release-dir .claude/rel/0.2.109
#   automation/gnlink_deploy.sh --release-dir .claude/rel/0.2.109 --verify-only
#
# --verify-only compares what is already published against the release directory and changes
# nothing. It is the right first command against a server whose state you did not create.

set -euo pipefail

# ---------------------------------------------------------------------------- configuration
#
# Everything here is overridable from the environment so the test suite can point the whole
# thing at a directory on this machine. Nothing here is a secret: the key is a path, and the
# key file itself never leaves the disk it lives on.

GNLINK_DEPLOY_HOST="${GNLINK_DEPLOY_HOST:-gnlink@192.168.0.6}"
GNLINK_DEPLOY_KEY="${GNLINK_DEPLOY_KEY:-$HOME/.ssh/remote60_deploy}"
GNLINK_REMOTE_ROOT="${GNLINK_REMOTE_ROOT:-/opt/gnlink}"
GNLINK_UPDATES_SUBDIR="${GNLINK_UPDATES_SUBDIR:-updates}"
GNLINK_MANIFEST_SUBDIR="${GNLINK_MANIFEST_SUBDIR:-update-manifests}"
# gnlink-owned on purpose. /opt/gnlink/backups is root-owned, so a backup written there fails --
# and a backup step that fails silently is worse than none, because rollback then has nothing.
GNLINK_BACKUP_SUBDIR="${GNLINK_BACKUP_SUBDIR:-manifest-backups}"
GNLINK_PUBLIC_BASE="${GNLINK_PUBLIC_BASE:-https://rem.shotan.net}"
# ssh = the real thing. local = the same steps against a directory on this machine, which is how
# the tests exercise ordering, idempotence and the failure paths without touching the NAS.
GNLINK_REMOTE_MODE="${GNLINK_REMOTE_MODE:-ssh}"
GNLINK_LOCAL_ROOT="${GNLINK_LOCAL_ROOT:-}"
# Empty means external verification is skipped and SAID to be skipped. It is never assumed.
GNLINK_VERIFY_PUBLIC="${GNLINK_VERIFY_PUBLIC:-1}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

RELEASE_DIR=""
PLATFORM="windows"
DRY_RUN=0
VERIFY_ONLY=0

# ---------------------------------------------------------------------------- output
#
# Two streams on purpose: progress on stdout, problems on stderr. Nothing printed here ever
# contains a credential -- the only secret in this procedure is an ssh key that is used by path
# and never read by this script.

log()  { printf '%s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
warn() { printf 'WARN  %s\n' "$*" >&2; }
die()  { printf 'ERROR %s\n' "$*" >&2; exit 1; }

# A failure that a human with sudo has to resolve. Printed as the exact operations to perform,
# because "escalate to the NAS session" without the commands just moves the guessing.
escalate() {
  printf '\nESCALATE -- the gnlink account cannot do this. Hand these exact operations over:\n' >&2
  printf '%s\n' "$@" >&2
  exit 3
}

usage() {
  sed -n '3,40p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
  exit 2
}

# ---------------------------------------------------------------------------- arguments

while [ $# -gt 0 ]; do
  case "$1" in
    --release-dir) RELEASE_DIR="${2:-}"; shift 2 ;;
    --platform)    PLATFORM="${2:-}"; shift 2 ;;
    --dry-run)     DRY_RUN=1; shift ;;
    --verify-only) VERIFY_ONLY=1; shift ;;
    -h|--help)     usage ;;
    *) die "unknown argument: $1 (try --help)" ;;
  esac
done

[ -n "$RELEASE_DIR" ] || die "--release-dir is required"
[ -d "$RELEASE_DIR" ] || die "no such release directory: $RELEASE_DIR"

MANIFEST="$RELEASE_DIR/$PLATFORM.manifest"
SIGNATURE="$RELEASE_DIR/$PLATFORM.sig"
PAYLOAD="$RELEASE_DIR/payload"

[ -f "$MANIFEST" ]  || die "no manifest at $MANIFEST"
[ -f "$SIGNATURE" ] || die "no signature at $SIGNATURE"
[ -d "$PAYLOAD" ]   || die "no payload directory at $PAYLOAD"

# ---------------------------------------------------------------------------- remote access
#
# One place decides how a command reaches the server, so the tests drive exactly the code that
# production drives. A test that re-implements the steps proves the test works.

remote_sh() {
  if [ "$GNLINK_REMOTE_MODE" = "local" ]; then
    bash -c "$1"
  else
    ssh -i "$GNLINK_DEPLOY_KEY" -o BatchMode=yes -o StrictHostKeyChecking=accept-new \
        "$GNLINK_DEPLOY_HOST" "$1"
  fi
}

# Local path for a remote path -- only meaningful in local mode, where the "server" is a
# directory. In ssh mode the remote path is used as-is.
remote_put() {
  local src="$1" dst="$2"
  if [ "$GNLINK_REMOTE_MODE" = "local" ]; then
    mkdir -p "$(dirname "$dst")"
    cp -- "$src" "$dst"
  else
    scp -q -i "$GNLINK_DEPLOY_KEY" -o BatchMode=yes -o StrictHostKeyChecking=accept-new \
        -- "$src" "$GNLINK_DEPLOY_HOST:$dst"
  fi
}

if [ "$GNLINK_REMOTE_MODE" = "local" ]; then
  [ -n "$GNLINK_LOCAL_ROOT" ] || die "GNLINK_REMOTE_MODE=local needs GNLINK_LOCAL_ROOT"
  GNLINK_REMOTE_ROOT="$GNLINK_LOCAL_ROOT"
fi

UPDATES_DIR="$GNLINK_REMOTE_ROOT/$GNLINK_UPDATES_SUBDIR"
MANIFEST_DIR="$GNLINK_REMOTE_ROOT/$GNLINK_MANIFEST_SUBDIR"
BACKUP_DIR="$GNLINK_REMOTE_ROOT/$GNLINK_BACKUP_SUBDIR"

# Every remote path this script writes must be inside the root it was told about. Checked rather
# than trusted, because the paths are built from a manifest and a manifest is data.
assert_inside_root() {
  case "$1" in
    "$GNLINK_REMOTE_ROOT"/*) : ;;
    *) die "refusing to touch a path outside $GNLINK_REMOTE_ROOT: $1" ;;
  esac
}

# ---------------------------------------------------------------------------- the manifest
#
# Read once, here, and everything downstream works from these arrays. The manifest is the
# contract; re-parsing it in three places is how the three places drift.

VERSION=""
declare -a ART_NAME=() ART_SIZE=() ART_SHA=() ART_URL=()

parse_manifest() {
  local line
  while IFS= read -r line || [ -n "$line" ]; do
    line="${line%$'\r'}"
    case "$line" in
      version=*) VERSION="${line#version=}" ;;
      artifact=*)
        local rest="${line#artifact=}"
        local name="${rest%%|*}"; rest="${rest#*|}"
        local size="${rest%%|*}"; rest="${rest#*|}"
        local sha="${rest%%|*}";  local url="${rest#*|}"
        ART_NAME+=("$name"); ART_SIZE+=("$size"); ART_SHA+=("$sha"); ART_URL+=("$url")
        ;;
    esac
  done < "$MANIFEST"
  [ -n "$VERSION" ] || die "the manifest declares no version"
  [ "${#ART_NAME[@]}" -gt 0 ] || die "the manifest declares no artifacts"
}

# The manifest's URLs decide where the files go. That is deliberate: the URL is inside the signed
# document, so a path invented here rather than read from there would be a path nobody signed.
remote_path_for_url() {
  local url="$1" prefix="$GNLINK_PUBLIC_BASE/$GNLINK_UPDATES_SUBDIR/"
  case "$url" in
    "$prefix"*) : ;;
    *) die "artifact url is outside $prefix, refusing to guess where it goes: $url" ;;
  esac
  printf '%s/%s' "$UPDATES_DIR" "${url#"$prefix"}"
}

# The installed name uses backslashes (ui\shell.html) because that is what the updater writes on
# Windows. The file on this disk is under ui/.
local_path_for_artifact() {
  printf '%s/%s' "$PAYLOAD" "$(printf '%s' "$1" | tr '\\' '/')"
}

sha256_of() { sha256sum -- "$1" | cut -d' ' -f1; }
size_of()   { wc -c < "$1" | tr -d ' '; }

# ---------------------------------------------------------------------------- preflight

preflight() {
  step "preflight"

  command -v sha256sum >/dev/null || die "sha256sum is required"
  if [ "$GNLINK_REMOTE_MODE" = "ssh" ]; then
    command -v ssh >/dev/null || die "ssh is required"
    command -v scp >/dev/null || die "scp is required"
    [ -f "$GNLINK_DEPLOY_KEY" ] || die "no deploy key at $GNLINK_DEPLOY_KEY (path only; never its contents)"
  fi

  parse_manifest
  log "release       $VERSION ($PLATFORM), ${#ART_NAME[@]} artifacts"
  log "manifest      $(size_of "$MANIFEST") bytes  sha256=$(sha256_of "$MANIFEST")"
  log "signature     $(size_of "$SIGNATURE") bytes  sha256=$(sha256_of "$SIGNATURE")"

  # 1. The signature. Refusing here is the cheapest possible place to refuse.
  if command -v node >/dev/null && [ -f "$REPO_ROOT/apps/directory/update_manifest.js" ]; then
    if node "$REPO_ROOT/automation/gnlink_verify_manifest.js" "$MANIFEST" "$SIGNATURE" "$PLATFORM"; then
      log "signature     verified with the server's own implementation"
    else
      die "the manifest does not verify -- nothing was uploaded"
    fi
  else
    warn "node or update_manifest.js unavailable: the signature was NOT checked here"
  fi

  # 2. The manifest against the list the product actually replaces. This is the check whose
  #    absence published 0.2.109 without GNLinkSetup.exe: everything downloaded, verified and
  #    hashed, and the release died at the swap with the host already going down.
  if command -v python >/dev/null; then
    if python "$REPO_ROOT/automation/gnlink_check_payload_set.py" "$MANIFEST" >/dev/null 2>&1; then
      log "payload set    the manifest names everything the update replaces"
    else
      python "$REPO_ROOT/automation/gnlink_check_payload_set.py" "$MANIFEST" >&2 || true
      die "the manifest does not name everything this update replaces -- nothing was uploaded"
    fi
  else
    warn "python unavailable: the payload set was NOT compared against the product's list"
  fi

  # 3. The document against the disk. A signature says the document has not changed since it was
  #    signed. It says nothing about whether the files it names exist, and on 0.2.108 those two
  #    came apart -- everything verified, and the APK hash belonged to a build that was gone.
  local i local_file actual_size actual_sha
  for i in "${!ART_NAME[@]}"; do
    local_file="$(local_path_for_artifact "${ART_NAME[$i]}")"
    [ -f "$local_file" ] || die "manifest names ${ART_NAME[$i]} but there is no file at $local_file"
    actual_size="$(size_of "$local_file")"
    actual_sha="$(sha256_of "$local_file")"
    [ "$actual_size" = "${ART_SIZE[$i]}" ] || \
      die "${ART_NAME[$i]}: manifest says ${ART_SIZE[$i]} bytes, the file is $actual_size"
    [ "$actual_sha" = "${ART_SHA[$i]}" ] || \
      die "${ART_NAME[$i]}: manifest hash and file hash differ"
    assert_inside_root "$(remote_path_for_url "${ART_URL[$i]}")"
  done
  log "artifacts     ${#ART_NAME[@]}/${#ART_NAME[@]} match the manifest, byte for byte"

  # 4. The server. Reachability, the directories we own, and room.
  local probe
  probe="$(remote_sh "mkdir -p '$UPDATES_DIR' '$MANIFEST_DIR' '$BACKUP_DIR' 2>&1 && \
                      test -w '$UPDATES_DIR' && test -w '$MANIFEST_DIR' && test -w '$BACKUP_DIR' && \
                      df -Pk '$GNLINK_REMOTE_ROOT' | awk 'NR==2 {print \$4}'" 2>&1)" || \
    escalate "ssh -i <key> $GNLINK_DEPLOY_HOST" \
             "mkdir -p $UPDATES_DIR $MANIFEST_DIR $BACKUP_DIR" \
             "chown gnlink:gnlink $UPDATES_DIR $MANIFEST_DIR $BACKUP_DIR" \
             "# reported: $probe"
  local free_kb total_kb=0
  free_kb="$(printf '%s' "$probe" | tail -n 1)"
  for i in "${!ART_SIZE[@]}"; do total_kb=$(( total_kb + (ART_SIZE[i] + 1023) / 1024 )); done
  log "server        writable; ${free_kb}KB free, this release needs ~${total_kb}KB"
  # Twice the payload, so the .tmp files and the existing release both fit.
  if [ "$free_kb" -lt $(( total_kb * 2 + 1024 )) ]; then
    die "not enough room on $GNLINK_REMOTE_ROOT: ${free_kb}KB free, need ~$(( total_kb * 2 + 1024 ))KB"
  fi
}

# ---------------------------------------------------------------------------- the lock
#
# mkdir is atomic on every filesystem that matters, which is why it and not a file. Two deploys
# interleaving would produce a version directory holding half of each.

LOCK_DIR=""
release_lock() {
  if [ -n "$LOCK_DIR" ]; then
    # Only our own lock is archived. Moving it releases the active lock without deleting NAS
    # files, and leaves its owner record available when diagnosing a failed or completed publish.
    local archive="$GNLINK_REMOTE_ROOT/.gnlink-deploy-lock-history/$(date -u +%Y%m%dT%H%M%S)-$$-$RANDOM"
    assert_inside_root "$archive"
    remote_sh "mkdir -p '$GNLINK_REMOTE_ROOT/.gnlink-deploy-lock-history' && mv -- '$LOCK_DIR' '$archive'"
    LOCK_DIR=""
  fi
}
trap release_lock EXIT

take_lock() {
  if [ "$DRY_RUN" = "1" ]; then
    log "dry run: no publish lock acquired"
    return 0
  fi
  LOCK_DIR="$GNLINK_REMOTE_ROOT/.gnlink-deploy.lock"
  assert_inside_root "$LOCK_DIR"
  if ! remote_sh "mkdir '$LOCK_DIR' 2>/dev/null"; then
    local who
    who="$(remote_sh "cat '$LOCK_DIR/owner' 2>/dev/null || echo unknown")"
    LOCK_DIR=""   # not ours: the trap must not remove someone else's lock
    die "another deploy holds the lock (owner: $who). Wait for it, or clear it by hand if it is stale."
  fi
  remote_sh "printf '%s\n' 'pid $$ on $(hostname) at $(date -u +%Y-%m-%dT%H:%M:%SZ)' > '$LOCK_DIR/owner'"
}

# ---------------------------------------------------------------------------- upload

upload_artifacts() {
  step "artifacts -> $UPDATES_DIR/$VERSION"
  local i local_file remote_file remote_state existing_sha uploaded=0 skipped=0
  for i in "${!ART_NAME[@]}"; do
    local_file="$(local_path_for_artifact "${ART_NAME[$i]}")"
    remote_file="$(remote_path_for_url "${ART_URL[$i]}")"
    assert_inside_root "$remote_file"

    remote_state="$(remote_sh "if [ -f '$remote_file' ]; then sha256sum -- '$remote_file' | cut -d' ' -f1; else echo ABSENT; fi")"
    if [ "$remote_state" = "${ART_SHA[$i]}" ]; then
      # Already there and identical. Re-running a deploy that half succeeded has to be safe, or
      # nobody will re-run it and they will fix it by hand instead.
      skipped=$(( skipped + 1 ))
      log "  = ${ART_NAME[$i]}"
      continue
    fi
    if [ "$remote_state" != "ABSENT" ]; then
      # A version path is immutable: the manifest signs the URL, so changing the bytes behind a
      # published URL makes every signature ever issued for it a lie.
      die "${ART_NAME[$i]} already exists at this version with DIFFERENT bytes. A published version is immutable -- publish a new version instead."
    fi
    if [ "$DRY_RUN" = "1" ]; then
      log "  + ${ART_NAME[$i]}  (dry-run, not uploaded)"
      uploaded=$(( uploaded + 1 ))
      continue
    fi
    remote_sh "mkdir -p '$(dirname "$remote_file")'"
    remote_put "$local_file" "$remote_file"
    # Explicit, not inherited. gnlink's umask happens to be 0002 today, which gives o+rX and lets
    # nginx read the file -- but 0.2.108 spent an afternoon returning 403 for exactly this, and
    # "it works because of the umask we happen to have" is not a property anyone verified.
    remote_sh "chmod 644 -- '$remote_file' && chmod 755 -- '$(dirname "$remote_file")'"
    uploaded=$(( uploaded + 1 ))
    log "  + ${ART_NAME[$i]}"
  done
  log "uploaded $uploaded, already present $skipped"
}

verify_on_server() {
  step "verify on the server"
  local i remote_file line remote_sha remote_size bad=0 checked=0 skipped=0
  for i in "${!ART_NAME[@]}"; do
    remote_file="$(remote_path_for_url "${ART_URL[$i]}")"
    if [ "$DRY_RUN" = "1" ] && ! remote_sh "test -f '$remote_file'"; then
      log "  ? ${ART_NAME[$i]}  (dry-run, nothing uploaded to check)"
      skipped=$(( skipped + 1 ))
      continue
    fi
    line="$(remote_sh "if [ -f '$remote_file' ]; then printf '%s %s %s' \"\$(sha256sum -- '$remote_file' | cut -d' ' -f1)\" \"\$(wc -c < '$remote_file' | tr -d ' ')\" \"\$(stat -c %a -- '$remote_file')\"; else echo 'ABSENT 0 0'; fi")"
    remote_sha="${line%% *}"; line="${line#* }"
    remote_size="${line%% *}"; local mode="${line#* }"
    if [ "$remote_sha" != "${ART_SHA[$i]}" ] || [ "$remote_size" != "${ART_SIZE[$i]}" ]; then
      warn "${ART_NAME[$i]}: server has $remote_size bytes / ${remote_sha:0:16}, manifest says ${ART_SIZE[$i]} / ${ART_SHA[$i]:0:16}"
      bad=$(( bad + 1 ))
      continue
    fi
    case "$mode" in
      *4|*5|*6|*7) : ;;  # world-readable
      *) warn "${ART_NAME[$i]}: mode $mode is not world-readable; nginx will answer 403"; bad=$(( bad + 1 )) ;;
    esac
    checked=$(( checked + 1 ))
  done
  [ "$bad" = "0" ] || die "$bad artifact(s) on the server do not match the manifest -- the manifest was NOT swapped, the previous release is still the published one"
  # The number CHECKED, not the number in the manifest. A dry run skips whatever it did not
  # upload, and reporting the manifest's count here said "all 10 match" after looking at none of
  # them -- a summary line that does not describe the work it just did.
  if [ "$skipped" -gt 0 ]; then
    log "$checked of ${#ART_NAME[@]} artifacts checked on the server; $skipped not present yet (dry run)"
  else
    log "all $checked artifacts on the server match the manifest and are readable"
  fi
}

# ---------------------------------------------------------------------------- publish

backup_current_pair() {
  step "back up the published pair"
  local stamp cur_manifest="$MANIFEST_DIR/$PLATFORM.manifest" cur_sig="$MANIFEST_DIR/$PLATFORM.sig"
  if ! remote_sh "test -f '$cur_manifest'"; then
    log "nothing published for $PLATFORM yet: no backup to take"
    return 0
  fi
  local cur_version
  cur_version="$(remote_sh "sed -n 's/^version=//p' '$cur_manifest' | head -n 1")"
  stamp="${cur_version:-unknown}"
  local dest="$BACKUP_DIR/$stamp"
  # Naming the backup after the published version alone is right exactly once. Publishing a
  # revision of a version that is ALREADY published -- adding an artifact, re-cutting a manifest --
  # computes the same name a second time, and copying over it would replace the rollback point
  # with the thing you would be rolling back FROM. The idempotence that makes re-running this
  # script safe everywhere else is what makes it unsafe here, so an existing backup is never
  # written over.
  if remote_sh "test -d '$dest'"; then
    dest="$BACKUP_DIR/$stamp-$(date -u +%Y%m%dT%H%M%SZ)"
    log "a backup of $stamp already exists; this one goes to $(basename "$dest")"
  fi
  assert_inside_root "$dest"
  if [ "$DRY_RUN" = "1" ]; then
    log "would back up $stamp -> $dest (dry-run)"
    return 0
  fi
  # The pair, together. A manifest restored without its signature verifies as nothing.
  remote_sh "mkdir -p '$dest' && cp -p -- '$cur_manifest' '$dest/$PLATFORM.manifest' && cp -p -- '$cur_sig' '$dest/$PLATFORM.sig'"
  local recorded
  recorded="$(remote_sh "cd '$dest' && sha256sum -- '$PLATFORM.manifest' '$PLATFORM.sig'")"
  remote_sh "printf '%s\n' '$recorded' > '$dest/SHA256SUMS'"
  log "backed up $stamp:"
  printf '%s\n' "$recorded" | sed 's/^/  /'
}

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
    die "the swap failed; the published pair may be partially replaced"
  fi
  log "published $VERSION"
  log "no restart: only manifest files changed; server code and environment are unchanged"
}

# ---------------------------------------------------------------------------- outside verification

verify_from_outside() {
  step "verify from outside, over https"
  if [ "$DRY_RUN" = "1" ]; then
    # A dry run uploads nothing, so every artifact it would have added still 404s. Checking anyway
    # would make a dry run of any new release exit nonzero -- and an exit code that is always 1
    # stops being a signal, which is worse than not checking.
    log "SKIPPED -- a dry run uploads nothing, so anything new would 404. Not attempted, not verified."
    return 0
  fi
  if [ "$GNLINK_VERIFY_PUBLIC" != "1" ] || [ "$GNLINK_REMOTE_MODE" = "local" ]; then
    log "SKIPPED -- not attempted, so not verified. Nothing below claims otherwise."
    return 0
  fi
  command -v curl >/dev/null || { warn "curl unavailable: external verification NOT performed"; return 0; }

  local i url tmp actual bad=0
  mkdir -p "$REPO_ROOT/.claude/deploy-tmp"
  tmp="$(mktemp "$REPO_ROOT/.claude/deploy-tmp/verify.XXXXXX")"
  for i in "${!ART_NAME[@]}"; do
    url="${ART_URL[$i]}"
    if ! curl -fsS --max-time 60 -o "$tmp" -- "$url"; then
      warn "could not fetch $url"; bad=$(( bad + 1 )); continue
    fi
    actual="$(sha256_of "$tmp")"
    if [ "$actual" != "${ART_SHA[$i]}" ]; then
      warn "$url served bytes that hash to ${actual:0:16}, manifest says ${ART_SHA[$i]:0:16}"
      bad=$(( bad + 1 ))
    fi
  done
  rm -f -- "$tmp"

  # The manifest endpoint needs a session or a host token. A 401 proves the endpoint is guarded;
  # it proves NOTHING about whether the right document is being served, and reporting it as
  # success would be reporting the lock as the room.
  local code
  code="$(curl -s -o /dev/null -w '%{http_code}' --max-time 30 -- "$GNLINK_PUBLIC_BASE/api/update/manifest?platform=$PLATFORM" || true)"
  log "GET /api/update/manifest (no credentials) -> $code  [401 is expected and is NOT evidence the release is correct]"

  [ "$bad" = "0" ] || die "$bad artifact(s) are not being served correctly"
  log "all ${#ART_NAME[@]} artifacts fetched anonymously over https and hash as signed"
}

compare_published() {
  step "compare what is published against $RELEASE_DIR"
  local cur_manifest="$MANIFEST_DIR/$PLATFORM.manifest"
  if ! remote_sh "test -f '$cur_manifest'"; then
    log "nothing is published for $PLATFORM"
    return 0
  fi
  local remote_sha local_sha remote_version
  remote_sha="$(remote_sh "sha256sum -- '$cur_manifest' | cut -d' ' -f1")"
  remote_version="$(remote_sh "sed -n 's/^version=//p' '$cur_manifest' | head -n 1")"
  local_sha="$(sha256_of "$MANIFEST")"
  log "published     $remote_version  sha256=$remote_sha"
  log "this release  $VERSION  sha256=$local_sha"
  if [ "$remote_sha" = "$local_sha" ]; then
    log "IDENTICAL -- this release is already published. Nothing to do."
  else
    log "DIFFERENT -- publishing this release would change what the server serves."
  fi
}

# ---------------------------------------------------------------------------- main

preflight

if [ "$VERIFY_ONLY" = "1" ]; then
  compare_published
  verify_on_server
  verify_from_outside
  step "verify-only: nothing was changed"
  exit 0
fi

take_lock
upload_artifacts
verify_on_server
backup_current_pair
publish_pair
verify_from_outside

step "done"
log "version        $VERSION"
log "manifest       sha256=$(sha256_of "$MANIFEST")"
log "signature      sha256=$(sha256_of "$SIGNATURE")"
log "artifacts      ${#ART_NAME[@]} at $UPDATES_DIR/$VERSION"
log "restart        not required"
log "still open     the field test: a real host taking this update end to end"
[ "$DRY_RUN" = "1" ] && log "DRY RUN -- no artifacts or manifest were published"
exit 0
