"""rel-0164 old-APK lane (Codex codex_rel_0164_split_decision.md (2), legacy combination 1): APK 0.2.32 on the shared test
AVD against the 0.2.164 candidate's server and host (oa_fixture.ps1: one pinned test host oa-pc-a through the shared
injection / power guard). Judged ONLY from what 0.2.32 (b554e76) itself has: its UI ids (loginIdInput, hostCardName,
listDevicesButton, targetCardTitle, viewerMenuButton / viewerMenuHostsButton, loginManualButton / connectButton) and its
diagnostics tags (login_attempt, login_manual, directory_chosen, targets_ready, select_ready, park_begin,
park_confirmed, park_reuse, park_refused, scene, video_debug) -- none of the 0.2.33 tags. The host's side from its own
log: the test host's `[input-trace]` lines (input injection none: the events are logged, never injected).

  arm S  the same-source fixture build of 0.2.32 (DIRECTORY_URL = the fixture's): 1 sign in, 2 the ordinary connect
         (directory), 3 the desktop picked -> first frame (time, screenshot sha256), 4 one input reaches the host, 5 PC
         목록: the park, 6 the parked PC again -> a picture again, 7 (with a standby lease) the lease undisturbed.
  arm P  the PUBLISHED 0.2.32 (its DIRECTORY_URL is the fixed https server, which this lane does not use). r2 (Codex
         codex_rel_0164_old_apk_lane_decision.md): its ONE app start must show an empty sign-in store (auto_login
         NO_CREDENTIAL, no sign_in_store line) or the arm is INVALID; 1 NOT RUN; 2 its manual DIRECT connect (10.0.2.2:
         UDP video + TCP control -- the host's own log must show one TCP control client); 3, 4 as S; 5 and 6 NOT RUN
         (the manual UI has no PC 목록 / park); instead 5d 연결 해제 -> the connect form (the host's TCP client gone)
         and 6r connect again (a new session) -> the desktop -> a picture; 7 as S.
No clipboard is changed. No real password is shown: adb errors and steps are cleaned (oa_adb._clean).

  python -I oa_avd.py <run folder> <S|P> <apk> <out>"""
import hashlib
import json
import os
import re
import subprocess
import sys
import threading
import time
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ.setdefault('KA_SERIAL', 'emulator-5554')
import oa_adb as ka  # noqa: E402

ROOT, ARM, APK, OUT = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
os.makedirs(OUT, exist_ok=True)
ACCOUNT, PW = open(os.path.join(ROOT, 'fixture_account.txt'), encoding='ascii').read().split()
HOST = 'oa-pc-a'
PKG = ka.PKG
STEPS = open(os.path.join(OUT, 'steps.txt'), 'a', encoding='utf-8')
results, not_run, marks = [], [], {}


def step(msg):
    line = f'{time.strftime("%H:%M:%S")} {msg}'
    print(line, flush=True)
    STEPS.write(line + '\n')
    STEPS.flush()


def verdict(name, ok, detail=''):
    results.append((name, bool(ok), detail))
    step(('PASS  ' if ok else 'FAIL  ') + name + ('  ' + detail if detail else ''))


def skip_case(name, why):
    not_run.append((name, why))
    step(f'NOT RUN  {name}: {why}')


def adb(*a, timeout=60):
    return subprocess.run([ka.ADB, '-s', ka.SERIAL, *a], capture_output=True, timeout=timeout).stdout.decode('utf-8', 'replace')


def tap(key):
    step(ka.tap(key))


def type_into(key, text):
    tap(key)
    time.sleep(0.4)
    ka.adb('shell', 'input', 'keycombination', '113', '29', check=False)
    ka.adb('shell', 'input', 'keyevent', '67', check=False)
    ka.adb('shell', 'input', 'text', text.replace(' ', '%s'))


def applog():
    p = os.path.join(OUT, '_cur.log')
    try:
        ka.adb('pull', ka.LOG_PATH, p)
        return open(p, encoding='utf-8', errors='replace').read()
    except Exception:
        return ''


def cursor():
    lines = [l for l in applog().splitlines() if l.strip()]
    return lines[-1] if lines else ''


def since(cur):
    log = applog()
    i = log.rfind(cur) if cur else -1
    return log[i + len(cur):] if i >= 0 else log


def wait_log(pattern, n0, sec):
    end = time.time() + sec
    while time.time() < end:
        m = re.search(pattern, since(n0))
        if m:
            return m
        time.sleep(0.5)
    return None


def shot(name):
    png = ka.adb('exec-out', 'screencap', '-p', binary=True)
    p = os.path.join(OUT, name + '.png')
    open(p, 'wb').write(png)
    digest = hashlib.sha256(png).hexdigest()
    step(f'  screenshot {name}.png sha256={digest[:16]} bytes={len(png)} at {time.strftime("%H:%M:%S")}')
    return digest


def host_log():
    ready = open(os.path.join(ROOT, 'a', 'directoryhost_ready.txt'), encoding='utf-8').read().strip()
    m = re.match(r'^ready run=(\S+) harness_pid=\d+ host_pid=\d+ host_log=(\S+)$', ready)
    if not m or m.group(1) != os.path.basename(os.path.normpath(ROOT)) + '-a':
        raise RuntimeError(f"the host's readiness is not this run's: {ready}")
    try:
        return open(m.group(2), encoding='utf-8', errors='replace').read()
    except OSError:
        return ''


def menu(button):
    """The viewer menu's [button] (0.2.32 ids): controls woken, the menu opened, the button tapped -- up to three times."""
    for attempt in range(3):
        if ka.wait(button, 2):
            tap(button)
            return
        for _ in range(3):
            if ka.wait('viewerMenuButton', 2):
                break
            ka.adb('shell', 'input', 'tap', '1200', '540')
            time.sleep(1.0)
        if ka.wait('viewerMenuButton', 2):
            tap('viewerMenuButton')
            if ka.wait(button, 8):
                tap(button)
                return
        step(f'  menu: {button} not reached (attempt {attempt + 1}), again')
        if ka.wait('viewerMenuCloseButton', 1):
            tap('viewerMenuCloseButton')
        time.sleep(1.0)
    raise RuntimeError(f'menu button not reached: {button}')


def pick_desktop(tag):
    """The targets screen: the desktop tab, the first card -> select_ready (0.2.32's own 'the first picture shown')."""
    n0 = cursor()
    if not ka.wait('listDevicesButton', 30):
        verdict(f'{tag}: the targets screen is shown', False)
        return None
    tap('listDevicesButton')
    if not ka.wait('targetCardTitle', 30):
        verdict(f'{tag}: a desktop card is listed', False)
        return None
    tap('targetCardTitle')
    t0 = time.time()
    m = wait_log(r'\[select_ready\][^\n]* ms=(-?\d+)', n0, 30)
    if not m:
        verdict(f'{tag}: the desktop picked -> first frame within 30 s', False,
                ' | '.join(re.findall(r'\[(?:select_[a-z_]+)\][^\n]*', since(n0)))[:300])
        return None
    time.sleep(1.5)
    digest = shot(tag.split()[0] + '_first_frame')
    vd = re.findall(r'\[video_debug\][^\n]* out=(\d+)', since(n0))
    verdict(f'{tag}: the desktop picked -> first frame', True,
            f'{m.group(1)} ms (wall {time.time() - t0:.1f} s) screenshot={digest[:16]} video_debug out={vd[-1] if vd else "?"}')
    return digest


class Invalid(Exception):
    """A precondition of the arm did not hold: nothing is judged (exit 2)."""


def tcp_clients(log):
    return len(re.findall(r'\[native-video-host\]\[control\] client connected', log))


def p_first_connect(video_port, control_port):
    """Arm P, the app's ONE start (rev r2, Codex codex_rel_0164_old_apk_lane_decision.md "P의 운영 로그인 방지"):
    onCreate starts beginAutoLogin before the manual button can be tapped, and the button does not cancel it. The
    package was installed and `pm clear`ed before this first start, so its sign-in store (KeystoreLoginVault, the
    app's own preferences + keystore) is empty: LoginFlow.comeBack answers NO_CREDENTIAL from vault.load() and
    settleOwedSignOuts has nothing owed -- neither sends anything (b554e76 LoginFlow.kt:225-226, :603-618). That is
    CONFIRMED from the app's own log before anything is tapped: `auto_login NO_CREDENTIAL` and no `sign_in_store`
    line; anything else is INVALID (the app is stopped at once). Then 수동 연결 -> 연결: the launch extras' host /
    videoPort / controlPort, TCP control + UDP video (b554e76's manual args: requireTcpControl=true,
    controlOverUdp=false) -- judged from the host's own log, not from the app's."""
    h0 = tcp_clients(host_log())
    ka.adb('shell', 'am', 'start', '-n', f'{PKG}/.MainActivity', '--es', 'host', '10.0.2.2',
           '--ei', 'videoPort', str(video_port), '--ei', 'controlPort', str(control_port))
    m = wait_log(r'\[auto_login\][^\n]*', '', 30)
    log = applog()
    store = re.findall(r'\[sign_in_store\][^\n]*', log)
    upd = re.findall(r'\[update_check\][^\n]*', log)
    step(f'  P precondition: {m.group(0) if m else "no auto_login line in 30 s"}; sign_in_store lines={len(store)}')
    step('  P start-up update check (the published build derives it from its fixed https directory; no session, so '
         'no credential): ' + (' | '.join(upd)[:240] if upd else 'no update_check line yet'))
    if not m or 'NO_CREDENTIAL' not in m.group(0) or store:
        ka.adb('shell', 'am', 'force-stop', PKG, check=False)
        raise Invalid('arm P precondition: the app did not start with an empty sign-in store '
                      f'({m.group(0) if m else "no auto_login"}; sign_in_store={store[:2]})')
    n0 = cursor()
    if not ka.wait('loginManualButton', 30):
        verdict('2 (P) the manual connect is offered', False)
        return False
    tap('loginManualButton')
    if not ka.wait('connectButton', 20):
        verdict('2 (P) the manual connect form is shown', False)
        return False
    tap('connectButton')
    ok = ka.wait('listDevicesButton', 40) is not None
    hl = host_log()
    tcp = tcp_clients(hl) - h0
    verdict(f'2 (P) the manual DIRECT connect (10.0.2.2: UDP video {video_port}, TCP control {control_port}) reaches '
            'the targets, and the host took ONE TCP control client', ok and tcp == 1,
            f'host tcp clients +{tcp} listen={"yes" if f"control waiting port={control_port}" in hl else "NO"} | ' +
            ' | '.join(re.findall(r'\[(?:login_manual|connect_failed|targets_ready|scene)\][^\n]*', since(n0)))[:260])
    return ok and tcp == 1


def p_disconnect():
    """Arm P, P-5d (NOT ⑤): the manual UI has no PC 목록 (SessionPark.canPark is false in manual mode, b554e76
    SessionPark.kt:78: viewerMenuHostsButton GONE) -- its way back is 연결 해제 (viewerMenuDisconnectButton, then the
    dialog's yes) to its home scene CONNECT. Judged: the app's viewer_disconnect, the connect form shown again, the
    host's TCP control client gone."""
    n0 = cursor()
    h0 = len(re.findall(r'\[control\] tcp client disconnected', host_log()))
    shown = False
    if ka.wait('viewerMenuButton', 2) is None:
        ka.adb('shell', 'input', 'tap', '1200', '540')
        time.sleep(1.0)
    hosts_offered = False
    if ka.wait('viewerMenuButton', 5):
        tap('viewerMenuButton')
        hosts_offered = ka.wait('viewerMenuHostsButton', 3) is not None
        if ka.wait('viewerMenuDisconnectButton', 8):
            tap('viewerMenuDisconnectButton')
            if ka.wait('android:id/button1', 8):
                tap('android:id/button1')
                shown = ka.wait('connectButton', 20) is not None
    time.sleep(1.5)
    gone = len(re.findall(r'\[control\] tcp client disconnected', host_log())) - h0
    log = since(n0)
    verdict('5d (P) 연결 해제 from the viewer menu -> the connect form again; the host saw its TCP control client go '
            '(the manual UI offers no PC 목록)', shown and gone >= 1 and '[viewer_disconnect]' in log and not hosts_offered,
            f'pc-list offered={hosts_offered} host tcp disconnects +{gone} | ' +
            ' | '.join(re.findall(r'\[(?:viewer_disconnect|park_[a-z_]+|scene)\][^\n]*', log))[:220])
    return shown


def p_reconnect(control_port):
    """Arm P, P-6r (NOT ⑥: nothing was parked): 연결 again from the connect form (no new app start) -> the targets."""
    h0 = tcp_clients(host_log())
    n0 = cursor()
    if not ka.wait('connectButton', 10):
        verdict('6r (P) the connect form is there to connect again', False)
        return False
    tap('connectButton')
    ok = ka.wait('listDevicesButton', 40) is not None
    tcp = tcp_clients(host_log()) - h0
    verdict(f'6r (P) connect again (a NEW manual session, TCP control {control_port}) reaches the targets', ok and tcp == 1,
            f'host tcp clients +{tcp} | ' +
            ' | '.join(re.findall(r'\[(?:connect_failed|targets_ready|scene)\][^\n]*', since(n0)))[:240])
    return ok and tcp == 1


def main():
    step(f'oa run arm={ARM} apk={APK}')
    logcat = None
    stopping = threading.Event()
    try:
        apk_digest = hashlib.sha256(open(APK, 'rb').read()).hexdigest()
        step(f'  apk sha256={apk_digest}')
        digest, version = ka.adb_identity()
        step(f'  adb client {ka.ADB} sha256={digest[:16]} {version} serial={ka.SERIAL}')
        fixture_ready = open(os.path.join(ROOT, 'fixture_ready.txt'), encoding='ascii').read().strip()
        step(f'  fixture: {fixture_ready}')
        standby = ' standby=ready' in fixture_ready
        control_port = int(re.search(r'controlPort=(\d+)', fixture_ready).group(1))
        for k in ('window_animation_scale', 'transition_animation_scale', 'animator_duration_scale'):
            adb('shell', 'settings', 'put', 'global', k, '0')
        adb('shell', 'input', 'keyevent', 'KEYCODE_WAKEUP')
        adb('shell', 'wm', 'dismiss-keyguard')
        adb('logcat', '-c')
        logcat = subprocess.Popen([ka.ADB, '-s', ka.SERIAL, 'logcat', '-b', 'all', '-v', 'threadtime'],
                                  stdout=open(os.path.join(OUT, 'logcat.txt'), 'wb'), stderr=subprocess.DEVNULL)

        def _watch(proc):
            rc = proc.wait()
            if not stopping.is_set():
                step(f'  LOGCAT ENDED rc={rc} -- no logcat from here (a gap, not an absence of events)')
        threading.Thread(target=_watch, args=(logcat,), daemon=True).start()
        def install():
            r = subprocess.run([ka.ADB, '-s', ka.SERIAL, 'install', '-r', '-d', APK], capture_output=True, timeout=180)
            return (r.stdout + r.stderr).decode(errors='replace').strip()
        res = install()
        step('install: ' + res[-300:])
        if 'INSTALL_FAILED_UPDATE_INCOMPATIBLE' in res:
            # the package on the AVD is signed by another key -- NOT expected between the two arms (r2: the published
            # 0.2.32 and the fixture build are both signed with the same Android Debug key, cert sha256 dcc806ae...);
            # an earlier, other-signed build of the package might be. Only with OA_REINSTALL=1 is the test package
            # uninstalled first (its data is cleared below anyway); else INVALID
            if os.environ.get('OA_REINSTALL') != '1':
                step('SETUP: another signer\'s build of the package is installed -- rerun with OA_REINSTALL=1 to uninstall it first')
                return 2
            step('  uninstall (OA_REINSTALL=1): ' + adb('uninstall', PKG).strip()[:120])
            res = install()
            step('install (again): ' + res[-300:])
        rc, out, err = ka.run('shell', 'pm', 'clear', PKG)
        step(f'  pm clear: rc={rc} stdout={out.strip()[:80]!r} stderr={err.strip()[:120]!r}')
        time.sleep(2)
        pkg = adb('shell', 'dumpsys', 'package', PKG)
        version_name = (re.findall(r'versionName=(\S+)', pkg) or ['?'])[0]
        step('  installed: ' + ' '.join(re.findall(r'versionName=\S+|versionCode=\d+', pkg)[:2]))
        # the installed bytes ARE this APK (pulled back and hashed) and it says 0.2.32 -- else nothing is judged
        base = re.search(r'package:(\S+base\.apk)', adb('shell', 'pm', 'path', PKG))
        pulled = os.path.join(OUT, '_installed_base.apk')
        if base:
            ka.adb('pull', base.group(1), pulled)
        got = hashlib.sha256(open(pulled, 'rb').read()).hexdigest() if base and os.path.exists(pulled) else 'none'
        step(f'  installed base.apk sha256={got[:16]} (the APK {apk_digest[:16]})')
        if got != apk_digest or version_name != '0.2.32':
            step(f'SETUP: the installed app is not this APK 0.2.32 (versionName={version_name}) -- nothing judged')
            return 2
        hlog0 = host_log()
        bind = re.findall(r'waiting client bindPort=(\d+)', hlog0)
        video_port = int(bind[-1]) if bind else 0
        step(f'  host oa-pc-a: udp bindPort={video_port} tcp controlPort={control_port}')

        # 1 + 2
        if ARM == 'S':
            ka.adb('shell', 'am', 'start', '-n', f'{PKG}/.MainActivity')
            time.sleep(3)
            n0 = cursor()
            if not ka.wait('loginIdInput', 30):
                verdict('1 sign in: the login screen is shown', False)
                return 1
            type_into('loginIdInput', ACCOUNT)
            type_into('loginPasswordInput', PW)
            tap('loginButton')
            listed = ka.wait('card:' + HOST, 40) is not None
            verdict('1 sign in (the fixture directory of the 0.2.164 candidate): the PC is listed', listed,
                    ' | '.join(re.findall(r'\[(?:login_attempt|login_failed|hosts_failed)\][^\n]*', since(n0)))[:200])
            if not listed:
                return 1
            n0 = cursor()
            hs0 = len(host_log())
            tap('card:' + HOST)
            targets = ka.wait('listDevicesButton', 40) is not None
            log = since(n0)
            # r2: the path 0.2.32 actually took, as its own log and the host's say (the fixture's hosts bind 127.0.0.1:
            # from the AVD the relay is expected; recorded, not assumed)
            hnew = host_log()[hs0:]
            path = re.findall(r'[^\n]*(?:relay|udp control session|session admitted|tcp client|client connected)[^\n]*', hnew)
            verdict('2 the ordinary connect (directory): the host\'s targets are shown', targets,
                    ' | '.join(re.findall(r'\[(?:directory_chosen|targets_ready|connect_failed|directory_connect_failed)\][^\n]*', log))[:300])
            step('  2: the host\'s lines of this connect: ' + ' || '.join(l.strip()[:140] for l in path[:4]))
            if not targets:
                return 1
        else:
            skip_case('1 sign in', 'the PUBLISHED 0.2.32 signs in only to its fixed https server (b554e76 build.gradle.kts:6/50)'
                      ' -- not this lane\'s fixture; see arm S')
            if not video_port or not control_port or not p_first_connect(video_port, control_port):
                return 1

        # 3
        if not pick_desktop('3 the desktop picked'):
            return 1

        # 4: one tap on the picture -> the host's log says it arrived (injection none: logged, not injected)
        n_in = len(re.findall(r'\[input-trace\][^\n]*', host_log()))
        ka.adb('shell', 'input', 'tap', '1200', '540')
        step('  one tap on the picture at 1200,540')
        time.sleep(3)
        traces = re.findall(r'\[input-trace\][^\n]*', host_log())[n_in:]
        verdict('4 one input reaches the host (its [input-trace]; not injected: input none)', len(traces) > 0,
                f'new={len(traces)} ' + ' | '.join(traces[:3])[:240])

        # 5 + 6
        if ARM == 'S':
            n0 = cursor()
            h0 = host_log()
            menu('viewerMenuHostsButton')
            listed = ka.wait('card:' + HOST, 30) is not None
            time.sleep(2)
            log = since(n0)
            ended = len(re.findall(r'udp control session ended', host_log())) - len(re.findall(r'udp control session ended', h0))
            verdict('5 PC 목록: 0.2.32\'s park (park_begin, park_confirmed) -- the PC listed', listed and '[park_begin]' in log
                    and '[park_confirmed]' in log, ' | '.join(re.findall(r'\[park_[a-z_]+\][^\n]*', log))[:300] +
                    f' | host sessions ended since: {ended}')
            shot('5_list')
            n0 = cursor()
            tap('card:' + HOST)
            m = wait_log(r'\[park_reuse\][^\n]*', n0, 30)
            verdict('6a the parked PC again: 0.2.32 reuses the parked session (park_reuse)', m is not None,
                    m.group(0)[:200] if m else ' | '.join(re.findall(r'\[(?:park_[a-z_]+|directory_[a-z_]+)\][^\n]*', since(n0)))[:240])
            pick_desktop('6b the desktop again after the park')
        else:
            # r2 (Codex: map 1-7 to what the arm's UI really has; no hidden handler, no fake capability)
            skip_case('5 PC 목록 (park)', 'the manual UI offers no PC 목록: SessionPark.canPark is false in manual mode '
                      '(b554e76 SessionPark.kt:78), viewerMenuHostsButton GONE -- P-5d below is 연결 해제, not a park')
            skip_case('6 the parked PC again (resume)', 'nothing is parked in manual mode -- P-6r below is a NEW manual '
                      'session, not a resume')
            if p_disconnect():
                shot('5d_connect_form')
                if p_reconnect(control_port):
                    pick_desktop('6r (P) the desktop again')

        # 7
        if standby:
            state = open(os.path.join(ROOT, 'standby', 'standby_state.txt'), encoding='utf-8', errors='replace').read()
            states = re.findall(r' State state=(\S+)(?: lost=(\S+))?', state)
            after_ready = []
            seen_ready = False
            for st, lost in states:
                if seen_ready:
                    after_ready.append((st, lost))
                seen_ready = seen_ready or st == 'ready'
            kept = seen_ready and all(st == 'ready' for st, _ in after_ready) and not re.search(r'EXITED', state)
            hl = host_log()
            sb_lines = re.findall(r'[^\n]*standby[^\n]*', hl)
            verdict('7 the PC-side standby lease on the same host stayed ready through 1-6 (no state change after ready, '
                    'the holder alive)', kept, f'states after ready={after_ready[:6]} host standby lines={len(sb_lines)}')
            step('  standby, the host\'s last lines: ' + ' || '.join(l.strip()[:160] for l in sb_lines[-4:]))
        else:
            skip_case('7 standby undisturbed', 'run without -WithStandby')
        open(os.path.join(OUT, 'app_final.log'), 'w', encoding='utf-8').write(applog())
    except Invalid as e:
        step(f'INVALID  {e} -- nothing of this arm is judged')
        return 2
    except Exception:
        tb = traceback.format_exc()
        open(os.path.join(OUT, 'avd_traceback.txt'), 'w', encoding='utf-8').write(tb)
        step('EXCEPTION ' + tb.strip().splitlines()[-1][:300] + ' (in full: avd_traceback.txt)')
        try:
            shot('exception')
        except Exception as e:
            step(f'  no screenshot at the exception ({e})')
        raise
    finally:
        if logcat:
            stopping.set()
            logcat.terminate()
        ka.adb('shell', 'am', 'force-stop', PKG, check=False)
        # a case never reached (an earlier step failed, a setup refusal, an exception) is NOT RUN, never silently absent
        seen = {re.match(r'\d', n).group(0) for n, *_ in results + not_run if re.match(r'\d', n)}
        for c in '1234567':
            if c not in seen:
                not_run.append((c, 'not reached (an earlier step failed or the run stopped)'))
                step(f'NOT RUN  {c}: not reached (an earlier step failed or the run stopped)')
        json.dump({'arm': ARM, 'results': results, 'not_run': not_run}, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
        step(f'run end: {sum(1 for _, ok, _ in results if ok)}/{len(results)} PASS, {len(not_run)} NOT RUN')
    return 0 if results and all(ok for _, ok, _ in results) else 1


if __name__ == '__main__':
    sys.exit(main())
