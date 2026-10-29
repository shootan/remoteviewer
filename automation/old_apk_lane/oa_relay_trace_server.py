"""old-APK lane r7f (Codex codex_old_apk_lane_src_r7d.md (3)): the 0.2.164 candidate's directory (53fced4 apps/directory)
as a TEST COPY with the relay trace -- and nothing else -- added. 53fced4's server.js has no REMOTE60_RELAY_TRACE at all:
setting the variable alone traces nothing. The trace is the one already reviewed (7a79b09 apps/directory/server.js, test
fixture only), re-cut against 53fced4 as relay_trace/relay_trace_53fced4.diff: additions, and three relay sends that gain
the trace's send callback (undefined -- a send exactly as before -- when the trace is off). No other branch's server.js
replaces the candidate's; no deployment, no NAS.

  python -I oa_relay_trace_server.py build <out>          the traced copy: 53fced4 files + the diff, every hash pinned
  python -I oa_relay_trace_server.py build <out> --plain  53fced4 unchanged (the negative control's server)
  python -I oa_relay_trace_server.py check <traced> <plain>
        7a79b09's relay_trace_test.js against both: the traced copy must pass it whole (trace off: no line; on: ENABLED,
        flows both ways, clocks, buckets, no credential); 53fced4 as it is must FAIL "the trace says it is on" with the
        variable set -- the variable alone instruments nothing -- while its relay itself works.
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
CANDIDATE = '53fced4'
BASE_SHA = '2742143d72bb9482429f3046109b0a833bbf8a5049a3b26a8e1eac2f586c4bb7'    # 53fced4 apps/directory/server.js
TRACED_SHA = '52918c04b8fef8f4e66af5c6abe4a7332429416397004432accb79d8c231492a'  # the same + relay_trace_53fced4.diff
DIFF = os.path.join(E, 'relay_trace', 'relay_trace_53fced4.diff')
DIFF_SHA = 'f72329cc3602733213ed57ca6aaf6ff6069a77199cc5de8f7ec47bf9bf47dc86'
TRACE_TEST = os.path.join(E, 'relay_trace', 'relay_trace_test_7a79b09.js')
TRACE_TEST_SHA = '7a762e7af8865fbc91c820092d4c5e15c27a1b6de8b3d3ae0164950cd0875b07'  # 7a79b09 test/relay_trace_test.js
# what server.js needs at run time (its require()s and package.json), and the two test helpers relay_trace_test uses
FILES = ['server.js', 'accounts.js', 'device_credentials.js', 'relay_diag.js', 'update_manifest.js', 'version_compare.js',
         'wake_target.js', 'package.json', 'test/relay_test.js', 'test/test_tmp.js']
TEST_TMP_ANCHOR = 'D:/remote/remote/.claude/test-tmp'  # test_tmp.js's own fixed anchor (the main repository's)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def git_show(path):
    return subprocess.run(['git', '-C', WT, 'show', f'{CANDIDATE}:apps/directory/{path}'], capture_output=True,
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


def build(out, plain):
    out = os.path.abspath(out)
    if os.path.dirname(out) != ANCHOR:
        raise SystemExit(f'the copy must be a folder directly under {ANCHOR}: {out}')
    os.makedirs(ANCHOR, exist_ok=True)
    os.makedirs(out)  # new: an existing folder is refused
    d = os.path.join(out, 'apps', 'directory')
    os.makedirs(os.path.join(d, 'test'))
    manifest = [f'candidate {CANDIDATE} ({"plain" if plain else "relay trace added"})']
    for f in FILES:
        b = git_show(f)
        if f == 'server.js':
            if sha(b) != BASE_SHA:
                raise SystemExit(f'53fced4 server.js is not the pinned one ({sha(b)})')
            if not plain:
                diff = open(DIFF, 'rb').read().replace(b'\r\n', b'\n')  # a CRLF checkout: the same bytes
                if sha(diff) != DIFF_SHA:
                    raise SystemExit(f'relay_trace_53fced4.diff is not the pinned one ({sha(diff)})')
                b = apply_diff(b, diff)
                if sha(b) != TRACED_SHA:
                    raise SystemExit(f'the traced server.js is not the pinned one ({sha(b)})')
        open(os.path.join(d, *f.split('/')), 'wb').write(b)
        manifest.append(f'{sha(b)}  apps/directory/{f}')
    t = open(TRACE_TEST, 'rb').read().replace(b'\r\n', b'\n')
    if sha(t) != TRACE_TEST_SHA:
        raise SystemExit(f'relay_trace_test_7a79b09.js is not the pinned one ({sha(t)})')
    open(os.path.join(d, 'test', 'relay_trace_test.js'), 'wb').write(t)
    manifest.append(f'{sha(t)}  apps/directory/test/relay_trace_test.js (7a79b09)')
    open(os.path.join(out, 'MANIFEST.txt'), 'w', encoding='utf-8').write('\n'.join(manifest) + '\n')
    print('\n'.join(manifest))
    print(f'server.js for the fixture: {os.path.join(d, "server.js")}')
    return 0


def run_trace_test(copy, tag):
    tmp = f'{TEST_TMP_ANCHOR}/oa-relay-trace-{tag}-{os.getpid()}'
    env = {**os.environ, 'REMOTE60_TEST_TMP': tmp}
    env.pop('REMOTE60_RELAY_TRACE', None)
    r = subprocess.run(['node', os.path.join(copy, 'apps', 'directory', 'test', 'relay_trace_test.js')], env=env,
                       capture_output=True, timeout=180)
    text = r.stdout.decode('utf-8', 'replace')
    open(os.path.join(copy, f'relay_trace_test_{tag}.txt'), 'w', encoding='utf-8').write(text)
    return r.returncode, text


def check(traced, plain):
    fails = 0

    def c(what, ok, detail=''):
        nonlocal fails
        fails += 0 if ok else 1
        print(f'{"PASS" if ok else "FAIL"}  {what}' + (f'  {detail}' if detail else ''), flush=True)

    for copy, want in ((traced, TRACED_SHA), (plain, BASE_SHA)):
        got = sha(open(os.path.join(copy, 'apps', 'directory', 'server.js'), 'rb').read())
        c(f'{os.path.basename(copy)}: server.js is the pinned one', got == want, got[:16])
    rc, text = run_trace_test(traced, 'traced')
    passes = re.findall(r'(?m)^PASS  .*', text)
    c('POSITIVE: the traced 53fced4 passes 7a79b09\'s relay_trace_test whole (off: no line; on: ENABLED, both ways, '
      'clocks, buckets, no credential; the relay works both ways)', rc == 0 and 'relay_trace_test: ALL PASS' in text,
      f'exit={rc} PASS lines={len(passes)}')
    ex = re.search(r'(?m)^  e\.g\. (\[relay-trace\][^\n]{0,200})', text)
    c('POSITIVE: a real [relay-trace] flow line was written', ex is not None, ex.group(1) if ex else 'none')
    rc2, text2 = run_trace_test(plain, 'plain')
    c('NEGATIVE: 53fced4 as it is, the variable set -- "the trace says it is on" FAILs (the variable alone traces nothing)',
      rc2 != 0 and re.search(r'(?m)^FAIL  the trace says it is on', text2) is not None, f'exit={rc2}')
    c('NEGATIVE control is sound: its relay itself works both times (relay_test passes)',
      len(re.findall(r'(?m)^PASS  the relay works', text2)) == 2, '')
    print(f'\nrelay trace copy check: {"PASS" if fails == 0 else f"{fails} FAILED"}\nRESULT: {"PASS" if fails == 0 else "FAIL"}')
    return 0 if fails == 0 else 1


if __name__ == '__main__':
    a = sys.argv[1:]
    if a[:1] == ['build'] and len(a) in (2, 3):
        sys.exit(build(a[1], a[2:] == ['--plain']))
    if a[:1] == ['check'] and len(a) == 3:
        sys.exit(check(os.path.abspath(a[1]), os.path.abspath(a[2])))
    print(__doc__)
    sys.exit(2)
