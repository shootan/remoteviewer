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

  // Coalesce `o` (asked later) into this one. The sink-scoped causes of an older sink epoch are dropped: a newer epoch
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
  }
  void Clear() { *this = DecoderKeyRequest{}; }
};

}  // namespace remote60::native_poc
