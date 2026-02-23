#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include <d3d11.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wrl/client.h>

namespace remote60::native_poc {

struct H264AccessUnit {
  std::vector<uint8_t> bytes;
  bool keyFrame = false;
  int64_t sampleTimeHns = 0;
};

struct DecodedFrameNv12 {
  uint32_t width = 0;
  uint32_t height = 0;
  int64_t sampleTimeHns = 0;
  bool sampleTimeFromOutput = false;
  std::vector<uint8_t> bytes;
};

bool bgra_to_nv12(const uint8_t* bgra, uint32_t width, uint32_t height, uint32_t bgraStride,
                  std::vector<uint8_t>* outNv12);
bool nv12_to_bgra(const uint8_t* nv12, uint32_t width, uint32_t height, std::vector<uint8_t>* outBgra);

class H264Encoder {
 public:
  H264Encoder() = default;
  ~H264Encoder();

  bool set_d3d11_device(ID3D11Device* device);
  bool initialize(uint32_t width, uint32_t height, uint32_t fps, uint32_t bitrate, uint32_t keyint);
  bool reconfigure_bitrate(uint32_t bitrate);
  bool encode_frame(const std::vector<uint8_t>& nv12, bool forceKeyFrame, int64_t inputSampleTimeHns,
                    std::vector<H264AccessUnit>* outUnits);
  const char* backend_name() const { return backendName_; }
  bool using_hardware() const { return usingHardware_; }
  void shutdown();

 private:
  bool configure_types();
  void apply_low_latency_codec_api();

  Microsoft::WRL::ComPtr<IMFTransform> enc_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint32_t fps_ = 0;
  uint32_t bitrate_ = 0;
  uint32_t keyint_ = 0;
  uint32_t outBufferBytes_ = 0;
  uint64_t frameIndex_ = 0;
  int64_t sampleDurationHns_ = 0;
  std::vector<uint8_t> sequenceHeaderAnnexb_;
  bool started_ = false;
  bool asyncTransform_ = false;
  bool usingHardware_ = false;
  const char* backendName_ = "unknown";
  uint32_t d3dManagerResetToken_ = 0;
  Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> d3dManager_;
  Microsoft::WRL::ComPtr<IMFMediaEventGenerator> eventGenerator_;
};

class H264Decoder {
 public:
  H264Decoder() = default;
  ~H264Decoder();

  bool set_d3d11_device(ID3D11Device* device);
  bool initialize(uint32_t width, uint32_t height, uint32_t fps);
  bool decode_access_unit(const std::vector<uint8_t>& annexb, bool keyFrame,
                          int64_t inputSampleTimeHns,
                          std::vector<DecodedFrameNv12>* outFrames);
  const char* backend_name() const { return backendName_; }
  bool using_hardware() const { return usingHardware_; }
  void reset();
  void shutdown();

 private:
  bool configure_input_type();
  bool configure_output_type();
  bool query_output_size(uint32_t* outWidth, uint32_t* outHeight) const;

  Microsoft::WRL::ComPtr<IMFTransform> dec_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint32_t fps_ = 0;
  uint64_t sampleIndex_ = 0;
  int64_t sampleDurationHns_ = 0;
  bool started_ = false;
  bool outputConfigured_ = false;
  bool usingHardware_ = false;
  const char* backendName_ = "unknown";
  uint64_t missingOutputTimestampCount_ = 0;
  std::deque<int64_t> pendingInputSampleTimesHns_;
  uint32_t d3dManagerResetToken_ = 0;
  Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> d3dManager_;
};

}  // namespace remote60::native_poc
