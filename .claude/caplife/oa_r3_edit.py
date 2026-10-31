base = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane'


def edit(name, reps):
    p = base + '\\' + name
    raw = open(p, 'rb').read()
    crlf = b'\r\n' in raw
    s = raw.decode('utf-8').replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, s.count(a), a[:80])
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


edit('oa_adb.py', [
("""ADB = os.path.join(os.environ['LOCALAPPDATA'], 'Android', 'Sdk', 'platform-tools', 'adb.exe')
""", """ADB = os.path.join(os.environ['LOCALAPPDATA'], 'Android', 'Sdk', 'platform-tools', 'adb.exe')
# old-APK lane r3 (Codex codex_old_apk_lane_src_r2.md N1): the device-free contrast (oa_pmclear_test.py) runs
# oa_avd.py against a FAKE adb. Only there: a real run is pinned (KE_ADB_SHA256, set by oa_slot.ps1, which also clears
# this variable), and a pin with a fake client refuses below -- so a fake can never stand in for the SDK client.
if os.environ.get('OA_TEST_FAKE_ADB'):
    if os.environ.get('KE_ADB_SHA256'):
        raise RuntimeError('OA_TEST_FAKE_ADB is for the device-free test only -- never with an adb pin')
    ADB = os.environ['OA_TEST_FAKE_ADB']
"""),
])

edit('oa_avd.py', [
("""        def install():
            r = subprocess.run([ka.ADB, '-s', ka.SERIAL, 'install', '-r', '-d', APK], capture_output=True, timeout=180)
            return (r.stdout + r.stderr).decode(errors='replace').strip()
        res = install()
        step('install: ' + res[-300:])""", """        def install():
            r = subprocess.run([ka.ADB, '-s', ka.SERIAL, 'install', '-r', '-d', APK], capture_output=True, timeout=180)
            return r.returncode, (r.stdout + r.stderr).decode(errors='replace').strip()
        irc, res = install()
        step(f'install: rc={irc} ' + res[-300:])"""),
("""            step('  uninstall (OA_REINSTALL=1): ' + adb('uninstall', PKG).strip()[:120])
            res = install()
            step('install (again): ' + res[-300:])
        rc, out, err = ka.run('shell', 'pm', 'clear', PKG)
        step(f'  pm clear: rc={rc} stdout={out.strip()[:80]!r} stderr={err.strip()[:120]!r}')
        time.sleep(2)""", """            step('  uninstall (OA_REINSTALL=1): ' + adb('uninstall', PKG).strip()[:120])
            irc, res = install()
            step(f'install (again): rc={irc} ' + res[-300:])
        # r3 (Codex codex_old_apk_lane_src_r2.md N1): the app is started ONLY after its install AND its data clear both
        # said so -- rc 0 and an explicit "Success" each. A failed / timed-out / silent clear could leave a sign-in or an
        # owed sign-out in place, and arm P's onCreate would act on it before any check after the start: so then the
        # arm is INVALID with no app start and no tap at all (the post-start NO_CREDENTIAL check stays, as a second
        # line). A failed clear is never retried into a success. Scope: this ONE package on the test AVD.
        if irc != 0 or not re.search(r'(?m)^Success\\s*$', res):
            raise Invalid(f'install did not say Success (rc={irc}): {res[-160:]!r} -- no app start')
        ok, why = clear_app_data()
        step(f'  pm clear {PKG}: {why}')
        if not ok:
            raise Invalid(f'the app data clear did not succeed ({why}) -- no app start, no tap')
        time.sleep(2)"""),
("""class Invalid(Exception):
    \"\"\"A precondition of the arm did not hold: nothing is judged (exit 2).\"\"\"
""", """class Invalid(Exception):
    \"\"\"A precondition of the arm did not hold: nothing is judged (exit 2).\"\"\"


def clear_app_data():
    \"\"\"r3 N1: `pm clear` of the test package, judged: (True, detail) only for exit 0 AND stdout exactly "Success". A
    non-zero exit, any other / empty answer, a timeout (OA_PM_CLEAR_TIMEOUT_SEC, default 60) or an error is False.\"\"\"
    timeout = float(os.environ.get('OA_PM_CLEAR_TIMEOUT_SEC', '60'))
    try:
        r = subprocess.run([ka.ADB, '-s', ka.SERIAL, 'shell', 'pm', 'clear', PKG], capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, f'timeout after {timeout:g} s'
    except Exception as e:  # noqa: BLE001 -- any failure to run it is a failure of the clear
        return False, f'could not run: {type(e).__name__}: {e}'
    out = r.stdout.decode('utf-8', 'replace').strip()
    err = r.stderr.decode('utf-8', 'replace').strip()
    ok = r.returncode == 0 and out == 'Success'
    return ok, f'rc={r.returncode} stdout={out[:80]!r} stderr={err[:120]!r} -> {"Success" if ok else "NOT a success"}'
"""),
])
print('ok')
