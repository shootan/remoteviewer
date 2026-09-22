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
#   --dry-run    do everything up to and including the gates; describe signing, and pass
#                --dry-run to the deploy script rather than publishing.
#
# It does NOT bump the version. A release script that edits the source is a release script that
# can publish something nobody reviewed.
#
# Exit 0 = every stage passed. Anything else = stop; the failing stage says why.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Overridable so the test can point it at a directory that does not exist and watch the
# preflight refuse, rather than having the copy succeed and a real build start.
MAIN_CHECKOUT_WEBVIEW2="${GNLINK_WEBVIEW2_SOURCE:-D:/remote/remote/third_party/webview2}"
CMAKE="/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"

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
step() { printf '\n=== %s\n' "$*"; }
die()  { printf 'FAIL  %s\n' "$*" >&2; exit 1; }

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

[ -n "$WORKTREE" ] || die "--worktree is required"
[ -n "$VERSION" ]  || die "--version is required"
[ -d "$WORKTREE" ] || die "no such worktree: $WORKTREE"
case "$VERSION" in
  *[!0-9.]*|"") die "--version must look like 0.2.134, got: $VERSION" ;;
esac

WORKTREE="$(cd "$WORKTREE" && pwd)"
[ -n "$BUILD_DIR" ] || BUILD_DIR="$WORKTREE/build-$VERSION"
[ -n "$REL_ROOT" ]  || REL_ROOT="$WORKTREE/.claude/rel"
REL_DIR="$REL_ROOT/$VERSION"
PAYLOAD="$REL_DIR/payload"

# ------------------------------------------------------------------- 1. preflight
step "1. preflight"

cd "$WORKTREE" || die "cannot enter $WORKTREE"
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

[ -x "$CMAKE" ] || die "cmake not found at $CMAKE"

# ------------------------------------------------------------------- 2. configure
step "2. fresh configure -> $BUILD_DIR"

# Fresh every time: a release built on top of a stale cache is a release nobody can reproduce.
rm -rf "$BUILD_DIR" || die "could not remove $BUILD_DIR"
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

rm -rf "$REL_DIR" || die "could not clear $REL_DIR"
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

# ------------------------------------------------------------------- 8. sign
step "8. sign"

if [ "$DO_SIGN" = "1" ]; then
  if [ "$DRY_RUN" = "1" ]; then
    say "dry run: would sign $REL_DIR/windows.manifest with the operational key"
  else
    powershell.exe -NoProfile -ExecutionPolicy Bypass \
      -File "$(cygpath -w "$SCRIPT_DIR/gnlink_release_sign.ps1")" \
      -ReleaseDir "$(cygpath -w "$REL_DIR")" || die "signing failed"
    [ -f "$REL_DIR/windows.sig" ] || die "no windows.sig after signing"
    say "signed and self-verified"
  fi
else
  say "not requested (--sign)"
fi

# ------------------------------------------------------------------- 9. deploy
step "9. deploy"

if [ "$DO_DEPLOY" = "1" ]; then
  [ -f "$REL_DIR/windows.sig" ] || die "--deploy without a signature; sign first"
  if [ "$DRY_RUN" = "1" ]; then
    "$SCRIPT_DIR/gnlink_deploy.sh" --release-dir "$REL_DIR" --dry-run || die "deploy dry run failed"
  else
    "$SCRIPT_DIR/gnlink_deploy.sh" --release-dir "$REL_DIR" || die "deploy failed"
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
say "signed    : $([ -f "$REL_DIR/windows.sig" ] && echo yes || echo no)"
say "deployed  : $([ "$DO_DEPLOY" = "1" ] && { [ "$DRY_RUN" = "1" ] && echo 'dry run only' || echo yes; } || echo no)"
say ""
say "artifact sha256:"
sed 's/^/  /' "$REL_DIR/SHA256SUMS.txt"
say ""
say "gates: parity PASS, payload-set PASS, checksums PASS"
exit 0
