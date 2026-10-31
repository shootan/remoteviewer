"""apk-decoder-liveness r1 negative runs: two mutants of the NEW sink, each removing one guarantee.
  m1 -- no generation check where the worker applies a codec result: a codec made while a reset was asked for
        (surface / owner / selection gone) is used, fed and shown (every codecResetSeq_ comparison on a result removed)
  m2 -- the frame inbox has NO bound (the oldest is never dropped)"""
import os
HERE = os.path.dirname(os.path.abspath(__file__))


def mutate(name, pairs):
    p = os.path.join(HERE, name, 'android_video_decoder.cpp')
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in pairs:
        assert s.count(a) == 1, (name, a[:60], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('mutated', name, len(pairs))


mutate('m1', [
    ('if (resetSeq != codecResetSeq_ || window != window_ || workerStop_) {',
     'if (false) {  // MUTANT m1'),
    ('  if (!codec_ || CodecResetPendingLocked()) return false;\n  AMediaCodec* const codec = codec_;',
     '  if (!codec_) return false;  // MUTANT m1\n  AMediaCodec* const codec = codec_;'),
    ('  if (resetSeq != codecResetSeq_) return true;', '  // MUTANT m1: no reset check after queueing'),
    ('  if (CodecResetPendingLocked()) return;  // the codec is about to go: its picture is not shown (r1)',
     '  // MUTANT m1: shown even when the codec is about to go'),
    ('  if (resetSeq == codecResetSeq_ && gate_.pending() != 0 &&', '  if (gate_.pending() != 0 &&  // MUTANT m1'),
    ('    if (!codec_ || CodecResetPendingLocked()) return;  // r1: the codec is about to go',
     '    if (!codec_) return;  // MUTANT m1'),
    ('    lastOutputDequeue_ = static_cast<int64_t>(outputIndex);\n    if (CodecResetPendingLocked()) return;',
     '    lastOutputDequeue_ = static_cast<int64_t>(outputIndex);  // MUTANT m1'),
])
mutate('m2', [
    ('while (inbox_.size() > kInboxMaxFrames || (inboxBytes_ > kInboxMaxBytes && inbox_.size() > 1)) {',
     'while (false && (inbox_.size() > kInboxMaxFrames || (inboxBytes_ > kInboxMaxBytes && inbox_.size() > 1))) {  // MUTANT m2'),
])
# m3 (r2 axis 2) -- made by hand from the 27d5966 sink: the surface's gate_.Rearm() removed (an owed IDR is not asked
# afresh when the decoder becomes able to take one). See report_decoder_liveness_r1.md.
