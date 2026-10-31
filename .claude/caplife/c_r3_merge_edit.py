SRC = r'C:\work\gnlink-wt\apk-inbox-c\apps\native_poc\src'


def load(p):
    raw = open(p, 'rb').read()
    return raw.decode('utf-8').replace('\r\n', '\n'), b'\r\n' in raw


def save(p, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


def rep(s, a, b):
    assert s.count(a) == 1, (a[:90], s.count(a))
    return s.replace(a, b)


p = SRC + r'\decoder_key_request.hpp'
s, crlf = load(p)
s = rep(s, '''  // Coalesce `o` (asked later) into this one. The sink-scoped causes of an older sink epoch are dropped: a newer epoch
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
  }''', '''  // Coalesce `o` into this one. C r3 (Codex codex_apk_inbox_c_src_r2.md N2): the order two asks are PUBLISHED in is not
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
  }''')
save(p, s, crlf)

p = SRC + r'\native_video_client_session.hpp'
s, crlf = load(p)
s = rep(s, '''  // TEST BUILD ONLY (apk-inbox C r2): one key request with these causes, through the same section every path uses.''',
        '''  // TEST BUILD ONLY (apk-inbox C r3): a whole descriptor (epoch / episode / token as a producer took it) published
  // through the same section every path uses -- e.g. an older one published after a newer one.
  bool TestPublishKey(const DecoderKeyRequest& r) {
    return RequestKey(r, static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                   std::chrono::steady_clock::now().time_since_epoch()).count()));
  }
  // TEST BUILD ONLY (apk-inbox C r2): one key request with these causes, through the same section every path uses.''')
save(p, s, crlf)
print('ok')
