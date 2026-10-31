C = 'C:/work/gnlink-wt/apk-prelogin-select/apps/android_direct_client/app/src/main/cpp/'

def edit(name, pairs):
    p = C + name
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (name, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

edit('android_video_decoder.hpp', [
    ('''#include "selection_ack_gate.hpp"''', '''#include "decoder_pending_slot.hpp"
#include "selection_ack_gate.hpp"'''),
    ('''  std::optional<remote60::native_poc::UdpH264AssembledFrame> pendingFrame_;''',
     '''  // apk-0232 r2 (N2): the frame held for an input buffer, with the gate asked again when it is queued and no newer
  // frame overtaking it (decoder_pending_slot.hpp, shared with the Windows tests).
  remote60::native_poc::DecoderPendingSlot pendingFrame_;'''),
])

edit('android_video_decoder.cpp', [
    ('''  if (!TryQueueFrameLocked(frame)) {
    if (!pendingFrame_.has_value()) {
      pendingFrame_ = std::move(frame);
      pendingFrameCount_ = 1;
      pendingFrameQueueRetryCount_ = 0;
    } else {
      const bool replacePending =
          ((frame.header.flags & 1u) != 0) || pendingFrame_->payload.empty();
      if (replacePending) {
        // The pending delta is being discarded in favour of this frame.
        if ((pendingFrame_->header.flags & 1u) == 0) decoderKeyframeRequest_ = true;
        pendingFrame_ = std::move(frame);
      } else if ((frame.header.flags & 1u) == 0) {
        // This delta is dropped outright. Everything after it references a picture the
        // decoder will never see, so the stream cannot recover without a fresh IDR.
        decoderKeyframeRequest_ = true;
      }
      pendingFrameCount_ = 1;
    }
  }
  PumpCodecLocked();''', '''  // apk-0232 r2 (N2): queued only if the gate admits its generation at this moment and nothing older is held; else
  // held / replacing the held frame / dropped (an IDR asked for) -- DecoderPendingSlot.
  const bool hadPending = pendingFrame_.has();
  bool keyRequest = false;
  pendingFrame_.Offer(std::move(frame), gate_, [this](remote60::native_poc::UdpH264AssembledFrame& f) {
    return TryQueueFrameLocked(f);
  }, &keyRequest);
  if (keyRequest) decoderKeyframeRequest_ = true;
  if (pendingFrame_.has()) {
    pendingFrameCount_ = 1;
    if (!hadPending) pendingFrameQueueRetryCount_ = 0;
  }
  PumpCodecLocked();'''),
    ('''  status += " pending=" + std::to_string(pendingFrame_.has_value() ? 1 : 0);''',
     '''  status += " pending=" + std::to_string(pendingFrame_.has() ? 1 : 0);
  status += " gateQueueDrops=" + std::to_string(pendingFrame_.gateDrops());  // apk-0232 r2'''),
    ('''  if (pendingFrame_.has_value() && codec_) {
    if (TryQueueFrameLocked(*pendingFrame_)) {
      pendingFrame_.reset();
      pendingFrameCount_ = 0;
      pendingFrameQueueRetryCount_ = 0;
    } else {
      ++pendingFrameQueueRetryCount_;
    }
  }''', '''  if (pendingFrame_.has() && codec_) {
    // apk-0232 r2 (N2): the held frame goes in only if the gate still admits its generation; else it is dropped.
    if (pendingFrame_.Pump(gate_, [this](remote60::native_poc::UdpH264AssembledFrame& f) { return TryQueueFrameLocked(f); })) {
      pendingFrameCount_ = 0;
      pendingFrameQueueRetryCount_ = 0;
    } else {
      ++pendingFrameQueueRetryCount_;
    }
  }'''),
    ('''  if (codec_ &&
      bootstrapFrame_.has_value() &&
      gate_.pending() != 0 &&''', '''  if (codec_ &&
      bootstrapFrame_.has_value() &&
      gate_.PermitsQueue(bootstrapFrame_->header.streamGeneration) &&  // apk-0232 r2 (N2)
      gate_.pending() != 0 &&'''),
    ('''  if (pendingFrame_.has_value() && (pendingFrame_->header.flags & 1u) != 0) {
    gate_.KeyLost(pendingFrame_->header.streamGeneration);
  }
  pendingFrame_.reset();''', '''  if (pendingFrame_.held() && (pendingFrame_.held()->header.flags & 1u) != 0) {
    gate_.KeyLost(pendingFrame_.held()->header.streamGeneration);
  }
  pendingFrame_.Reset();'''),
])
print('ok')
