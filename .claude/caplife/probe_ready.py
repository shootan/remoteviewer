import os, re, sys
ROOT = sys.argv[1]
for k in 'abc':
    ready = open(os.path.join(ROOT, k, 'directoryhost_ready.txt'), encoding='utf-8').read().strip()
    m = re.match(r'^ready run=(\S+) harness_pid=\d+ host_pid=\d+ host_log=(\S+)$', ready)
    print(k, bool(m), m.group(1) if m else None, os.path.basename(os.path.normpath(ROOT)) + '-' + k, repr(ready[:60]))
