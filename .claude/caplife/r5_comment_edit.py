base = r'C:\work\gnlink-wt\apk-decoder-liveness\apps'
N = '\r\n'


def edit(rel, reps):
    p = base + '\\' + rel
    s = open(p, 'rb').read().decode('utf-8')
    for a, b in reps:
        a = a.replace('\n', N)
        b = b.replace('\n', N)
        assert s.count(a) == 1, (rel, s.count(a), a[:80])
        s = s.replace(a, b)
    open(p, 'wb').write(s.encode('utf-8'))


edit(r'android_direct_client\app\src\main\cpp\android_video_decoder.cpp', [
("""  // The worker ends first, joined WITHOUT mu_ (it takes mu_ on every pass). Then nothing else touches the codec:
  // what is left is stopped and released here, inline.""",
 """  // The worker ends first, joined WITHOUT mu_ (it takes mu_ on every pass). Then nothing else touches the codec:
  // what is left is stopped and released here, inline -- r5 (Codex src r4): so the destructor, once quiescent, DOES
  // call the codec (stop / delete) and release windows under mu_ on the destroying thread; nobody else can be
  // waiting on mu_ by then, but those calls can be as slow as anywhere else."""),
("""  // again, a key asked for, the deltas after it not fed before the next IDR) and counted apart. The cap is more than
  // one whole second of the highest bitrate the client asks for (kRuntimeBitrateMax 30 Mbit/s = 3.75 MB), so a
  // stream within it never meets it; the assembler itself admits up to 16 MB (kMaxUdpAssembledPayloadBytes).""",
 """  // again, a key asked for, the deltas after it not fed before the next IDR) and counted apart. The cap is more than
  // one whole second of the highest bitrate the client asks for (kRuntimeBitrateMax 30 Mbit/s = 3.75 MB) -- r5
  // (Codex src r4): an AVERAGE, which does NOT bound a single IDR: an encoder may make one IDR larger than 4 MiB at
  // 30 Mbit/s (rate control over a window, a scene of fine detail), and such an IDR is refused here, every time it
  // comes. The assembler itself admits up to 16 MB (kMaxUdpAssembledPayloadBytes)."""),
("""// (CommitQueue). In between the receive side cannot move it (it no longer picks a candidate); a control-side change
// (Prepare / Reset / an answer) makes it stale and always asks for a codec reset too. The contract, per exchange:""",
 """// (CommitQueue). In between the receive side cannot move it (it no longer picks a candidate); a control-side change
// makes it stale. r5 (Codex src r4) -- which of them ask for a codec reset: Prepare, Abort, a stream reset, a new
// binding / unbind (RequestCodecResetLocked with the gate's Prepare / Reset) do; an answer (OnAck), BeginLocal and
// Refuse do NOT -- they only leave the awaiting state, and while awaiting PermitsQueue admits nothing, so no
// reservation can exist for them to make stale. A stale commit with no reset pending is that unreachable case's
// defence (voidExchange: a reset asked for here). The contract, per exchange:"""),
])
edit(r'android_direct_client\app\src\main\cpp\android_video_decoder.hpp', [
("""  // copy during a replay -- at most kInboxMaxBytes + 4 x kInboxMaxBytes = 20 MB of frames in the sink.""",
 """  // copy during a replay -- at most kInboxMaxBytes + 4 x kInboxMaxBytes = 20 MB of frame PAYLOAD in the sink. r5
  // (Codex src r4): a logical payload budget, not a bound on the process's memory -- vector capacity, allocator
  // overhead, the assembler's and the codec's own buffers are outside it."""),
])
edit(r'native_poc\src\selection_ack_gate.hpp', [
("""//     the epoch the frame was reserved in; a stale reservation pays nothing and binds nothing. The decoder asks for
//     a codec reset with every such change, and voids the codec itself if a commit is ever stale without one. So an
//     exchange begun before such a change completes, if at all, into a codec that change has voided; an exchange
//     begun after it is judged against the new state.""",
 """//     the epoch the frame was reserved in; a stale reservation pays nothing and binds nothing. The decoder asks for
//     a codec reset with Prepare and Reset (a new pick, an abandon, a stream reset, a new binding); OnAck, BeginLocal
//     and Refuse only leave the awaiting state, in which PermitsQueue admits nothing -- so no reservation exists for
//     them to make stale -- and a stale commit with no reset pending (that unreachable case) makes the decoder void
//     the codec itself. So an exchange begun before such a change completes, if at all, into a codec the change has
//     voided; an exchange begun after it is judged against the new state."""),
])
print('ok')
