# -*- coding: utf-8 -*-
"""Adds the set that actually decides whether a swap can happen: config_.payloadNames.

check_release.py compares against product_image_names() (what the updater stops) and the
installer payload (what a fresh install lays down). Neither is the set Swap enforces.

Swap walks config_.payloadNames and refuses if any of them is missing from staging
(update_effects.cpp:468-474), and staging only ever contains what Download fetched -- which is
`fields.artifacts`, the manifest's list and nothing else (update_effects.cpp:312). So a name in
payloadNames but not in the manifest fails the update at the swap, no matter what is reachable
over http. That is why publishing the file without naming it in the manifest would not have
fixed anything.

The set is read out of updater_effects.cpp rather than typed, for the same reason as before: a
list transcribed by hand is a list that drifts.
"""
import io
import re
import sys

REL = sys.argv[1] if len(sys.argv) > 1 else '.claude/rel/0.2.109-r2'
PLATFORM = sys.argv[2] if len(sys.argv) > 2 else 'windows'

names = set()
for line in io.open(REL + '/' + PLATFORM + '.manifest', encoding='utf-8').read().split('\n'):
    if line.startswith('artifact='):
        names.add(line[len('artifact='):].split('|')[0])

# product_image_names() ...
src = io.open('apps/native_poc/src/update_process_targets.cpp', encoding='utf-8').read()
block = src[src.index('product_image_names()'):src.index('return names;')]
payload = list(re.findall(r'L"([A-Za-z0-9_.]+\.exe)"', block))

# ... plus the two the updater appends (updater_effects.cpp:293-295).
eff = io.open('apps/native_poc/src/updater_effects.cpp', encoding='utf-8').read()
appended = re.findall(r'payloadNames\.push_back\(L"([^"]+)"\)', eff)
payload += [a.replace('\\\\', '\\') for a in appended]

print('production payloadNames (updater_effects.cpp:293-295): %d entries' % len(payload))
bad = 0
for n in payload:
    ok = n in names
    if not ok:
        bad += 1
    print('%s  %-24s %s' % ('PASS' if ok else 'FAIL', n,
                            'in the manifest' if ok else 'NOT in the manifest -> Swap would refuse'))

extra = sorted(names - set(payload))
if extra:
    print('\nin the manifest but not swapped: %s' % ', '.join(extra))
    print('  (GNLinkUpdater.exe is expected here -- the updater does not replace itself)')

print('\n%s' % ('every name Swap requires is in the manifest' if bad == 0
                else '%d name(s) Swap requires are missing' % bad))
sys.exit(1 if bad else 0)
