# C r2: single-fix mutants of the CURRENT source (each undoes ONE r2 fix) -> neg\<name>. The 5fe90a9 source is neg\prev5fe90a9.
import os
import shutil

WT = r'C:\work\gnlink-wt\apk-inbox-c'
CPP = WT + r'\apps\android_direct_client\app\src\main\cpp'
SRC = WT + r'\apps\native_poc\src'
FILES = [CPP + r'\android_video_decoder.cpp', CPP + r'\android_video_decoder.hpp', CPP + r'\android_video_decoder_sh_seam.hpp',
         CPP + r'\entry_sink_adapter.hpp', SRC + r'\decoder_chain_inbox.hpp', SRC + r'\native_video_client_session.cpp']


def make(name, fname, a, b):
    d = WT + '\\neg\\' + name
    os.makedirs(d, exist_ok=True)
    for f in FILES:
        shutil.copyfile(f, d + '\\' + os.path.basename(f))
    p = d + '\\' + fname
    raw = open(p, 'rb').read()
    crlf = b'\r\n' in raw
    s = raw.decode('utf-8').replace('\r\n', '\n')
    assert s.count(a) == 1, (name, s.count(a))
    s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))
    print('made', name)


SESS = 'native_video_client_session.cpp'
# N2: a new pick / reset does not consume the limiter's pending flag
make('r2_no_consume', SESS, '''  ++keyHeldVersion_;
  (void)keyframeRequests_.ConsumePending(nullptr);
''', '''  ++keyHeldVersion_;
''')
# N2: an action with no descriptor goes as a legacy request (the r1 reading)
make('r2_empty_legacy', SESS, '''        if (sentKey.empty()) {
          // C r2 (Codex N2)''', '''        if (sentKey.empty()) sentKey.causes = kKeyCauseLegacy;
        if (false) {
          // C r2 (Codex N2)''')
INBOX = 'decoder_chain_inbox.hpp'
# N1-a: a loss trims nothing (the queued deltas behind it stay)
make('r2_no_trim', INBOX, '''    while (!items_.empty()) {
      const ChainAuInfo a = info_(items_.front());
      if (a.key && chain_seq_after(a.seq, seq)) return dropped;''', '''    while (false && !items_.empty()) {
      const ChainAuInfo a = info_(items_.front());
      if (a.key && chain_seq_after(a.seq, seq)) return dropped;''')
# N1-b: a committed IDR does not close the tail gap
make('r2_no_keycommit', INBOX, '''    if (gapOpen_ && chain_seq_after(seq, gapAfterSeq_)) gapOpen_ = false;''',
     '''    (void)seq;''')
# N2: the held check acts on whatever is held now (no version compare)
make('r2_held_clear_all', SESS, '''        if (keyHeldVersion_ == heldVersion) {''', '''        if (true) {''')
# N2: the RX publish split in two sections (the limiter, then the descriptor)
make('r2_publish_split', SESS, '''  std::lock_guard<std::mutex> lk(keyLedgerMu_);
  const bool queued = keyframeRequests_.Request(2, nowUs).queued;
#ifdef REMOTE60_CLIENT_SESSION_TEST_HOOKS
  RunTestKeyHook("publish");  // inside the section: a test may change the SINK here (never this session)
#endif
''', '''  bool queued = false;
  {
    std::lock_guard<std::mutex> lk0(keyLedgerMu_);
    queued = keyframeRequests_.Request(2, nowUs).queued;
  }
#ifdef REMOTE60_CLIENT_SESSION_TEST_HOOKS
  RunTestKeyHook("publish");
#endif
  std::lock_guard<std::mutex> lk(keyLedgerMu_);
''')
# §8: the session never tells the sink the owed asks are spent
make('r2_no_exhaust_notice', SESS, '''          encodedFrameSink_->OnDecoderKeyOwedExhausted(owedExhaustToken_, now_us());  // C r2 §8: the last, sent''',
     '''          (void)0;  // mutant: no notice''')
print('ok')
