p = r'C:\work\gnlink-wt\apk-inbox-c\apps\native_poc\src\decoder_key_request.hpp'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')
a = '''  // Coalesce `o` (asked later) into this one. The sink-scoped causes of an older sink epoch are dropped: a newer epoch
  // means the sink has moved on, so they could not be valid any more (the newer ask carries the current state).
  void Merge(const DecoderKeyRequest& o) {
    constexpr uint32_t kSinkScoped = kKeyCauseRecovery | kKeyCauseCodecUp | kKeyCauseSelectionAbort;
    if ((o.causes & kSinkScoped) != 0) {
      if ((causes & kSinkScoped) != 0 && o.sinkEpoch != sinkEpoch) causes &= ~kSinkScoped;
      sinkEpoch = o.sinkEpoch;
      if ((o.causes & kKeyCauseRecovery) != 0) episodeId = o.episodeId;
    }
    if ((o.causes & kKeyCauseOwed) != 0) owedToken = o.owedToken;
    causes |= o.causes;
  }'''
b = '''  // Coalesce `o` into this one. C r3 (Codex codex_apk_inbox_c_src_r2.md N2): the order two asks are PUBLISHED in is not
  // the order the sink ISSUED them -- each producer (RX, the control pass) takes its descriptor outside the ledger lock,
  // so an older one can be published after a newer one. The sink's own issue order decides: its key epoch (moves
  // forward at every change), within one epoch the recovery episode, and the owed duty's token (both only grow). The
  // NEWER side's identity is kept; the OLDER side's causes of that kind are dropped (they could not be valid any more)
  // -- never moved onto the newer epoch / episode / token. Session-scoped causes (pipeline, legacy) are always kept.
  void Merge(const DecoderKeyRequest& o) {
    constexpr uint32_t kSinkScoped = kKeyCauseRecovery | kKeyCauseCodecUp | kKeyCauseSelectionAbort;
    uint32_t in = o.causes;
    if ((in & kSinkScoped) != 0) {
      if ((causes & kSinkScoped) == 0 || o.sinkEpoch > sinkEpoch) {
        causes &= ~kSinkScoped;  // ours (if any) are of an older epoch
        sinkEpoch = o.sinkEpoch;
        episodeId = (in & kKeyCauseRecovery) != 0 ? o.episodeId : 0;
      } else if (o.sinkEpoch < sinkEpoch) {
        in &= ~kSinkScoped;  // theirs are of an older epoch: dropped, not promoted
      } else if ((in & kKeyCauseRecovery) != 0) {  // the same epoch: the newer recovery episode
        if ((causes & kKeyCauseRecovery) == 0 || o.episodeId > episodeId) {
          episodeId = o.episodeId;
        } else if (o.episodeId < episodeId) {
          in &= ~kKeyCauseRecovery;
        }
      }
    }
    if ((in & kKeyCauseOwed) != 0) {
      if ((causes & kKeyCauseOwed) == 0 || o.owedToken > owedToken) {
        owedToken = o.owedToken;
      } else if (o.owedToken < owedToken) {
        in &= ~kKeyCauseOwed;  // an older duty's token: dropped
      }
    }
    causes |= in;
  }'''
assert s.count(a) == 1
s = s.replace(a, b)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
