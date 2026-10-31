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


edit('ke_avd.py', [
    ('import time\n', 'import time\nimport traceback\n'),
    ('''        open(os.path.join(OUT, 'app_final.log'), 'w', encoding='utf-8').write(applog())
    finally:''', '''        open(os.path.join(OUT, 'app_final.log'), 'w', encoding='utf-8').write(applog())
    except Exception:
        # AVD r5 (ke-r22): the exception itself, whole, in the run folder -- and what the screen showed -- before it
        # propagates (the bundle keeps stderr too, avd_stderr.txt). Never a password: adb errors are redacted.
        tb = traceback.format_exc()
        open(os.path.join(OUT, 'avd_traceback.txt'), 'w', encoding='utf-8').write(tb)
        step('EXCEPTION ' + tb.strip().splitlines()[-1][:300] + ' (in full: avd_traceback.txt)')
        try:
            shot('exception')
            step('  screenshot at the exception: exception.png')
        except Exception as e:
            step(f'  no screenshot at the exception ({e})')
        raise
    finally:'''),
    ('''    if clear:
        ka_avd.adb('shell', 'pm', 'clear', PKG, check=False)''', '''    if clear:
        cleared = ka_avd.adb('shell', 'pm', 'clear', PKG, check=False).strip()
        step(f'  pm clear: {cleared or "(no answer)"}')  # AVD r5: said, not assumed'''),
])

edit('ka_avd.py', [
    ('''def adb(*args, check=True, binary=False):
    r = subprocess.run([ADB, '-s', SERIAL, *args], capture_output=True)
    if check and r.returncode != 0:
        raise RuntimeError(f'adb {args}: {r.stderr.decode(errors="replace")}')''', '''def _shown(args):
    """The arguments as an error may show them: what follows `input text` is redacted (it can be a password)."""
    out = list(args)
    for i in range(len(out) - 1):
        if out[i] == 'input' and out[i + 1] == 'text' and i + 2 < len(out):
            out[i + 2] = '<redacted>'
    return tuple(out)


def adb(*args, check=True, binary=False):
    r = subprocess.run([ADB, '-s', SERIAL, *args], capture_output=True)
    if check and r.returncode != 0:
        raise RuntimeError(f'adb {_shown(args)}: {r.stderr.decode(errors="replace")}')'''),
    ('''def dump():
    for attempt in range(12):''', '''def dump():
    last = ''
    for attempt in range(12):'''),
    ('''        out = adb('exec-out', 'uiautomator', 'dump', '/dev/tty', check=False)
        i = out.find('<?xml')''', '''        out = adb('exec-out', 'uiautomator', 'dump', '/dev/tty', check=False)
        last = out.strip()[-160:]
        i = out.find('<?xml')'''),
    ('''    raise RuntimeError('uiautomator dump failed')''', '''    raise RuntimeError(f'uiautomator dump failed (12 tries; the last answer: {last!r})')  # AVD r5: say what it said'''),
])
