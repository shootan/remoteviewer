# C r4: single-condition mutants of the keep-prefix prototype -> neg\<name> (each undoes ONE safety condition).
import os
import shutil

WT = r'C:\work\gnlink-wt\apk-inbox-c'
CPP = WT + r'\apps\android_direct_client\app\src\main\cpp'
SRC = WT + r'\apps\native_poc\src'
FILES = [CPP + r'\android_video_decoder.cpp', CPP + r'\android_video_decoder.hpp', CPP + r'\android_video_decoder_sh_seam.hpp',
         CPP + r'\entry_sink_adapter.hpp', SRC + r'\decoder_chain_inbox.hpp', SRC + r'\native_video_client_session.cpp']


def make(name, edits):
    d = WT + '\\neg\\' + name
    if os.path.exists(d):
        raise SystemExit(f'{d} exists')
    os.makedirs(d)
    for f in FILES:
        shutil.copyfile(f, d + '\\' + os.path.basename(f))
    for fname, a, b in edits:
        p = d + '\\' + fname
        raw = open(p, 'rb').read()
        crlf = b'\r\n' in raw
        s = raw.decode('utf-8').replace('\r\n', '\n')
        assert s.count(a) == 1, (name, a[:60], s.count(a))
        s = s.replace(a, b)
        if crlf:
            s = s.replace('\n', '\r\n')
        open(p, 'wb').write(s.encode('utf-8'))
    print('made', name)


H = 'decoder_chain_inbox.hpp'
S = 'android_video_decoder.cpp'
make('pm_no_contig', [(H, '''      if (a.gen != head.gen || a.seq != prev + 1u) return false;''', '''      if (a.gen != head.gen) return false;''')])
make('pm_no_frontier', [(H, '''    if (head.key || head.seq != frontierSeq_ + 1u) return false;''', '''    if (head.key) return false;''')])
make('pm_no_age', [
    (H, '''    if (head.enqueueUs == 0 || frontierNowUs_ < head.enqueueUs || frontierNowUs_ - head.enqueueUs > ageBudgetUs_) return false;''', ''),
    (H, '''    return !a.key && a.enqueueUs != 0 && nowUs > a.enqueueUs && nowUs - a.enqueueUs > budgetUs;''', '''    return false;'''),
    (S, '''  if (prefixPolicy_ && pendingFrame_.has() && (pendingFrame_.held()->header.flags & 1u) == 0 && pendingEnqueueUs_ != 0 &&''',
     '''  if (false && prefixPolicy_ && pendingFrame_.has() && (pendingFrame_.held()->header.flags & 1u) == 0 && pendingEnqueueUs_ != 0 &&'''),
])
make('pm_no_backpressure', [(S, '''      if (prefixPolicy_ && pendingFrame_.has()) {
        ++backpressurePasses_;
        break;
      }''', '''      if (false) {
        ++backpressurePasses_;
        break;
      }''')])
make('pm_no_chain_invalid', [(CPP + r'\android_video_decoder.hpp' and 'android_video_decoder.hpp',
                              '''  void InvalidateChainLocked() { chainValid_ = false; }''', '''  void InvalidateChainLocked() {}''')])
print('ok')
