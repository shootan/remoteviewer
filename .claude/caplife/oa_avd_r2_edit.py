p = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane\oa_avd.py'
s = open(p, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (s.count(a), a[:80])
    s = s.replace(a, b)


rep("""            # the package on the AVD is signed by another key (the published 0.2.32 vs a debug-signed build): only with
            # OA_REINSTALL=1 is the test package uninstalled first (its data is cleared below anyway); else INVALID""",
    """            # the package on the AVD is signed by another key -- NOT expected between the two arms (r2: the published
            # 0.2.32 and the fixture build are both signed with the same Android Debug key, cert sha256 dcc806ae...);
            # an earlier, other-signed build of the package might be. Only with OA_REINSTALL=1 is the test package
            # uninstalled first (its data is cleared below anyway); else INVALID""")
rep("""            n0 = cursor()
            tap('card:' + HOST)
            targets = ka.wait('listDevicesButton', 40) is not None
            log = since(n0)
            verdict('2 the ordinary connect (directory): the host\\'s targets are shown', targets,
                    ' | '.join(re.findall(r'\\[(?:directory_chosen|targets_ready|connect_failed|directory_connect_failed)\\][^\\n]*', log))[:300])""",
    """            n0 = cursor()
            hs0 = len(host_log())
            tap('card:' + HOST)
            targets = ka.wait('listDevicesButton', 40) is not None
            log = since(n0)
            # r2: the path 0.2.32 actually took, as its own log and the host's say (the fixture's hosts bind 127.0.0.1:
            # from the AVD the relay is expected; recorded, not assumed)
            hnew = host_log()[hs0:]
            path = re.findall(r'[^\\n]*(?:relay|udp control session|session admitted|tcp client|client connected)[^\\n]*', hnew)
            verdict('2 the ordinary connect (directory): the host\\'s targets are shown', targets,
                    ' | '.join(re.findall(r'\\[(?:directory_chosen|targets_ready|connect_failed|directory_connect_failed)\\][^\\n]*', log))[:300])
            step('  2: the host\\'s lines of this connect: ' + ' || '.join(l.strip()[:140] for l in path[:4]))""")
rep("""            skip_case('1 sign in', 'the PUBLISHED 0.2.32 signs in only to its fixed https server (b554e76 build.gradle.kts:6/50)'
                      ' -- not this lane\\'s fixture; see arm S')
            if not video_port or not manual_connect('2 (P)', video_port, control_port):
                return 1""",
    """            skip_case('1 sign in', 'the PUBLISHED 0.2.32 signs in only to its fixed https server (b554e76 build.gradle.kts:6/50)'
                      ' -- not this lane\\'s fixture; see arm S')
            if not video_port or not control_port or not p_first_connect(video_port, control_port):
                return 1""")
rep("""        # 5
        n0 = cursor()
        h0 = host_log()
        menu('viewerMenuHostsButton')
        listed = ka.wait('card:' + HOST, 30) is not None
        time.sleep(2)
        log = since(n0)
        ended = len(re.findall(r'udp control session ended', host_log())) - len(re.findall(r'udp control session ended', h0))
        if ARM == 'S':
            verdict('5 PC 목록: 0.2.32\\'s park (park_begin, park_confirmed) -- the PC listed', listed and '[park_begin]' in log
                    and '[park_confirmed]' in log, ' | '.join(re.findall(r'\\[park_[a-z_]+\\][^\\n]*', log))[:300] +
                    f' | host sessions ended since: {ended}')
        else:
            verdict('5 (P) PC 목록 in manual mode: 0.2.32 refuses the park and disconnects (park_refused manual=true)',
                    re.search(r'\\[park_refused\\][^\\n]*manual=true', log) is not None,
                    ' | '.join(re.findall(r'\\[(?:park_[a-z_]+|scene)\\][^\\n]*', log))[:300])
        shot('5_list')

        # 6
        if ARM == 'S':
            n0 = cursor()
            tap('card:' + HOST)
            m = wait_log(r'\\[park_reuse\\][^\\n]*', n0, 30)
            verdict('6a the parked PC again: 0.2.32 reuses the parked session (park_reuse)', m is not None,
                    m.group(0)[:200] if m else ' | '.join(re.findall(r'\\[(?:park_[a-z_]+|directory_[a-z_]+)\\][^\\n]*', since(n0)))[:240])
            pick_desktop('6b the desktop again after the park')
        else:
            if manual_connect('6a (P) the manual connect again', video_port, control_port):
                pick_desktop('6b (P) the desktop again')
""",
    """        # 5 + 6
        if ARM == 'S':
            n0 = cursor()
            h0 = host_log()
            menu('viewerMenuHostsButton')
            listed = ka.wait('card:' + HOST, 30) is not None
            time.sleep(2)
            log = since(n0)
            ended = len(re.findall(r'udp control session ended', host_log())) - len(re.findall(r'udp control session ended', h0))
            verdict('5 PC 목록: 0.2.32\\'s park (park_begin, park_confirmed) -- the PC listed', listed and '[park_begin]' in log
                    and '[park_confirmed]' in log, ' | '.join(re.findall(r'\\[park_[a-z_]+\\][^\\n]*', log))[:300] +
                    f' | host sessions ended since: {ended}')
            shot('5_list')
            n0 = cursor()
            tap('card:' + HOST)
            m = wait_log(r'\\[park_reuse\\][^\\n]*', n0, 30)
            verdict('6a the parked PC again: 0.2.32 reuses the parked session (park_reuse)', m is not None,
                    m.group(0)[:200] if m else ' | '.join(re.findall(r'\\[(?:park_[a-z_]+|directory_[a-z_]+)\\][^\\n]*', since(n0)))[:240])
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
""")
rep("""    except Exception:
        tb = traceback.format_exc()""", """    except Invalid as e:
        step(f'INVALID  {e} -- nothing of this arm is judged')
        return 2
    except Exception:
        tb = traceback.format_exc()""")
open(p, 'w', encoding='utf-8').write(s)
print('ok')
