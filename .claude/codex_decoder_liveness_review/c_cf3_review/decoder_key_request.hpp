#pragma once

// apk-inbox C (design caplife/design_apk_decoder_inbox_r2.md r3, §3-§4 D8): one logical "ask the host for an IDR", as
// the decoder sink hands it to the session and as the session keeps it while the request waits -- HELD (the limiter
// refused it; asked again on the control loop) or ACCEPTED (the limiter took it; the scheduler will turn it into a
// KeyframeRequest action). Before either moves on (held -> the limiter again, accepted -> the control send) the session
// asks the sink which of its causes are still valid; a request whose causes have all gone is not sent. Nothing here is
// on the wire: the host still receives the same KeyframeRequest message.
//
// Causes are a SET: requests coalesced into one pending send keep every cause (r3 보충: an owed duty and an inbox
// recovery merged into one request lose neither), and one cause going stale does not cancel the others.

#include <cstdint>

namespace remote60::native_poc {

enum DecoderKeyCause : uint32_t {
  kKeyCauseRecovery = 1u << 0,         // the sink's recovery episode (a chain loss: inbox, gap, await-key, slot)
  kKeyCauseCodecUp = 1u << 1,          // a codec made after a failure (r4 N3)
  kKeyCauseSelectionAbort = 1u << 2,   // a switch abandoned while the stream stays active (mobile-abr r5)
  kKeyCauseOwed = 1u << 3,             // the selection's owed IDR (session: KeyframeOwedFor, bounded 8)
  kKeyCausePipeline = 1u << 4,         // the session's own assembler asked (transport loss): no sink state
  kKeyCauseLegacy = 1u << 5,           // a sink that only answers ConsumeDecoderKeyframeRequest (no descriptor)
};

struct DecoderKeyRequest {
  uint32_t causes = 0;
  // The sink's state when it asked: its key epoch (moves at every bind / unbind / pick / abort / stream reset / new
  // surface -- anything that makes an earlier ask moot) and, for kKeyCauseRecovery, the episode it belongs to.
  uint64_t sinkEpoch = 0;
  uint64_t episodeId = 0;
  uint64_t owedToken = 0;  // kKeyCauseOwed: the duty's token (KeyframeOwedFor)

  bool empty() const { return causes == 0; }

  // Coalesce `o` into this one. C r3 (Codex codex_apk_inbox_c_src_r2.md N2): the order two asks are PUBLISHED in is not
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
  }
  void Clear() { *this = DecoderKeyRequest{}; }
};

}  // namespace remote60::native_poc
