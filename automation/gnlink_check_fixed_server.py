#!/usr/bin/env python3
"""Build gate: what ships has one directory server and no way to be pointed at another.

    python automation/gnlink_check_fixed_server.py --dir <folder with the product exes>
    python automation/gnlink_check_fixed_server.py --apk <release.apk>
    python automation/gnlink_check_fixed_server.py --self-test

Two questions are asked of each file, by reading its bytes:

  1. Is the fixed address in it?   (apps/native_poc/src/fixed_directory.hpp, BuildConfig)
  2. Is any test switch in it?     The test builds that talk to a fixture directory are
     separate executables compiled with REMOTE60_SHELL_TEST_SEAM / REMOTE60_HOST_TEST_SEAM.
     Each of those code paths prints a message naming itself, and the wrappers read environment
     variables; those strings are what is looked for.

GNLinkClient.exe, GNLinkHost.exe and GNLinkStream.exe must carry the address: the first two
sign in to it, and the streaming host compares the address it is handed against it to decide
whether a token cached under a former name may be presented. GNLinkViewer.exe is handed its
directory on the command line (--directory-url) by the client and decides nothing from the
constant, so for it only question 2 decides; whether the address is present is printed and
not judged.

This reads bytes. It says nothing about what the program does with them -- that is what
client_recovery_ui_runner.js and host_login_ui_runner.js are for.

Exit 0 when every check passes, 1 when one fails, 2 when it was not given something to check.
"""

import argparse
import os
import sys
import tempfile
import zipfile

FIXED_ADDRESS = 'https://gnlink.shotan.net'

# What only a test build contains.
TEST_MARKERS = [
    'shell-test-seam',            # client_shell_main.cpp, REMOTE60_SHELL_TEST_SEAM
    'host-test-seam',             # host_app_main.cpp, REMOTE60_HOST_TEST_SEAM
    'GNLINK_HOST_TEST_',          # host_app_ui_test.cpp (ROOT, DIRECTORY)
    'GNLINK_RECOVERY_TEST_ROOT',  # client_recovery_ui_test.cpp
]

# (file, must carry the address)
WINDOWS_FILES = [
    ('GNLinkClient.exe', True),
    ('GNLinkHost.exe', True),
    ('GNLinkStream.exe', True),
    ('GNLinkViewer.exe', False),
]

# What the sign-in screen's removed field was called; a release APK has no resource by that name.
APK_REMOVED = ['loginServerInput', 'login_server_hint', 'login_needs_server']


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
        report.note('%s is handed its directory by its parent; the address is %s in it'
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


def self_test():
    """The gate against files made to fail it. A gate that cannot fail is decoration."""
    outer = Report()
    with tempfile.TemporaryDirectory(prefix='gnlink-fixed-server-gate-') as scratch:
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
        missing = make('missing', {})
        os.remove(os.path.join(missing, 'GNLinkViewer.exe'))
        outer.check(verdict(missing) == 1, 'a missing program fails rather than being skipped')
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
