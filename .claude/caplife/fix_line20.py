p = 'D:/remote/remote/.claude/caplife/rel0232_r5_batch.ps1'
lines = open(p, encoding='utf-8').read().split('\n')
for i, l in enumerate(lines):
    if 'prelogin_ui_control -Tag rel5_prelogin_ui_control' in l and l.startswith('$r = &'):
        lines[i] = r'$r = & D:\remote\remote\.claude\caplife\sh_run.ps1 -Scenario prelogin_ui_control -Tag rel5_prelogin_ui_control -Seam false -NoBuild'
open(p, 'w', encoding='utf-8').write('\n'.join(lines))
print('ok')
