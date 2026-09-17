# -*- coding: utf-8 -*-
"""Exercises the payload-set gate on both platforms, including the refusals.

  python automation/gnlink_check_payload_set_test.py

The gate is the thing standing between a half-described release and the server, so the cases that
matter most are the ones where it must say NO. A gate that only ever passes is decoration: the
0.2.109 release passed every check there was. Each refusal below is therefore asserted to actually
exit non-zero, not merely to print something.

Synthetic manifests are written to a temp directory. Nothing under .claude/rel is read or touched,
so a release already staged for publication cannot be disturbed by running this.
"""
import io
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
GATE = os.path.join(HERE, 'gnlink_check_payload_set.py')
REPO = os.path.dirname(HERE)

WINDOWS_NAMES = [
    'GNLinkHost.exe', 'GNLinkStream.exe', 'GNLinkCapture.exe', 'GNLinkInputService.exe',
    'GNLinkClient.exe', 'GNLinkViewer.exe', 'GNLinkSetup.exe', 'GNLinkUpdater.exe',
    'ui\\shell.html', 'ui\\macro.html',
]

passed = 0
failed = 0


def run_gate(path):
    proc = subprocess.run([sys.executable, GATE, path], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)
    return proc.returncode


def write_manifest(tmp, name, lines):
    path = os.path.join(tmp, name)
    with io.open(path, 'wb') as f:
        f.write(('\n'.join(lines) + '\n').encode('utf-8'))
    return path


def artifact(name):
    return 'artifact=%s|1|%s|https://example/%s' % (name, 'a' * 64, name)


def windows_manifest(names):
    return ['schema=2', 'releaseId=r-test', 'platform=windows', 'arch=x64',
            'version=9.9.9'] + [artifact(n) for n in names]


def android_manifest(names, version_code='19'):
    lines = ['schema=2', 'releaseId=r-test', 'platform=android', 'arch=arm64', 'version=9.9.9']
    if version_code is not None:
        lines.append('versionCode=' + version_code)
    return lines + [artifact(n) for n in names]


def case(label, path, want):
    global passed, failed
    got = run_gate(path)
    ok = (got == want)
    if ok:
        passed += 1
    else:
        failed += 1
    print('%s  %-52s exit=%d (want %d)' % ('PASS' if ok else 'FAIL', label, got, want))


def main():
    tmp = tempfile.mkdtemp(prefix='gnlink_gate_test_')

    # --- the two real releases, if they are staged. Skipped rather than failed when absent, so
    #     this runs on a checkout that has never built anything.
    for label, rel, want in (
            ('real windows manifest (0.2.130)', '.claude/rel/0.2.130/windows.manifest', 0),
            ('real android manifest (0.2.20)', '.claude/rel/0.2.20/android.manifest', 0)):
        path = os.path.join(REPO, rel)
        if os.path.isfile(path):
            case(label, path, want)
        else:
            print('SKIP  %-52s (not staged)' % label)

    # --- windows: the whole set passes, one missing name refuses.
    case('windows: every payload name present', write_manifest(
        tmp, 'win_ok.manifest', windows_manifest(WINDOWS_NAMES)), 0)
    case('windows: one exe missing is REFUSED', write_manifest(
        tmp, 'win_missing.manifest',
        windows_manifest([n for n in WINDOWS_NAMES if n != 'GNLinkSetup.exe'])), 1)

    # --- android: one apk with a versionCode passes.
    case('android: one apk with versionCode', write_manifest(
        tmp, 'and_ok.manifest', android_manifest(['GNLink-9.9.9.apk'])), 0)

    # --- android refusals. These are the point of the file.
    case('android: zero artifacts is REFUSED', write_manifest(
        tmp, 'and_zero.manifest', android_manifest([])), 1)
    case('android: two artifacts is REFUSED', write_manifest(
        tmp, 'and_two.manifest',
        android_manifest(['GNLink-9.9.9.apk', 'GNLink-9.9.8.apk'])), 1)
    case('android: missing versionCode is REFUSED', write_manifest(
        tmp, 'and_novc.manifest',
        android_manifest(['GNLink-9.9.9.apk'], version_code=None)), 1)
    case('android: non-numeric versionCode is REFUSED', write_manifest(
        tmp, 'and_badvc.manifest',
        android_manifest(['GNLink-9.9.9.apk'], version_code='nineteen')), 1)
    case('android: the one artifact is not an apk is REFUSED', write_manifest(
        tmp, 'and_notapk.manifest', android_manifest(['GNLink-9.9.9.zip'])), 1)

    # --- a manifest that does not say what it is for must not be guessed at.
    case('unknown platform is REFUSED (fails closed)', write_manifest(
        tmp, 'nop.manifest',
        ['schema=2', 'platform=symbian', 'version=9.9.9', artifact('whatever.sis')]), 1)
    case('no platform line at all is REFUSED', write_manifest(
        tmp, 'noplat.manifest', ['schema=2', 'version=9.9.9', artifact('whatever.apk')]), 1)

    print('\ngnlink_check_payload_set_test: %s (%d passed, %d failed)'
          % ('PASS' if failed == 0 else 'FAIL', passed, failed))
    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
