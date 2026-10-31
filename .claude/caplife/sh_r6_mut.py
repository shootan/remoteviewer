import sys, shutil
p = 'C:/work/gnlink-wt/apk-prelogin-select/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/SharpenOutputLog.kt'
bak = 'D:/remote/remote/.claude/caplife/apk/shr6_outputlog.bak'
MUT = {
    # a: a counter per log (r5): renderer A and B both start at BASE+1
    'A': [('    private val outputs = LinkedHashMap<Long, Output>()\n',
           '    private val outputs = LinkedHashMap<Long, Output>()\n    private var localSeqM = 0L  // MUTANT a\n'),
          ('        val ts = BASE + NEXT.incrementAndGet()\n', '        val ts = BASE + (++localSeqM)\n')],
    # b: a REDRAW never shows a frame for the first time (r5)
    'B': [('            SharpenOutputLog.Kind.REDRAW, SharpenOutputLog.Kind.VIDEO -> Decision(true, o.decoderTs, o.placementGen)',
           '            SharpenOutputLog.Kind.REDRAW -> Decision(false, o.decoderTs, o.placementGen)  // MUTANT b\n'
           '            SharpenOutputLog.Kind.VIDEO -> Decision(true, o.decoderTs, o.placementGen)')],
    # c: a cleared / unknown buffer keeps the previous shown proof (r5)
    'C': [('    private val NO_PICTURE = Decision(false, 0L, NONE_SHOWN)', '    private val NO_PICTURE = Decision(false, 0L, null)  // MUTANT c')],
}
m, act = sys.argv[1], sys.argv[2]
if act == 'apply':
    shutil.copyfile(p, bak)
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in MUT[m]:
        assert s.count(old) == 1, (m, s.count(old), old[:60])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))
else:
    shutil.copyfile(bak, p)
print(m, act, 'ok')
