# usage: python -I r4_mutant.py M16 <apply|revert>
import sys
p = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\apps\\native_poc\\src\\host_stage_time_limit.cpp'
good = '''  if (capture.restartPending && capture.restartOwner == CaptureState::RestartOwner::General &&
      clientSession.streamControlActive.load(std::memory_order_acquire) &&
      nowUs >= capture.restartRetryAtUs && restart_capture_session(hx)) {'''
bad = '''  if (capture.restartPending && capture.restartOwner != CaptureState::RestartOwner::DesktopRebuild &&  // MUTANT M16 (r3 rule)
      clientSession.streamControlActive.load(std::memory_order_acquire) &&
      nowUs >= capture.restartRetryAtUs && restart_capture_session(hx, capture.restartOwner)) {'''
op = sys.argv[2]
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
a, b = (good, bad) if op == 'apply' else (bad, good)
assert s.count(a) == 1
s = s.replace(a, b)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('M16', op, 'ok')
