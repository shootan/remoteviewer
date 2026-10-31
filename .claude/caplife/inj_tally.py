"""Tally input injection in every saved host log of this worker's runs (read-only)."""
import collections
import os
import re
import sys

ROOTS = [
    'D:/remote/remote/.claude/caplife',
    'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e',
    'C:/work/gnlink-wt/feat-apk-0233/build/test-scratch',
    'C:/work/gnlink-wt/ka-r3/build/test-scratch',
    'C:/work/gnlink-wt/ka-r3/build/apps/native_poc/Release/test-scratch',
]
mode_re = re.compile(r'input injection (enabled mode=\S+|disabled \([^)]*\)(?: mode=\S+)?|blocked[^\r\n]*)')
ev_re = re.compile(r'\[input-timing\] .*?eventKind=(\d+).*?injectionEnabled=1.*?osInjectionConfirmed=(\d)')
total = collections.Counter()
files_on = []
seen = set()
for root in ROOTS:
    if not os.path.isdir(root):
        continue
    for d, _, fs in os.walk(root):
        for f in fs:
            if not (f.endswith('.log') or f.endswith('.txt')):
                continue
            p = os.path.join(d, f).replace('\\', '/')
            try:
                size = os.path.getsize(p)
                if size > 200_000_000:
                    continue
                txt = open(p, encoding='utf-8', errors='replace').read()
            except OSError:
                continue
            if 'input injection' not in txt and 'injectionEnabled=1' not in txt:
                continue
            modes = collections.Counter(m.group(1) for m in mode_re.finditer(txt))
            kinds = collections.Counter()
            confirmed = 0
            for m in ev_re.finditer(txt):
                kinds[m.group(1)] += 1
                confirmed += m.group(2) == '1'
            if any(k.startswith('enabled') for k in modes) or kinds:
                files_on.append((p, dict(modes), dict(kinds), confirmed))
                for k, v in kinds.items():
                    total[k] += v
                total['confirmed'] += confirmed
for p, modes, kinds, conf in sorted(files_on):
    print(f'{p} | {modes} | kinds={kinds} osConfirmed={conf}')
print('TOTAL', dict(total), 'files', len(files_on))
