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
  // Provenance of the INPUT that produced this AU (host kick / static refresh = synthetic). Carried
  // through the accepted-input FIFO next to the timestamp, because an async MFT returns an older
  // input's AU during the current call -- the call's own flag would land on the wrong AU. (0.2.97)
  bool synthetic = false;
  // The host's flush epoch the input was accepted in (0 = untagged). Same FIFO: lets the emit
  // stage tell a pre-flush AU from the first AU of the new epoch (P11, host_epoch_gate.hpp).
  uint64_t inputEpoch = 0;
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
  // Provenance of the input AU this output came from (wire synthetic flag), mapped through the
  // pending-input FIFO like the timestamp, so a delayed decoder output keeps its own flag. (0.2.97)
  bool synthetic = false;
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
  // Diagnostic for the async output-starvation wedge: this call saw MFT events but none were
  // METransformHaveOutput. Repeated while input keeps being accepted is the wedge fingerprint.
  uint32_t asyncNeedInputOnlyCall = 0;
  // Held input-timestamp FIFO depth after this call, and the cumulative count of FIFO overflow
  // drops. A depth pinned near the 64 cap with output stalled is decisive starvation evidence.
  uint32_t pendingInputDepth = 0;
  uint64_t pendingInputOverflowTotal = 0;
  // A06: the accepted-input FIFO overflowed and no output carries trustworthy provenance until
  // the encoder is rebuilt. The stage closes the emit gate and asks for that rebuild.
  uint8_t provenanceInvalid = 0;
  uint8_t asyncEnabled = 0;
};

bool bgra_to_nv12(const uint8_t* bgra, uint32_t width, uint32_t height, uint32_t bgraStride,
                  std::vector<uint8_t>* outNv12);
bool bgra_to_nv12_buffer(const uint8_t* bgra, uint32_t width, uint32_t height,
                         uint32_t bgraStride, uint8_t* outNv12, size_t outNv12Size);
bool nv12_to_bgra(const uint8_t* nv12, uint32_t width, uint32_t height, std::vector<uint8_t>* outBgra);

// One entry per input the encoder's MFT accepted, in order (A02). An asynchronous MFT answers a
// call with an OLDER input's access unit, so every output is stamped from the front of this queue
// rather than from the current call: the capture timestamp the viewer paces on, the synthetic flag
// the wire carries, and the flush epoch the emit gate judges (host_epoch_gate.hpp). The three used
// to be three parallel deques and one path (an output with no usable bytes) popped only two of
// them, which shifted every later AU's epoch by one for the rest of the session -- so they are one
// record now and every path moves them together.
struct PendingInput {
  int64_t tsHns = 0;
  bool synthetic = false;
  uint64_t epoch = 0;
};

// The FIFO itself, separate from the codec so the rules are testable without an MFT.
//
// Overflow (A06): 64 accepted inputs with no output means the queue no longer describes what the
// MFT will emit. Dropping the oldest entry would silently re-label later outputs with a NEWER
// input's provenance -- including its epoch, which can open the emit gate for a pre-flush picture.
// Instead the FIFO latches `provenance invalid`: it empties, every later output is stamped epoch 0
// (unknown -> the gate fails closed), and only a successful encoder rebuild (Reset, from
// H264Encoder::initialize) clears the latch, because only a rebuild also empties what the MFT is
// still holding. Clear() -- the shutdown half of a rebuild -- deliberately keeps the latch.
class PendingInputFifo {
 public:
  static constexpr size_t kMaxEntries = 64;

  void Push(const PendingInput& in) {
    entries_.push_back(in);
    if (entries_.size() <= kMaxEntries) return;
    ++overflowTotal_;
    ++fallbackTotal_;
    provenanceInvalid_ = true;
    entries_.clear();
  }

  // An output that carries bytes: takes the provenance of the input that produced it. False when
  // the queue is empty (no provenance: the caller stamps epoch 0 and counts a fallback).
  bool Pop(PendingInput* out) {
    if (entries_.empty()) {
      ++fallbackTotal_;
      return false;
    }
    if (out) *out = entries_.front();
    entries_.pop_front();
    return true;
  }

  // An output with no usable bytes: the MFT consumed an input for it all the same, so its entry
  // goes too -- all three fields at once (the 0.2.97 fix, and the epoch the pre-A02 code forgot).
  void PopForEmptyOutput() {
    if (entries_.empty()) return;
    entries_.pop_front();
    ++fallbackTotal_;
  }

  // True while no output may be trusted with provenance (see the overflow note above).
  bool provenance_invalid() const { return provenanceInvalid_; }
  size_t size() const { return entries_.size(); }
  uint64_t overflow_total() const { return overflowTotal_; }
  uint64_t fallback_total() const { return fallbackTotal_; }
  void NoteFallback() { ++fallbackTotal_; }

  // shutdown(): the queue is meaningless without the MFT, but the latch survives -- a rebuild is
  // only complete when initialize() succeeds.
  void Clear() { entries_.clear(); }
  // A successful initialize(): a fresh MFT holds nothing, so provenance starts trustworthy again.
  void Reset() {
    entries_.clear();
    provenanceInvalid_ = false;
  }

 private:
  std::deque<PendingInput> entries_;
  bool provenanceInvalid_ = false;
  uint64_t overflowTotal_ = 0;
  uint64_t fallbackTotal_ = 0;
};

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
  // Provenance of the NEXT input (kick / static refresh = true); rides the accepted-input FIFO so
  // each AU reports the flag of the input that produced it. (0.2.97)
  void set_next_input_synthetic(bool synthetic) { nextInputSynthetic_ = synthetic; }
  // Flush epoch of the NEXT input (CaptureState::inputEpoch); same FIFO, same reason. (P11)
  void set_next_input_epoch(uint64_t epoch) { nextInputEpoch_ = epoch; }
  // A06: the accepted-input FIFO overflowed, so nothing this encoder emits may be trusted with
  // provenance (every AU goes out tagged epoch 0, which the emit gate refuses). Only a rebuild --
  // shutdown() followed by a successful initialize() -- clears it.
  bool provenance_invalid() const { return pendingInputs_.provenance_invalid(); }
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
  PendingInputFifo pendingInputs_;
  bool nextInputSynthetic_ = false;
  uint64_t nextInputEpoch_ = 0;
  std::vector<uint8_t> sequenceHeaderAnnexb_;
  bool spsProfileReported_ = false;
  bool started_ = false;
  bool asyncTransform_ = false;
  bool usingHardware_ = false;
  bool stableTextTune_ = false;
  const char* backendName_ = "unknown";
  uint64_t sampleTimeOutputTimestampTotalSamples_ = 0;
  uint64_t sampleTimeOutputTimestampFallbackCount_ = 0;
  uint64_t pendingInputOverflowTotal_ = 0;
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
  // Provenance of the NEXT input AU (wire synthetic flag); mapped to the decoded frame through the
  // pending-input FIFO like its timestamp. (0.2.97)
  void set_next_input_synthetic(bool synthetic) { nextInputSynthetic_ = synthetic; }
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
  std::deque<bool> pendingInputSynthetic_;  // lockstep with pendingInputSampleTimesHns_ (0.2.97)
  bool nextInputSynthetic_ = false;
  uint32_t d3dManagerResetToken_ = 0;
  Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> d3dManager_;
  Microsoft::WRL::ComPtr<IMFVideoSampleAllocatorEx> videoAllocator_;
};

}  // namespace remote60::native_poc
