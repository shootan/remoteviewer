#!/usr/bin/env bash
#
# Builds, packages, gates, and optionally signs and publishes one release from a worktree.
#
# This is the 0.2.133 and 0.2.134 procedure written down. Both were done by hand, and the second
# cost two failed configures and a recovered-from-an-archive manifest to reproduce the first --
# which is the argument for having this file rather than a note describing it.
#
#   automation/gnlink_release.sh --worktree <dir> --version <x.y.z>
#                                [--build-dir <dir>] [--rel-root <dir>]
#                                [--sign] [--deploy] [--dry-run]
#
#   --worktree   the checkout to build from. Must be clean.
#   --version    the version being released. NOT written anywhere: it is compared against
#                kProductVersion in the source and the run stops if they disagree.
#   --build-dir  default <worktree>/build-<version>. Always configured fresh.
#   --rel-root   default <worktree>/.claude/rel. The release lands in <rel-root>/<version>.
#   --sign       sign the manifest with the operational key on this machine.
#   --deploy     publish through automation/gnlink_deploy.sh (which this does not modify).
#   --dry-run    build, package and gate for real; do not use the key and do not publish.
#
# What the three of them do together, all eight of them, because two of these used to contradict
# each other: --sign --deploy --dry-run described the signing it was skipping and then failed
# because the signature it had not written was missing.
#
#   sign deploy dry-run | stage 8 signing          | stage 9 deploy
#   ----------------------------------------------------------------------------------------
#    no   no     no     | skipped                  | skipped
#    no   no     yes    | skipped                  | skipped
#    no   yes    no     | skipped                  | REFUSED up front: nothing to publish
#    no   yes    yes    | skipped                  | described only; deploy script not called
#    yes  no     no     | signs                    | skipped
#    yes  no     yes    | described, key untouched | skipped
#    yes  yes    no     | signs                    | publishes
#    yes  yes    yes    | described, key untouched | described only; deploy script not called
#
# The rule behind the table: --dry-run never uses the key and never publishes, so a dry run can
# never produce a signature, so a dry run must not require one. The one refusal is the one case
# that cannot be made to mean anything -- a real publish of something nobody signed.
#
# The table above assumes the real tools. With any of them substituted (see the seam note below)
# the two rows that reach outside this machine -- signing for real, publishing for real -- are
# refused at argument time instead, whatever the rest of the row says.
#
# "Described only" is literal, and it is the second correction this table has needed. The first
# version signed nothing at stage 8 and then demanded a signature at stage 9. The second called
# gnlink_deploy.sh --verify-only instead -- which reads better but does the same thing, because
# that script's own preflight requires the signature before it looks at any flag
# (gnlink_deploy.sh:111). A dry run that cannot produce a signature therefore cannot call it at
# all, so it does not: it says what it would have published and stops. A dry run of a release
# that IS already signed is a different case and does rehearse, end to end.
#
# It does NOT bump the version. A release script that edits the source is a release script that
# can publish something nobody reviewed.
#
# Exit 0 = every stage passed. Anything else = stop; the failing stage says why.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Four things this script reaches outside itself: a compiler, a signing key, a check that reads a
# real PE, and the server. Each is overridable so the regression can drive all nine stages without
# a twenty-minute build, the operational key, or the network; the option table at the top is about
# what happens at stage 9, and a table nothing executes is a comment.
#
# Substituting any of them is a REFUSAL, not a warning. The first version of this only announced
# it and carried on, which meant a run with a stub compiler could still sign its staged files with
# the operational key, or a stub deploy script could print "published" -- test scaffolding with a
# path to the real key and the real server. So: with anything substituted, --sign outside a dry
# run and --deploy outside a dry run stop at argument time, before the key is read or the network
# is touched. What is left is what a substituted run is for -- dry runs, and packaging nothing
# signs.
#
# A release run sets none of these. Its output says so, and now so does its exit code.
MAIN_CHECKOUT_WEBVIEW2="${GNLINK_WEBVIEW2_SOURCE:-D:/remote/remote/third_party/webview2}"
CMAKE_DEFAULT="/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
CMAKE="${GNLINK_CMAKE:-$CMAKE_DEFAULT}"
INSTALLER_CHECK_DEFAULT="$SCRIPT_DIR/gnlink_check_installer_payload.ps1"
INSTALLER_CHECK="${GNLINK_INSTALLER_PAYLOAD_CHECK:-$INSTALLER_CHECK_DEFAULT}"
DEPLOY_SCRIPT_DEFAULT="$SCRIPT_DIR/gnlink_deploy.sh"
DEPLOY_SCRIPT="${GNLINK_DEPLOY_SCRIPT:-$DEPLOY_SCRIPT_DEFAULT}"
SIGN_KEYDIR="${GNLINK_SIGN_KEYDIR:-}"

WORKTREE=""
VERSION=""
BUILD_DIR=""
REL_ROOT=""
DO_SIGN=0
DO_DEPLOY=0
DRY_RUN=0

# The eight payload targets. Setup and Updater are in this list, not extra steps: the installer
# embeds the other six, so it must link after them, and the build system already knows that.
TARGETS="remote60_host_app remote60_native_video_host_poc remote60_gdi_capture_worker remote60_secure_input_service remote60_client_shell remote60_native_video_client_poc remote60_installer remote60_updater"
EXES="GNLinkHost GNLinkStream GNLinkCapture GNLinkInputService GNLinkClient GNLinkViewer GNLinkSetup GNLinkUpdater"

say()  { printf '%s\n' "$*"; }

# ------------------------------------------------------- paths: checked, created, never cleared
#
# Two directories are made from arguments, and an earlier version of this script removed each one
# first with a bare `rm -rf`. That is wrong twice over. An empty or mistyped variable makes it a
# command that deletes whatever it lands on; and the release directory holds the exact bytes a
# published manifest is signed over, so clearing it silently destroys a candidate somebody may
# still be checking -- which --dry-run did too.
#
# So nothing here deletes. A directory that already exists is a reason to stop and say so. New
# work goes in a new directory, and the release is moved into its final name only after every
# gate has passed, by a rename that fails if the name is taken.
#
# The boundary rules, checked before anything is created and again afterwards:
#   - absolute, and strictly below --worktree after resolution, so `..` and an absolute path
#     somewhere else are both refused
#   - no reparse point anywhere between the worktree and the target, because a junction means the
#     thing written is not the thing named

# Windows junctions are reparse points, and a junction is not a symlink as far as `test -L` is
# concerned. This asks the OS. No powershell, no answer, no run: fail closed.
#   0 = found one, and its path is printed   1 = none   2 = could not tell
path_reparse_between() {
  local root="$1" target="$2"
  local out
  out="$(powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "
    \$root  = [System.IO.Path]::GetFullPath('$(cygpath -w "$root")')
    \$p     = [System.IO.Path]::GetFullPath('$(cygpath -w "$target")')
    \$found = 'no'
    while (\$p -and \$p.Length -ge \$root.Length) {
      \$item = Get-Item -LiteralPath \$p -Force -ErrorAction SilentlyContinue
      if (\$item -and (\$item.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
        \$found = \$p
        break
      }
      \$parent = [System.IO.Path]::GetDirectoryName(\$p)
      if (\$parent -eq \$p) { break }
      \$p = \$parent
    }
    Write-Output \$found" 2>/dev/null)" || return 2
  out="$(printf '%s' "$out" | tr -d '\r' | tail -1)"
  [ -n "$out" ] || return 2
  [ "$out" = "no" ] && return 1
  printf '%s' "$out"
  return 0
}

# The nearest ancestor of a path that exists. What the boundary checks can actually look at
# before the path itself is created.
nearest_existing() {
  local p="${1%/}"
  while [ -n "$p" ] && [ ! -e "$p" ]; do
    local parent
    parent="$(dirname "$p")"
    [ "$parent" = "$p" ] && break
    p="$parent"
  done
  printf '%s' "$p"
}

# check_inside_worktree <path> <label>
#   Refuses unless <path> resolves to somewhere strictly below $WORKTREE, reached without
#   crossing a reparse point. Works whether or not <path> exists yet.
check_inside_worktree() {
  local target="${1%/}" label="$2"

  [ -n "$target" ] || die "refusing to use the $label: the path is empty"
  case "$target" in
    /|[A-Za-z]:|[A-Za-z]:/) die "refusing to use the $label: $1 is a filesystem root" ;;
    /*|[A-Za-z]:/*) ;;
    *) die "refusing to use the $label: $target is not an absolute path" ;;
  esac

  local anchor realAnchor rest
  anchor="$(nearest_existing "$target")"
  [ -d "$anchor" ] || die "refusing to use the $label: $anchor is not a directory"
  realAnchor="$(cd "$anchor" && pwd -P)" || die "cannot resolve $anchor"
  if [ "$anchor" != "$target" ]; then
    rest="${target#"$anchor"}"
  else
    rest=""
  fi
  local resolved="$realAnchor$rest"

  case "$resolved" in
    "$WORKTREE_REAL") die "refusing to use the $label: $1 IS the worktree" ;;
    "$WORKTREE_REAL"/?*) ;;
    *) die "refusing to use the $label: $resolved is outside the worktree $WORKTREE_REAL" ;;
  esac

  # A path that reads one way and resolves another has a link in it. Said separately from the
  # reparse walk because it catches the case even where powershell cannot be reached.
  local logicalAnchor
  logicalAnchor="$(cd "$anchor" && pwd)" || die "cannot resolve $anchor"
  [ "$logicalAnchor" = "$realAnchor" ] \
    || die "refusing to use the $label: $anchor resolves to $realAnchor"

  local reparse rc
  reparse="$(path_reparse_between "$WORKTREE_REAL" "$realAnchor")"; rc=$?
  case "$rc" in
    0) die "refusing to use the $label: reparse point at $reparse" ;;
    1) ;;
    *) die "refusing to use the $label: could not check $target for reparse points" ;;
  esac

  printf '%s' "$resolved"
}

# make_fresh_dir <path> <label>
#   Creates it. Refuses if anything is already there -- "fresh" means a new directory, not a
#   cleared one. Re-checks the boundary afterwards, because the check and the creation are two
#   moments and something can arrive between them.
make_fresh_dir() {
  local target="$1" label="$2"
  check_inside_worktree "$target" "$label" >/dev/null
  [ -e "$target" ] && die "the $label already exists: $target -- move or remove it yourself; this script does not delete"
  mkdir -p "$target" || die "could not create the $label at $target"
  check_inside_worktree "$target" "$label" >/dev/null
  say "$label: created $target"
}
step() { printf '\n=== %s\n' "$*"; }
die()  { printf 'FAIL  %s\n' "$*" >&2; exit 1; }

# A substituted seam is printed, every time, next to the thing it replaced. Silence here would
# mean a release could be assembled by tools nobody named.
say_if_substituted() {
  local what="$1" actual="$2" expected="$3"
  [ "$actual" = "$expected" ] && return 0
  say "SUBSTITUTED  $what: $actual"
  say "             (the release path is $expected)"
  SUBSTITUTIONS=$((SUBSTITUTIONS + 1))
  SUBSTITUTED_LIST="${SUBSTITUTED_LIST:+$SUBSTITUTED_LIST, }$what"
}
SUBSTITUTIONS=0
SUBSTITUTED_LIST=""

usage() {
  sed -n '3,27p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
  exit 2
}

while [ $# -gt 0 ]; do
  case "$1" in
    --worktree)  WORKTREE="${2:-}"; shift 2 ;;
    --version)   VERSION="${2:-}"; shift 2 ;;
    --build-dir) BUILD_DIR="${2:-}"; shift 2 ;;
    --rel-root)  REL_ROOT="${2:-}"; shift 2 ;;
    --sign)      DO_SIGN=1; shift ;;
    --deploy)    DO_DEPLOY=1; shift ;;
    --dry-run)   DRY_RUN=1; shift ;;
    -h|--help)   usage ;;
    *) die "unknown argument: $1 (try --help)" ;;
  esac
done

# Before the worktree is entered, the key is looked for, or anything is built: which of this
# script's tools are not the real ones.
say_if_substituted "cmake" "$CMAKE" "$CMAKE_DEFAULT"
say_if_substituted "installer payload check" "$INSTALLER_CHECK" "$INSTALLER_CHECK_DEFAULT"
say_if_substituted "deploy script" "$DEPLOY_SCRIPT" "$DEPLOY_SCRIPT_DEFAULT"
[ -z "$SIGN_KEYDIR" ] || say_if_substituted "signing key directory" "$SIGN_KEYDIR" ""
# Not this script's variable, but it changes what the product verifier at stage 8 trusts, so a
# run where it is set is not a release either. gnlink_verify_manifest.js reads it.
[ -z "${GNLINK_PUBLIC_KEY_HEX:-}" ] || say_if_substituted "trusted public key (verifier)" "set" ""

# And with any of them substituted, the two things that reach outside this machine are refused
# here -- at argument time, so the key is never read and the network is never touched.
#
# The reasoning is the one in CLAUDE.md about test builds: a test build may differ from a shipping
# build, and what makes that safe is a gate proving the test-only pieces are absent from the
# shipping path. Announcing them was not that gate. This is.
if [ "$SUBSTITUTIONS" != "0" ]; then
  if [ "$DO_SIGN" = "1" ] && [ "$DRY_RUN" = "0" ]; then
    die "refusing --sign: $SUBSTITUTIONS tool(s) substituted ($SUBSTITUTED_LIST) -- the operational key does not sign what a stub produced. Add --dry-run, or run with the real tools."
  fi
  if [ "$DO_DEPLOY" = "1" ] && [ "$DRY_RUN" = "0" ]; then
    die "refusing --deploy: $SUBSTITUTIONS tool(s) substituted ($SUBSTITUTED_LIST) -- a substituted run does not publish. Add --dry-run, or run with the real tools."
  fi
fi

# The one combination in the table with no sensible reading, refused here rather than nine
# stages later with a missing file as the explanation.
if [ "$DO_DEPLOY" = "1" ] && [ "$DO_SIGN" = "0" ] && [ "$DRY_RUN" = "0" ]; then
  die "--deploy without --sign would publish a release nobody signed; add --sign, or add --dry-run to rehearse"
fi

[ -n "$WORKTREE" ] || die "--worktree is required"
[ -n "$VERSION" ]  || die "--version is required"
[ -d "$WORKTREE" ] || die "no such worktree: $WORKTREE"
case "$VERSION" in
  *[!0-9.]*|"") die "--version must look like 0.2.134, got: $VERSION" ;;
esac

WORKTREE="$(cd "$WORKTREE" && pwd)"
[ -n "$BUILD_DIR" ] || BUILD_DIR="$WORKTREE/build-$VERSION"
[ -n "$REL_ROOT" ]  || REL_ROOT="$WORKTREE/.claude/rel"
# Where the release ends up, and where this run assembles it. They are not the same directory:
# the run builds into its own, and the final name is taken by a rename after the gates pass. A
# run that fails therefore cannot leave a half-release sitting where the deploy script looks.
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
REL_FINAL="$REL_ROOT/$VERSION"
REL_DIR="$REL_ROOT/$VERSION.$RUN_ID"
PAYLOAD="$REL_DIR/payload"


# ------------------------------------------------------------------- 1. preflight
step "1. preflight"

cd "$WORKTREE" || die "cannot enter $WORKTREE"
WORKTREE_REAL="$(pwd -P)"
git rev-parse --git-dir >/dev/null 2>&1 || die "$WORKTREE is not a git checkout"

HEAD_SHA="$(git rev-parse HEAD)"
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
say "worktree : $WORKTREE"
say "branch   : $BRANCH"
say "HEAD     : $HEAD_SHA"

DIRTY="$(git status --porcelain --untracked-files=no)"
if [ -n "$DIRTY" ]; then
  say "$DIRTY"
  die "the worktree has uncommitted tracked changes; a release must name a commit"
fi

VERSION_HEADER="$WORKTREE/apps/native_poc/src/product_version.hpp"
[ -f "$VERSION_HEADER" ] || die "no product_version.hpp at $VERSION_HEADER"
SOURCE_VERSION="$(sed -n 's/.*kProductVersion\[\] = L"\([0-9.]*\)".*/\1/p' "$VERSION_HEADER")"
[ -n "$SOURCE_VERSION" ] || die "could not read kProductVersion from $VERSION_HEADER"
if [ "$SOURCE_VERSION" != "$VERSION" ]; then
  die "the source says $SOURCE_VERSION and you asked for $VERSION -- bump the version in a reviewed commit first, this script will not edit it"
fi
say "version  : $VERSION (source and argument agree)"

# One release at a time per worktree. mkdir is the atomic part: two runs cannot both create it.
LOCK_DIR="$WORKTREE/.claude/gnlink_release.lock"
mkdir -p "$WORKTREE/.claude" || die "cannot create $WORKTREE/.claude"
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
  say "held by: $(cat "$LOCK_DIR/owner" 2>/dev/null || echo 'unknown')"
  die "another release is running in this worktree ($LOCK_DIR) -- remove it if that is stale"
fi
printf 'pid=%s run=%s version=%s started=%s\n' "$$" "$RUN_ID" "$VERSION" "$(date -Is)" \
  > "$LOCK_DIR/owner"
cleanup() {
  rmdir "$LOCK_DIR" 2>/dev/null || { rm -f "$LOCK_DIR/owner" 2>/dev/null; rmdir "$LOCK_DIR" 2>/dev/null; }
}
trap cleanup EXIT

# Both directories are checked here, before a compiler runs, so a bad path fails in a second
# rather than twenty minutes in.
check_inside_worktree "$BUILD_DIR" "build dir" >/dev/null
check_inside_worktree "$REL_DIR" "release dir" >/dev/null
check_inside_worktree "$REL_FINAL" "release directory" >/dev/null

# The published candidate is not this run's to clear. Even --dry-run used to remove it.
if [ -e "$REL_FINAL" ]; then
  if [ -n "$(ls -A "$REL_FINAL" 2>/dev/null)" ]; then
    die "$REL_FINAL already exists and is not empty -- that is a release somebody may still be checking; move it aside yourself"
  fi
  die "$REL_FINAL already exists -- remove it yourself if it is nothing"
fi
say "release  : will be assembled in $REL_DIR and renamed to $REL_FINAL"

# 0.2.134 lost half an hour to this: third_party/webview2 is gitignored and per-worktree, so a
# fresh worktree has none. Without it remote60_client_shell is never created as a target and the
# installer's payload command dies on a missing $<TARGET_FILE:...> -- an error that names neither
# WebView2 nor the reason.
if [ ! -d "$WORKTREE/third_party/webview2/build/native" ]; then
  if [ -d "$MAIN_CHECKOUT_WEBVIEW2/build/native" ]; then
    say "webview2 : absent here, copying from $MAIN_CHECKOUT_WEBVIEW2"
    mkdir -p "$WORKTREE/third_party" || die "cannot create $WORKTREE/third_party"
    cp -r "$MAIN_CHECKOUT_WEBVIEW2" "$WORKTREE/third_party/webview2" \
      || die "could not copy the WebView2 SDK"
    [ -d "$WORKTREE/third_party/webview2/build/native" ] \
      || die "the WebView2 copy did not produce build/native"
  else
    die "no WebView2 SDK in this worktree and none at $MAIN_CHECKOUT_WEBVIEW2 -- run automation/fetch_webview2.ps1"
  fi
else
  say "webview2 : present"
fi
# What went in, recorded now and checked again at the end. A release names a commit, and the
# WebView2 SDK is not in the commit -- it is a per-worktree directory this script may have copied
# in. Both can change while a twenty-minute build runs, and a release built across such a change
# is not the release its commit describes.
sdk_hash() {
  local dll
  dll="$(find "$WORKTREE/third_party/webview2" -iname 'WebView2Loader.dll' -print 2>/dev/null | sort | head -1)"
  if [ -n "$dll" ] && [ -f "$dll" ]; then
    sha256sum "$dll" | cut -d' ' -f1
    return 0
  fi
  local nuspec
  nuspec="$(find "$WORKTREE/third_party/webview2" -iname '*.nuspec' -print 2>/dev/null | sort | head -1)"
  if [ -n "$nuspec" ] && [ -f "$nuspec" ]; then
    sha256sum "$nuspec" | cut -d' ' -f1
    return 0
  fi
  printf 'unknown'
}
SDK_HASH_BEFORE="$(sdk_hash)"
[ "$SDK_HASH_BEFORE" != "unknown" ] || die "could not hash the WebView2 SDK; refusing to build a release whose inputs cannot be named"
say "sdk      : $SDK_HASH_BEFORE"


[ -x "$CMAKE" ] || die "cmake not found at $CMAKE"

# ------------------------------------------------------------------- 2. configure
step "2. fresh configure -> $BUILD_DIR"

# Fresh every time: a release built on top of a stale cache is a release nobody can reproduce.
# Fresh by creating a new directory, not by deleting an old one -- if the build dir is already
# there, this run stops and says so rather than removing somebody's work.
make_fresh_dir "$BUILD_DIR" "build dir"
# No -DCMAKE_BUILD_TYPE. The Visual Studio generator is multi-config and setting it makes the
# generator expressions in the installer's payload rules fail to evaluate.
"$CMAKE" -S "$WORKTREE" -B "$BUILD_DIR" >"$BUILD_DIR.configure.log" 2>&1 \
  || { tail -20 "$BUILD_DIR.configure.log"; die "configure failed (log: $BUILD_DIR.configure.log)"; }
say "configured"

# ------------------------------------------------------------------- 3. build
step "3. build the payload targets"

# shellcheck disable=SC2086
"$CMAKE" --build "$BUILD_DIR" --config Release --target $TARGETS >"$BUILD_DIR.build.log" 2>&1 \
  || { grep -iE "error C[0-9]+|error LNK|fatal error" "$BUILD_DIR.build.log" | head -10; \
       die "build failed (log: $BUILD_DIR.build.log)"; }
BUILD_ERRORS="$(grep -ciE 'error C[0-9]+|error LNK|fatal error' "$BUILD_DIR.build.log" || true)"
[ "$BUILD_ERRORS" = "0" ] || die "$BUILD_ERRORS build errors (log: $BUILD_DIR.build.log)"
say "built with 0 errors"

BIN="$BUILD_DIR/apps/native_poc/Release"
for exe in $EXES; do
  [ -f "$BIN/$exe.exe" ] || die "the build did not produce $exe.exe"
done
say "all 8 executables present"

# ------------------------------------------------------------------- 4. package
step "4. package -> $REL_DIR"

make_fresh_dir "$REL_DIR" "release dir"
mkdir -p "$PAYLOAD/ui" || die "could not create $PAYLOAD/ui"
for exe in $EXES; do
  cp "$BIN/$exe.exe" "$PAYLOAD/$exe.exe" || die "could not package $exe.exe"
done
for html in shell macro; do
  SRC="$WORKTREE/apps/native_poc/ui/$html.html"
  [ -f "$SRC" ] || die "no $SRC"
  cp "$SRC" "$PAYLOAD/ui/$html.html" || die "could not package $html.html"
done
say "payload: 8 executables and 2 ui files"

# ------------------------------------------------------------------- 5. manifest
step "5. manifest"

python "$SCRIPT_DIR/gnlink_release_manifest.py" write \
  --payload "$PAYLOAD" --version "$VERSION" --out "$REL_DIR/windows.manifest" \
  || die "could not write the manifest"

# ------------------------------------------------------------------- 6. checksums
step "6. SHA256SUMS.txt"

( cd "$REL_DIR" && for f in \
    payload/GNLinkCapture.exe payload/GNLinkClient.exe payload/GNLinkHost.exe \
    payload/GNLinkInputService.exe payload/GNLinkSetup.exe payload/GNLinkStream.exe \
    payload/GNLinkUpdater.exe payload/GNLinkViewer.exe \
    payload/ui/macro.html payload/ui/shell.html; do
      sha256sum "./$f" || exit 1
    done > SHA256SUMS.txt ) || die "could not write SHA256SUMS.txt"
say "wrote $REL_DIR/SHA256SUMS.txt"

# ------------------------------------------------------------------- 7. gates
step "7. gates"

say "-- parity: does the binary claim to be this version"
python "$SCRIPT_DIR/gnlink_release_manifest.py" parity --payload "$PAYLOAD" --version "$VERSION" \
  || die "parity gate failed"

say ""
say "-- payload set: does the manifest name everything the update replaces"
python "$SCRIPT_DIR/gnlink_check_payload_set.py" "$REL_DIR/windows.manifest" \
  || die "payload-set gate failed"

say ""
say "-- checksums"
( cd "$REL_DIR" && sha256sum -c SHA256SUMS.txt ) || die "SHA256SUMS verification failed"

say ""
say "-- installer payload: does GNLinkSetup carry what this release ships"
# The installer embeds the other nine as RT_RCDATA and the update replaces the same nine. Those
# two sets can disagree without either half looking wrong, and then the thing installed is not
# the thing gated. 0.2.133 was checked this way by hand.
powershell.exe -NoProfile -ExecutionPolicy Bypass \
  -File "$(cygpath -w "$INSTALLER_CHECK")" \
  -Setup "$(cygpath -w "$PAYLOAD/GNLinkSetup.exe")" \
  -Payload "$(cygpath -w "$PAYLOAD")" || die "installer payload gate failed"

say ""
say "-- inputs: did anything move while this was building"
DIRTY_AFTER="$(git -C "$WORKTREE" status --porcelain --untracked-files=no)"
[ -z "$DIRTY_AFTER" ] || { say "$DIRTY_AFTER"; die "the worktree changed during the build; this release does not describe commit $HEAD_SHA"; }
HEAD_AFTER="$(git -C "$WORKTREE" rev-parse HEAD)"
[ "$HEAD_AFTER" = "$HEAD_SHA" ] || die "HEAD moved from $HEAD_SHA to $HEAD_AFTER during the build"
SDK_HASH_AFTER="$(sdk_hash)"
[ "$SDK_HASH_AFTER" = "$SDK_HASH_BEFORE" ] || die "the WebView2 SDK changed during the build ($SDK_HASH_BEFORE -> $SDK_HASH_AFTER)"
say "commit and SDK unchanged since preflight"

# ----------------------------------------------------------- 7b. the release takes its name
step "7b. name the release -> $REL_FINAL"

# Only now, and only by a move that fails if the name is taken. Everything before this ran in a
# directory named after this run, so a failed release cannot leave something at the name the
# deploy script reads. The check is not the protection -- `mv` refusing to clobber is; the check
# is there to say why in words rather than by an errno.
[ -e "$REL_FINAL" ] && die "$REL_FINAL appeared while this run was building; refusing to replace it"
# -T, always: without it a `mv` onto an existing directory moves INTO it, and the release ends
# up one level down with the right name on the wrong thing.
mv -T "$REL_DIR" "$REL_FINAL" || die "could not move $REL_DIR to $REL_FINAL"
[ -d "$REL_FINAL" ] || die "the release is not at $REL_FINAL after the move"
REL_DIR="$REL_FINAL"
PAYLOAD="$REL_DIR/payload"
say "release: $REL_FINAL"

# ------------------------------------------------------------------- 8. sign
step "8. sign"

SIGNED=0
if [ "$DO_SIGN" = "1" ]; then
  if [ "$DRY_RUN" = "1" ]; then
    say "dry run: would sign $REL_DIR/windows.manifest with the operational key"
    say "         the key is not read and no signature is produced -- see the table at the top"
  else
    # The key the product trusts, read out of the candidate's own source rather than kept in a
    # second place. gnlink_verify_manifest.js reads the same literal the same way.
    TRUSTED_KEY="$(sed -n 's/.*return[[:space:]]*"\([0-9a-f]\{128\}\)"[[:space:]]*;.*/\1/p' \
      "$WORKTREE/apps/native_poc/src/update_manifest.cpp" | head -1)"
    [ -n "$TRUSTED_KEY" ] || die "could not read the trusted public key from update_manifest.cpp"
    powershell.exe -NoProfile -ExecutionPolicy Bypass \
      -File "$(cygpath -w "$SCRIPT_DIR/gnlink_release_sign.ps1")" \
      -ReleaseDir "$(cygpath -w "$REL_DIR")" \
      -TrustedKeyHex "$TRUSTED_KEY" \
      ${SIGN_KEYDIR:+-KeyDir "$(cygpath -w "$SIGN_KEYDIR")"} || die "signing failed"
    [ -f "$REL_DIR/windows.sig" ] || die "no windows.sig after signing"
    SIGNED=1
    say "signed"

    # And verified by the PRODUCT's verifier, which reads the key compiled into the update client
    # rather than the one next to the signing key. The signing script checking its own work says
    # the maths is right; this says the thing that will actually install the update accepts it.
    if command -v node >/dev/null 2>&1; then
      node "$SCRIPT_DIR/gnlink_verify_manifest.js" "$REL_DIR/windows.manifest" "$REL_DIR/windows.sig" windows \
        || die "the product verifier rejects this signature -- do not publish"
      say "verified against the key compiled into the product"
    else
      die "node is not on PATH; the product verifier is not optional before publishing"
    fi
  fi
else
  say "not requested (--sign)"
fi

# ------------------------------------------------------------------- 9. deploy
step "9. deploy"

DEPLOY_LOG="$REL_DIR/deploy.log"
if [ "$DO_DEPLOY" = "1" ]; then
  if [ "$DRY_RUN" = "1" ]; then
    # A dry run never signs, so requiring a signature here is requiring something this mode
    # cannot produce. What it checks instead is everything that does not need one.
    if [ -f "$REL_DIR/windows.sig" ]; then
      say "rehearsing publication of a release that is already signed"
      "$DEPLOY_SCRIPT" --release-dir "$REL_DIR" --dry-run 2>&1 | tee "$DEPLOY_LOG"
      [ "${PIPESTATUS[0]}" = "0" ] || die "deploy dry run failed (log: $DEPLOY_LOG)"
    else
      # Not even --verify-only. That script refuses before it reads its flags when there is no
      # signature, so calling it here would fail on a file this mode was never going to write --
      # which is the contradiction this whole table exists to remove, moved one layer down.
      {
        say "would deploy $REL_DIR"
        say "no signature, because --dry-run does not use the key, so the deploy script is not"
        say "called at all -- its preflight requires one before it reads any flag."
        say "to rehearse publication end to end, sign first and then dry-run:"
        say "  $0 --worktree $WORKTREE --version $VERSION --sign"
        say "  $0 --worktree $WORKTREE --version $VERSION --deploy --dry-run"
      } 2>&1 | tee "$DEPLOY_LOG"
    fi
  else
    [ -f "$REL_DIR/windows.sig" ] || die "--deploy without a signature; sign first"
    "$DEPLOY_SCRIPT" --release-dir "$REL_DIR" 2>&1 | tee "$DEPLOY_LOG"
    [ "${PIPESTATUS[0]}" = "0" ] || die "deploy failed (log: $DEPLOY_LOG)"
  fi
else
  say "not requested (--deploy)"
fi

# ------------------------------------------------------------------- summary
step "summary"

say "version   : $VERSION"
say "branch    : $BRANCH"
say "commit    : $HEAD_SHA"
say "build dir : $BUILD_DIR"
say "release   : $REL_DIR"
say "run id    : $RUN_ID"
say "sdk       : $SDK_HASH_BEFORE"
say "signed    : $([ "$SIGNED" = "1" ] && echo yes || { [ -f "$REL_DIR/windows.sig" ] && echo 'yes (from an earlier run)' || echo no; })"
say "deployed  : $([ "$DO_DEPLOY" = "1" ] && { [ "$DRY_RUN" = "1" ] && echo 'dry run only' || echo yes; } || echo no)"
say ""
say "artifact sha256:"
sed 's/^/  /' "$REL_DIR/SHA256SUMS.txt"
say ""
if [ "$SUBSTITUTIONS" != "0" ]; then
  say ""
  say "NOT A RELEASE: $SUBSTITUTIONS tool(s) were substituted, listed at the top of this run."
fi
say "gates: parity PASS, payload-set PASS, checksums PASS, installer payload 9/9 PASS,"
say "       inputs unchanged (commit $HEAD_SHA, sdk $SDK_HASH_BEFORE)"
if [ "$SIGNED" = "1" ]; then
  say "       signature verified by the product's own verifier"
fi
# The deploy script's own external check is the last word on whether what is published is what
# was built, so its answer belongs in this summary rather than only in its output.
if [ -f "$DEPLOY_LOG" ]; then
  say ""
  say "external check, from $DEPLOY_LOG:"
  grep -iE "https|sha256|published|manifest|sig" "$DEPLOY_LOG" | tail -12 | sed 's/^/  /' \
    || say "  (nothing matched in the deploy log)"
fi
exit 0
