#pragma once

#include <cstddef>
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
  bool sampleTimeFromOutput = false;
};

struct DecodedFrameNv12 {
  // Coded plane geometry -- the byte-buffer layout. H.264 aligns the coded height to 16
  // rows, so a 1080p stream decodes into a 1088-row plane whose bottom 8 rows are padding.
  uint32_t width = 0;
  uint32_t height = 0;
  // Display aperture -- the pixels that are real content. Always inside the coded plane and
  // even-aligned; equals the coded size when the decoder reports no aperture.
  uint32_t visibleLeft = 0;
  uint32_t visibleTop = 0;
  uint32_t visibleWidth = 0;
  uint32_t visibleHeight = 0;
  int64_t sampleTimeHns = 0;
  bool sampleTimeFromOutput = false;
  std::vector<uint8_t> bytes;
  // Hardware decoders expose their NV12 output as a D3D11 surface. Keeping both the sample
  // and texture alive prevents the decoder pool from reusing the surface before paint.
  Microsoft::WRL::ComPtr<IMFSample> surfaceSample;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> surfaceTexture;
  uint32_t surfaceSubresource = 0;
};

struct H264EncodeFrameStats {
  uint64_t encodeCallUs = 0;
  uint64_t colorConvertUs = 0;
  uint64_t sampleCreateUs = 0;
  uint64_t processInputUs = 0;
  uint64_t processOutputDrainUs = 0;
  uint64_t processOutputDrainLoops = 0;
  uint64_t processOutputSamples = 0;
  uint64_t processOutputBytes = 0;
  uint32_t processInputNotAcceptingCount = 0;
  uint32_t processOutputNeedMoreInputCount = 0;
  uint32_t processOutputStreamChangeCount = 0;
  uint32_t processOutputErrorCount = 0;
  uint32_t asyncPollCount = 0;
  uint32_t asyncPollNoEventCount = 0;
  uint32_t asyncPollNeedInputCount = 0;
  uint32_t asyncPollHaveOutputCount = 0;
  uint8_t asyncEnabled = 0;
};

bool bgra_to_nv12(const uint8_t* bgra, uint32_t width, uint32_t height, uint32_t bgraStride,
                  std::vector<uint8_t>* outNv12);
bool bgra_to_nv12_buffer(const uint8_t* bgra, uint32_t width, uint32_t height,
                         uint32_t bgraStride, uint8_t* outNv12, size_t outNv12Size);
bool nv12_to_bgra(const uint8_t* nv12, uint32_t width, uint32_t height, std::vector<uint8_t>* outBgra);

class H264Encoder {
 public:
  H264Encoder() = default;
  ~H264Encoder();

  bool set_d3d11_device(ID3D11Device* device);
  bool initialize(uint32_t width, uint32_t height, uint32_t fps, uint32_t bitrate, uint32_t keyint);
  bool reconfigure_bitrate(uint32_t bitrate);
  bool encode_frame(const std::vector<uint8_t>& nv12, bool forceKeyFrame, int64_t inputSampleTimeHns,
                    std::vector<H264AccessUnit>* outUnits, H264EncodeFrameStats* encodeStats = nullptr);
  /** Converts BGRA directly into the Media Foundation input buffer, avoiding the temporary
   *  NV12 vector and the full-frame copy performed by encode_frame. */
  bool encode_frame_bgra(const uint8_t* bgra, uint32_t width, uint32_t height,
                         uint32_t bgraStride, bool forceKeyFrame, int64_t inputSampleTimeHns,
                         std::vector<H264AccessUnit>* outUnits,
                         H264EncodeFrameStats* encodeStats = nullptr);
  /** Zero-copy variant: wraps an NV12 texture in a DXGI surface buffer. The texture must not
   *  be written again until the MFT releases it (tracked by the caller). */
  bool encode_frame_surface(ID3D11Texture2D* texture, bool forceKeyFrame,
                            int64_t inputSampleTimeHns, std::vector<H264AccessUnit>* outUnits,
                            H264EncodeFrameStats* encodeStats = nullptr);
  const char* backend_name() const { return backendName_; }
  bool using_hardware() const { return usingHardware_; }
  void shutdown();

 private:
  bool configure_types();
  void apply_low_latency_codec_api();
  bool apply_rate_control(const char* reason);
  bool encode_sample_common(IMFSample* sampleRaw, int64_t sampleTime, bool forceKeyFrame,
                            std::vector<H264AccessUnit>* outUnits,
                            H264EncodeFrameStats* encodeStats, uint64_t encodeCallStartUs);
  void report_sps_profile_once(const uint8_t* data, size_t size);

  Microsoft::WRL::ComPtr<IMFTransform> enc_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint32_t fps_ = 0;
  uint32_t bitrate_ = 0;
  uint32_t keyint_ = 0;
  uint32_t outBufferBytes_ = 0;
  uint64_t frameIndex_ = 0;
  int64_t sampleDurationHns_ = 0;
  // Hardware/async MFTs can return one or more older outputs while accepting the current
  // input. Keep the accepted input timeline so each output is stamped with the frame that
  // actually produced it, rather than the input from the current encode call.
  std::deque<int64_t> pendingInputSampleTimesHns_;
  std::vector<uint8_t> sequenceHeaderAnnexb_;
  bool spsProfileReported_ = false;
  bool started_ = false;
  bool asyncTransform_ = false;
  bool usingHardware_ = false;
  bool stableTextTune_ = false;
  const char* backendName_ = "unknown";
  uint64_t sampleTimeOutputTimestampTotalSamples_ = 0;
  uint64_t sampleTimeOutputTimestampFallbackCount_ = 0;
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
                          std::vector<DecodedFrameNv12>* outFrames,
                          bool* outPendingTimestampOverflow = nullptr);
  const char* backend_name() const { return backendName_; }
  bool using_hardware() const { return usingHardware_; }
  void reset();
  void shutdown();

 private:
  bool configure_input_type();
  bool configure_output_type();
  bool configure_surface_allocator(IMFMediaType* outputType);
  bool query_output_size(uint32_t* outWidth, uint32_t* outHeight) const;
  bool query_output_geometry(uint32_t* codedWidth, uint32_t* codedHeight, uint32_t* visibleLeft,
                             uint32_t* visibleTop, uint32_t* visibleWidth,
                             uint32_t* visibleHeight) const;

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
  Microsoft::WRL::ComPtr<IMFVideoSampleAllocatorEx> videoAllocator_;
};

}  // namespace remote60::native_poc
