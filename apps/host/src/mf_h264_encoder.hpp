#pragma once

#include <cstdint>
#include <string>

namespace remote60::host {

struct MfEncoderProbeStats {
  bool mfStartup = false;
  bool mftCreated = false;
  bool inputTypeSet = false;
  bool outputTypeSet = false;
  bool codecApiBitrateSet = false;
  bool codecApiGopSet = false;
  bool codecApiLowLatencySet = false;
  bool beginStreaming = false;
  bool startOfStream = false;
  bool processOutputNeedMoreInput = false;
  uint32_t inputTypeCount = 0;
  uint32_t outputTypeCount = 0;
  uint32_t bitrateKbps = 12000;
  uint32_t gopFrames = 60;
  std::string detail;
};

MfEncoderProbeStats run_mf_h264_encoder_probe();

}  // namespace remote60::host
