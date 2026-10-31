# usage: python -I r7_mut.py <M6|M7> <apply|revert>
import sys
S = 'C:\\work\\gnlink-wt\\rel-0163\\apps\\native_poc\\src\\'
MUTS = {
    # M6: the r6 shape -- local mode read a second time after the ticket, and ordinary frames permitted unchecked
    'M6': [
        (S + 'viewer_video_receiver_frame.cpp',
         '''    const bool frameLocal = ticket.local;''',
         '''    const bool frameLocal = ctx.sel.IsLiveLocal(frameOp);  // MUTANT M6'''),
        (S + 'viewer_selection_gate.cpp',
         '''    if (pending.load(std::memory_order_acquire) && localAckless.load(std::memory_order_acquire) &&
        epoch.load(std::memory_order_acquire) != opEpoch) {
      localFramesRefused.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    return permit();  // an ordinary frame''',
         '''    if (false) {  // MUTANT M6
      return false;
    }
    return permit();  // an ordinary frame'''),
    ],
    # M7: only the ordinary-mix check removed
    'M7': [
        (S + 'viewer_selection_gate.cpp',
         '''    if (pending.load(std::memory_order_acquire) && localAckless.load(std::memory_order_acquire) &&
        epoch.load(std::memory_order_acquire) != opEpoch) {
      localFramesRefused.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    return permit();  // an ordinary frame''',
         '''    if (false) {  // MUTANT M7
      return false;
    }
    return permit();  // an ordinary frame'''),
    ],
}
name, op = sys.argv[1], sys.argv[2]
for path, good, bad in MUTS[name]:
    s = open(path, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    a, b = (good, bad) if op == 'apply' else (bad, good)
    assert s.count(a) == 1, (name, op, path, s.count(a))
    s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(path, 'wb').write(s.encode('utf-8'))
print(name, op, 'ok')
