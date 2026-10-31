p = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_filter_avd.py'
s = open(p, 'rb').read().decode('utf-8')
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('\r\n', '\n')
old = r"""              name + ': the redraw keeps the frame\'s timestamp (after the cleared swaps)',
              'first=%s last=%s' % (pc.get('firstTs'), pc.get('lastTs')))"""
new = r"""              name + ': the redraw shows the SAME decoder frame (after the cleared swaps)',
              'first=%s last=%s' % (pc.get('firstTs'), pc.get('lastTs')))
        # r5 (N1-c1): the last output buffer is named, by its own timestamp, a REDRAW of the pan's layout (gen 2)
        check(pc.get('lastKind') == 'REDRAW' and pc.get('lastGen') == 2 and pc.get('outTs', 0) >= (1 << 62),
              name + ': the latched buffer is identified as a REDRAW of the new layout (its own output timestamp)',
              'kind=%s gen=%s outTs=%s' % (pc.get('lastKind'), pc.get('lastGen'), pc.get('outTs')))"""
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new)
open(p, 'wb').write(s.replace('\n', nl).encode('utf-8'))
print('ok')
