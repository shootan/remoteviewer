# usage: python -I r3_mutant.py <name> <apply|revert>
import sys
wt = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\'
S = wt + 'apps\\native_poc\\src\\'
L = wt + 'libs\\capture\\src\\'

MUT = {
    # lib (Codex M9 deterministic): any acquire answers the full-frame request again (the r1 rule)
    'M9': (L + 'capture_backend_dxgi.cpp',
           '''      const bool carriesImage = !testPointerOnly && frameInfo.LastPresentTime.QuadPart != 0;''',
           '''      const bool carriesImage = true;  // MUTANT M9'''),
    # N3-host: the r2 torn read -- packed from one look, the handled key from a later one
    'M10': (S + 'host_backend_status_board.hpp',
            '''    BackendStatusSnapshot out;
    {
      std::lock_guard<std::mutex> lk(mu_);
      out = s_;
    }
    if (testAfterRead) testAfterRead();
    return out;''',
            '''    BackendStatusSnapshot out;  // MUTANT M10
    {
      std::lock_guard<std::mutex> lk(mu_);
      out = s_;
    }
    if (testAfterRead) testAfterRead();
    {
      std::lock_guard<std::mutex> lk(mu_);
      out.handledKey = s_.handledKey;
    }
    return out;'''),
    # N2 end: an ended Serve's requests are handled again (the r2 rule)
    'M11': (S + 'host_backend_owner.hpp',
            '''  if (s.endedServe != 0 && (reqServe & kBackendServeSeqMask) <= s.endedServe) return BackendRequestFate::Drop;''',
            '''  if (false && s.endedServe != 0 && (reqServe & kBackendServeSeqMask) <= s.endedServe) return BackendRequestFate::Drop;  // MUTANT M11'''),
    # N1-a: the generic retry takes a desktop rebuild's failure over (the r2 behaviour)
    'M12': (S + 'host_stage_time_limit.cpp',
            '''  if (capture.restartPending && capture.restartOwner != CaptureState::RestartOwner::DesktopRebuild &&''',
            '''  if (capture.restartPending && true /* MUTANT M12 */ &&'''),
    # N1-a: a failed detach counts as rebuilt (only "stale" opens the rebuild -- the r2 rule)
    'M13': (S + 'host_stage_backend.cpp',
            '''    const bool failedHere = capture.restartPending && capture.restartOwner == CaptureState::RestartOwner::DesktopRebuild &&
                            backend.desktopRebuildGen == dgen;''',
            '''    const bool failedHere = false;  // MUTANT M13'''),
    # N1-b: no continuity token at cache adoption
    'M14': (S + 'host_capture_session.cpp',
            '''        meta.cacheContinuity == capture.cacheContinuity.load(std::memory_order_acquire)) {''',
            '''        true /* MUTANT M14 */) {'''),
    # N1-b: cookie 0 waved through again
    'M15': (S + 'host_capture_session.cpp',
            '''    return meta.attachmentCookie != 0 &&
           meta.attachmentCookie == capture.attachmentCookie.load(std::memory_order_acquire) &&''',
            '''    return (meta.attachmentCookie == 0 ||  // MUTANT M15
            meta.attachmentCookie == capture.attachmentCookie.load(std::memory_order_acquire)) &&'''),
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
