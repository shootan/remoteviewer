import re
SRC = r'C:\work\gnlink-wt\apk-inbox-c\apps\native_poc\src'
for name in ('android_decoder_owed_key_test', 'android_decoder_binding_test', 'android_decoder_decode_stats_test',
             'android_decoder_liveness_test'):
    p = SRC + '\\' + name + '.cpp'
    raw = open(p, 'rb').read()
    crlf = b'\r\n' in raw
    s = raw.decode('utf-8').replace('\r\n', '\n')
    a = '  f.header.streamGeneration = gen;\n'
    assert s.count(a) == 1, name
    s = s.replace(a, a + '''  // apk-inbox C: the AU seq increasing, as the session's assembler delivers it (decoder_chain_inbox.hpp ORDER). Before
  // C it was left 0 -- to an order-aware inbox every frame would be the same AU again.
  static uint32_t fixtureSeq = 0;
  f.header.seq = ++fixtureSeq;
''')
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))
    print('ok', name)

# the order resync (a safety valve) in the chain inbox
p = SRC + r'\decoder_chain_inbox.hpp'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')
old = '''    if (haveLast_ && a.gen == lastGen_) {
      if (a.seq == lastSeq_) {
        r.admit = ChainAdmit::Duplicate;
        ++duplicates_;
        return r;
      }
      if (!chain_seq_after(a.seq, lastSeq_)) {
        r.admit = ChainAdmit::Stale;
        ++stale_;
        return r;
      }
    }
'''
new = '''    if (haveLast_ && a.gen == lastGen_ && orderDropsInRow_ < kOrderResyncAfter) {
      if (a.seq == lastSeq_) {
        r.admit = ChainAdmit::Duplicate;
        ++duplicates_;
        ++orderDropsInRow_;
        return r;
      }
      if (!chain_seq_after(a.seq, lastSeq_)) {
        r.admit = ChainAdmit::Stale;
        ++stale_;
        ++orderDropsInRow_;
        return r;
      }
    }
    if (haveLast_ && a.gen == lastGen_ && orderDropsInRow_ >= kOrderResyncAfter) {
      // the safety valve: so many in a row is no reordering -- the sequence restarted (not expected: the assembler
      // ignores anything not newer, and a stream reset clears this inbox). The order starts over from this AU.
      ++orderResyncs_;
    }
    orderDropsInRow_ = 0;
'''
assert s.count(old) == 1
s = s.replace(old, new)
old2 = '''  // Everything goes and the order and the gap start over (a new pick / binding / stream).
  void Clear() {
    items_.clear();
    bytes_ = 0;
    gapOpen_ = false;
    haveLast_ = false;
  }
'''
new2 = '''  // Everything goes and the order and the gap start over (a new pick / binding / stream).
  void Clear() {
    items_.clear();
    bytes_ = 0;
    gapOpen_ = false;
    haveLast_ = false;
    orderDropsInRow_ = 0;
  }

  // Duplicate / older AUs in a row after which the order is taken to have restarted (about a second at 30 fps).
  static constexpr uint32_t kOrderResyncAfter = 30;
'''
assert s.count(old2) == 1
s = s.replace(old2, new2)
old3 = '''  uint64_t lost() const { return lost_; }
'''
new3 = '''  uint64_t lost() const { return lost_; }
  uint64_t orderResyncs() const { return orderResyncs_; }
'''
assert s.count(old3) == 1
s = s.replace(old3, new3)
old4 = '''  uint64_t duplicates_ = 0, stale_ = 0, oversize_ = 0, gapDeltas_ = 0, skipped_ = 0, lost_ = 0;
};
'''
new4 = '''  uint64_t duplicates_ = 0, stale_ = 0, oversize_ = 0, gapDeltas_ = 0, skipped_ = 0, lost_ = 0;
  uint32_t orderDropsInRow_ = 0;
  uint64_t orderResyncs_ = 0;
};
'''
assert s.count(old4) == 1
s = s.replace(old4, new4)
# document it in the header's ORDER paragraph
old5 = '''// the sink sees strictly increasing `seq`. The duplicate / older-than-last checks here are DEFENSIVE: what a second
// path or a later change could feed in. Comparison: (int32_t)(b - a) > 0 means b is after a (wrap-safe within 2^31).'''
new5 = '''// the sink sees strictly increasing `seq`. The duplicate / older-than-last checks here are DEFENSIVE: what a second
// path or a later change could feed in. Comparison: (int32_t)(b - a) > 0 means b is after a (wrap-safe within 2^31).
// A safety valve: kOrderResyncAfter such drops in a row are taken as a restarted sequence, not reordering -- the order
// starts over from the next AU (counted), so no unforeseen restart can blank the picture for good.'''
assert s.count(old5) == 1
s = s.replace(old5, new5)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok inbox')
