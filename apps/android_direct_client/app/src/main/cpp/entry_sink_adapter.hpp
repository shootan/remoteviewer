#pragma once

// keepalive-multi r1: one PC connection's view of the one decoder. Each PC's ClientSessionController is given its own
// adapter instead of the decoder itself; the adapter carries the binding token that PC was put on screen with (0 = not
// on screen) and passes it on every call, both directions -- frames, resets, discontinuities, the decode episode,
// selection answers / tags, the key request and owed key, the decode generation and the stats drains. The decoder
// compares the token with its current binding under its own lock (AndroidVideoDecoderSink::*As), so a parked or closed
// PC's late callback -- arriving after another PC was bound -- does nothing to the picture on screen.

#include <atomic>
#include <cstdint>

#include "android_video_decoder.hpp"
#include "native_video_client_session.hpp"

namespace remote60::android_direct {

class EntrySinkAdapter : public remote60::native_poc::ClientEncodedFrameSink {
 public:
  explicit EntrySinkAdapter(AndroidVideoDecoderSink* decoder) : decoder_(decoder) {}

  // The token this PC is bound with now (0 = not bound: every call is a no-op without touching the decoder).
  void SetToken(uint64_t token) { token_.store(token, std::memory_order_release); }
  uint64_t Token() const { return token_.load(std::memory_order_acquire); }

  void OnEncodedH264Frame(remote60::native_poc::UdpH264AssembledFrame&& frame) override {
    if (const uint64_t t = Token()) decoder_->OnEncodedH264FrameAs(t, std::move(frame));
  }
  void OnVideoStreamReset() override {
    if (const uint64_t t = Token()) decoder_->OnVideoStreamResetAs(t);
  }
  void OnVideoDiscontinuity() override {
    if (const uint64_t t = Token()) decoder_->OnVideoDiscontinuityAs(t);
  }
  bool ConsumeDecoderKeyframeRequest() override {
    const uint64_t t = Token();
    return t != 0 && decoder_->ConsumeDecoderKeyframeRequestAs(t);
  }
  // apk-inbox C: the want with its descriptor, its validity and its send -- for this PC's binding only (a PC not on
  // screen asks nothing; its held / accepted requests keep only their session-scoped causes).
  bool TakeDecoderKeyRequest(remote60::native_poc::DecoderKeyRequest* out) override {
    const uint64_t t = Token();
    return t != 0 && decoder_->TakeDecoderKeyRequestAs(t, out);
  }
  uint32_t DecoderKeyRequestValidCauses(const remote60::native_poc::DecoderKeyRequest& request) override {
    const uint64_t t = Token();
    return t != 0 ? decoder_->DecoderKeyRequestValidCausesAs(t, request)
                  : request.causes & (remote60::native_poc::kKeyCausePipeline | remote60::native_poc::kKeyCauseLegacy);
  }
  void OnDecoderKeyRequestSent(const remote60::native_poc::DecoderKeyRequest& request, uint64_t nowUs) override {
    if (const uint64_t t = Token()) decoder_->OnDecoderKeyRequestSentAs(t, request, nowUs);
  }
  void OnDecoderKeyOwedExhausted(uint64_t owedToken, uint64_t nowUs) override {  // C r2 §8
    if (const uint64_t t = Token()) decoder_->OnDecoderKeyOwedExhaustedAs(t, owedToken, nowUs);
  }
  bool DrainPresentationStats(remote60::native_poc::ClientPresentationStats* out) override {
    const uint64_t t = Token();
    return t != 0 && decoder_->DrainPresentationStatsAs(t, out);
  }
  bool DrainDecodeStats(remote60::native_poc::ClientDecodeStats* out) override {
    const uint64_t t = Token();
    return t != 0 && decoder_->DrainDecodeStatsAs(t, out);
  }
  uint64_t DecodeGeneration() override {
    const uint64_t t = Token();
    return t != 0 ? decoder_->DecodeGenerationAs(t) : 0;
  }
  void OnDecodeEpisodeStart() override {
    if (const uint64_t t = Token()) decoder_->OnDecodeEpisodeStartAs(t);
  }
  void OnWindowSelectionControlResult(const remote60::native_poc::ControlWindowSelectedMessage& msg) override {
    if (const uint64_t t = Token()) decoder_->OnWindowSelectionControlResultAs(t, msg);
  }
  uint64_t CurrentSelectionTag() override {
    const uint64_t t = Token();
    return t != 0 ? decoder_->CurrentSelectionTagAs(t) : 0;
  }
  uint64_t KeyframeOwedFor() override {
    const uint64_t t = Token();
    return t != 0 ? decoder_->KeyframeOwedForAs(t) : 0;
  }
  void OnWindowSelectionControlResultFor(const remote60::native_poc::ControlWindowSelectedMessage& msg,
                                         uint64_t requestTag) override {
    if (const uint64_t t = Token()) decoder_->OnWindowSelectionControlResultForAs(t, msg, requestTag);
  }
  // apk-0232 local selection (keepalive-r2 merge): only for the PC on screen; a PC not bound selects nothing.
  bool BeginLocalSelectionFor(uint64_t tag) override {
    const uint64_t t = Token();
    return t != 0 && decoder_->BeginLocalSelectionForAs(t, tag);
  }
  uint64_t LocalSelectionBoundFor(uint64_t tag) override {
    const uint64_t t = Token();
    return t != 0 ? decoder_->LocalSelectionBoundForAs(t, tag) : 0;
  }
  void RefuseSelectionFor(uint64_t tag) override {
    if (const uint64_t t = Token()) decoder_->RefuseSelectionForAs(t, tag);
  }
  LocalCommit CommitLocalSelectionFor(uint64_t tag, const std::function<void(uint64_t generation)>& commit) override {
    const uint64_t t = Token();
    return t != 0 ? decoder_->CommitLocalSelectionForAs(t, tag, commit) : LocalCommit::Abandoned;
  }

 private:
  AndroidVideoDecoderSink* decoder_;
  std::atomic<uint64_t> token_{0};
};

}  // namespace remote60::android_direct
