p = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/ke_avd.py'
s = open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
if crlf:
    s = s.replace('\r\n', '\n')
anchor = "        # E11 -- sign-out with a lease\n"
assert s.count(anchor) == 1
e13 = '''        # E13 (keepalive-r2 e, Codex ka_e r3 decision 2) -- an activation that FAILS for real (the pinned test host's
        # fail_activation_once fault: that one StandbyActivate refused as an unknown entry), then the same tap's ordinary
        # connect. First the positive control: the same steps, no fault armed -> the activation succeeds.
        mark('E13')
        fault_c = os.path.join(ROOT, 'host_c', 'fault.txt')
        open(fault_c, 'w', encoding='ascii').write('')  # nothing armed
        tap('card:' + H['c'])
        select('E13 control: C (ordinary)')
        to_list('E13 control C')
        ready = (wait_card(H['c'], '대기 중', 40) or ('',))[0] == '대기 중'
        n0 = cursor()
        tap('card:' + H['c'])
        select('E13 control: C by activation')
        log = since(n0)
        verdict('E13 control (no fault): C stands by, the tap activates its lease -- no failure, no ordinary connect',
                ready and '[standby_activate]' in log and re.search(r'\\[directory_connect_started\\] \\S+ \\S+ gen=[1-9]\\d* standby=1', log)
                is not None and '[standby_activate_failed]' not in log and
                'fail_activation applied' not in hostlog('c'),
                ' | '.join(re.findall(r'\\[(?:standby_activate|standby_activate_failed|directory_connect_started)\\][^\\n]*', log))[:300])
        to_list('E13 C again')
        ready = (wait_card(H['c'], '대기 중', 40) or ('',))[0] == '대기 중'
        token = 'e13-' + str(int(time.time()))
        open(fault_c, 'w', encoding='ascii').write(f'fail_activation_once={token}\\n')  # this episode only
        step(f'  armed fail_activation_once={token} on C')
        n0 = cursor()
        hl0 = len(hostlog('c'))
        tap('card:' + H['c'])
        select('E13 C: activation fails, the same tap connects ordinarily')
        log = since(n0)
        hl = hostlog('c')[hl0:]
        open(fault_c, 'w', encoding='ascii').write('')  # disarmed (the token is spent anyway)
        verdict('E13 the fault applied once on C (its log names this token)',
                ready and hl.count('fault: fail_activation applied token=' + token) == 1, hl[-300:])
        verdict('E13 the failure is real and still this tap\\'s: nativeConnectStandby gen=0, mine=true',
                re.search(r'\\[standby_activate_failed\\] handle=\\d+ op=\\d+ gen=0 why=\\S* mine=true', log) is not None,
                ' | '.join(re.findall(r'\\[standby_activate_failed\\][^\\n]*', log))[:200])
        verdict('E13 the same tap\\'s ordinary connect opens C (a directory connect after the failure, the viewer reached)',
                '[directory_connect]' in log.split('[standby_activate_failed]', 1)[-1] and
                re.search(r'\\[directory_connect_started\\] \\S+ \\S+ gen=[1-9]\\d*(?! standby=1)', log.split('[standby_activate_failed]', 1)[-1])
                is not None,
                ' | '.join(re.findall(r'\\[(?:directory_connect|directory_connect_started|standby_release)\\][^\\n]*', log))[:300])
        to_list('E13 C after the fallback')

'''
s = s.replace(anchor, e13 + anchor)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
