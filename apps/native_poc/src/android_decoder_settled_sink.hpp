#pragma once
// TEST ONLY (apk-decoder-liveness r1, Codex codex_apk_decoder_liveness_design_r1.md): the APK's decoder sink hands its
// codec work to a worker of its own and returns at once. The older sink tests (owed key, geometry, decode stats) judge
// the LOGIC -- what reached the stand-in codec, what is owed, what was measured -- right after each call, as they did
// when the caller's thread did the codec work itself. This wraps the PRODUCT sink (android_video_decoder.cpp, unchanged
// by it) so that each call returns only once the worker has taken everything handed over before it and finished a pass
// (WaitIdleForTest, compiled only with REMOTE60_DECODER_TEST_HOOKS). What runs concurrently -- that no caller waits for
// the codec -- is judged apart, by android_decoder_liveness_test.cpp, without this.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "android_video_decoder.hpp"

namespace remote60::android_direct {

class SettledSink : public AndroidVideoDecoderSink {
 public:
  using Base = AndroidVideoDecoderSink;
  using Frame = remote60::native_poc::UdpH264AssembledFrame;

  void Settle() {
    if (WaitIdleForTest(5000)) return;
    std::printf("FAIL  the decoder's worker did not settle within 5 s\nRESULT: FAIL\n");
    std::fflush(stdout);
    std::exit(2);
  }

  void SetSurface(JNIEnv* env, jobject surface) {
    Base::SetSurface(env, surface);
    Settle();
  }
  void OnEncodedH264Frame(Frame&& frame) override {
    Base::OnEncodedH264Frame(std::move(frame));
    Settle();
  }
  void OnVideoStreamReset() override {
    Base::OnVideoStreamReset();
    Settle();
  }
  void OnVideoDiscontinuity() override {
    Base::OnVideoDiscontinuity();
    Settle();
  }
  bool ConsumeDecoderKeyframeRequest() override {
    Settle();
    return Base::ConsumeDecoderKeyframeRequest();
  }
  bool DrainPresentationStats(remote60::native_poc::ClientPresentationStats* out) override {
    Settle();
    return Base::DrainPresentationStats(out);
  }
  bool DrainDecodeStats(remote60::native_poc::ClientDecodeStats* out) override {
    Settle();
    return Base::DrainDecodeStats(out);
  }
  uint64_t DecodeGeneration() override {
    Settle();
    return Base::DecodeGeneration();
  }
  void OnDecodeEpisodeStart() override {
    Base::OnDecodeEpisodeStart();
    Settle();
  }
  void OnWindowSelectionControlResult(const remote60::native_poc::ControlWindowSelectedMessage& msg) override {
    Base::OnWindowSelectionControlResult(msg);
    Settle();
  }
  void OnWindowSelectionControlResultFor(const remote60::native_poc::ControlWindowSelectedMessage& msg,
                                         uint64_t requestTag) override {
    Base::OnWindowSelectionControlResultFor(msg, requestTag);
    Settle();
  }
  uint64_t KeyframeOwedFor() override {
    Settle();
    return Base::KeyframeOwedFor();
  }
  uint64_t CurrentSelectionTag() override {
    Settle();
    return Base::CurrentSelectionTag();
  }
  void PrepareForWindowSelection(uint64_t selectionGeneration) {
    Base::PrepareForWindowSelection(selectionGeneration);
    Settle();
  }
  void AbortWindowSelection() {
    Base::AbortWindowSelection();
    Settle();
  }
  std::string DebugStatus() {
    Settle();
    return Base::DebugStatus();
  }
  uint64_t VideoSizePacked() {
    Settle();
    return Base::VideoSizePacked();
  }
  uint64_t ReadySelectionGeneration() {
    Settle();
    return Base::ReadySelectionGeneration();
  }
  uint64_t LastOutputPresentationUs() {
    Settle();
    return Base::LastOutputPresentationUs();
  }
};

}  // namespace remote60::android_direct
