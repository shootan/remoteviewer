# usage: python -I r6_mut.py <M4|M5> <apply|revert>
import sys
S = 'C:\\work\\gnlink-wt\\rel-0163\\apps\\native_poc\\src\\'
MUT = {
    # R3 1: the permission released before the write (the r5 behaviour)
    'M4': (S + 'viewer_video_receiver_frame.cpp',
           '''      if (!ctx.sel.CommitFrame(frameOp, frameLocal, candidate, h.streamGeneration, &revealGen, &selHeld)) {''',
           '''      if (!ctx.sel.CommitFrame(frameOp, frameLocal, candidate, h.streamGeneration, &revealGen, nullptr)) {  // MUTANT M4'''),
    # R3 2: the revealed exception by epoch + any active generation (the r5 behaviour)
    'M5': (S + 'viewer_selection_gate.cpp',
           '''    if (!pending.load(std::memory_order_acquire) && revealedOp.load(std::memory_order_acquire) == opEpoch &&''',
           '''    if (!pending.load(std::memory_order_acquire) && /* MUTANT M5 */ true &&'''),
}
name, op = sys.argv[1], sys.argv[2]
path, good, bad = MUT[name]
s = open(path, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
a, b = (good, bad) if op == 'apply' else (bad, good)
assert s.count(a) == 1, (name, op, s.count(a))
s = s.replace(a, b)
if crlf:
    s = s.replace('\n', '\r\n')
open(path, 'wb').write(s.encode('utf-8'))
print(name, op, 'ok')
