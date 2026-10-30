"""old-APK lane: a directory as a TEST COPY with the relay trace -- and nothing else -- added, for one of two refs.
  --ref new  the 0.2.164 candidate, 53fced4 apps/directory (r7f; Codex codex_old_apk_lane_src_r7d.md (3))
  --ref old  the published 0.2.163 directory, fae0e1f apps/directory (= main a0f991a; byte-identical to what the NAS
             runs, read 2026-10-30) -- old-ref r1 (Codex codex_old_apk_lane_peer_lost_decision_r1.md)
Neither server.js has REMOTE60_RELAY_TRACE: the variable alone traces nothing. The trace is the one reviewed in 7a79b09
(test fixture only), cut against each base as relay_trace/relay_trace_<ref>.diff: additions, and the relay sends that gain
the trace's send callback (undefined -- a send exactly as before -- when off): three in 53fced4, two in fae0e1f (it has no
standby lease: 7a79b09's standby hunks have nothing to attach to; its client->host forward re-cut by hand). old-ref r1
adds, to both: each periodic snap carries the buckets of the seconds completed since the previous snap (the fixture ends
Node by its handle -- no close line then). No other branch's server.js; no deployment, no NAS.

  python -I oa_relay_trace_server.py build <out> [--ref new|old] [--plain]
  python -I oa_relay_trace_server.py check <traced> <plain> [--ref new|old]
        7a79b09's relay_trace_test.js and relay_trace_snap_test.js against the traced copy (POSITIVE: trace off no line;
        on: ENABLED, flows both ways, clocks, buckets at the close AND in a snap written before Node is killed, no
        credential); the plain base with the variable set must FAIL "the trace says it is on" (NEGATIVE) while its relay
        works.
<out> is a NEW folder under <worktree>/.claude/oa_relay_server (refused if it exists). The fixture takes
<out>/apps/directory/server.js through -ServerJs and its sha256 through -ServerSha256."""
import hashlib
import os
import re
import subprocess
import sys

E = os.path.dirname(os.path.abspath(__file__))
WT = os.path.normpath(os.path.join(E, '..', '..'))
ANCHOR = os.path.join(WT, '.claude', 'oa_relay_server')
REFS = {
    'new': {'commit': '53fced4',
            'base': '2742143d72bb9482429f3046109b0a833bbf8a5049a3b26a8e1eac2f586c4bb7',
            'diff': 'relay_trace_53fced4.diff',
            'diff_sha': '9883b4db456b3ac2f63305c85b29fddd4638853167170c03e9c1226da41a96a3',
            'traced': '18f6b6d211f0337d9f38db1b689ea20e74261225ab0827f5a0132708205e879e'},
    'old': {'commit': 'fae0e1f',
            'base': 'fdc9cbe8cdfc0015005a2074f90893099751bae45997429c4542c672dea4af7a',
            'diff': 'relay_trace_fae0e1f.diff',
            'diff_sha': 'e7d993ebeffe3fc44704bcfea3bd966ddea8a1ad0a9bdc9fec0a62d717b2e865',
            'traced': '196405dd60a82c46bab2e57a563b19822517710a92e87d17f62591d3801088d5'},
}
TESTS = {'relay_trace_test.js': ('relay_trace_test_7a79b09.js',
                                 '7a762e7af8865fbc91c820092d4c5e15c27a1b6de8b3d3ae0164950cd0875b07'),
         'relay_trace_snap_test.js': ('relay_trace_snap_test.js',
                                      '2cf7195192a6216c917844afff6f4b3defd214054035bb82c7ed61c8f6d122b6')}
# what server.js needs at run time (its require()s and package.json) -- from the ref itself
FILES = ['server.js', 'accounts.js', 'device_credentials.js', 'relay_diag.js', 'update_manifest.js', 'version_compare.js',
         'wake_target.js', 'package.json']
# the test helpers the trace tests use -- 53fced4's (relay_test.js is the same in both refs; fae0e1f has no test_tmp.js)
HELPERS = ['test/relay_test.js', 'test/test_tmp.js']
TEST_TMP_ANCHOR = 'D:/remote/remote/.claude/test-tmp'  # test_tmp.js's own fixed anchor (the main repository's)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def git_show(commit, path):
    return subprocess.run(['git', '-C', WT, 'show', f'{commit}:apps/directory/{path}'], capture_output=True,
                          check=True).stdout


def apply_diff(src, diff):
    """A unified diff applied EXACTLY: every hunk's old lines must stand at its stated line (no fuzz, no offset)."""
    lines = src.split(b'\n')
    out, pos = [], 0
    hunks = re.split(rb'(?m)^(?=@@ )', diff)[1:]
    for h in hunks:
        head, *body = h.split(b'\n')
        m = re.match(rb'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@', head)
        start = int(m.group(1)) - 1
        old, new = [], []
        for l in body:
            if l.startswith(b' '):
                old.append(l[1:]); new.append(l[1:])
            elif l.startswith(b'-'):
                old.append(l[1:])
            elif l.startswith(b'+'):
                new.append(l[1:])
        if start < pos or lines[start:start + len(old)] != old:
            raise SystemExit(f'the diff does not apply exactly at line {start + 1}')
        out += lines[pos:start] + new
        pos = start + len(old)
    return b'\n'.join(out + lines[pos:])


def build(out, ref, plain):
    r = REFS[ref]
    out = os.path.abspath(out)
    if os.path.dirname(out) != ANCHOR:
        raise SystemExit(f'the copy must be a folder directly under {ANCHOR}: {out}')
    os.makedirs(ANCHOR, exist_ok=True)
    os.makedirs(out)  # new: an existing folder is refused
    d = os.path.join(out, 'apps', 'directory')
    os.makedirs(os.path.join(d, 'test'))
    manifest = [f'ref {ref} = {r["commit"]} ({"plain" if plain else "relay trace added"})']
    for f in FILES:
        b = git_show(r['commit'], f)
        if f == 'server.js':
            if sha(b) != r['base']:
                raise SystemExit(f'{r["commit"]} server.js is not the pinned one ({sha(b)})')
            if not plain:
                diff = open(os.path.join(E, 'relay_trace', r['diff']), 'rb').read().replace(b'\r\n', b'\n')
                if sha(diff) != r['diff_sha']:
                    raise SystemExit(f'{r["diff"]} is not the pinned one ({sha(diff)})')
                b = apply_diff(b, diff)
                if sha(b) != r['traced']:
                    raise SystemExit(f'the traced server.js is not the pinned one ({sha(b)})')
        open(os.path.join(d, *f.split('/')), 'wb').write(b)
        manifest.append(f'{sha(b)}  apps/directory/{f}')
    for f in HELPERS:
        b = git_show('53fced4', f)
        open(os.path.join(d, *f.split('/')), 'wb').write(b)
        manifest.append(f'{sha(b)}  apps/directory/{f} (53fced4, test helper)')
    for name, (src, pin) in TESTS.items():
        t = open(os.path.join(E, 'relay_trace', src), 'rb').read().replace(b'\r\n', b'\n')
        if sha(t) != pin:
            raise SystemExit(f'{src} is not the pinned one ({sha(t)})')
        open(os.path.join(d, 'test', name), 'wb').write(t)
        manifest.append(f'{sha(t)}  apps/directory/test/{name}')
    open(os.path.join(out, 'MANIFEST.txt'), 'w', encoding='utf-8').write('\n'.join(manifest) + '\n')
    print('\n'.join(manifest))
    print(f'server.js for the fixture: {os.path.join(d, "server.js")}')
    return 0


def run_test(copy, name, tag):
    tmp = f'{TEST_TMP_ANCHOR}/oa-relay-trace-{tag}-{os.getpid()}'
    env = {**os.environ, 'REMOTE60_TEST_TMP': tmp}
    env.pop('REMOTE60_RELAY_TRACE', None)
    r = subprocess.run(['node', os.path.join(copy, 'apps', 'directory', 'test', name)], env=env, capture_output=True,
                       timeout=180)
    text = r.stdout.decode('utf-8', 'replace')
    open(os.path.join(copy, f'{name}.{tag}.txt'), 'w', encoding='utf-8').write(text)
    return r.returncode, text


def check(traced, plain, ref):
    r = REFS[ref]
    fails = 0

    def c(what, ok, detail=''):
        nonlocal fails
        fails += 0 if ok else 1
        print(f'{"PASS" if ok else "FAIL"}  {what}' + (f'  {detail}' if detail else ''), flush=True)

    for copy, want in ((traced, r['traced']), (plain, r['base'])):
        got = sha(open(os.path.join(copy, 'apps', 'directory', 'server.js'), 'rb').read())
        c(f'{os.path.basename(copy)}: server.js is the pinned one ({ref})', got == want, got[:16])
    rc, text = run_test(traced, 'relay_trace_test.js', 'traced')
    c(f'POSITIVE ({ref} {r["commit"]}): 7a79b09\'s relay_trace_test passes whole (off: no line; on: ENABLED, both ways, '
      'clocks, buckets at the close, no credential; the relay works both ways)',
      rc == 0 and 'relay_trace_test: ALL PASS' in text, f'exit={rc} PASS lines={len(re.findall(r"(?m)^PASS  ", text))}')
    ex = re.search(r'(?m)^  e\.g\. (\[relay-trace\][^\n]{0,200})', text)
    c('POSITIVE: a real [relay-trace] flow line was written', ex is not None, ex.group(1) if ex else 'none')
    rc3, text3 = run_test(traced, 'relay_trace_snap_test.js', 'traced')
    ex3 = re.search(r'(?m)^  e\.g\. ([^\n]{0,240})', text3)
    c('POSITIVE: a periodic snap written BEFORE Node is killed carries 1 s buckets by direction and header kind',
      rc3 == 0 and 'relay_trace_snap_test: ALL PASS' in text3, f'exit={rc3} {ex3.group(1) if ex3 else ""}')
    rc2, text2 = run_test(plain, 'relay_trace_test.js', 'plain')
    c(f'NEGATIVE: {r["commit"]} as it is, the variable set -- "the trace says it is on" FAILs (the variable alone traces '
      'nothing)', rc2 != 0 and re.search(r'(?m)^FAIL  the trace says it is on', text2) is not None, f'exit={rc2}')
    c('NEGATIVE control is sound: its relay itself works both times (relay_test passes)',
      len(re.findall(r'(?m)^PASS  the relay works', text2)) == 2, '')
    print(f'\nrelay trace copy check ({ref}): {"PASS" if fails == 0 else f"{fails} FAILED"}\n'
          f'RESULT: {"PASS" if fails == 0 else "FAIL"}')
    return 0 if fails == 0 else 1


if __name__ == '__main__':
    a = sys.argv[1:]
    ref = 'new'
    if '--ref' in a:
        i = a.index('--ref')
        ref = a[i + 1]
        del a[i:i + 2]
    if ref not in REFS:
        print(__doc__)
        sys.exit(2)
    if a[:1] == ['build'] and len(a) in (2, 3):
        sys.exit(build(a[1], ref, a[2:] == ['--plain']))
    if a[:1] == ['check'] and len(a) == 3:
        sys.exit(check(os.path.abspath(a[1]), os.path.abspath(a[2]), ref))
    print(__doc__)
    sys.exit(2)
