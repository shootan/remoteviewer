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


def manifest_names(path):
    found = []
    for line in io.open(path, encoding='utf-8').read().split('\n'):
        if line.startswith('artifact='):
            found.append(line[len('artifact='):].split('|')[0])
    return found


def main():
    if len(sys.argv) < 2:
        print('usage: gnlink_check_payload_set.py <manifest>', file=sys.stderr)
        return 2

    expected = expected_names()
    present = manifest_names(sys.argv[1])
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


if __name__ == '__main__':
    sys.exit(main())
