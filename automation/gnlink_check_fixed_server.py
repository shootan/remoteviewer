#!/usr/bin/env python3
"""Build gate: what ships signs in to one directory server, and carries no test switch.

    python automation/gnlink_check_fixed_server.py --dir <folder with the product exes>
    python automation/gnlink_check_fixed_server.py --apk <release.apk>
        (also: the stored sign-in's keystore alias and routes are in it, and the backup rules
         that leave the stored sign-in out are named by its manifest)
    python automation/gnlink_check_fixed_server.py --self-test   (REMOTE60_TEST_TMP: see self_test_run_dir)

Two questions are asked of each file, by reading its bytes:

  1. Is the fixed address in it?   (apps/native_poc/src/fixed_directory.hpp, BuildConfig)
  2. Is any test switch in it?     The test builds that talk to a fixture directory are
     separate executables compiled with REMOTE60_SHELL_TEST_SEAM / REMOTE60_HOST_TEST_SEAM /
     REMOTE60_STREAM_TEST_SEAM, and the update client's option-failure case with
     REMOTE60_UPDATE_HTTP_TEST_SEAM. Each of those code paths carries a message naming itself,
     and the wrappers read environment variables; those strings are what is looked for.

What this does NOT claim. It is about the directory: where a sign-in, a host list, a connect and
a log upload go. REMOTE60_UPDATE_MANIFEST_URL / BuildConfig.UPDATE_MANIFEST_URL (an operator's
own manifest address, fetched without a credential) and GNLinkStream's REMOTE60_DIRECTORY_URL
fallback are product behaviour, present in what ships, and not looked for here.

GNLinkClient.exe, GNLinkHost.exe and GNLinkStream.exe must carry the address: the first two
sign in to it, and the streaming host compares the address it is handed against it to decide
whether a token cached under a former name may be presented. GNLinkViewer.exe is handed its
directory on the command line (--directory-url) by the client and decides nothing from the
constant, and GNLinkUpdater.exe is handed its manifest address by the program that starts it;
for those two only question 2 decides, and whether the address is present is printed and not
judged.

This reads bytes. It says nothing about what the program does with them -- that is what
client_recovery_ui_runner.js and host_login_ui_runner.js are for.

Exit 0 when every check passes, 1 when one fails, 2 when it was not given something to check.
"""

import argparse
import os
import re
import stat
import sys
import time
import zipfile

FIXED_ADDRESS = 'https://gnlink.shotan.net'

# What only a test build contains.
TEST_MARKERS = [
    'shell-test-seam',            # client_shell_main.cpp, REMOTE60_SHELL_TEST_SEAM
    'host-test-seam',             # host_app_main.cpp, REMOTE60_HOST_TEST_SEAM
    'GNLINK_HOST_TEST_',          # host_app_ui_test.cpp (ROOT, DIRECTORY)
    'GNLINK_RECOVERY_TEST_ROOT',  # client_recovery_ui_test.cpp
    'stream-test-seam',           # directory_client.cpp, REMOTE60_STREAM_TEST_SEAM
    'os-input-record',            # os_input_effects.hpp, REMOTE60_OS_INPUT_RECORD_ONLY (record-only test host)
    'GNLINK_STREAM_TEST_',        # the same seam's environment (DIRECTORY, FORMER_NAME)
    'REMOTE60_PRELOGIN_CAPTURE_ATTACH',  # host_startup_config.cpp: the capture-attach trigger, test build only
    'update-http-test-seam',      # update_http.cpp, REMOTE60_UPDATE_HTTP_TEST_SEAM
    'sis-test-seam',              # secure_input_service_test_seam.hpp, REMOTE60_SIS_TEST_SEAM (r21 E1)
    'REMOTE60_SIS_TEST_',         # the same seam's environment (SUFFIX, CREATE_FAIL, STOP_EVENT, DIAG)
    'GNLINK_SIS_TEST_',           # lockscreen-cred r1: the service seam's lock-screen agent target (CRED_TARGET_TITLE)
    'mt-ab',                      # mt_ab_test_seam.hpp, REMOTE60_MT_AB_TEST_SEAM (dxgi-wedge-fix r1, Codex B1)
    'GNLINK_CHAIN_',              # client_reconnect_restore_e2e_test.cpp (dxgi-wedge-fix r1)
    'viewer-present-fail-seam',   # viewer_present_fail_seam.hpp, REMOTE60_VIEWER_PRESENT_FAIL_TEST_SEAM (r3 E3)
    'GNLINK_VIEWER_TEST_',        # the same seam's environment (PRESENT_FAIL_FILE)
    # rel 0.2.163 (LK-UX + capture-lifecycle): the test-build host's own log tag for its seams
    # (host_stage_backend.cpp: "[native-video-host][test-seam] ..."), the LK relay's test hooks
    # (SetRetryIoHookForTest / KeptEntriesForTest -- a name that reaches a binary as text means a hook
    # was wired into something that prints or exports it), and the capture lib's test-only still-screen
    # modes as the test host spells them in its environment (=nofirst / =pointerfirst).
    '][test-seam]',
    'ForTest',
    'pointerfirst',
]

# rel 0.2.163: every test-build environment name LK-UX and capture-lifecycle added, each checked ALONE
# (ascii and wide) by the self-test -- the prefixes above cover them, this keeps a prefix change from
# silently uncovering one.
RELEASE_0163_SEAM_NAMES = [
    'GNLINK_STREAM_TEST_DESKTOP_ALIAS',
    'GNLINK_STREAM_TEST_STILL_SCREEN',
    'GNLINK_STREAM_TEST_SECURE_DESKTOP_EVENT',
    'GNLINK_STREAM_TEST_WGC_STILL',
    'GNLINK_STREAM_TEST_FAIL_RESTART_FILE',
    'GNLINK_STREAM_TEST_LOGON_DESKTOP',
    'GNLINK_STREAM_TEST_UNLOCK_PIPE',
    'GNLINK_VIEWER_TEST_STATUS_POLLS',
    'GNLINK_VIEWER_TEST_SEAL_FAIL',
    'GNLINK_STREAM_TEST_PRELOGIN_ROLE',  # apk-0232 evidence A: the test host's logon-screen role
]

# (file, must carry the address)
WINDOWS_FILES = [
    ('GNLinkClient.exe', True),
    ('GNLinkHost.exe', True),
    ('GNLinkStream.exe', True),
    ('GNLinkViewer.exe', False),
    ('GNLinkUpdater.exe', False),
    ('GNLinkInputService.exe', False),  # r21: the secure-input service; its lifecycle test build is separate
]

# What the sign-in screen's removed field was called; a release APK has no resource by that name.
APK_REMOVED = ['loginServerInput', 'login_server_hint', 'login_needs_server']

# What a release APK that stays signed in carries: the keystore alias and preferences file of
# the stored sign-in (KeystoreLoginVault), the two backup rule files that leave it out, and the
# manifest attributes that name them. An APK built without these keeps a credential that
# travels with a backup.
APK_SIGN_IN_CODE = ['gnlink.login.v1', 'gnlink_login', '/api/session/refresh',
                    '/api/session/logout']
APK_BACKUP_RESOURCES = ['backup_rules', 'data_extraction_rules']
APK_BACKUP_ATTRIBUTES = ['fullBackupContent', 'dataExtractionRules']


def contains(data, text):
    """Narrow or wide: the Windows programs hold both kinds of literal."""
    return text.encode('ascii') in data or text.encode('utf-16-le') in data


class Report:
    def __init__(self):
        self.failed = 0
        self.passed = 0

    def check(self, ok, label):
        print(('PASS ' if ok else 'FAIL ') + label)
        if ok:
            self.passed += 1
        else:
            self.failed += 1

    def note(self, text):
        print('      (' + text + ')')


def check_windows_file(report, path, must_carry):
    name = os.path.basename(path)
    with open(path, 'rb') as handle:
        data = handle.read()
    has_address = contains(data, FIXED_ADDRESS)
    if must_carry:
        report.check(has_address, '%s carries %s' % (name, FIXED_ADDRESS))
    else:
        report.note('%s is not required to carry the address; it is %s in it'
                    % (name, 'present' if has_address else 'not present'))
    found = [marker for marker in TEST_MARKERS if contains(data, marker)]
    report.check(not found, '%s has no test switch%s'
                 % (name, (' -- found: ' + ', '.join(found)) if found else ''))


def check_windows_dir(report, directory):
    for name, must_carry in WINDOWS_FILES:
        path = os.path.join(directory, name)
        if not os.path.isfile(path):
            report.check(False, '%s is present in %s' % (name, directory))
            continue
        check_windows_file(report, path, must_carry)


# sh-filter r2 (Codex H4): what only the sharpening TEST seam (src/shseam, -Pgnlink.shTestSeam=true) puts
# into the code -- its marker and the two launch-intent keys that inject faults or force a strength. The
# shipped half (src/noseam) has none of them. Searched in every classes*.dex.
# sh-strength r2: + the display-size pass A/B switch (B is a test-build experiment, never a shipped intent key).
APK_TEST_MARKERS = ['sh-test-seam', 'shTestFault', 'shTestStrength', 'shTestDisplayPass']
# sh-filter r3 (R2): the native half of the same seam (debug variant only) -- its marker and the system
# property prefix it reads. Searched in every lib/*/*.so of the APK.
APK_NATIVE_TEST_MARKERS = ['sh-test-seam', 'debug.gnlink.sh.']


def apk_native_markers_found(so_bytes):
    return [m for m in APK_NATIVE_TEST_MARKERS if m.encode('ascii') in so_bytes]


def apk_test_markers_found(dex):
    return [m for m in APK_TEST_MARKERS if m.encode('ascii') in dex or m.encode('utf-16-le') in dex]


def check_apk(report, path):
    name = os.path.basename(path)
    with zipfile.ZipFile(path) as apk:
        dex = b''.join(apk.read(n) for n in apk.namelist()
                       if n.startswith('classes') and n.endswith('.dex'))
        everything = dex + b''.join(apk.read(n) for n in apk.namelist()
                                    if n == 'resources.arsc' or n.startswith('res/layout'))
    report.check(len(dex) > 0, '%s has code to read' % name)
    report.check(FIXED_ADDRESS.encode('ascii') in dex, '%s carries %s' % (name, FIXED_ADDRESS))
    found = [item for item in APK_REMOVED
             if item.encode('ascii') in everything or item.encode('utf-16-le') in everything]
    report.check(not found, '%s has no server field or text asking for one%s'
                 % (name, (' -- found: ' + ', '.join(found)) if found else ''))
    seam = apk_test_markers_found(dex)
    report.check(not seam, '%s has no sharpening test seam%s'
                 % (name, (' -- found: ' + ', '.join(seam)) if seam else ''))
    with zipfile.ZipFile(path) as apk:
        native = b''.join(apk.read(n) for n in apk.namelist() if n.startswith('lib/') and n.endswith('.so'))
    native_seam = apk_native_markers_found(native)
    report.check(not native_seam, '%s has no sharpening test seam in its native libraries%s'
                 % (name, (' -- found: ' + ', '.join(native_seam)) if native_seam else ''))

    with zipfile.ZipFile(path) as apk:
        names = apk.namelist()
        table = apk.read('resources.arsc') if 'resources.arsc' in names else b''
        manifest = apk.read('AndroidManifest.xml') if 'AndroidManifest.xml' in names else b''
    missing = [item for item in APK_SIGN_IN_CODE if item.encode('ascii') not in dex]
    report.check(not missing, '%s carries the stored sign-in and its two routes%s'
                 % (name, (' -- missing: ' + ', '.join(missing)) if missing else ''))
    missing = [item for item in APK_BACKUP_RESOURCES if not contains(table, item)]
    missing += [item for item in APK_BACKUP_ATTRIBUTES if not contains(manifest, item)]
    report.check(not missing, '%s leaves the stored sign-in out of backups%s'
                 % (name, (' -- missing: ' + ', '.join(missing)) if missing else ''))


# test-injection-finding (Codex codex_avd_guard_src_r2.md T): the self-test's fixtures live ONLY inside the repository's
# test root -- REMOTE60_TEST_TMP, a plain folder directly under this anchor, no link anywhere from the drive down --
# in a NEW run folder of its own; its cleanup removes only what is inside that run folder, and nothing is ever made
# or removed under %TEMP% (it was tempfile.TemporaryDirectory until 2026-10-28: a creation and a deletion outside the
# repository). Anything else is refused BEFORE anything is created.
TEST_ANCHOR = r'D:\remote\remote\.claude\test-tmp'


def _is_link(path):
    try:
        st = os.lstat(path)
    except FileNotFoundError:
        return False
    return stat.S_ISLNK(st.st_mode) or bool(getattr(st, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT)


def _no_link_on_path(path):
    """Every existing component of `path`, from the drive down, is not a link."""
    full = os.path.abspath(path)
    drive, rest = os.path.splitdrive(full)
    cur = drive + os.sep
    for part in [x for x in rest.split(os.sep) if x]:
        cur = os.path.join(cur, part)
        if _is_link(cur):
            return False, 'a link on the path: ' + cur
    return True, ''


def self_test_run_dir(environ=None):
    """(run folder, why): a NEW run folder of this self-test inside the validated test root, or (None, why) -- in which
    case nothing at all was created."""
    environ = os.environ if environ is None else environ
    given = environ.get('REMOTE60_TEST_TMP', '')
    if not given:
        return None, 'REMOTE60_TEST_TMP is not set (the self-test never falls back to %TEMP%)'
    full = os.path.abspath(given)
    name = os.path.basename(full)
    if not re.fullmatch(r'[A-Za-z0-9._-]{1,64}', name) or name in ('.', '..'):
        return None, 'the test root must be a plain ASCII folder name'
    if os.path.normcase(os.path.dirname(full)) != os.path.normcase(TEST_ANCHOR):
        return None, 'REMOTE60_TEST_TMP must be a folder directly under ' + TEST_ANCHOR
    if not os.path.isdir(TEST_ANCHOR):
        return None, 'the test anchor is missing'
    ok, why = _no_link_on_path(full)
    if not ok:
        return None, why
    if os.path.exists(full):
        if not os.path.isdir(full) or _is_link(full):
            return None, 'the test root is not a plain folder'
    else:
        try:
            os.mkdir(full)
        except OSError as e:
            return None, 'the test root could not be created: %s' % e
    for attempt in range(16):
        run = os.path.join(full, 'gate-selftest-%d-%d-%d' % (os.getpid(), int(time.time() * 1000), attempt))
        try:
            os.mkdir(run)  # only a folder this process creates (an existing one is never adopted)
        except FileExistsError:
            continue
        except OSError as e:
            return None, 'the run folder could not be created: %s' % e
        if _is_link(run):
            return None, 'the run folder is a link'
        return run, ''
    return None, 'no new run folder could be made'


def cleanup_run_dir(run):
    """Removes what is inside this self-test's own run folder, then the folder: files and folders only. A link -- a
    symlink or a directory junction -- is never entered, followed or removed: it is pruned BEFORE the walk would go
    below it (T1, Codex codex_avd_guard_src_r3.md: a bottom-up walk had already gone inside a junction before it was
    checked), left in place and said. Returns (files removed, folders removed, what was left)."""
    files = dirs = 0
    left = []
    run = os.path.abspath(run)
    if _is_link(run) or not os.path.isdir(run):
        return 0, 0, [run]  # not a plain folder of its own: nothing is touched
    head = run + os.sep
    to_files, to_dirs = [], []
    for here, subdirs, names in os.walk(run, topdown=True, followlinks=False):
        keep = []
        for d in subdirs:  # decided before the walk goes below: a link is pruned, never entered
            path = os.path.join(here, d)
            if not os.path.abspath(path).startswith(head) or _is_link(path):
                left.append(path)
            else:
                keep.append(d)
                to_dirs.append(path)
        subdirs[:] = keep
        for n in names:
            path = os.path.join(here, n)
            if not os.path.abspath(path).startswith(head) or _is_link(path):
                left.append(path)
            else:
                to_files.append(path)
    for path in to_files:
        try:
            os.remove(path)
            files += 1
        except OSError:
            left.append(path)
    for path in sorted(to_dirs, key=lambda x: x.count(os.sep), reverse=True):  # the deepest first
        try:
            os.rmdir(path)
            dirs += 1
        except OSError:
            left.append(path)
    try:
        os.rmdir(run)
        dirs += 1
    except OSError:
        left.append(run)
    return files, dirs, left


def self_test():
    """The gate against files made to fail it. A gate that cannot fail is decoration."""
    outer = Report()
    scratch, why = self_test_run_dir()
    if scratch is None:
        print('FAIL  self-test refused: %s -- nothing created' % why)
        outer.check(False, 'a valid repository test root for the self-test')
        return outer
    print('NOTE  self-test run folder: %s' % scratch)
    try:
        def make(folder, overrides):
            directory = os.path.join(scratch, folder)
            os.makedirs(directory)
            for name, must_carry in WINDOWS_FILES:
                body = b'MZ' + os.urandom(64)
                if must_carry:
                    body += FIXED_ADDRESS.encode('ascii') + b'\0'
                body = overrides.get(name, lambda b: b)(body)
                with open(os.path.join(directory, name), 'wb') as handle:
                    handle.write(body)
            return directory

        def verdict(directory):
            inner = Report()
            saved = sys.stdout
            sys.stdout = open(os.devnull, 'w')
            try:
                check_windows_dir(inner, directory)
            finally:
                sys.stdout.close()
                sys.stdout = saved
            return inner.failed

        outer.check(verdict(make('good', {})) == 0, 'a product set passes')
        outer.check(verdict(make('no-address', {
            'GNLinkHost.exe': lambda b: b.replace(FIXED_ADDRESS.encode('ascii'), b'x' * 25)})) == 1,
            'a host without the address fails')
        outer.check(verdict(make('other-address', {
            'GNLinkClient.exe': lambda b: b.replace(b'gnlink.shotan', b'gnlimk.shotan')})) == 1,
            'a client with a different address fails')
        outer.check(verdict(make('wide-address', {
            'GNLinkHost.exe': lambda b: b.replace(FIXED_ADDRESS.encode('ascii'),
                                                  FIXED_ADDRESS.encode('utf-16-le'))})) == 0,
            'the address as a wide literal counts')
        for marker in TEST_MARKERS:
            outer.check(verdict(make('marker-' + marker.strip('_'), {
                'GNLinkStream.exe': lambda b, m=marker: b + m.encode('ascii')})) == 1,
                'a streaming host containing "%s" fails' % marker)
        outer.check(verdict(make('wide-marker', {
            'GNLinkClient.exe': lambda b: b + 'shell-test-seam'.encode('utf-16-le')})) == 1,
            'a test switch as a wide literal fails too')
        # lockscreen-cred r1 (Codex L6): the new prefix on its own, ASCII and wide, in the service binary.
        for enc in ('ascii', 'utf-16-le'):
            outer.check(verdict(make('sis-agent-' + enc, {
                'GNLinkInputService.exe': lambda b, e=enc: b + 'GNLINK_SIS_TEST_CRED_TARGET_TITLE'.encode(e)})) == 1,
                'a service carrying GNLINK_SIS_TEST_ alone (%s) fails' % enc)

        # rel 0.2.163: each LK-UX / capture-lifecycle test-build name alone, ascii and wide, in the
        # streaming host and the viewer.
        for seam in RELEASE_0163_SEAM_NAMES:
            for enc in ('ascii', 'utf-16-le'):
                target = 'GNLinkViewer.exe' if seam.startswith('GNLINK_VIEWER_') else 'GNLinkStream.exe'
                outer.check(verdict(make('r0163-%s-%s' % (seam, enc), {
                    target: lambda b, s=seam, e=enc: b + s.encode(e)})) == 1,
                    '%s carrying %s alone (%s) fails' % (target, seam, enc))

        # sh-filter r2 (H4): the APK seam check on dex bytes (a real APK pair is the release-time control:
        # the noseam build passes, the shseam build fails -- see the sh-filter report).
        outer.check(not apk_test_markers_found(b'dex\n035\0' + FIXED_ADDRESS.encode('ascii')),
                    'a dex without the sharpening seam passes the seam check')
        for marker in APK_TEST_MARKERS:
            outer.check(apk_test_markers_found(b'dex\n035\0' + marker.encode('ascii')) == [marker],
                        'a dex containing "%s" fails the seam check' % marker)
        outer.check(not apk_native_markers_found(b'\x7fELF' + FIXED_ADDRESS.encode('ascii')),
                    'a native library without the sharpening seam passes')
        for marker in APK_NATIVE_TEST_MARKERS:
            outer.check(apk_native_markers_found(b'\x7fELF' + marker.encode('ascii')) == [marker],
                        'a native library containing "%s" fails' % marker)
        missing = make('missing', {})
        os.remove(os.path.join(missing, 'GNLinkViewer.exe'))
        outer.check(verdict(missing) == 1, 'a missing program fails rather than being skipped')
    finally:
        files, dirs, left = cleanup_run_dir(scratch)
        print('NOTE  self-test cleanup (its own run folder only): %d file(s), %d folder(s) removed%s'
              % (files, dirs, '' if not left else '; LEFT: ' + ', '.join(left)))
        outer.check(not left, 'the self-test left nothing in its run folder (and removed nothing else)')
    return outer


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--dir', help='folder holding GNLinkClient/Host/Stream/Viewer.exe')
    parser.add_argument('--apk', help='release APK')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()

    if args.self_test:
        report = self_test()
    elif args.dir or args.apk:
        report = Report()
        if args.dir:
            check_windows_dir(report, args.dir)
        if args.apk:
            check_apk(report, args.apk)
    else:
        parser.print_usage()
        return 2

    if report.failed:
        print('gnlink_check_fixed_server: FAIL (%d failed, %d passed)' % (report.failed, report.passed))
        return 1
    print('gnlink_check_fixed_server: ALL PASS (%d checks)' % report.passed)
    return 0


if __name__ == '__main__':
    sys.exit(main())
