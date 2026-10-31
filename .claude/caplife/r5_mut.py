# usage: python -I r5_mut.py <M1|M2|M3> <apply|revert>
import sys
S = 'C:\\work\\gnlink-wt\\rel-0163\\apps\\native_poc\\src\\'
MUT = {
    # R1-run: the live run taken at hold time (the r4 behaviour), and no run check before the immediate run
    'M1': (S + 'viewer_picker.cpp',
           '''    ctx.sel.deferredRun.store(runAtPick, std::memory_order_seq_cst);''',
           '''    ctx.sel.deferredRun.store(ctx.session.controlRunGen.load(std::memory_order_acquire), std::memory_order_seq_cst);  // MUTANT M1'''),
    # R4: the panel applied before the gate's boundary (the r4 order)
    'M2': (S + 'viewer_selection_gate.cpp',
           '''  std::lock_guard<std::mutex> lk(mu);
  if (pending.load(std::memory_order_acquire) && localAckless.load(std::memory_order_acquire)) {
    return SelectionAck::IgnoredLocal;  // the panel is not touched either
  }
  const std::pair<bool, uint64_t> r = applyPanel();''',
           '''  const std::pair<bool, uint64_t> r = applyPanel();  // MUTANT M2
  std::lock_guard<std::mutex> lk(mu);
  if (pending.load(std::memory_order_acquire) && localAckless.load(std::memory_order_acquire)) {
    return SelectionAck::IgnoredLocal;
  }'''),
    # R3: the final commit does not check the op (a local frame is adopted whatever became of its op)
    'M3': (S + 'viewer_selection_gate.cpp',
           '''  if (!frameLocal) return true;  // an ordinary frame: as before (the generation gate decided it)''',
           '''  if (true) return true;  // MUTANT M3'''),
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
