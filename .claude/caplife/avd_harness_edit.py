E = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/'


def load(name):
    s = open(E + name, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    return (s.replace('\r\n', '\n') if crlf else s), crlf


def save(name, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(E + name, 'w', encoding='utf-8', newline='').write(s)
    print('ok', name)


def rep(s, a, b):
    assert s.count(a) == 1, (a[:90], s.count(a))
    return s.replace(a, b)


def guard_block(s, start_marker, end_marker, skip_lines):
    """Wraps the lines from start_marker (inclusive) to end_marker (exclusive) under `if OLD_HOST:` (8 spaces)."""
    i = s.index(start_marker)
    j = s.index(end_marker, i)
    block = s[i:j]
    body = ''.join(('    ' + ln if ln.strip() else ln) for ln in block.splitlines(True))
    return s[:i] + skip_lines + '        if OLD_HOST:\n' + body + s[j:]


s, crlf = load('ke_avd.py')
s = rep(s, "H = {k: f'ke-pc-{k}' for k in ('a', 'b', 'c', 'old')}",
        "# 2026-10-28 (test-injection-finding): the older host (a build from before standby -- and before the power policy)\n"
        "# cannot pass the shared injection / power guard: the old-host cases are NOT RUN, and the fixture starts no old host.\n"
        "OLD_HOST = False\n"
        "H = {k: f'ke-pc-{k}' for k in (('a', 'b', 'c', 'old') if OLD_HOST else ('a', 'b', 'c'))}")
s = rep(s, "results = []\n", "results = []\nnot_run = []  # (case, why): recorded apart -- never counted as a PASS\n")
s = rep(s, "def verdict(name, ok, detail=''):",
        "def skip_case(name, why):\n    not_run.append((name, why))\n    step(f'NOT RUN  {name}: {why}')\n\n\ndef verdict(name, ok, detail=''):")
s = guard_block(s, "        # E7f -- 끊기 DURING a first transition", "        # E9 -- a card's 끊기\n",
                "        if not OLD_HOST:\n            skip_case('E7f', 'needs the older host (old-host cases NOT RUN since 2026-10-28)')\n"
                "            skip_case('E8', 'the older host itself (old-host cases NOT RUN since 2026-10-28)')\n")
s = guard_block(s, "        # E12 -- the ON build: the recent PCs on their own\n",
                "        open(os.path.join(OUT, 'app_final.log'), 'w', encoding='utf-8').write(applog())\n",
                "        if not OLD_HOST:\n            skip_case('E12', 'its recent three include the older host (old-host cases NOT RUN since '\n"
                "                             '2026-10-28); to be redesigned without it')\n")
s = rep(s, "        json.dump({'results': results, 'marks': marks}, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)\n"
           "        step(f'run end: {sum(1 for _, ok, _ in results if ok)}/{len(results)} PASS')",
        "        json.dump({'results': results, 'not_run': not_run, 'marks': marks}, open(os.path.join(OUT, 'result.json'), 'w'),\n"
        "                  indent=1)\n"
        "        step(f'run end: {sum(1 for _, ok, _ in results if ok)}/{len(results)} PASS, {len(not_run)} NOT RUN')")
save('ke_avd.py', s, crlf)

f, crlf = load('ke_fixture.ps1')
f = rep(f, "Copy-Item 'C:\\work\\gnlink-wt\\fix-capture-lifecycle-baseline\\Release\\GNLinkStreamPinnedSelectTest.exe' \"$b\\GNLinkStreamPinnedOld.exe\" -Force\n",
        "# 2026-10-28 (test-injection-finding): no older host -- its build predates the power policy and cannot pass the shared\n"
        "# guard (input none + power off, both whole OFF lines before any client). The old-host cases are NOT RUN.\n"
        "\"  old host: NOT RUN (no build of it passes the injection / power guard)\" | Out-File $sum -Append -Encoding utf8\n")
f = rep(f, " old host $((Get-FileHash \"$b\\GNLinkStreamPinnedOld.exe\").Hash.ToLower())\"", "\"")
f = rep(f, ",\n  @{ n = 'old'; name = 'ke-pc-old'; mark = 'ff8000'; exe = 'GNLinkStreamPinnedOld.exe' })", ")")
save('ke_fixture.ps1', f, crlf)
