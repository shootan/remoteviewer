# -*- coding: utf-8 -*-
"""Refuses to publish a manifest that does not name everything the update replaces.

0.2.109 was published without GNLinkSetup.exe. Download, signature and every hash passed; the
release then died at the swap, with the host already on its way down and nothing to bring back.
Nothing in the pipeline had compared the manifest against the list the product actually replaces,
because that list only existed in C++ and the publisher only ever looked at the files on disk.

So it is compared here, before anything is uploaded. The expected set is READ from
product_payload_names() rather than written down again: a list transcribed into a second place is
a list that drifts, which is the failure this exists to stop.

  python automation/gnlink_check_payload_set.py <manifest>

Exit 0 = the manifest names everything. Anything else = do not publish.

WHAT "EVERYTHING" MEANS DEPENDS ON THE PLATFORM, and until now this file did not know that. It
applied the Windows list -- ten exe and html names read from the C++ swap targets -- to every
manifest it was handed, so an Android manifest naming its one APK was refused for "not naming
GNLinkHost.exe". gnlink_deploy.sh calls this unconditionally, which meant --platform android could
never publish: the already-published 0.2.19 manifest is refused by this check too. That is why the
server holds no android manifest backups -- the android releases that exist went up some other way,
and a release published outside the script is a release with nothing to roll back to.

The fix is a branch, not an exemption. Android has an invariant worth enforcing too, it is just a
different one: one artifact, it is an APK, and it carries the versionCode the phone compares
against. The Windows path is untouched.
"""
import io
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, 'apps', 'native_poc', 'src', 'update_process_targets.cpp')


def expected_names():
    src = io.open(SRC, encoding='utf-8').read()
    # The stop list, from product_image_names().
    stop_block = src[src.index('product_image_names()'):src.index('return names;')]
    names = re.findall(r'L"([^"]+)"', stop_block)
    # Plus what product_payload_names() appends on top of it.
    start = src.index('std::vector<std::wstring> product_payload_names()')
    body = src[start:src.index('return names;', start)]
    for extra in re.findall(r'names\.push_back\(L"([^"]+)"\)', body):
        names.append(extra.replace('\\\\', '\\'))
    if not names:
        raise SystemExit('could not read product_payload_names() from %s' % SRC)
    return names


def manifest_lines(path):
    return io.open(path, encoding='utf-8').read().split('\n')


def manifest_names(path):
    found = []
    for line in manifest_lines(path):
        if line.startswith('artifact='):
            found.append(line[len('artifact='):].split('|')[0])
    return found


def manifest_field(path, key):
    """The value of a `key=value` line, or None. The first occurrence wins."""
    prefix = key + '='
    for line in manifest_lines(path):
        if line.startswith(prefix):
            return line[len(prefix):].strip()
    return None


def check_windows(present):
    """The original check, unchanged: the manifest must name every file the swap replaces."""
    expected = expected_names()
    lower = set(n.lower() for n in present)

    missing = [n for n in expected if n.lower() not in lower]
    extra = [n for n in present if n.lower() not in set(e.lower() for e in expected)]

    print('expected %d names from product_payload_names(), manifest names %d'
          % (len(expected), len(present)))
    for n in expected:
        print('%s  %s' % ('PASS' if n.lower() in lower else 'FAIL', n))
    if extra:
        # Not an error: a manifest may carry a file the swap does not place. It is printed so that
        # the decision is visible rather than silent.
        print('\nin the manifest and not replaced by the swap: %s' % ', '.join(extra))

    if missing:
        print('\nREFUSING: the manifest does not name %s.' % ', '.join(missing), file=sys.stderr)
        print('The update would download, verify, stop the product and then fail at the swap.',
              file=sys.stderr)
        return 1
    print('\nthe manifest names everything the update replaces')
    return 0


def check_android(path, present):
    """Android replaces the whole app, so the invariant is about the one package that does it.

    Not a weaker check -- a different one. An Android update that names no APK, or names two and
    leaves the installer to choose, or omits the versionCode the phone compares against, is as
    broken at install time as a Windows manifest missing GNLinkSetup.exe was at swap time.
    """
    print('android release: expected exactly one .apk artifact with a versionCode, '
          'manifest names %d' % len(present))
    problems = []

    if len(present) != 1:
        problems.append('expected exactly 1 artifact, found %d (%s)'
                        % (len(present), ', '.join(present) if present else 'none'))
    for n in present:
        print('%s  %s' % ('PASS' if n.lower().endswith('.apk') else 'FAIL', n))
    if not [n for n in present if n.lower().endswith('.apk')]:
        problems.append('no artifact is an .apk')

    raw = manifest_field(path, 'versionCode')
    if raw is None:
        problems.append('no versionCode= line; the phone has nothing to compare against')
        print('FAIL  versionCode=')
    else:
        try:
            code = int(raw)
        except ValueError:
            code = -1
        if code <= 0:
            problems.append('versionCode=%r is not a positive integer' % raw)
            print('FAIL  versionCode=%s' % raw)
        else:
            print('PASS  versionCode=%d' % code)

    if problems:
        print('\nREFUSING: %s.' % '; '.join(problems), file=sys.stderr)
        print('The update would be offered and then fail to install, or never be offered at all.',
              file=sys.stderr)
        return 1
    print('\nthe manifest names the package this update installs')
    return 0


def main():
    if len(sys.argv) < 2:
        print('usage: gnlink_check_payload_set.py <manifest>', file=sys.stderr)
        return 2

    path = sys.argv[1]
    platform = manifest_field(path, 'platform')
    present = manifest_names(path)

    if platform == 'windows':
        return check_windows(present)
    if platform == 'android':
        return check_android(path, present)

    # Fail closed. A manifest that does not say what it is for cannot be checked against anything,
    # and guessing from the artifact names is how the wrong list got applied in the first place.
    print('REFUSING: the manifest does not declare a platform this check knows '
          '(platform=%r).' % platform, file=sys.stderr)
    return 1


if __name__ == '__main__':
    sys.exit(main())
