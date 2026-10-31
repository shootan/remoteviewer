import sys, shutil
base = 'C:/work/gnlink-wt/apk-prelogin-select/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/'
files = {'R': base + 'SharpenRenderer.kt', 'M': base + 'MainActivity.kt'}
bak = 'D:/remote/remote/.claude/caplife/apk/sh_r4_%s.bak'
MUT = {
    # a: r3's fall-through -- after two cleared swaps the video is drawn into whatever size the output has
    'Ma': ('R', 'if (!matches) {', 'if (!matches && layoutClearSwaps.get() < 2) {  // MUTANT a'),
    # b: r3's redraw wait -- a redraw that meets a mismatch clears and is never retried
    'Mb': ('R', 'if (newFrame) frameWaitsForLayout = true else if (!frameWaitsForLayout) redrawWaitsForLayout = true',
           'if (newFrame) frameWaitsForLayout = true  // MUTANT b'),
    # c: no input fence
    'Mc': ('M', '        sharpenPlacementSent != null && sharpenDisplayPassActive() && sharpenShownPlacementGen != sharpenPlacementGenSent',
           '        false  // MUTANT c'),
}
m, act = sys.argv[1], sys.argv[2]
key, old, new = MUT[m]
p = files[key]
if act == 'apply':
    shutil.copyfile(p, bak % key)
    s = open(p, 'rb').read().decode('utf-8')
    assert s.count(old) == 1, (m, s.count(old))
    open(p, 'wb').write(s.replace(old, new).encode('utf-8'))
else:
    shutil.copyfile(bak % key, p)
print(m, act, 'ok')
