E = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/'


def edit(name, reps):
    s = open(E + name, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, a[:70], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(E + name, 'w', encoding='utf-8', newline='').write(s)
    print('ok', name)


edit('ka_avd.py', [
    ('''def _shown(args):
    """The arguments as an error may show them: what follows `input text` is redacted (it can be a password)."""
    out = list(args)
    for i in range(len(out) - 1):
        if out[i] == 'input' and out[i + 1] == 'text' and i + 2 < len(out):
            out[i + 2] = '<redacted>'
    return tuple(out)


def adb(*args, check=True, binary=False):
    r = subprocess.run([ADB, '-s', SERIAL, *args], capture_output=True)
    if check and r.returncode != 0:
        raise RuntimeError(f'adb {_shown(args)}: {r.stderr.decode(errors="replace")}')
    return r.stdout if binary else r.stdout.decode('utf-8', errors='replace')''',
     '''def _secrets(args):
    """The values that must never be shown: what follows `input text` (it can be a password)."""
    return [args[i + 2] for i in range(len(args) - 2) if args[i] == 'input' and args[i + 1] == 'text' and args[i + 2]]


def _shown(args):
    """The arguments as an error may show them: what follows `input text` is redacted."""
    out = list(args)
    for i in range(len(out) - 1):
        if out[i] == 'input' and out[i + 1] == 'text' and i + 2 < len(out):
            out[i + 2] = '<redacted>'
    return tuple(out)


def _clean(text, args):
    """AVD r6 (Codex codex_avd_r5_scripts_src.md N1): adb's own words with every secret of this call removed -- adb (or
    the shell behind it) may repeat the input in its error; argv being redacted is not enough."""
    for secret in _secrets(args):
        text = text.replace(secret, '<redacted>')
    return text


def run(*args):
    """One adb call: (exit code, stdout, stderr), both texts cleaned of this call's secrets. The call itself is not
    changed (its argv carries the real value)."""
    r = subprocess.run([ADB, '-s', SERIAL, *args], capture_output=True)
    return (r.returncode, _clean(r.stdout.decode('utf-8', errors='replace'), args),
            _clean(r.stderr.decode('utf-8', errors='replace'), args))


def adb(*args, check=True, binary=False):
    r = subprocess.run([ADB, '-s', SERIAL, *args], capture_output=True)
    if check and r.returncode != 0:
        raise RuntimeError(f'adb {_shown(args)} (exit {r.returncode}): '
                           f'{_clean(r.stderr.decode(errors="replace"), args)}')
    return r.stdout if binary else r.stdout.decode('utf-8', errors='replace')'''),
])

edit('ke_avd.py', [
    ('''        cleared = ka_avd.adb('shell', 'pm', 'clear', PKG, check=False).strip()
        step(f'  pm clear: {cleared or "(no answer)"}')  # AVD r5: said, not assumed''',
     '''        rc, out, err = ka_avd.run('shell', 'pm', 'clear', PKG)  # AVD r6: one call, its exit code and both texts
        step(f'  pm clear: rc={rc} stdout={out.strip()[:80]!r} stderr={err.strip()[:120]!r}')'''),
])
