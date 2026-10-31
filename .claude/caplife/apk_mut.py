# usage: python -I apk_mut.py <PRE|G1|G2|S2> <apply|revert>
import subprocess, sys
WT = 'C:\\work\\gnlink-wt\\apk-prelogin-select\\'
S = WT + 'apps\\native_poc\\src\\'
G = S + 'selection_ack_gate.hpp'
SC = S + 'native_video_client_session.cpp'
MUTS = {
    # G1: a local selection is bound when the gate lets its IDR through, not when the IDR reaches the decoder
    'G1': [(G, '''      candidate_ = frameStreamGeneration;
      return true;''', '''      candidate_ = frameStreamGeneration;
      expected_ = frameStreamGeneration;  // MUTANT G1
      keyOwed_ = false;
      return true;''')],
    # G2: an unbound local selection lets deltas through
    'G2': [(G, '''      if (!isKey || frameStreamGeneration == 0) return false;''',
            '''      if (frameStreamGeneration == 0) return false;  // MUTANT G2''')],
    # S2: the local path taken for a secure desktop / LockCred host too, not only PreloginV1
    'S2': [(SC, '''  if (role.known && (role.flags & kCaptureFlagPreloginV1) != 0) {
    if (windowId != 0) {''', '''  if (role.known && (role.flags & (kCaptureFlagPreloginV1 | kCaptureFlagSecureDesktopActive)) != 0) {  // MUTANT S2
    if (windowId != 0) {''')],
    # N1 (r1 shape): no lock around the queued pick's turn, and the panel's answer ignored
    'N1': [(SC, '''  std::unique_lock<std::mutex> ls(localSelectMu_);
  bool sinkLocal = false;''', '''  std::unique_lock<std::mutex> ls(localSelectMu_, std::defer_lock);  // MUTANT N1
  bool sinkLocal = false;'''),
           (SC, '''    if (windowPanel_.ConvertSelectToLocal(tag, "desktop_select_local")) {''',
                '''    if (windowPanel_.ConvertSelectToLocal(tag, "desktop_select_local") || true) {  // MUTANT N1'''),
           (SC, '''  ls.unlock();
  const auto panelSnapshot = windowPanel_.Snapshot();
  std::lock_guard<std::mutex> lock(mu_);
  SyncWindowPanelSnapshotLocked(panelSnapshot);
  UpdateConnectedStatusLocked(panelSnapshot.status);
}

// apk-0232 r1: once''', '''  if (ls.owns_lock()) ls.unlock();  // MUTANT N1
  const auto panelSnapshot = windowPanel_.Snapshot();
  std::lock_guard<std::mutex> lock(mu_);
  SyncWindowPanelSnapshotLocked(panelSnapshot);
  UpdateConnectedStatusLocked(panelSnapshot.status);
}

// apk-0232 r1: once''')],
    # N2 (r1 shape): the held frame retried without the gate, and a newer frame queued while one is held
    'N2': [(S + 'decoder_pending_slot.hpp', '''    if (!gate.PermitsQueue(frame_->header.streamGeneration)) {
      ++gateDrops_;
      frame_.reset();
      return true;
    }''', '''    if (false) {  // MUTANT N2
      ++gateDrops_;
      frame_.reset();
      return true;
    }'''),
           (S + 'decoder_pending_slot.hpp', '''    if (!frame_.has_value() && tryQueue(frame)) return;''',
                                             '''    if (tryQueue(frame)) return;  // MUTANT N2''')],
}
name, op = sys.argv[1], sys.argv[2]
if name == 'PRE':
    # the session as it was at 5970f04 (its header, panel and gate keep the new API it does not call)
    if op == 'apply':
        import shutil
        shutil.copyfile(SC, SC + '.fixed')
        old = subprocess.run(['git', '-C', WT, 'show', '5970f04:apps/native_poc/src/native_video_client_session.cpp'],
                             capture_output=True, check=True).stdout
        open(SC, 'wb').write(old)
    else:
        import shutil, os
        shutil.copyfile(SC + '.fixed', SC)
        os.remove(SC + '.fixed')
    print(name, op, 'ok')
    sys.exit(0)
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
