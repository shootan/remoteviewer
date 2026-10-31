p = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/ke_avd.py'
lines = open(p, encoding='utf-8').read().split('\n')
assert 'host.log' in lines[103] and 'glob.glob' in lines[103], lines[103]
lines[103] = "        for cand in glob.glob('C:/work/gnlink-wt/feat-apk-0233/build/test-scratch/**/host.log', recursive=True):"
open(p, 'w', encoding='utf-8').write('\n'.join(lines))
print('ok')
