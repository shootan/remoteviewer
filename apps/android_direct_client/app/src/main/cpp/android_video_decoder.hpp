#pragma once

#include <jni.h>

#include <mutex>
#include <string>
#include <vector>

#include "native_video_client_session.hpp"

struct ANativeWindow;
struct AMediaCodec;

namespace remote60::android_direct {

class AndroidVideoDecoderSink : public remote60::native_poc::ClientEncodedFrameSink {
 public:
  AndroidVideoDecoderSink() = default;
  ~AndroidVideoDecoderSink() override;

  void SetSurface(JNIEnv* env, jobject surface);
  void OnEncodedH264Frame(remote60::native_poc::UdpH264AssembledFrame&& frame) override;
  void OnVideoStreamReset() override;
  void OnWindowSelectionControlResult(
      const remote60::native_poc::ControlWindowSelectedMessage& msg) override;
  void PrepareForWindowSelection(uint64_t selectionGeneration);
  void AbortWindowSelection();
  std::string DebugStatus();
  uint64_t VideoSizePacked();
  uint64_t ReadySelectionGeneration();
  uint64_t LastOutputPresentationUs();

 private:
  bool EnsureCodecLocked(uint32_t width, uint32_t height);
  void ResetCodecLocked();
  void ReleaseSurfaceLocked();
  bool UpdateCodecConfigLocked(const std::vector<uint8_t>& annexb);
  void UpdateOutputFormatLocked();
  void DrainOutputLocked();

  std::mutex mu_;
  ANativeWindow* window_ = nullptr;
  AMediaCodec* codec_ = nullptr;
  uint32_t configuredWidth_ = 0;
  uint32_t configuredHeight_ = 0;
  uint32_t outputWidth_ = 0;
  uint32_t outputHeight_ = 0;
  uint64_t inputFrameCount_ = 0;
  uint64_t outputFrameCount_ = 0;
  uint64_t pendingSelectionGeneration_ = 0;
  uint64_t readySelectionGeneration_ = 0;
  uint64_t expectedStreamGeneration_ = 0;
  uint64_t latestInputStreamGeneration_ = 0;
  uint64_t latestOutputStreamGeneration_ = 0;
  uint64_t lastOutputPresentationUs_ = 0;
  uint64_t staleFrameDropCount_ = 0;
  bool awaitingSelectionAck_ = false;
  std::vector<uint8_t> csd0_;
  std::vector<uint8_t> csd1_;
};

}  // namespace remote60::android_direct
