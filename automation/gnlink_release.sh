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
# The eight payload targets, and the verifier.
#
# remote60_verify_release is built HERE, with the payload, rather than at stage 8 where it is
# used. Built at stage 8 it would be compiled AFTER the drift check at stage 7 -- so the one
# binary whose whole job is to say "this candidate is what it claims" would itself come from a
# tree nothing had checked since. Now it is inside the same window as everything else, and a
# dry run produces it too, which is what lets a reviewer see it was built at all.
TARGETS="remote60_host_app remote60_native_video_host_poc remote60_gdi_capture_worker remote60_secure_input_service remote60_client_shell remote60_native_video_client_poc remote60_installer remote60_updater remote60_verify_release"
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

# The path questions that need the OS go to a script, with the path as an ARGUMENT.
#
# They used to go to `powershell -Command "...'$(cygpath -w "$p")'..."`, which puts the path
# inside a script as text. A path with a quote or a dollar sign in it stops being data at that
# point. These are the paths a release creates everything under, so this is not hypothetical
# enough to leave alone.
PATH_PROBE="$SCRIPT_DIR/gnlink_path_probe.ps1"

# Folds `.` and `..` textually, without touching the disk.
#
# This is the check that was missing, and it is the one that mattered. The old version resolved
# only as far as the deepest EXISTING ancestor and then appended the rest of the string, so
# `<worktree>/new/../../outside/evil` -- whose ancestors do not exist yet -- was compared as a
# string beginning with `<worktree>/` and passed. It would then have been created outside the
# worktree. Measured before fixing, not deduced.
normalise_path() {
  local p="$1"
  # Two characters this must not try to be clever about.
  #
  # A backslash is a separator to Windows and to CMake, and was not one here -- so
  # `<worktree>/new\\..\\..\\escape` folded to nothing, stayed inside the worktree as a
  # string, and walked out of it the moment anything on the Windows side read it.
  #
  # A glob metacharacter is worse than it looks. The fold loop splits on / with an unquoted
  # expansion, so `build-*` was expanded against the filesystem: with build-aaa and build-bbb
  # present it came out as `build-aaa/build-bbb`, a path nobody named. Measured, not feared.
  #
  # Both are refused rather than translated. A release directory is not a place to guess what
  # somebody meant.
  case "$p" in
    *\\*) printf 'error:backslash'; return 1 ;;
    *'*'*|*'?'*|*'['*) printf 'error:glob'; return 1 ;;
  esac
  # Windows-isms this script has no business accepting: a UNC share, or a drive-relative path
  # (`C:foo`) whose meaning depends on a per-drive current directory.
  case "$p" in
    //*|\\\\*) printf 'error:unc'; return 1 ;;
    [A-Za-z]:[!/]*) printf 'error:drive-relative'; return 1 ;;
  esac
  local prefix="" rest="$p"
  case "$p" in
    [A-Za-z]:/*) prefix="${p%%:*}:"; rest="${p#[A-Za-z]:}" ;;
    /*) ;;
    *) printf 'error:relative'; return 1 ;;
  esac
  local out="" seg
  local IFS=/
  # The expansion below is unquoted on purpose -- that is what splits on / -- which also
  # makes it a glob. The guard above refuses those characters, and this turns expansion off
  # as well, so the splitting does not depend on the guard being the only line of defence.
  set -f
  for seg in $rest; do
    case "$seg" in
      ""|.) ;;
      ..)
        # Below the root is not a place. Refusing beats silently clamping at /.
        if [ -z "$out" ]; then set +f; printf 'error:above-root'; return 1; fi
        out="${out%/*}"
        ;;
      *) out="$out/$seg" ;;
    esac
  done
  set +f
  printf '%s%s' "$prefix" "${out:-/}"
  return 0
}

# The ONE form every path comparison in this script uses. (RV-14)
#
# Two forms were being compared: `pwd -P`, which keeps whatever case the drive was entered in
# (`cd /D/...` gives `/D/...`), and `cygpath -u`, which gives `/d/...` and maps %TEMP% to `/tmp`.
# Measured on this PC: a legitimate path was then "outside the worktree", and the `D:/...` form the
# usage text allows did not work at all. Going through the Windows form and back gives the same
# answer for /D/x, /d/x and D:/x, and for %TEMP% spelled either way.
canon_path() {
  local w
  w="$(cygpath -w -- "$1" 2>/dev/null)" || return 1
  [ -n "$w" ] || return 1
  cygpath -u -- "$w" 2>/dev/null
}

# none | reparse:<path> | error:<why>, straight from the probe. An error is a refusal.
path_reparse_between() {
  local root="$1" target="$2" out
  out="$(powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass \
           -File "$(cygpath -w "$PATH_PROBE")" \
           -Mode reparse -Root "$(cygpath -w "$root")" -Path "$(cygpath -w "$target")" 2>/dev/null)" \
    || { printf 'error:probe-failed'; return 2; }
  out="$(printf '%s' "$out" | tr -d '\r' | tail -1)"
  case "$out" in
    none) return 1 ;;
    reparse:*) printf '%s' "${out#reparse:}"; return 0 ;;
    *) printf '%s' "$out"; return 2 ;;
  esac
}

# Where a path really is, once every link on the way has been followed.
path_real() {
  local target="$1" out
  out="$(powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass \
           -File "$(cygpath -w "$PATH_PROBE")" \
           -Mode real -Path "$(cygpath -w "$target")" 2>/dev/null)" || return 1
  out="$(printf '%s' "$out" | tr -d '\r' | tail -1)"
  case "$out" in
    error:*) return 1 ;;
    "") return 1 ;;
  esac
  # Back to the form the rest of this script compares in. The probe speaks Windows paths
  # because it is Windows that knows where a junction goes; everything here is POSIX, and a
  # comparison between the two forms never matches -- which reads as "outside the worktree"
  # for every legitimate path. Caught the moment the probe was first wired in.
  cygpath -u "$out" 2>/dev/null || return 1
  return 0
}

# The nearest ancestor of a path that exists, computed on an ALREADY NORMALISED path so that a
# `..` cannot walk the search somewhere the caller did not name.
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
#   Refuses unless <path> is, after folding `.` and `..`, strictly below $WORKTREE, and is
#   reached without crossing a reparse point. Works whether or not <path> exists yet, and prints
#   the normalised path on success.
check_inside_worktree() {
  local given="$1" label="$2"
  [ -n "$given" ] || die "refusing to use the $label: the path is empty"

  local target
  target="$(normalise_path "${given%/}")" \
    || die "refusing to use the $label: $given is not a usable absolute path ($target)"
  case "$target" in
    /|[A-Za-z]:|[A-Za-z]:/) die "refusing to use the $label: $given is a filesystem root" ;;
  esac
  # Folded first (textually, so `..` cannot be resolved by anything that follows links), then
  # put in the one form WORKTREE_REAL is in.
  target="$(canon_path "$target")" \
    || die "refusing to use the $label: cannot put $given in canonical form"

  # Compared after normalisation, both sides. This is the check the string-append version could
  # not make, because it never had the whole path in normal form.
  case "$target" in
    "$WORKTREE_REAL") die "refusing to use the $label: $given IS the worktree" ;;
    "$WORKTREE_REAL"/?*) ;;
    *) die "refusing to use the $label: $target is outside the worktree $WORKTREE_REAL" ;;
  esac

  local anchor
  anchor="$(nearest_existing "$target")"
  [ -d "$anchor" ] || die "refusing to use the $label: $anchor is not a directory"

  # Where the existing part of the path actually is. A junction here means the rest would be
  # created somewhere other than where it reads.
  local realAnchor
  realAnchor="$(path_real "$anchor")" \
    || die "refusing to use the $label: cannot resolve $anchor -- an unreadable path is not a safe one"
  local logicalAnchor
  logicalAnchor="$(cd "$anchor" && pwd -P)" || die "cannot resolve $anchor"
  case "$realAnchor" in
    "$WORKTREE_REAL"|"$WORKTREE_REAL"/*) ;;
    *) die "refusing to use the $label: $anchor resolves to $realAnchor, outside the worktree" ;;
  esac

  local reparse rc
  reparse="$(path_reparse_between "$WORKTREE_REAL" "$anchor")"; rc=$?
  case "$rc" in
    0) die "refusing to use the $label: reparse point at $reparse" ;;
    1) ;;
    *) die "refusing to use the $label: could not check $given for reparse points ($reparse)" ;;
  esac

  printf '%s' "$target"
}

# make_fresh_dir <path> <label>
#   Creates it. Refuses if anything is already there -- "fresh" means a new directory, not a
#   cleared one. Re-checks the boundary afterwards, because the check and the creation are two
#   moments and something can arrive between them.
make_fresh_dir() {
  local target="$1" label="$2"
  local normalised
  # The `|| die` is not decoration. `die` inside $( ) kills the SUBSHELL and the caller keeps
  # going with an empty string -- which became `mkdir ""` and a complaint about a lock held
  # at "()" . A refusal has to stop the run, not hand back nothing.
  normalised="$(check_inside_worktree "$target" "$label")" \
    || die "refusing to create the $label"
  # The parents may be made freely; the directory itself by a plain mkdir, which is the atomic
  # test-and-create. `[ -e ]` then `mkdir -p` was two moments, and `mkdir -p` does not fail on a
  # directory that appeared between them. (RV-14)
  mkdir -p "$(dirname "$normalised")" || die "could not create the parent of the $label at $normalised"
  if ! mkdir "$normalised" 2>/dev/null; then
    [ -e "$normalised" ] \
      && die "the $label already exists: $normalised -- move or remove it yourself; this script does not delete"
    die "could not create the $label at $normalised"
  fi
  # Again, now that it exists: the check before could only look at an ancestor, and between the
  # two moments the path is a thing anyone with write access to the parent can replace.
  local after
  after="$(path_real "$normalised")" \
    || die "refusing the $label: cannot resolve $normalised after creating it"
  case "$after" in
    "$WORKTREE_REAL"/?*) ;;
    *) die "the $label was created at $after, outside the worktree -- something replaced it" ;;
  esac
  local reparse rc
  reparse="$(path_reparse_between "$WORKTREE_REAL" "$normalised")"; rc=$?
  [ "$rc" = "1" ] || die "the $label at $normalised is or is under a reparse point after creation ($reparse)"
  say "$label: created $normalised"
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
RESERVED_FINAL=0
SUBSTITUTED_LIST=""

usage() {
  sed -n '3,27p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
  exit 2
}

# An option that takes a value, given last with none, used to be `shift 2` on one argument -- which
# fails without shifting, and with no `set -e` the loop went round forever. (RV-14)
need_value() { [ $# -ge 2 ] || die "$1 needs a value"; }

while [ $# -gt 0 ]; do
  case "$1" in
    --worktree)  need_value "$@"; WORKTREE="$2"; shift 2 ;;
    --version)   need_value "$@"; VERSION="$2"; shift 2 ;;
    --build-dir) need_value "$@"; BUILD_DIR="$2"; shift 2 ;;
    --rel-root)  need_value "$@"; REL_ROOT="$2"; shift 2 ;;
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
# Environment the tools read on their own. (RV-11) The first list is every GNLINK_* setting
# gnlink_deploy.sh takes from the environment -- the switches its own regression uses to publish
# into a local directory, skip the outside check, or talk to another host. With
# GNLINK_REMOTE_MODE=local a `--sign --deploy` used to sign with the real key, "publish" to a
# local directory and report `deployed : yes`. The second list changes what the compiler and
# CMake produce, or which WebView2 SDK is copied in. Set to anything -- even empty -- counts.
for env_name in GNLINK_REMOTE_MODE GNLINK_LOCAL_ROOT GNLINK_VERIFY_PUBLIC GNLINK_DEPLOY_HOST \
                GNLINK_DEPLOY_KEY GNLINK_REMOTE_ROOT GNLINK_PUBLIC_BASE GNLINK_UPDATES_SUBDIR \
                GNLINK_MANIFEST_SUBDIR GNLINK_BACKUP_SUBDIR \
                _CL_ CL LINK CMAKE_TOOLCHAIN_FILE CMAKE_GENERATOR GNLINK_WEBVIEW2_SOURCE; do
  if [ -n "${!env_name+set}" ]; then
    say_if_substituted "environment $env_name" "set" ""
  fi
done
# Every GNLINK_* variable, not only the ones named above: a new switch in a tool should not be
# able to slip past this list by being newer than it.
for env_name in $(compgen -e | grep '^GNLINK_' || true); do
  case " GNLINK_CMAKE GNLINK_INSTALLER_PAYLOAD_CHECK GNLINK_DEPLOY_SCRIPT GNLINK_SIGN_KEYDIR GNLINK_PUBLIC_KEY_HEX GNLINK_REMOTE_MODE GNLINK_LOCAL_ROOT GNLINK_VERIFY_PUBLIC GNLINK_DEPLOY_HOST GNLINK_DEPLOY_KEY GNLINK_REMOTE_ROOT GNLINK_PUBLIC_BASE GNLINK_UPDATES_SUBDIR GNLINK_MANIFEST_SUBDIR GNLINK_BACKUP_SUBDIR GNLINK_WEBVIEW2_SOURCE " in
    *" $env_name "*) ;;  # already counted above
    *) say_if_substituted "environment $env_name" "set" "" ;;
  esac
done

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
WORKTREE_REAL="$(canon_path "$(pwd -P)")" || die "cannot put $WORKTREE in canonical form"
[ -n "$WORKTREE_REAL" ] || die "cannot put $WORKTREE in canonical form"
git rev-parse --git-dir >/dev/null 2>&1 || die "$WORKTREE is not a git checkout"

# The gates come from where this script is, so this script has to be the candidate's. (RV-13)
# Run from another checkout, the manifest writer and its ARTIFACTS list, the payload-set gate
# (which reads that checkout's update_process_targets.cpp), the installer check, the verifier and
# the deploy script would all be that checkout's -- and a candidate that added or renamed a
# payload file would pass gates that agree with each other and not with it. That is how 0.2.109
# failed. GNLINK_ALLOW_FOREIGN_SCRIPT_DIR=1 lets the regression run fixture worktrees; being a
# GNLINK_* variable it is a substitution, so such a run can neither sign nor publish.
SCRIPT_DIR_REAL="$(canon_path "$(cd "$SCRIPT_DIR" && pwd -P)")" || die "cannot resolve $SCRIPT_DIR"
case "$SCRIPT_DIR_REAL" in
  "$WORKTREE_REAL"/?*) ;;
  *)
    if [ "${GNLINK_ALLOW_FOREIGN_SCRIPT_DIR:-}" = "1" ]; then
      say "tools    : from $SCRIPT_DIR_REAL, OUTSIDE the worktree (GNLINK_ALLOW_FOREIGN_SCRIPT_DIR)"
    else
      die "this script is $SCRIPT_DIR_REAL, not inside --worktree $WORKTREE_REAL -- run the candidate's own automation/gnlink_release.sh; its gates must be the candidate's"
    fi
    ;;
esac

# A git command that fails prints nothing, and nothing reads as "clean" or "unchanged". (RV-14)
HEAD_SHA="$(git rev-parse HEAD)" || die "git rev-parse HEAD failed"
[ -n "$HEAD_SHA" ] || die "git rev-parse HEAD printed nothing"
BRANCH="$(git rev-parse --abbrev-ref HEAD)" || die "git rev-parse --abbrev-ref HEAD failed"
say "worktree : $WORKTREE"
say "branch   : $BRANCH"
say "HEAD     : $HEAD_SHA"

DIRTY="$(git status --porcelain --untracked-files=no)" || die "git status failed; cannot tell whether the worktree is clean"
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
#
# The lock is a directory this script creates and removes, so it goes through the same boundary
# check as everything else it creates -- it was the one path that did not, which is exactly the
# sort of exception that is fine until --rel-root or --worktree points somewhere odd.
mkdir -p "$WORKTREE/.claude" || die "cannot create $WORKTREE/.claude"
LOCK_DIR="$(check_inside_worktree "$WORKTREE/.claude/gnlink_release.lock" "lock")" \
  || die "refusing to take the lock"
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
  say "held by: $(cat "$LOCK_DIR/owner" 2>/dev/null || echo 'unknown')"
  die "another release is running in this worktree ($LOCK_DIR) -- remove it if that is stale"
fi
# Who holds it, in a form the cleanup can check. A run that crashed leaves this behind, and the
# next run should be able to read whose it was rather than guess.
LOCK_OWNER="pid=$$ run=$RUN_ID"
printf '%s version=%s started=%s\n' "$LOCK_OWNER" "$VERSION" "$(date -Is)" > "$LOCK_DIR/owner"

cleanup() {
  # Only this run's lock. Without the check, a run that found a stale lock, was told to remove it,
  # and was then started again alongside a real one would delete the live holder's lock on its way
  # out -- turning one mistake into two runs in the same worktree.
  local held
  held="$(head -1 "$LOCK_DIR/owner" 2>/dev/null || true)"
  case "$held" in
    "$LOCK_OWNER"*) ;;
    *) return 0 ;;
  esac
  rm -f "$LOCK_DIR/owner" 2>/dev/null
  rmdir "$LOCK_DIR" 2>/dev/null

  # A reservation this run made and never filled is given back. Only if it is EMPTY: anything in
  # it is either this run's release, which is finished and staying, or somebody else's, which was
  # never ours to remove.
  if [ "$RESERVED_FINAL" = "1" ] && [ "$REL_DIR" != "$REL_FINAL" ] && [ -d "$REL_FINAL" ]; then
    rmdir "$REL_FINAL" 2>/dev/null || true
  fi
}
trap cleanup EXIT

# Both directories are checked here, before a compiler runs, so a bad path fails in a second
# rather than twenty minutes in.
# The normalised form is KEPT, not checked and thrown away. It used to be discarded here, so
# the path that was checked and the path handed to CMake at stage 2 were different strings --
# which is most of what made the backslash case reachable.
BUILD_DIR="$(check_inside_worktree "$BUILD_DIR" "build dir")" \
  || die "refusing to use the build dir"
REL_DIR="$(check_inside_worktree "$REL_DIR" "release dir")" \
  || die "refusing to use the release dir"
PAYLOAD="$REL_DIR/payload"
check_inside_worktree "$REL_FINAL" "release directory" >/dev/null


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
# The SDK inputs the build actually consumes, hashed as a set.
#
# The first version hashed whichever WebView2Loader.dll `find` happened to return first, which
# says nothing about the headers the code compiles against or the import library it links. A
# release "pinned" by one arbitrary file out of a package is not pinned.
sdk_inputs() {
  local root="$WORKTREE/third_party/webview2"
  [ -d "$root" ] || return 1
  # Headers compiled against, the import library linked, the loader shipped beside the binaries,
  # and the package's own manifest. Sorted, so the list is the same on every machine.
  find "$root" \( -iname '*.h' -o -iname '*.nuspec' \
                  -o -iname 'WebView2Loader.dll' -o -iname 'WebView2LoaderStatic.lib' \
                  -o -iname 'WebView2Loader.dll.lib' \) -type f -print 2>/dev/null | sort
}

sdk_hash() {
  local files
  files="$(sdk_inputs)" || { printf 'unknown'; return 0; }
  [ -n "$files" ] || { printf 'unknown'; return 0; }
  # One hash over the list of (hash, relative name) pairs: a file appearing or disappearing
  # changes it, not just a file changing.
  printf '%s\n' "$files" | while IFS= read -r f; do
    printf '%s  %s\n' "$(sha256sum "$f" | cut -d' ' -f1)" "${f#"$WORKTREE"/}"
  done | sha256sum | cut -d' ' -f1
}
SDK_HASH_BEFORE="$(sdk_hash)"
[ "$SDK_HASH_BEFORE" != "unknown" ] || die "could not hash the WebView2 SDK; refusing to build a release whose inputs cannot be named"
say "sdk      : $SDK_HASH_BEFORE"


[ -x "$CMAKE" ] || die "cmake not found at $CMAKE"

# The published name is RESERVED here, by creating it, rather than checked here and taken later.
#
# `mv -T` was doing the taking, on the stated belief that it fails if the name exists. It does not:
# it replaces an existing EMPTY directory without a word. Measured, not assumed -- an empty
# directory at the target was silently swallowed. And a check at this point followed by a move
# nine stages later is a gap wide enough for a second run to walk through anyway.
#
# mkdir is the atomic part, the same reason the lock uses it: exactly one of two concurrent runs
# can create this name, and the other is told so here rather than at the end of a build.
REL_FINAL="$(check_inside_worktree "$REL_FINAL" "release directory")" \
  || die "refusing to reserve the release directory"
if ! mkdir -p "$(dirname "$REL_FINAL")" 2>/dev/null; then
  die "cannot create $(dirname "$REL_FINAL")"
fi
if ! mkdir "$REL_FINAL" 2>/dev/null; then
  if [ -n "$(ls -A "$REL_FINAL" 2>/dev/null)" ]; then
    die "$REL_FINAL already exists and is not empty -- that is a release somebody may still be checking; move it aside yourself"
  fi
  die "$REL_FINAL already exists -- another run may hold it, or it is left over; remove it yourself if it is nothing"
fi
# Reserved, not finished. A run that dies before stage 7b leaves an empty directory here, which
# the next run reports rather than silently reusing.
RESERVED_FINAL=1

# ------------------------------------------------------------------- 2. configure
step "2. fresh configure -> $BUILD_DIR"

# Fresh every time: a release built on top of a stale cache is a release nobody can reproduce.
# Fresh by creating a new directory, not by deleting an old one -- if the build dir is already
# there, this run stops and says so rather than removing somebody's work.
make_fresh_dir "$BUILD_DIR" "build dir"
# The logs sit beside the build dir, not in it, so the fresh build dir says nothing about them.
# An earlier run's log is somebody's evidence; `>` would have replaced it silently. (RV-14)
for existing_log in "$BUILD_DIR.configure.log" "$BUILD_DIR.build.log"; do
  [ ! -e "$existing_log" ] || die "$existing_log already exists -- an earlier run's log; move it aside yourself"
done
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
# A substituted run still lands at the real release path -- that is what lets a reviewer inspect
# it -- so the directory says, in a file that travels with it, that it is not a release.
# gnlink_deploy.sh refuses any release directory carrying it, so signing it by hand later cannot
# get stub output published. (RV-11)
if [ "$SUBSTITUTIONS" != "0" ]; then
  {
    printf 'NOT A RELEASE -- assembled with %s substituted tool(s) or setting(s):\n' "$SUBSTITUTIONS"
    printf '  %s\n' "$SUBSTITUTED_LIST"
    printf 'run %s, commit %s\n' "$RUN_ID" "$HEAD_SHA"
  } > "$REL_DIR/NOT_A_RELEASE.txt" || die "could not write the NOT_A_RELEASE marker"
fi
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
# Pinned now, checked again right before it is signed and before it is published: between stage 5
# and stage 8 there are gates, a move and minutes of wall time, and a signature over a document
# that changed in between would be a signature over something no gate looked at. (RV-14)
MANIFEST_SHA="$(sha256sum "$REL_DIR/windows.manifest" | cut -d' ' -f1)"
[ -n "$MANIFEST_SHA" ] || die "could not hash the manifest"
say "manifest sha256: $MANIFEST_SHA"
manifest_unchanged() {
  local now
  now="$(sha256sum "$REL_DIR/windows.manifest" 2>/dev/null | cut -d' ' -f1)"
  [ "$now" = "$MANIFEST_SHA" ] || die "windows.manifest changed since stage 5 ($MANIFEST_SHA -> ${now:-missing}) -- $1"
}

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
INSTALLER_OUT="$(powershell.exe -NoProfile -ExecutionPolicy Bypass \
  -File "$(cygpath -w "$INSTALLER_CHECK")" \
  -Setup "$(cygpath -w "$PAYLOAD/GNLinkSetup.exe")" \
  -Payload "$(cygpath -w "$PAYLOAD")" 2>&1)"
INSTALLER_RC=$?
printf '%s\n' "$INSTALLER_OUT" | tr -d '\r'
[ "$INSTALLER_RC" = "0" ] || die "installer payload gate failed"
# What the summary says about this gate is the gate's own count, not a fixed phrase: the
# summary used to print "9/9 PASS" whatever ran here, stub included. (RV-12)
INSTALLER_RESULT="$(printf '%s\n' "$INSTALLER_OUT" | tr -d '\r' \
  | sed -n 's/^\([0-9][0-9]*\/[0-9][0-9]*\) embedded payloads match the release payload$/\1/p' | tail -1)"
[ -n "$INSTALLER_RESULT" ] || die "the installer payload check exited 0 but did not report a match count"

say ""
say "-- inputs: did anything move while this was building"
# What "unchanged" covers, said exactly: the TRACKED files of this commit, the WebView2 inputs
# listed by sdk_inputs, and the commit itself. It does not cover untracked files -- `git status
# --untracked-files=no` does not look at them, and the earlier note claiming this pinned every
# input was wrong about its own check.
DIRTY_AFTER="$(git -C "$WORKTREE" status --porcelain --untracked-files=no)" \
  || die "git status failed after the build; cannot tell whether the worktree changed"
[ -z "$DIRTY_AFTER" ] || { say "$DIRTY_AFTER"; die "the worktree changed during the build; this release does not describe commit $HEAD_SHA"; }
HEAD_AFTER="$(git -C "$WORKTREE" rev-parse HEAD)" || die "git rev-parse HEAD failed after the build"
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
# The name was reserved at preflight, so what happens here is filling it, not taking it.
[ "$RESERVED_FINAL" = "1" ] || die "the release name was never reserved; refusing to publish into it"
[ -d "$REL_FINAL" ] || die "$REL_FINAL disappeared while this run was building"
[ -z "$(ls -A "$REL_FINAL" 2>/dev/null)" ] \
  || die "$REL_FINAL has contents that are not this run's; refusing to mix them"

# Contents, not the directory: the directory is the reservation. `mv -T` would have replaced it,
# which is the behaviour this whole arrangement exists to avoid.
( shopt -s dotglob nullglob; mv "$REL_DIR"/* "$REL_FINAL"/ ) \
  || die "could not move the release from $REL_DIR into $REL_FINAL"
[ -z "$(ls -A "$REL_DIR" 2>/dev/null)" ] \
  || die "$REL_DIR still has contents after the move; the release is in two places"
rmdir "$REL_DIR" 2>/dev/null || true

REL_DIR="$REL_FINAL"
PAYLOAD="$REL_DIR/payload"

# And the bytes are what they were before the move. A move that half-succeeded, or a payload
# that changed underneath it, is not something the earlier checksum run can speak for.
( cd "$REL_DIR" && sha256sum -c SHA256SUMS.txt ) >/dev/null \
  || die "the release does not match its own SHA256SUMS after being moved into $REL_FINAL"
# SHA256SUMS covers the payload, not the manifest; the manifest has its own pin from stage 5.
manifest_unchanged "after the gates and the move"
say "release: $REL_FINAL (verified after the move)"

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
    manifest_unchanged "refusing to sign it"
    powershell.exe -NoProfile -ExecutionPolicy Bypass \
      -File "$(cygpath -w "$SCRIPT_DIR/gnlink_release_sign.ps1")" \
      -ReleaseDir "$(cygpath -w "$REL_DIR")" \
      -TrustedKeyHex "$TRUSTED_KEY" \
      ${SIGN_KEYDIR:+-KeyDir "$(cygpath -w "$SIGN_KEYDIR")"} || die "signing failed"
    [ -f "$REL_DIR/windows.sig" ] || die "no windows.sig after signing"
    manifest_unchanged "the signature is over a document no gate checked"
    SIGNED=1
    say "signed"

    # Verified by the binary that was BUILT FROM THIS CANDIDATE, using the key compiled into it.
    #
    # The previous version called gnlink_verify_manifest.js and described it as "the product
    # verifier". It is not: it is the SERVER's implementation in JavaScript, reading the key out
    # of update_manifest.cpp as text. A useful second opinion, and the wrong thing to claim as the
    # product accepting the document -- the product is C++, and "two implementations agree" is a
    # different sentence from "the shipped one accepts it".
    #
    # remote60_verify_release links the same load_manifest, the same default_verifier and the same
    # compiled-in key as the update client, and it does both halves in one run: it accepts the
    # real document and refuses the same document with one byte flipped. Acceptance alone is
    # equally true of a verifier that never looks at the signature.
    # Built at stage 3 with the payload, inside the drift window. Only run here.
    VERIFIER="$BUILD_DIR/apps/native_poc/Release/remote60_verify_release.exe"
    [ -f "$VERIFIER" ] || die "the verifier is not at $VERIFIER -- it should have been built with the payload"
    "$VERIFIER" "$REL_DIR/windows.manifest" "$REL_DIR/windows.sig" windows \
      || die "the verifier built from this candidate rejects this release -- do not publish"
    say "verified by remote60_verify_release, built from this candidate (accepts, and refuses a tampered copy)"

    # The server's implementation as well, said as what it is. If these two ever disagree, one of
    # the two places the key lives has drifted.
    if command -v node >/dev/null 2>&1; then
      node "$SCRIPT_DIR/gnlink_verify_manifest.js" "$REL_DIR/windows.manifest" "$REL_DIR/windows.sig" windows \
        || die "the server-side verifier rejects this signature -- do not publish"
      say "...and by the server-side implementation (gnlink_verify_manifest.js)"
    else
      say "server-side verifier skipped: node is not on PATH (the compiled one above is the gate)"
    fi
  fi
else
  say "not requested (--sign)"
fi

# ------------------------------------------------------------------- 9. deploy
step "9. deploy"

DEPLOY_LOG="$REL_DIR/deploy.log"
DEPLOY_RC=""
if [ "$DO_DEPLOY" = "1" ]; then
  if [ "$DRY_RUN" = "1" ]; then
    # A dry run never signs, so requiring a signature here is requiring something this mode
    # cannot produce. What it checks instead is everything that does not need one.
    #
    # There used to be a branch here for "a dry run of a release that is already signed", which
    # called the deploy script with --dry-run. It could not be reached -- the release directory is
    # created fresh by this run, so no signature can be in it -- and had it been reached, the
    # deploy script's --dry-run still makes remote directories over ssh. Removed rather than kept
    # as a path nobody runs. (RV-14)
    #
    # Not even --verify-only. That script refuses before it reads its flags when there is no
    # signature, so calling it here would fail on a file this mode was never going to write --
    # which is the contradiction this whole table exists to remove, moved one layer down.
    {
      say "would deploy $REL_DIR"
      say "no signature, because --dry-run does not use the key, so the deploy script is not"
      say "called at all -- its preflight requires one before it reads any flag."
      # NOT "run this script again with --deploy --dry-run". By then the release directory
      # exists, and refusing to reuse an existing one is the rule two stages up -- so the
      # advice contradicted the guard and could not be followed. The deploy script takes a
      # release directory directly, which is what this case actually needs.
      say "to rehearse publication of a release that IS signed, point the deploy"
      say "script at it directly:"
      say "  automation/gnlink_deploy.sh --release-dir <signed release dir> --dry-run"
    } 2>&1 | tee "$DEPLOY_LOG"
  else
    [ -f "$REL_DIR/windows.sig" ] || die "--deploy without a signature; sign first"
    manifest_unchanged "refusing to publish it"
    "$DEPLOY_SCRIPT" --release-dir "$REL_DIR" 2>&1 | tee "$DEPLOY_LOG"
    DEPLOY_RC="${PIPESTATUS[0]}"
    # The deploy script's exit codes mean different things and are passed on as they are: 3 is
    # "a person with sudo has to do the printed operations", 4 is "the manifest/sig swap failed
    # part-way -- restore the backup pair". Folding both into 1 hid which one happened. (RV-14)
    case "$DEPLOY_RC" in
      0) ;;
      3) printf 'ESCALATE  the deploy script needs operations the gnlink account cannot do -- they are printed above (log: %s)\n' "$DEPLOY_LOG" >&2
         exit 3 ;;
      4) printf 'FAIL  the manifest/sig swap failed part-way: what is published may be a NEW manifest with an OLD signature. Restore the exact backup pair by hand (log: %s)\n' "$DEPLOY_LOG" >&2
         exit 4 ;;
      *) die "deploy failed with exit $DEPLOY_RC (log: $DEPLOY_LOG)" ;;
    esac
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
# From what the deploy script DID and what its outside check SAID, not from the flag that asked
# for it. `deployed : yes` used to follow from --deploy alone, so a run whose outside check was
# skipped read exactly like one whose check passed. (RV-12)
external_result() {
  [ -f "$DEPLOY_LOG" ] || { printf 'no deploy log'; return; }
  local verified
  verified="$(grep -oE 'all [0-9]+ artifacts fetched anonymously over https and hash as signed' "$DEPLOY_LOG" | tail -1)"
  if [ -n "$verified" ]; then printf 'VERIFIED -- %s' "$verified"
  elif grep -q 'SKIPPED' "$DEPLOY_LOG"; then printf 'NOT VERIFIED -- the outside check was skipped'
  elif grep -qi 'curl unavailable' "$DEPLOY_LOG"; then printf 'NOT VERIFIED -- curl was unavailable'
  else printf 'NOT VERIFIED -- the deploy log has no result line for it'
  fi
}
if [ "$DO_DEPLOY" != "1" ]; then
  say "deployed  : no (not requested)"
elif [ "$DRY_RUN" = "1" ]; then
  say "deployed  : no (dry run; the deploy script was not called)"
else
  say "deployed  : deploy script exited $DEPLOY_RC; outside check: $(external_result)"
fi
say ""
say "artifact sha256:"
sed 's/^/  /' "$REL_DIR/SHA256SUMS.txt"
say ""
if [ "$SUBSTITUTIONS" != "0" ]; then
  say ""
  say "NOT A RELEASE: $SUBSTITUTIONS tool(s) were substituted, listed at the top of this run."
fi
say "gates: parity PASS, payload-set PASS, checksums PASS, installer payload $INSTALLER_RESULT (as reported by $(basename "$INSTALLER_CHECK")),"
say "       tracked files, the listed SDK inputs and the commit unchanged since preflight"
say "       (untracked files are NOT covered by that check)"
say "commit    : $HEAD_SHA"
say "sdk set   : $SDK_HASH_BEFORE  ($(sdk_inputs 2>/dev/null | wc -l | tr -d ' ') files)"
# Every tool this run can call, at the path it actually used (a substituted one included), and a
# missing one is a failure rather than a file quietly left out of the hash. (RV-12)
SCRIPT_FILES=("$SCRIPT_DIR/gnlink_release.sh" "$SCRIPT_DIR/gnlink_release_sign.ps1"
              "$SCRIPT_DIR/gnlink_path_probe.ps1" "$SCRIPT_DIR/gnlink_release_manifest.py"
              "$SCRIPT_DIR/gnlink_check_payload_set.py" "$SCRIPT_DIR/gnlink_verify_manifest.js"
              "$INSTALLER_CHECK" "$DEPLOY_SCRIPT")
for tool_file in "${SCRIPT_FILES[@]}"; do
  [ -f "$tool_file" ] || die "summary: the tool $tool_file is missing, so this run cannot say what ran it"
done
say "scripts   : $(sha256sum "${SCRIPT_FILES[@]}" | sha256sum | cut -d' ' -f1)"
say "           (the ${#SCRIPT_FILES[@]} tools this script calls, as used by this run:"
for tool_file in "${SCRIPT_FILES[@]}"; do
  say "            $(sha256sum "$tool_file" | cut -c1-16)  $tool_file"
done
say "           )"
if [ "$SIGNED" = "1" ]; then
  say "       signature verified by the product's own verifier"
fi
# The deploy script's own external check is the last word on whether what is published is what
# was built, so its answer belongs in this summary rather than only in its output.
if [ -f "$DEPLOY_LOG" ]; then
  say ""
  say "external check, from $DEPLOY_LOG:"
  # SKIPPED, WARN and failures are kept: filtering on "https|sha256|..." kept the heading of a
  # check that never ran and dropped the line saying it never ran. (RV-12)
  grep -iE "https|sha256|published|manifest|sig|SKIPPED|WARN|could not|ERROR|not verified" "$DEPLOY_LOG" \
    | tail -16 | sed 's/^/  /' \
    || say "  (nothing matched in the deploy log)"
fi
exit 0
