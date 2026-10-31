# usage: python -I r2_mutant.py <M5|M6|M7|M8|M9> <apply|revert>
import sys
wt = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\'
S = wt + 'apps\\native_poc\\src\\'
L = wt + 'libs\\capture\\src\\'

MUT = {
    # N1: the raw-queue provenance refusal removed
    'M5': (S + 'host_capture_session.cpp',
           '''    if (!provenanceCurrent()) {
      // Not this attachment's / not this desktop's: never queued for the encoder (its lease goes back).''',
           '''    if (false && !provenanceCurrent()) {  // MUTANT M5
      // Not this attachment's / not this desktop's: never queued for the encoder (its lease goes back).'''),
    # N2: a newer-epoch request is dropped instead of left (the old TakeForEpoch rule)
    'M6': (S + 'host_main_loop_mailbox.hpp',
           '''      case BackendRequestFate::Leave:
        return std::nullopt;''',
           '''      case BackendRequestFate::Leave:  // MUTANT M6
        backend_.reset();
        return std::nullopt;'''),
    # N3: a question overwrites the change waiting (the r1 rule)
    'M7': (S + 'native_video_client_shared_core.cpp',
           '''  queryPending_.store(true, std::memory_order_release);  // r2 N3: backend_ (a change waiting) stays as it is''',
           '''  backend_.store(0, std::memory_order_release);  // MUTANT M7
  pending_.store(true, std::memory_order_release);'''),
    # N4: a new source does not open a new budget (the r1 rule: only a publish closes an episode)
    'M8': (S + 'host_stage_pop_frame.cpp',
           '''  if (!wasOpen || key != capture.fullFrameEpisodeKey) {''',
           '''  if (!wasOpen) {  // MUTANT M8'''),
    # lib: a pointer-only acquire answers the full-frame request again (the r1 rule)
    'M9': (L + 'capture_backend_dxgi.cpp',
           '''      const bool carriesImage = frameInfo.LastPresentTime.QuadPart != 0;''',
           '''      const bool carriesImage = true;  // MUTANT M9'''),
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
