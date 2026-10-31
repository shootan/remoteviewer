import sys, shutil
base = 'C:/work/gnlink-wt/apk-prelogin-select/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/'
MUT = {
    # c1: the latest swap instead of the latched buffer (r4's scalar)
    'L': ('SharpenOutputLog.kt', '    fun lookup(ts: Long): Output? = outputs[ts]',
          '    fun lookup(ts: Long): Output? = if (outputs.containsKey(ts)) outputs.values.last() else null  // MUTANT latest'),
    # c1: a cleared buffer taken as a frame (the same decoder timestamp)
    'C': ('SharpenOutputLog.kt', '            SharpenOutputLog.Kind.CLEAR -> NOTHING',
          '            SharpenOutputLog.Kind.CLEAR -> Decision(true, false, o.decoderTs, o.placementGen)  // MUTANT clear'),
    # c2: only the first DOWN held (r4): the stream is the PC's
    'R': ('ViewerTouchRouter.kt', 'owner = if (localNow) Owner.LOCAL else if (layoutShown) Owner.PC else Owner.SWALLOW',
          'owner = if (localNow) Owner.LOCAL else Owner.PC  // MUTANT r4'),
}
m, act = sys.argv[1], sys.argv[2]
f, old, new = MUT[m]
p = base + f
bak = 'D:/remote/remote/.claude/caplife/apk/shr5_%s.bak' % m
if act == 'apply':
    shutil.copyfile(p, bak)
    s = open(p, 'rb').read().decode('utf-8')
    assert s.count(old) == 1, (m, s.count(old))
    open(p, 'wb').write(s.replace(old, new).encode('utf-8'))
else:
    shutil.copyfile(bak, p)
print(m, act, 'ok')
