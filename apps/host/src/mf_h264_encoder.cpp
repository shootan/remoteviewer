#include "mf_h264_encoder.hpp"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mftransform.h>
#include <codecapi.h>
#include <wmcodecdsp.h>

namespace remote60::host {
namespace {

bool ConfigureOutputType(IMFTransform* encoder, uint32_t& outCount) {
  outCount = 0;
  for (DWORD i = 0;; ++i) {
    IMFMediaType* t = nullptr;
    HRESULT hr = encoder->GetOutputAvailableType(0, i, &t);
    if (hr == MF_E_NO_MORE_TYPES) return false;
    if (FAILED(hr) || !t) continue;
    ++outCount;

    GUID subtype{};
    hr = t->GetGUID(MF_MT_SUBTYPE, &subtype);
    if (SUCCEEDED(hr) && subtype == MFVideoFormat_H264) {
      MFSetAttributeSize(t, MF_MT_FRAME_SIZE, 1920, 1080);
      MFSetAttributeRatio(t, MF_MT_FRAME_RATE, 60, 1);
      t->SetUINT32(MF_MT_AVG_BITRATE, 12 * 1000 * 1000);
      t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
      hr = encoder->SetOutputType(0, t, 0);
      t->Release();
      if (SUCCEEDED(hr)) return true;
      continue;
    }

    t->Release();
  }
}

bool ConfigureInputType(IMFTransform* encoder, uint32_t& outCount) {
  outCount = 0;
  for (DWORD i = 0;; ++i) {
    IMFMediaType* t = nullptr;
    HRESULT hr = encoder->GetInputAvailableType(0, i, &t);
    if (hr == MF_E_NO_MORE_TYPES) return false;
    if (FAILED(hr) || !t) continue;
    ++outCount;

    GUID subtype{};
    hr = t->GetGUID(MF_MT_SUBTYPE, &subtype);
    if (SUCCEEDED(hr) && subtype == MFVideoFormat_NV12) {
      MFSetAttributeSize(t, MF_MT_FRAME_SIZE, 1920, 1080);
      MFSetAttributeRatio(t, MF_MT_FRAME_RATE, 60, 1);
      MFSetAttributeRatio(t, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
      t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
      hr = encoder->SetInputType(0, t, 0);
      t->Release();
      if (SUCCEEDED(hr)) return true;
      continue;
    }

    t->Release();
  }
}

bool ApplyCodecParameters(IMFTransform* encoder, MfEncoderProbeStats& out) {
  ICodecAPI* codecApi = nullptr;
  HRESULT hr = encoder->QueryInterface(IID_PPV_ARGS(&codecApi));
  if (FAILED(hr) || !codecApi) return false;

  VARIANT v;
  VariantInit(&v);

  v.vt = VT_UI4;
  v.ulVal = out.bitrateKbps * 1000;
  out.codecApiBitrateSet = SUCCEEDED(codecApi->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &v));

  v.vt = VT_UI4;
  v.ulVal = out.gopFrames;
  out.codecApiGopSet = SUCCEEDED(codecApi->SetValue(&CODECAPI_AVEncMPVGOPSize, &v));

  v.vt = VT_BOOL;
  v.boolVal = VARIANT_TRUE;
  out.codecApiLowLatencySet = SUCCEEDED(codecApi->SetValue(&CODECAPI_AVLowLatencyMode, &v));

  VariantClear(&v);
  codecApi->Release();
  return out.codecApiBitrateSet || out.codecApiGopSet || out.codecApiLowLatencySet;
}

}  // namespace

MfEncoderProbeStats run_mf_h264_encoder_probe() {
  MfEncoderProbeStats out;

  HRESULT hr = MFStartup(MF_VERSION);
  if (FAILED(hr)) {
    out.detail = "mf_startup_failed";
    return out;
  }
  out.mfStartup = true;

  IMFTransform* encoder = nullptr;
  hr = CoCreateInstance(CLSID_CMSH264EncoderMFT, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&encoder));
  if (FAILED(hr) || !encoder) {
    out.detail = "h264_mft_create_failed";
    MFShutdown();
    return out;
  }
  out.mftCreated = true;

  out.outputTypeSet = ConfigureOutputType(encoder, out.outputTypeCount);
  out.inputTypeSet = out.outputTypeSet ? ConfigureInputType(encoder, out.inputTypeCount) : false;
  if (out.inputTypeSet && out.outputTypeSet) {
    ApplyCodecParameters(encoder, out);
  }

  if (out.inputTypeSet && out.outputTypeSet) {
    out.beginStreaming = SUCCEEDED(encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
    out.startOfStream = SUCCEEDED(encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));

    MFT_OUTPUT_STREAM_INFO osi{};
    HRESULT hrInfo = encoder->GetOutputStreamInfo(0, &osi);
    IMFMediaBuffer* outBuf = nullptr;
    IMFSample* outSample = nullptr;
    if (SUCCEEDED(hrInfo) && SUCCEEDED(MFCreateMemoryBuffer(osi.cbSize > 0 ? osi.cbSize : 1 << 20, &outBuf)) &&
        SUCCEEDED(MFCreateSample(&outSample))) {
      outSample->AddBuffer(outBuf);
      MFT_OUTPUT_DATA_BUFFER odb{};
      odb.dwStreamID = 0;
      odb.pSample = outSample;
      DWORD status = 0;
      HRESULT hrOut = encoder->ProcessOutput(0, 1, &odb, &status);
      out.processOutputNeedMoreInput = (hrOut == MF_E_TRANSFORM_NEED_MORE_INPUT);
      if (odb.pEvents) odb.pEvents->Release();
    }
    if (outBuf) outBuf->Release();
    if (outSample) outSample->Release();
  }

  out.detail = (out.inputTypeSet && out.outputTypeSet) ? "ok" : "h264_mft_type_config_failed";

  encoder->Release();
  MFShutdown();
  return out;
}

}  // namespace remote60::host
