L = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane'


def load(p):
    raw = open(p, 'rb').read()
    return raw.decode('utf-8').replace('\r\n', '\n'), b'\r\n' in raw


def save(p, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


def rep(s, a, b):
    assert s.count(a) == 1, (a[:100], s.count(a))
    return s.replace(a, b)


# ---- oa_avd: the old-ref scope (to the 30 s watch)
p = L + r'\oa_avd.py'
s, crlf = load(p)
s = rep(s, '''        # r7f: 30 s, no input, the connection watched; a break ends the arm here (no 4, nothing after)
        if not observe_connection('3w the connection'):
            return 1
''', '''        # r7f: 30 s, no input, the connection watched; a break ends the arm here (no 4, nothing after)
        if not observe_connection('3w the connection'):
            return 1
        # old-ref r1 (Codex codex_old_apk_lane_peer_lost_decision_r1.md): the old/new reference runs compare login ->
        # ordinary / relay -> pick / first frame -> the 30 s watch. With OA_SCOPE=to-3w (oa_slot.ps1 -Scope to-3w,
        # recorded there) the arm ends HERE: no input at all after the watch, 4-7 NOT RUN by scope (not by a failure).
        if os.environ.get('OA_SCOPE') == 'to-3w':
            step('  scope to-3w (old-ref r1): the arm ends after the 30 s watch -- 4 5 6 7 NOT RUN by scope')
            for c in '4567':
                skip_case(c, 'old-ref scope to-3w: login -> relay -> pick / first frame -> 30 s watch only')
            open(os.path.join(OUT, 'app_final.log'), 'w', encoding='utf-8').write(applog())
            return 0
''')
save(p, s, crlf)

# ---- oa_slot: -Scope full | to-3w
p = L + r'\oa_slot.ps1'
s, crlf = load(p)
s = rep(s, '''  [switch]$WithStandby,
  [string[]]$HolderCmd = @()
)''', '''  [switch]$WithStandby,
  [string[]]$HolderCmd = @(),
  # old-ref r1: full (default) | to-3w -- the reference runs end the arm after the 30 s watch (4-7 NOT RUN by scope)
  [ValidateSet('full', 'to-3w')][string]$Scope = 'full'
)''')
s = rep(s, '''Say "  arm $Arm standby=$([bool]$WithStandby) build=$BuildDir server=$ServerJs serverSha256=$ServerSha256 relayTrace=$env:KE_RELAY_TRACE"''',
        '''Say "  arm $Arm standby=$([bool]$WithStandby) build=$BuildDir server=$ServerJs serverSha256=$ServerSha256 relayTrace=$env:KE_RELAY_TRACE scope=$Scope"''')
s = rep(s, '''      $env:KA_SERIAL = 'emulator-5554'
''', '''      $env:KA_SERIAL = 'emulator-5554'
      if ($Scope -eq 'to-3w') { $env:OA_SCOPE = 'to-3w' } else { Remove-Item env:OA_SCOPE -ErrorAction SilentlyContinue }
''')
save(p, s, crlf)

# ---- the stub test: the scope ends the arm after 3w, no tap on the picture; without it 4 runs
p = L + r'\oa_pick_test.py'
s, crlf = load(p)
anchor = '''
    print(f'\\n{checks} checks, {fails} failed\\nRESULT: {"PASS" if fails == 0 else "FAIL"}')'''
new = r'''
    # old-ref r1: OA_SCOPE=to-3w -- the reference runs stop after the 30 s watch
    os.environ['OA_SCOPE'] = 'to-3w'
    try:
        rc, four, nr, i4, after = run_flow('flow-scope-3w', input_mode='pair')
    finally:
        os.environ.pop('OA_SCOPE', None)
    res = run_flow.res['results']
    steps_s = {r[0].split()[0]: r[1] for r in res}
    reasons = {n: w for n, w in run_flow.res['not_run']}
    check('old-ref: OA_SCOPE=to-3w -- through 3w all PASS, then the arm ends: exit 0, NO tap on the picture, 4 5 6 7 NOT RUN '
          'by scope', rc == 0 and steps_s.get('3w') is True and i4 < 0 and
          all('old-ref scope' in reasons.get(c, '') for c in '4567'), f'rc={rc} steps={steps_s} not run={reasons}')
    rc, four, nr, i4, after = run_flow('flow-scope-full', input_mode='pair')
    check('old-ref contrast: without the scope the same flow goes on to 4 (the picture tapped)', i4 >= 0 and four,
          f'rc={rc} 4={[f[1] for f in four]}')
'''
s = rep(s, anchor, new + anchor)
save(p, s, crlf)
print('ok')
