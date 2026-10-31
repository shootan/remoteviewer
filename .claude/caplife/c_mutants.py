# apk-inbox C: the negative builds' sources -- the 79b5c25 sink (the previous source) and one mutant per new boundary.
# Each goes into its own new folder <worktree>/neg/<name>/ (untracked): the sink files, the chain inbox header and the
# session source, one of them changed. Nothing in the worktree's own sources is touched.
import os
import subprocess
import sys

WT = r'C:\work\gnlink-wt\apk-inbox-c'
CPP = WT + r'\apps\android_direct_client\app\src\main\cpp'
SRC = WT + r'\apps\native_poc\src'
NEG = WT + r'\neg'
SINK_FILES = ['android_video_decoder.cpp', 'android_video_decoder.hpp', 'entry_sink_adapter.hpp',
              'android_video_decoder_sh_seam.hpp']


def read(p):
    return open(p, 'rb').read().decode('utf-8')


def write(p, s):
    open(p, 'wb').write(s.encode('utf-8'))


def fresh(name):
    d = os.path.join(NEG, name)
    os.makedirs(d)  # new: refused if it exists
    for f in SINK_FILES:
        write(os.path.join(d, f), read(os.path.join(CPP, f)))
    write(os.path.join(d, 'decoder_chain_inbox.hpp'), read(os.path.join(SRC, 'decoder_chain_inbox.hpp')))
    write(os.path.join(d, 'native_video_client_session.cpp'), read(os.path.join(SRC, 'native_video_client_session.cpp')))
    return d


def mutate(d, f, old, new):
    p = os.path.join(d, f)
    s = read(p)
    n = s.count(old)
    assert n == 1, (d, f, old[:70], n)
    write(p, s.replace(old, new))


os.makedirs(NEG, exist_ok=True)

# the previous source: 79b5c25's sink (the session stays C's: its defaults serve an old sink as before)
d = os.path.join(NEG, 'old79b5c25')
os.makedirs(d)
for f in SINK_FILES:
    blob = subprocess.run(['git', '-C', WT, 'show', '79b5c25:apps/android_direct_client/app/src/main/cpp/' + f],
                          capture_output=True, check=True).stdout
    open(os.path.join(d, f), 'wb').write(blob)
write(os.path.join(d, 'decoder_chain_inbox.hpp'), read(os.path.join(SRC, 'decoder_chain_inbox.hpp')))
write(os.path.join(d, 'native_video_client_session.cpp'), read(os.path.join(SRC, 'native_video_client_session.cpp')))

# m_dropall: no chain awareness -- every overflow takes the whole inbox (no IDR + suffix kept)
d = fresh('m_dropall')
mutate(d, 'decoder_chain_inbox.hpp', '    if (fit >= 0) {\r\n', '    if (false && fit >= 0) {\r\n')
mutate(d, 'decoder_chain_inbox.hpp', '    if (lastKey >= 0) {\r\n', '    if (false && lastKey >= 0) {\r\n')

# m_nodedupe: no order check (a duplicate / older AU admitted)
d = fresh('m_nodedupe')
mutate(d, 'decoder_chain_inbox.hpp', 'if (haveLast_ && a.gen == lastGen_ && orderDropsInRow_ < kOrderResyncAfter) {',
       'if (false && haveLast_ && a.gen == lastGen_ && orderDropsInRow_ < kOrderResyncAfter) {')

# m_anyidr: the episode closed by ANY committed IDR (not only one after the loss point)
d = fresh('m_anyidr')
mutate(d, 'decoder_chain_inbox.hpp', 'if (state_ == State::Idle || !chain_seq_after(seq, lossSeq_)) return false;',
       'if (state_ == State::Idle) return false;')

# m_attempt_on_want: an attempt counted when the want is handed out (a refusal / merge would count)
d = fresh('m_attempt_on_want')
mutate(d, 'decoder_chain_inbox.hpp', '    wantOut_ = true;\r\n    wantOutUs_ = nowUs;\r\n    ++wants_;\r\n',
       '    wantOut_ = false;\r\n    ++attempts_;\r\n    nextDueUs_ = nowUs + (attempts_ < kMaxRequests ? kBackoffUs[attempts_ - 1] : 0);\r\n'
       '    if (attempts_ >= kMaxRequests) finalDeadlineUs_ = nowUs + kFinalWindowUs;\r\n    wantOutUs_ = nowUs;\r\n    ++wants_;\r\n')

# m_notailgap: deltas after a loss admitted (no tail gap)
d = fresh('m_notailgap')
mutate(d, 'decoder_chain_inbox.hpp', '    if (gapOpen_) {\r\n      if (!a.key) {', '    if (gapOpen_) {\r\n      if (false && !a.key) {')

# m_nocandidate: the episode asks even while a usable IDR is preserved
d = fresh('m_nocandidate')
mutate(d, 'decoder_chain_inbox.hpp', 'if (state_ != State::Open || candidate || attempts_ >= kMaxRequests) return false;',
       'if (state_ != State::Open || attempts_ >= kMaxRequests) return false;')

# m_exhaust_blocks: an exhausted episode blocks the inbox (the "stop the video and pass" shape)
d = fresh('m_exhaust_blocks')
mutate(d, 'android_video_decoder.cpp', '  const uint32_t seq = frame.header.seq;\r\n  const uint64_t gen = frame.header.streamGeneration;\r\n',
       '  if (recovery_.state() == remote60::native_poc::KeyRecoveryEpisode::State::Exhausted) return;\r\n'
       '  const uint32_t seq = frame.header.seq;\r\n  const uint64_t gen = frame.header.streamGeneration;\r\n')

# m_no_owed_coalesce: the episode asks on top of the owed duty (4 + 8)
d = fresh('m_no_owed_coalesce')
mutate(d, 'android_video_decoder.cpp', '  const bool owedCovers = gate_.OwedToken() != 0;', '  const bool owedCovers = false;')

# m_slot_loss_key: the pending slot's loss point taken at the arriving IDR (the bug fixed in C r1)
d = fresh('m_slot_loss_key')
mutate(d, 'android_video_decoder.cpp', 'NoteChainLossLocked(kWantPendingSlot, offeredKey ? heldSeq : offeredSeq);',
       'NoteChainLossLocked(kWantPendingSlot, offeredSeq);')

# m_skip_accepted: the session sends an accepted request without asking the sink
d = fresh('m_skip_accepted')
mutate(d, 'native_video_client_session.cpp', '        if (!sentKey.empty() && encodedFrameSink_) {\r\n          REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkSinkHook, now_us()));\r\n          sentKey.causes =',
       '        if (false && !sentKey.empty() && encodedFrameSink_) {\r\n          REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkSinkHook, now_us()));\r\n          sentKey.causes =')

# m_skip_held: the session hands a held request to the limiter again without asking the sink
d = fresh('m_skip_held')
mutate(d, 'native_video_client_session.cpp', '      if (!held.empty() && encodedFrameSink_) {\r\n        REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkSinkHook, now_us()));\r\n        valid =',
       '      if (false && !held.empty() && encodedFrameSink_) {\r\n        REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkSinkHook, now_us()));\r\n        valid =')
print('ok')
