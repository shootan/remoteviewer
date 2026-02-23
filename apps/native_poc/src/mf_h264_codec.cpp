#include "mf_h264_codec.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mftransform.h>
#include <wmcodecdsp.h>

namespace remote60::native_poc {
namespace {

int clamp_u8_int(int v) {
  return std::min(255, std::max(0, v));
}

bool set_codecapi_u32(IMFTransform* transform, const GUID& key, uint32_t value) {
  if (!transform) return false;
  Microsoft::WRL::ComPtr<ICodecAPI> codecApi;
  if (FAILED(transform->QueryInterface(IID_PPV_ARGS(&codecApi))) || !codecApi) return false;
  VARIANT v{};
  v.vt = VT_UI4;
  v.ulVal = value;
  return SUCCEEDED(codecApi->SetValue(&key, &v));
}

bool set_codecapi_bool(IMFTransform* transform, const GUID& key, bool value) {
  if (!transform) return false;
  Microsoft::WRL::ComPtr<ICodecAPI> codecApi;
  if (FAILED(transform->QueryInterface(IID_PPV_ARGS(&codecApi))) || !codecApi) return false;
  VARIANT v{};
  v.vt = VT_BOOL;
  v.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
  return SUCCEEDED(codecApi->SetValue(&key, &v));
}

bool set_mf_attr_u32(IMFTransform* transform, const GUID& key, uint32_t value) {
  if (!transform) return false;
  Microsoft::WRL::ComPtr<IMFAttributes> attrs;
  if (FAILED(transform->GetAttributes(&attrs)) || !attrs) return false;
  return SUCCEEDED(attrs->SetUINT32(key, value));
}

uint32_t low_latency_vbv_bytes(uint32_t bitrate) {
  // Keep VBV short for interactive remote rendering to minimize encoder queue depth.
  const uint32_t minBytes = 8192;
  const uint32_t maxBytes = std::max<uint32_t>(32768, bitrate / 8);
  const uint32_t target = std::max<uint32_t>(minBytes, bitrate / 80);
  return std::min<uint32_t>(maxBytes, target);
}

bool annexb_contains_idr(const uint8_t* data, size_t size) {
  if (!data || size < 5) return false;
  for (size_t i = 0; i + 4 < size; ++i) {
    const bool start3 = (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1);
    const bool start4 = (i + 4 < size && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 &&
                         data[i + 3] == 1);
    if (!start3 && !start4) continue;
    const size_t nalPos = start4 ? (i + 4) : (i + 3);
    if (nalPos >= size) continue;
    const uint8_t nalType = (data[nalPos] & 0x1F);
    if (nalType == 5) return true;
  }
  return false;
}

bool h264_has_start_code(const uint8_t* data, size_t size) {
  if (!data || size < 4) return false;
  for (size_t i = 0; i + 3 < size; ++i) {
    if (data[i] == 0 && data[i + 1] == 0 &&
        ((data[i + 2] == 1) || (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1))) {
      return true;
    }
  }
  return false;
}

bool h264_avcc_to_annexb(const std::vector<uint8_t>& in, std::vector<uint8_t>* out) {
  if (!out) return false;
  out->clear();
  if (in.size() < 5) return false;
  size_t offset = 0;
  while (offset + 4 <= in.size()) {
    const uint32_t nalLen = (static_cast<uint32_t>(in[offset]) << 24) |
                            (static_cast<uint32_t>(in[offset + 1]) << 16) |
                            (static_cast<uint32_t>(in[offset + 2]) << 8) |
                            static_cast<uint32_t>(in[offset + 3]);
    offset += 4;
    if (nalLen == 0 || offset + nalLen > in.size()) {
      out->clear();
      return false;
    }
    out->push_back(0);
    out->push_back(0);
    out->push_back(0);
    out->push_back(1);
    out->insert(out->end(), in.data() + offset, in.data() + offset + nalLen);
    offset += nalLen;
  }
  return !out->empty();
}

bool h264_config_avcc_to_annexb(const uint8_t* data, size_t size, std::vector<uint8_t>* out) {
  if (!data || size < 7 || !out) return false;
  out->clear();
  // AVCDecoderConfigurationRecord
  size_t offset = 0;
  const uint8_t configurationVersion = data[offset++];
  if (configurationVersion != 1) return false;
  offset += 3;  // profile/compat/level
  if (offset >= size) return false;
  offset += 1;  // lengthSizeMinusOne
  if (offset >= size) return false;
  const uint8_t numSps = static_cast<uint8_t>(data[offset++] & 0x1F);
  for (uint8_t i = 0; i < numSps; ++i) {
    if (offset + 2 > size) return false;
    const uint16_t n = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
    offset += 2;
    if (n == 0 || offset + n > size) return false;
    out->push_back(0);
    out->push_back(0);
    out->push_back(0);
    out->push_back(1);
    out->insert(out->end(), data + offset, data + offset + n);
    offset += n;
  }
  if (offset >= size) return false;
  const uint8_t numPps = data[offset++];
  for (uint8_t i = 0; i < numPps; ++i) {
    if (offset + 2 > size) return false;
    const uint16_t n = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
    offset += 2;
    if (n == 0 || offset + n > size) return false;
    out->push_back(0);
    out->push_back(0);
    out->push_back(0);
    out->push_back(1);
    out->insert(out->end(), data + offset, data + offset + n);
    offset += n;
  }
  return !out->empty();
}

bool create_input_sample(const std::vector<uint8_t>& bytes, int64_t sampleTimeHns,
                         int64_t sampleDurationHns, IMFSample** outSample) {
  if (!outSample) return false;
  *outSample = nullptr;
  Microsoft::WRL::ComPtr<IMFSample> sample;
  Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
  if (FAILED(MFCreateSample(&sample)) || !sample) return false;
  if (FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(bytes.size()), &buffer)) || !buffer) return false;
  BYTE* dst = nullptr;
  DWORD maxLen = 0;
  DWORD curLen = 0;
  if (FAILED(buffer->Lock(&dst, &maxLen, &curLen)) || !dst) return false;
  std::memcpy(dst, bytes.data(), bytes.size());
  buffer->Unlock();
  if (FAILED(buffer->SetCurrentLength(static_cast<DWORD>(bytes.size())))) return false;
  if (FAILED(sample->AddBuffer(buffer.Get()))) return false;
  (void)sample->SetSampleTime(sampleTimeHns);
  (void)sample->SetSampleDuration(sampleDurationHns);
  *outSample = sample.Detach();
  return true;
}

bool sample_to_bytes(IMFSample* sample, std::vector<uint8_t>* out) {
  if (!sample || !out) return false;
  Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
  if (FAILED(sample->ConvertToContiguousBuffer(&buffer)) || !buffer) return false;
  BYTE* src = nullptr;
  DWORD maxLen = 0;
  DWORD curLen = 0;
  if (FAILED(buffer->Lock(&src, &maxLen, &curLen)) || !src) return false;
  out->assign(src, src + curLen);
  buffer->Unlock();
  return !out->empty();
}

bool sample_to_nv12_bytes_from_2d_buffer(IMFSample* sample, uint32_t width, uint32_t height,
                                         std::vector<uint8_t>* out) {
  if (!sample || !out || width == 0 || height == 0) return false;

  DWORD bufferCount = 0;
  if (FAILED(sample->GetBufferCount(&bufferCount)) || bufferCount == 0) return false;

  for (DWORD i = 0; i < bufferCount; ++i) {
    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->GetBufferByIndex(i, &buffer)) || !buffer) continue;

    Microsoft::WRL::ComPtr<IMF2DBuffer> buffer2d;
    if (FAILED(buffer.As(&buffer2d)) || !buffer2d) continue;

    BYTE* scan0 = nullptr;
    LONG pitch = 0;
    if (FAILED(buffer2d->Lock2D(&scan0, &pitch)) || !scan0 || pitch == 0) continue;

    const LONG absPitch = (pitch >= 0) ? pitch : -pitch;
    const size_t yPlaneBytes = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t uvPlaneBytes =
        static_cast<size_t>(width) * static_cast<size_t>((height + 1) / 2);
    out->assign(yPlaneBytes + uvPlaneBytes, 0);
    auto* dstY = out->data();
    auto* dstUV = out->data() + yPlaneBytes;

    const BYTE* yBase = (pitch >= 0)
                            ? scan0
                            : (scan0 + static_cast<ptrdiff_t>(height - 1) * absPitch);
    for (uint32_t y = 0; y < height; ++y) {
      const BYTE* srcRow =
          (pitch >= 0) ? (yBase + static_cast<ptrdiff_t>(y) * absPitch)
                       : (yBase - static_cast<ptrdiff_t>(y) * absPitch);
      std::memcpy(dstY + static_cast<size_t>(y) * width, srcRow, width);
    }

    const uint32_t uvRows = (height + 1) / 2;
    const BYTE* uvBase = (pitch >= 0)
                             ? (yBase + static_cast<ptrdiff_t>(height) * absPitch)
                             : (yBase - static_cast<ptrdiff_t>(height) * absPitch);
    for (uint32_t y = 0; y < uvRows; ++y) {
      const BYTE* srcRow =
          (pitch >= 0) ? (uvBase + static_cast<ptrdiff_t>(y) * absPitch)
                       : (uvBase - static_cast<ptrdiff_t>(y) * absPitch);
      std::memcpy(dstUV + static_cast<size_t>(y) * width, srcRow, width);
    }

    buffer2d->Unlock2D();
    return true;
  }

  return false;
}

bool env_truthy_local(const char* key) {
  if (!key) return false;
  const char* v = std::getenv(key);
  if (!v) return false;
  return (std::strcmp(v, "1") == 0) ||
         (_stricmp(v, "true") == 0) ||
         (_stricmp(v, "on") == 0);
}

std::string env_string_local(const char* key) {
  if (!key) return {};
  const char* v = std::getenv(key);
  if (!v) return {};
  return std::string(v);
}

std::string guid_to_string(const GUID& g) {
  wchar_t wbuf[64] = {};
  const int n = StringFromGUID2(g, wbuf, 64);
  if (n <= 1) return "{}";
  char buf[128] = {};
  const int c = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, static_cast<int>(sizeof(buf)), nullptr, nullptr);
  if (c <= 1) return "{}";
  return std::string(buf);
}

std::string hr_to_hex(HRESULT hr) {
  char buf[32] = {};
  std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
  return std::string(buf);
}

void codec_debug_log(const char* msg) {
  if (!msg) return;
  if (!env_truthy_local("REMOTE60_NATIVE_DEBUG_CODEC")) return;
  std::fprintf(stderr, "[mf_h264_codec] %s\n", msg);
  std::fflush(stderr);
}

enum class MftBackendMode {
  Auto,
  HardwareOnly,
  SoftwareOnly,
};

MftBackendMode parse_mft_backend_mode(const std::string& mode) {
  // Keep default path stable: explicit backend is required to force hardware probing.
  if (mode.empty()) {
    return MftBackendMode::SoftwareOnly;
  }
  if (_stricmp(mode.c_str(), "mft_auto") == 0 || _stricmp(mode.c_str(), "auto") == 0) {
    return MftBackendMode::Auto;
  }
  if (_stricmp(mode.c_str(), "mft_hw") == 0 || _stricmp(mode.c_str(), "hw") == 0) {
    return MftBackendMode::HardwareOnly;
  }
  if (_stricmp(mode.c_str(), "mft_sw") == 0 || _stricmp(mode.c_str(), "sw") == 0) {
    return MftBackendMode::SoftwareOnly;
  }
  return MftBackendMode::Auto;
}

void release_activate_array(IMFActivate** activates, UINT32 count) {
  if (!activates) return;
  for (UINT32 i = 0; i < count; ++i) {
    if (activates[i]) activates[i]->Release();
  }
  CoTaskMemFree(activates);
}

bool try_activate_first(IMFActivate** activates, UINT32 count, IMFTransform** outTransform) {
  if (!activates || !outTransform) return false;
  *outTransform = nullptr;
  for (UINT32 i = 0; i < count; ++i) {
    if (!activates[i]) continue;
    IMFTransform* candidate = nullptr;
    if (SUCCEEDED(activates[i]->ActivateObject(IID_PPV_ARGS(&candidate))) && candidate) {
      *outTransform = candidate;
      return true;
    }
  }
  return false;
}

bool try_activate_matching_name(IMFActivate** activates, UINT32 count, const wchar_t* nameNeedle,
                                IMFTransform** outTransform) {
  if (!activates || !nameNeedle || !outTransform) return false;
  *outTransform = nullptr;
  for (UINT32 i = 0; i < count; ++i) {
    if (!activates[i]) continue;
    WCHAR* friendly = nullptr;
    UINT32 cch = 0;
    const HRESULT hrName =
        activates[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &friendly, &cch);
    const bool nameMatch = SUCCEEDED(hrName) && friendly && std::wcsstr(friendly, nameNeedle) != nullptr;
    if (friendly) {
      CoTaskMemFree(friendly);
      friendly = nullptr;
    }
    if (!nameMatch) continue;
    IMFTransform* candidate = nullptr;
    if (SUCCEEDED(activates[i]->ActivateObject(IID_PPV_ARGS(&candidate))) && candidate) {
      *outTransform = candidate;
      return true;
    }
  }
  return false;
}

bool create_mft_from_clsid_string(const wchar_t* clsidString, IMFTransform** outTransform) {
  if (!clsidString || !outTransform) return false;
  *outTransform = nullptr;
  CLSID clsid{};
  if (FAILED(CLSIDFromString(clsidString, &clsid))) return false;
  IMFTransform* transform = nullptr;
  const HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&transform));
  if (FAILED(hr) || !transform) return false;
  *outTransform = transform;
  return true;
}

bool decoder_supports_h264_input(IMFTransform* transform) {
  if (!transform) return false;
  Microsoft::WRL::ComPtr<IMFMediaType> inType;
  if (FAILED(MFCreateMediaType(&inType)) || !inType) return false;
  (void)inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  (void)inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
  return SUCCEEDED(transform->SetInputType(0, inType.Get(), MFT_SET_TYPE_TEST_ONLY));
}

bool create_video_mft_from_enum(const GUID& category, const GUID& inSubtype, const GUID& outSubtype,
                                DWORD flags, IMFTransform** outTransform) {
  if (!outTransform) return false;
  *outTransform = nullptr;
  MFT_REGISTER_TYPE_INFO inputInfo{};
  inputInfo.guidMajorType = MFMediaType_Video;
  inputInfo.guidSubtype = inSubtype;
  MFT_REGISTER_TYPE_INFO outputInfo{};
  outputInfo.guidMajorType = MFMediaType_Video;
  outputInfo.guidSubtype = outSubtype;

  IMFActivate** activates = nullptr;
  UINT32 count = 0;
  const HRESULT hr = MFTEnumEx(category, flags, &inputInfo, &outputInfo, &activates, &count);
  if (FAILED(hr) || count == 0 || !activates) return false;

  IMFTransform* transform = nullptr;
  const bool ok = try_activate_first(activates, count, &transform);
  release_activate_array(activates, count);
  if (!ok || !transform) return false;
  *outTransform = transform;
  return true;
}

bool create_video_mft_from_enum_matching_name(const GUID& category, const GUID& inSubtype, const GUID& outSubtype,
                                              DWORD flags, const wchar_t* nameNeedle,
                                              IMFTransform** outTransform) {
  if (!outTransform || !nameNeedle) return false;
  *outTransform = nullptr;
  MFT_REGISTER_TYPE_INFO inputInfo{};
  inputInfo.guidMajorType = MFMediaType_Video;
  inputInfo.guidSubtype = inSubtype;
  MFT_REGISTER_TYPE_INFO outputInfo{};
  outputInfo.guidMajorType = MFMediaType_Video;
  outputInfo.guidSubtype = outSubtype;

  IMFActivate** activates = nullptr;
  UINT32 count = 0;
  const HRESULT hr = MFTEnumEx(category, flags, &inputInfo, &outputInfo, &activates, &count);
  if (FAILED(hr) || count == 0 || !activates) return false;

  IMFTransform* transform = nullptr;
  const bool ok = try_activate_matching_name(activates, count, nameNeedle, &transform);
  release_activate_array(activates, count);
  if (!ok || !transform) return false;
  *outTransform = transform;
  return true;
}

bool create_h264_encoder_transform(IMFTransform** outTransform, bool* outUsingHardware, const char** outBackendName) {
  if (!outTransform || !outUsingHardware || !outBackendName) return false;
  *outTransform = nullptr;
  *outUsingHardware = false;
  *outBackendName = "none";

  IMFTransform* transform = nullptr;
  const std::string backendRaw = env_string_local("REMOTE60_NATIVE_ENCODER_BACKEND");
  // AMD-specific direct backend (AMF MFT) to avoid generic hw enum ambiguity.
  if (_stricmp(backendRaw.c_str(), "amf_hw") == 0 || _stricmp(backendRaw.c_str(), "amf_mft") == 0) {
    codec_debug_log("encoder backend=amf_hw begin");
    constexpr DWORD kEnumFlagsHw = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER;
    if (create_video_mft_from_enum_matching_name(MFT_CATEGORY_VIDEO_ENCODER, MFVideoFormat_NV12, MFVideoFormat_H264,
                                                 kEnumFlagsHw, L"AMDh264Encoder", &transform)) {
      *outTransform = transform;
      *outUsingHardware = true;
      *outBackendName = "amf_mft_h264enc";
      return true;
    }
    if (create_mft_from_clsid_string(L"{adc9bc80-0f41-46c6-ab75-d693d793597d}", &transform)) {
      *outTransform = transform;
      *outUsingHardware = true;
      *outBackendName = "amf_mft_h264enc";
      return true;
    }
    *outBackendName = "amf_mft_h264enc_unavailable";
    codec_debug_log("encoder backend=amf_hw unavailable");
    return false;
  }

  const MftBackendMode backendMode = parse_mft_backend_mode(backendRaw);

  if (backendMode != MftBackendMode::SoftwareOnly) {
    // Prefer synchronous hardware MFT first. Async encoder MFTs may require an event-driven
    // pipeline and can stall in this polling-style POC path.
    constexpr DWORD kEnumFlagsHw = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER;
    if (create_video_mft_from_enum(MFT_CATEGORY_VIDEO_ENCODER, MFVideoFormat_NV12, MFVideoFormat_H264,
                                   kEnumFlagsHw, &transform)) {
      *outTransform = transform;
      *outUsingHardware = true;
      *outBackendName = "mft_enum_hw";
      return true;
    }
  }

  if (backendMode == MftBackendMode::HardwareOnly) {
    *outBackendName = "mft_hw_unavailable";
    return false;
  }

  if (backendMode != MftBackendMode::HardwareOnly) {
    constexpr DWORD kEnumFlagsSw = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER | MFT_ENUM_FLAG_LOCALMFT;
    if (create_video_mft_from_enum(MFT_CATEGORY_VIDEO_ENCODER, MFVideoFormat_NV12, MFVideoFormat_H264,
                                   kEnumFlagsSw, &transform)) {
      *outTransform = transform;
      *outUsingHardware = false;
      *outBackendName = "mft_enum_sw";
      return true;
    }
  }

  HRESULT hr = CoCreateInstance(CLSID_CMSH264EncoderMFT, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&transform));
  if (FAILED(hr) || !transform) return false;
  *outTransform = transform;
  *outUsingHardware = false;
  *outBackendName = "clsid_cmsh264enc";
  return true;
}

bool create_h264_decoder_transform(IMFTransform** outTransform, bool* outUsingHardware, const char** outBackendName) {
  if (!outTransform || !outUsingHardware || !outBackendName) return false;
  *outTransform = nullptr;
  *outUsingHardware = false;
  *outBackendName = "none";

  IMFTransform* transform = nullptr;
  const std::string backendRaw = env_string_local("REMOTE60_NATIVE_DECODER_BACKEND");
  // AMD-specific direct backend (AMF D3D11 decoder MFT).
  if (_stricmp(backendRaw.c_str(), "amf_hw") == 0 || _stricmp(backendRaw.c_str(), "amf_mft") == 0) {
    codec_debug_log("decoder backend=amf_hw begin");
    constexpr DWORD kEnumFlagsHw = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER;
    if (create_video_mft_from_enum_matching_name(MFT_CATEGORY_VIDEO_DECODER, MFVideoFormat_H264, MFVideoFormat_NV12,
                                                 kEnumFlagsHw, L"AMD D3D11 Hardware MFT Playback Decoder",
                                                 &transform)) {
      if (decoder_supports_h264_input(transform)) {
        *outTransform = transform;
        *outUsingHardware = true;
        *outBackendName = "amf_mft_h264dec";
        return true;
      }
      transform->Release();
      transform = nullptr;
    }
    if (create_mft_from_clsid_string(L"{17796aeb-0f66-4663-b8fb-99cbee0224ce}", &transform)) {
      if (decoder_supports_h264_input(transform)) {
        *outTransform = transform;
        *outUsingHardware = true;
        *outBackendName = "amf_mft_h264dec";
        return true;
      }
      transform->Release();
      transform = nullptr;
    }
    if (create_video_mft_from_enum(MFT_CATEGORY_VIDEO_DECODER, MFVideoFormat_H264, MFVideoFormat_NV12,
                                   kEnumFlagsHw, &transform)) {
      *outTransform = transform;
      *outUsingHardware = true;
      *outBackendName = "mft_enum_hw";
      return true;
    }
    *outBackendName = "amf_mft_h264dec_unavailable";
    codec_debug_log("decoder backend=amf_hw unavailable");
    return false;
  }

  const MftBackendMode backendMode = parse_mft_backend_mode(backendRaw);

  if (backendMode != MftBackendMode::SoftwareOnly) {
    constexpr DWORD kEnumFlagsHw = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER;
    if (create_video_mft_from_enum(MFT_CATEGORY_VIDEO_DECODER, MFVideoFormat_H264, MFVideoFormat_NV12,
                                   kEnumFlagsHw, &transform)) {
      *outTransform = transform;
      *outUsingHardware = true;
      *outBackendName = "mft_enum_hw";
      return true;
    }
  }

  if (backendMode == MftBackendMode::HardwareOnly) {
    *outBackendName = "mft_hw_unavailable";
    return false;
  }

  if (backendMode != MftBackendMode::HardwareOnly) {
    constexpr DWORD kEnumFlagsSw = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER | MFT_ENUM_FLAG_LOCALMFT;
    if (create_video_mft_from_enum(MFT_CATEGORY_VIDEO_DECODER, MFVideoFormat_H264, MFVideoFormat_NV12,
                                   kEnumFlagsSw, &transform)) {
      *outTransform = transform;
      *outUsingHardware = false;
      *outBackendName = "mft_enum_sw";
      return true;
    }
  }

  HRESULT hr = CoCreateInstance(CLSID_CMSH264DecoderMFT, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&transform));
  if (FAILED(hr) || !transform) return false;
  *outTransform = transform;
  *outUsingHardware = false;
  *outBackendName = "clsid_cmsh264dec";
  return true;
}

}  // namespace

bool bgra_to_nv12(const uint8_t* bgra, uint32_t width, uint32_t height, uint32_t bgraStride,
                  std::vector<uint8_t>* outNv12) {
  if (!bgra || width == 0 || height == 0 || !outNv12) return false;
  const size_t yPlaneSize = static_cast<size_t>(width) * static_cast<size_t>(height);
  const size_t uvPlaneSize = static_cast<size_t>(width) * static_cast<size_t>((height + 1) / 2);
  outNv12->assign(yPlaneSize + uvPlaneSize, 0);

  auto* yPlane = outNv12->data();
  auto* uvPlane = outNv12->data() + yPlaneSize;

  for (uint32_t y = 0; y < height; ++y) {
    const uint8_t* row = bgra + static_cast<size_t>(y) * bgraStride;
    for (uint32_t x = 0; x < width; ++x) {
      const uint8_t b = row[x * 4 + 0];
      const uint8_t g = row[x * 4 + 1];
      const uint8_t r = row[x * 4 + 2];
      const int yy = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
      yPlane[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(clamp_u8_int(yy));
    }
  }

  for (uint32_t y = 0; y < height; y += 2) {
    const uint8_t* row0 = bgra + static_cast<size_t>(y) * bgraStride;
    const uint8_t* row1 = bgra + static_cast<size_t>(std::min<uint32_t>(y + 1, height - 1)) * bgraStride;
    uint8_t* uvRow = uvPlane + static_cast<size_t>(y / 2) * width;
    for (uint32_t x = 0; x < width; x += 2) {
      const uint32_t x1 = std::min<uint32_t>(x + 1, width - 1);
      const std::array<const uint8_t*, 4> px = {
          row0 + x * 4, row0 + x1 * 4, row1 + x * 4, row1 + x1 * 4};
      int r = 0, g = 0, b = 0;
      for (const auto* p : px) {
        b += p[0];
        g += p[1];
        r += p[2];
      }
      r /= 4;
      g /= 4;
      b /= 4;
      const int uu = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
      const int vv = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
      uvRow[x] = static_cast<uint8_t>(clamp_u8_int(uu));
      if (x + 1 < width) uvRow[x + 1] = static_cast<uint8_t>(clamp_u8_int(vv));
    }
  }

  return true;
}

bool nv12_to_bgra(const uint8_t* nv12, uint32_t width, uint32_t height, std::vector<uint8_t>* outBgra) {
  if (!nv12 || width == 0 || height == 0 || !outBgra) return false;
  const size_t yPlaneSize = static_cast<size_t>(width) * static_cast<size_t>(height);
  const auto* yPlane = nv12;
  const auto* uvPlane = nv12 + yPlaneSize;
  outBgra->resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

  struct Nv12Tables {
    int yScale[256]{};
    int rv[256]{};
    int gu[256]{};
    int gv[256]{};
    int bu[256]{};
  };
  static const Nv12Tables kTbl = []() {
    Nv12Tables t{};
    for (int i = 0; i < 256; ++i) {
      const int y = std::max(0, i - 16);
      const int uv = i - 128;
      t.yScale[i] = 298 * y;
      t.rv[i] = 409 * uv;
      t.gu[i] = -100 * uv;
      t.gv[i] = -208 * uv;
      t.bu[i] = 516 * uv;
    }
    return t;
  }();

  for (uint32_t y = 0; y < height; ++y) {
    const uint8_t* yRow = yPlane + static_cast<size_t>(y) * width;
    const uint8_t* uvRow = uvPlane + static_cast<size_t>(y / 2) * width;
    uint8_t* out = outBgra->data() + static_cast<size_t>(y) * width * 4;

    uint32_t x = 0;
    for (; x + 1 < width; x += 2) {
      const uint8_t u = uvRow[x + 0];
      const uint8_t v = uvRow[x + 1];
      const int rv = kTbl.rv[v];
      const int guv = kTbl.gu[u] + kTbl.gv[v];
      const int bu = kTbl.bu[u];

      const int y0 = kTbl.yScale[yRow[x + 0]];
      const int r0 = (y0 + rv + 128) >> 8;
      const int g0 = (y0 + guv + 128) >> 8;
      const int b0 = (y0 + bu + 128) >> 8;
      out[(x + 0) * 4 + 0] = static_cast<uint8_t>(clamp_u8_int(b0));
      out[(x + 0) * 4 + 1] = static_cast<uint8_t>(clamp_u8_int(g0));
      out[(x + 0) * 4 + 2] = static_cast<uint8_t>(clamp_u8_int(r0));
      out[(x + 0) * 4 + 3] = 255;

      const int y1 = kTbl.yScale[yRow[x + 1]];
      const int r1 = (y1 + rv + 128) >> 8;
      const int g1 = (y1 + guv + 128) >> 8;
      const int b1 = (y1 + bu + 128) >> 8;
      out[(x + 1) * 4 + 0] = static_cast<uint8_t>(clamp_u8_int(b1));
      out[(x + 1) * 4 + 1] = static_cast<uint8_t>(clamp_u8_int(g1));
      out[(x + 1) * 4 + 2] = static_cast<uint8_t>(clamp_u8_int(r1));
      out[(x + 1) * 4 + 3] = 255;
    }

    if (x < width) {
      const uint32_t uvBase = (x / 2u) * 2u;
      const uint8_t u = uvRow[uvBase];
      const uint8_t v = uvRow[std::min<uint32_t>(uvBase + 1u, width - 1u)];
      const int yy = kTbl.yScale[yRow[x]];
      const int r = (yy + kTbl.rv[v] + 128) >> 8;
      const int g = (yy + kTbl.gu[u] + kTbl.gv[v] + 128) >> 8;
      const int b = (yy + kTbl.bu[u] + 128) >> 8;
      out[x * 4 + 0] = static_cast<uint8_t>(clamp_u8_int(b));
      out[x * 4 + 1] = static_cast<uint8_t>(clamp_u8_int(g));
      out[x * 4 + 2] = static_cast<uint8_t>(clamp_u8_int(r));
      out[x * 4 + 3] = 255;
    }
  }
  return true;
}

H264Encoder::~H264Encoder() {
  shutdown();
}

bool H264Encoder::set_d3d11_device(ID3D11Device* device) {
  d3dManager_.Reset();
  d3dManagerResetToken_ = 0;
  if (!device) return false;

  UINT token = 0;
  Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> manager;
  if (FAILED(MFCreateDXGIDeviceManager(&token, &manager)) || !manager) {
    return false;
  }
  if (FAILED(manager->ResetDevice(device, token))) {
    return false;
  }
  d3dManager_ = manager;
  d3dManagerResetToken_ = token;
  return true;
}

bool H264Encoder::configure_types() {
  if (!enc_) return false;

  auto make_output_h264_type = [&]() -> Microsoft::WRL::ComPtr<IMFMediaType> {
    Microsoft::WRL::ComPtr<IMFMediaType> outType;
    if (FAILED(MFCreateMediaType(&outType)) || !outType) return nullptr;
    (void)outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    (void)outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    (void)MFSetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, width_, height_);
    (void)MFSetAttributeRatio(outType.Get(), MF_MT_FRAME_RATE, fps_, 1);
    (void)MFSetAttributeRatio(outType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    (void)outType->SetUINT32(MF_MT_AVG_BITRATE, bitrate_);
    (void)outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    return outType;
  };

  auto make_input_nv12_type = [&]() -> Microsoft::WRL::ComPtr<IMFMediaType> {
    Microsoft::WRL::ComPtr<IMFMediaType> inType;
    if (FAILED(MFCreateMediaType(&inType)) || !inType) return nullptr;
    (void)inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    (void)inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    (void)MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, width_, height_);
    (void)MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE, fps_, 1);
    (void)MFSetAttributeRatio(inType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    (void)inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    return inType;
  };

  auto try_direct_types = [&]() -> bool {
    auto outType = make_output_h264_type();
    auto inType = make_input_nv12_type();
    if (!outType || !inType) return false;

    // Some hardware MFTs expect input-first, others output-first. Try both orders.
    if (SUCCEEDED(enc_->SetInputType(0, inType.Get(), 0)) &&
        SUCCEEDED(enc_->SetOutputType(0, outType.Get(), 0))) {
      return true;
    }
    if (SUCCEEDED(enc_->SetOutputType(0, outType.Get(), 0)) &&
        SUCCEEDED(enc_->SetInputType(0, inType.Get(), 0))) {
      return true;
    }
    return false;
  };

  if (std::strcmp(backendName_, "amf_mft_h264enc") == 0) {
    codec_debug_log("encoder configure types: direct path for amf");
    for (DWORD i = 0; i < 8; ++i) {
      IMFMediaType* outTypeRaw = nullptr;
      const HRESULT hr = enc_->GetOutputAvailableType(0, i, &outTypeRaw);
      if (hr == MF_E_NO_MORE_TYPES) break;
      if (FAILED(hr) || !outTypeRaw) {
        std::string line = "encoder amf: GetOutputAvailableType failed idx=" + std::to_string(i) +
                           " hr=" + hr_to_hex(hr);
        codec_debug_log(line.c_str());
        continue;
      }
      Microsoft::WRL::ComPtr<IMFMediaType> outType;
      outType.Attach(outTypeRaw);
      GUID st{};
      if (SUCCEEDED(outType->GetGUID(MF_MT_SUBTYPE, &st))) {
        const std::string s = guid_to_string(st);
        std::string line = "encoder amf: out subtype=" + s;
        codec_debug_log(line.c_str());
      }
    }
    for (DWORD i = 0; i < 8; ++i) {
      IMFMediaType* inTypeRaw = nullptr;
      const HRESULT hr = enc_->GetInputAvailableType(0, i, &inTypeRaw);
      if (hr == MF_E_NO_MORE_TYPES) break;
      if (FAILED(hr) || !inTypeRaw) {
        std::string line = "encoder amf: GetInputAvailableType failed idx=" + std::to_string(i) +
                           " hr=" + hr_to_hex(hr);
        codec_debug_log(line.c_str());
        continue;
      }
      Microsoft::WRL::ComPtr<IMFMediaType> inType;
      inType.Attach(inTypeRaw);
      GUID st{};
      if (SUCCEEDED(inType->GetGUID(MF_MT_SUBTYPE, &st))) {
        const std::string s = guid_to_string(st);
        std::string line = "encoder amf: in subtype=" + s;
        codec_debug_log(line.c_str());
      }
    }

    bool outConfigured = false;
    for (DWORD i = 0; i < 64; ++i) {
      IMFMediaType* outTypeRaw = nullptr;
      const HRESULT hr = enc_->GetOutputAvailableType(0, i, &outTypeRaw);
      if (hr == MF_E_NO_MORE_TYPES) break;
      if (FAILED(hr) || !outTypeRaw) continue;
      Microsoft::WRL::ComPtr<IMFMediaType> outType;
      outType.Attach(outTypeRaw);

      GUID st{};
      if (FAILED(outType->GetGUID(MF_MT_SUBTYPE, &st))) continue;
      if (st != MFVideoFormat_H264) continue;

      (void)MFSetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, width_, height_);
      (void)MFSetAttributeRatio(outType.Get(), MF_MT_FRAME_RATE, fps_, 1);
      (void)MFSetAttributeRatio(outType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
      (void)outType->SetUINT32(MF_MT_AVG_BITRATE, bitrate_);
      (void)outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
      if (SUCCEEDED(enc_->SetOutputType(0, outType.Get(), 0))) {
        outConfigured = true;
        codec_debug_log("encoder amf: output type configured by enum");
        break;
      }
    }

    bool inConfigured = false;
    for (DWORD i = 0; i < 64; ++i) {
      IMFMediaType* inTypeRaw = nullptr;
      const HRESULT hr = enc_->GetInputAvailableType(0, i, &inTypeRaw);
      if (hr == MF_E_NO_MORE_TYPES) break;
      if (FAILED(hr) || !inTypeRaw) continue;
      Microsoft::WRL::ComPtr<IMFMediaType> inType;
      inType.Attach(inTypeRaw);

      GUID st{};
      if (FAILED(inType->GetGUID(MF_MT_SUBTYPE, &st))) continue;
      if (st != MFVideoFormat_NV12) continue;

      (void)MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, width_, height_);
      (void)MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE, fps_, 1);
      (void)MFSetAttributeRatio(inType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
      (void)inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
      if (SUCCEEDED(enc_->SetInputType(0, inType.Get(), 0))) {
        inConfigured = true;
        codec_debug_log("encoder amf: input type configured by enum");
        break;
      }
    }

    if (outConfigured && inConfigured) {
      return true;
    }

    auto outType = make_output_h264_type();
    auto inType = make_input_nv12_type();
    if (!outType || !inType) return false;

    codec_debug_log("encoder amf: fallback test output type");
    {
      const HRESULT hr = enc_->SetOutputType(0, outType.Get(), MFT_SET_TYPE_TEST_ONLY);
      std::string line = "encoder amf: fallback test output hr=" + hr_to_hex(hr);
      codec_debug_log(line.c_str());
    }
    codec_debug_log("encoder amf: fallback test input type");
    {
      const HRESULT hr = enc_->SetInputType(0, inType.Get(), MFT_SET_TYPE_TEST_ONLY);
      std::string line = "encoder amf: fallback test input hr=" + hr_to_hex(hr);
      codec_debug_log(line.c_str());
    }

    codec_debug_log("encoder amf: fallback set input type");
    {
      const HRESULT hr = enc_->SetInputType(0, inType.Get(), 0);
      std::string line = "encoder amf: fallback set input hr=" + hr_to_hex(hr);
      codec_debug_log(line.c_str());
      if (SUCCEEDED(hr)) {
      codec_debug_log("encoder amf: fallback set output type");
        const HRESULT hrOut = enc_->SetOutputType(0, outType.Get(), 0);
        std::string lineOut = "encoder amf: fallback set output hr=" + hr_to_hex(hrOut);
        codec_debug_log(lineOut.c_str());
        if (SUCCEEDED(hrOut)) {
        codec_debug_log("encoder amf: fallback direct types configured(input->output)");
        return true;
        }
      }
    }
    codec_debug_log("encoder amf: fallback retry output->input");
    {
      const HRESULT hrOut = enc_->SetOutputType(0, outType.Get(), 0);
      const HRESULT hrIn = SUCCEEDED(hrOut) ? enc_->SetInputType(0, inType.Get(), 0) : E_FAIL;
      std::string line = "encoder amf: fallback retry output hr=" + hr_to_hex(hrOut) +
                         " input hr=" + hr_to_hex(hrIn);
      codec_debug_log(line.c_str());
      if (SUCCEEDED(hrOut) && SUCCEEDED(hrIn)) {
      codec_debug_log("encoder amf: fallback direct types configured(output->input)");
      return true;
      }
    }
    codec_debug_log("encoder amf: configure types failed");
    return false;
  }

  // Try direct type setup first to avoid expensive/unstable full type enumeration.
  if (try_direct_types()) {
    return true;
  }

  for (DWORD i = 0;; ++i) {
    IMFMediaType* outTypeRaw = nullptr;
    const HRESULT hr = enc_->GetOutputAvailableType(0, i, &outTypeRaw);
    if (hr == MF_E_NO_MORE_TYPES) return false;
    if (FAILED(hr) || !outTypeRaw) continue;
    Microsoft::WRL::ComPtr<IMFMediaType> outType;
    outType.Attach(outTypeRaw);

    GUID st{};
    if (FAILED(outType->GetGUID(MF_MT_SUBTYPE, &st)) || st != MFVideoFormat_H264) continue;
    (void)MFSetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, width_, height_);
    (void)MFSetAttributeRatio(outType.Get(), MF_MT_FRAME_RATE, fps_, 1);
    (void)outType->SetUINT32(MF_MT_AVG_BITRATE, bitrate_);
    (void)outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(enc_->SetOutputType(0, outType.Get(), 0))) break;
  }

  for (DWORD i = 0;; ++i) {
    IMFMediaType* inTypeRaw = nullptr;
    const HRESULT hr = enc_->GetInputAvailableType(0, i, &inTypeRaw);
    if (hr == MF_E_NO_MORE_TYPES) return false;
    if (FAILED(hr) || !inTypeRaw) continue;
    Microsoft::WRL::ComPtr<IMFMediaType> inType;
    inType.Attach(inTypeRaw);

    GUID st{};
    if (FAILED(inType->GetGUID(MF_MT_SUBTYPE, &st)) || st != MFVideoFormat_NV12) continue;
    (void)MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, width_, height_);
    (void)MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE, fps_, 1);
    (void)MFSetAttributeRatio(inType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    (void)inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(enc_->SetInputType(0, inType.Get(), 0))) return true;
  }
}

void H264Encoder::apply_low_latency_codec_api() {
  if (!enc_) return;
  (void)set_mf_attr_u32(enc_.Get(), MF_LOW_LATENCY, 1);
  (void)set_codecapi_bool(enc_.Get(), CODECAPI_AVLowLatencyMode, true);
  (void)set_codecapi_bool(enc_.Get(), CODECAPI_AVEncCommonLowLatency, true);
  (void)set_codecapi_bool(enc_.Get(), CODECAPI_AVEncCommonRealTime, true);
  (void)set_codecapi_u32(enc_.Get(), CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR);
  (void)set_codecapi_u32(enc_.Get(), CODECAPI_AVEncCommonMeanBitRate, bitrate_);
  (void)set_codecapi_u32(enc_.Get(), CODECAPI_AVEncCommonMaxBitRate, std::max<uint32_t>(bitrate_, (bitrate_ * 11) / 10));
  // Keep encoder VBV short to reduce internal queue growth.
  (void)set_codecapi_u32(enc_.Get(), CODECAPI_AVEncCommonBufferSize, low_latency_vbv_bytes(bitrate_));
  (void)set_codecapi_u32(enc_.Get(), CODECAPI_AVEncMPVDefaultBPictureCount, 0);
  (void)set_codecapi_bool(enc_.Get(), CODECAPI_AVEncMPVGOPOpen, false);
  (void)set_codecapi_u32(enc_.Get(), CODECAPI_AVEncMPVGOPSize, std::max<uint32_t>(1, keyint_));
  (void)set_codecapi_u32(enc_.Get(), CODECAPI_AVEncCommonQualityVsSpeed, 100);
}

bool H264Encoder::initialize(uint32_t width, uint32_t height, uint32_t fps, uint32_t bitrate, uint32_t keyint) {
  shutdown();
  codec_debug_log("encoder initialize: start");
  width_ = width;
  height_ = height;
  fps_ = std::max<uint32_t>(1, fps);
  bitrate_ = std::max<uint32_t>(100000, bitrate);
  keyint_ = std::max<uint32_t>(1, keyint);
  sampleDurationHns_ = std::max<int64_t>(1, 10000000LL / static_cast<int64_t>(fps_));

  IMFTransform* encRaw = nullptr;
  codec_debug_log("encoder initialize: create transform");
  if (!create_h264_encoder_transform(&encRaw, &usingHardware_, &backendName_) || !encRaw) return false;
  codec_debug_log("encoder initialize: transform created");
  enc_.Attach(encRaw);
  (void)set_mf_attr_u32(enc_.Get(), MF_TRANSFORM_ASYNC_UNLOCK, 1);
  if (d3dManager_) {
    codec_debug_log("encoder initialize: set d3d manager");
    (void)enc_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                               reinterpret_cast<ULONG_PTR>(d3dManager_.Get()));
  }
  asyncTransform_ = false;
  eventGenerator_.Reset();
  {
    Microsoft::WRL::ComPtr<IMFAttributes> attrs;
    if (SUCCEEDED(enc_->GetAttributes(&attrs)) && attrs) {
      UINT32 asyncFlag = 0;
      if (SUCCEEDED(attrs->GetUINT32(MF_TRANSFORM_ASYNC, &asyncFlag)) && asyncFlag != 0) {
        asyncTransform_ = true;
      }
    }
    if (asyncTransform_) {
      (void)enc_->QueryInterface(IID_PPV_ARGS(&eventGenerator_));
      codec_debug_log("encoder initialize: async transform detected");
    }
  }

  codec_debug_log("encoder initialize: configure types");
  if (!configure_types()) {
    shutdown();
    return false;
  }
  codec_debug_log("encoder initialize: types configured");
  apply_low_latency_codec_api();
  (void)enc_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
  (void)enc_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
  codec_debug_log("encoder initialize: stream started");
  started_ = true;
  frameIndex_ = 0;
  sequenceHeaderAnnexb_.clear();

  Microsoft::WRL::ComPtr<IMFMediaType> outType;
  if (SUCCEEDED(enc_->GetOutputCurrentType(0, &outType)) && outType) {
    UINT32 blobSize = 0;
    if (SUCCEEDED(outType->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &blobSize)) && blobSize > 0) {
      std::vector<uint8_t> blob(blobSize);
      if (SUCCEEDED(outType->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, blob.data(), blobSize, &blobSize))) {
        std::vector<uint8_t> annexb;
        if (h264_config_avcc_to_annexb(blob.data(), blob.size(), &annexb)) {
          sequenceHeaderAnnexb_ = std::move(annexb);
        }
      }
    }
  }

  MFT_OUTPUT_STREAM_INFO osi{};
  if (SUCCEEDED(enc_->GetOutputStreamInfo(0, &osi))) {
    outBufferBytes_ = (osi.cbSize > 0) ? static_cast<uint32_t>(osi.cbSize) : (1u << 20);
  } else {
    outBufferBytes_ = 1u << 20;
  }
  return true;
}

bool H264Encoder::reconfigure_bitrate(uint32_t bitrate) {
  bitrate_ = std::max<uint32_t>(100000, bitrate);
  bool ok = true;
  ok = set_codecapi_u32(enc_.Get(), CODECAPI_AVEncCommonMeanBitRate, bitrate_) && ok;
  ok = set_codecapi_u32(enc_.Get(), CODECAPI_AVEncCommonMaxBitRate, std::max<uint32_t>(bitrate_, (bitrate_ * 11) / 10)) && ok;
  ok = set_codecapi_u32(enc_.Get(), CODECAPI_AVEncCommonBufferSize, low_latency_vbv_bytes(bitrate_)) && ok;
  return ok;
}

bool H264Encoder::encode_frame(const std::vector<uint8_t>& nv12, bool forceKeyFrame, int64_t inputSampleTimeHns,
                               std::vector<H264AccessUnit>* outUnits) {
  if (!enc_ || !started_ || !outUnits) return false;
  outUnits->clear();

  Microsoft::WRL::ComPtr<IMFSample> sample;
  IMFSample* inputSample = nullptr;
  const int64_t sampleTime = (inputSampleTimeHns > 0)
                                 ? inputSampleTimeHns
                                 : (static_cast<int64_t>(frameIndex_) * sampleDurationHns_);
  if (!create_input_sample(nv12, sampleTime,
                           sampleDurationHns_, &inputSample)) {
    return false;
  }
  sample.Attach(inputSample);

  if (forceKeyFrame) {
    (void)set_codecapi_u32(enc_.Get(), CODECAPI_AVEncVideoForceKeyFrame, 1);
  }

  auto drain_outputs = [&]() -> bool {
    while (true) {
      MFT_OUTPUT_STREAM_INFO osi{};
      if (FAILED(enc_->GetOutputStreamInfo(0, &osi))) return false;

      Microsoft::WRL::ComPtr<IMFMediaBuffer> outBuffer;
      Microsoft::WRL::ComPtr<IMFSample> outSample;
      MFT_OUTPUT_DATA_BUFFER odb{};
      odb.dwStreamID = 0;
      if ((osi.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == 0) {
        const uint32_t cb = (osi.cbSize > 0) ? static_cast<uint32_t>(osi.cbSize) : outBufferBytes_;
        if (FAILED(MFCreateMemoryBuffer(cb, &outBuffer)) || !outBuffer) return false;
        if (FAILED(MFCreateSample(&outSample)) || !outSample) return false;
        if (FAILED(outSample->AddBuffer(outBuffer.Get()))) return false;
        odb.pSample = outSample.Get();
      }
      DWORD status = 0;
      const HRESULT po = enc_->ProcessOutput(0, 1, &odb, &status);
      if (po == MF_E_TRANSFORM_NEED_MORE_INPUT) {
        if (odb.pEvents) odb.pEvents->Release();
        break;
      }
      if (po == MF_E_TRANSFORM_STREAM_CHANGE) {
        if (odb.pEvents) odb.pEvents->Release();
        if (!configure_types()) return false;
        continue;
      }
      if (FAILED(po)) {
        if (asyncTransform_ && po == E_UNEXPECTED) {
          if (odb.pEvents) odb.pEvents->Release();
          break;
        }
        if (env_truthy_local("REMOTE60_NATIVE_DEBUG_CODEC")) {
          std::string line = "encoder ProcessOutput failed hr=" + hr_to_hex(po);
          codec_debug_log(line.c_str());
        }
        if (odb.pEvents) odb.pEvents->Release();
        return false;
      }

      IMFSample* produced = odb.pSample ? odb.pSample : outSample.Get();
      std::vector<uint8_t> bytes;
      if (produced && sample_to_bytes(produced, &bytes) && !bytes.empty()) {
        if (!h264_has_start_code(bytes.data(), bytes.size())) {
          std::vector<uint8_t> annexb;
          if (h264_avcc_to_annexb(bytes, &annexb) && !annexb.empty()) {
            bytes.swap(annexb);
          }
        }
        UINT32 cleanPoint = 0;
        const bool sampleKey = SUCCEEDED(produced->GetUINT32(MFSampleExtension_CleanPoint, &cleanPoint)) &&
                               cleanPoint != 0;
        H264AccessUnit au{};
        const bool maybeKey = sampleKey || annexb_contains_idr(bytes.data(), bytes.size());
        au.keyFrame = maybeKey;
        int64_t outSampleTimeHns = 0;
        if (SUCCEEDED(produced->GetSampleTime(&outSampleTimeHns))) {
          au.sampleTimeHns = outSampleTimeHns;
        }
        if (maybeKey && !sequenceHeaderAnnexb_.empty() &&
            !annexb_contains_idr(sequenceHeaderAnnexb_.data(), sequenceHeaderAnnexb_.size())) {
          // Keep existing behavior if sequence blob is malformed.
        }
        if (maybeKey && !sequenceHeaderAnnexb_.empty()) {
          std::vector<uint8_t> prefixed;
          prefixed.reserve(sequenceHeaderAnnexb_.size() + bytes.size());
          prefixed.insert(prefixed.end(), sequenceHeaderAnnexb_.begin(), sequenceHeaderAnnexb_.end());
          prefixed.insert(prefixed.end(), bytes.begin(), bytes.end());
          au.bytes = std::move(prefixed);
        } else {
          au.bytes = std::move(bytes);
        }
        outUnits->push_back(std::move(au));
      }
      if (odb.pEvents) odb.pEvents->Release();
    }
    return true;
  };

  HRESULT inHr = enc_->ProcessInput(0, sample.Get(), 0);
  if (inHr == MF_E_NOTACCEPTING) {
    // Some hardware/async encoders require draining queued output before accepting new input.
    if (!drain_outputs()) return false;
    inHr = enc_->ProcessInput(0, sample.Get(), 0);
  }
  if (FAILED(inHr)) {
    if (env_truthy_local("REMOTE60_NATIVE_DEBUG_CODEC")) {
      std::string line = "encoder ProcessInput failed hr=" + hr_to_hex(inHr);
      codec_debug_log(line.c_str());
    }
    return false;
  }
  ++frameIndex_;

  if (asyncTransform_ && eventGenerator_) {
    bool sawEvent = false;
    for (int poll = 0; poll < 24; ++poll) {
      Microsoft::WRL::ComPtr<IMFMediaEvent> ev;
      const HRESULT ehr = eventGenerator_->GetEvent(MF_EVENT_FLAG_NO_WAIT, &ev);
      if (ehr == MF_E_NO_EVENTS_AVAILABLE) {
        Sleep(1);
        continue;
      }
      if (FAILED(ehr) || !ev) break;
      sawEvent = true;
      MediaEventType et = MEUnknown;
      (void)ev->GetType(&et);
      if (et == METransformHaveOutput) {
        if (!drain_outputs()) return false;
      } else if (et == METransformNeedInput) {
        break;
      }
    }
    if (!sawEvent) {
      if (!drain_outputs()) return false;
    }
    if (!outUnits->empty()) return true;
  }

  if (!drain_outputs()) return false;

  return true;
}

void H264Encoder::shutdown() {
  if (enc_) {
    (void)enc_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    (void)enc_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
    enc_.Reset();
  }
  started_ = false;
  frameIndex_ = 0;
  sequenceHeaderAnnexb_.clear();
  asyncTransform_ = false;
  eventGenerator_.Reset();
  usingHardware_ = false;
  backendName_ = "unknown";
}

H264Decoder::~H264Decoder() {
  shutdown();
}

bool H264Decoder::set_d3d11_device(ID3D11Device* device) {
  d3dManager_.Reset();
  d3dManagerResetToken_ = 0;
  if (!device) return false;

  UINT token = 0;
  Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> manager;
  if (FAILED(MFCreateDXGIDeviceManager(&token, &manager)) || !manager) return false;
  if (FAILED(manager->ResetDevice(device, token))) return false;
  d3dManager_ = manager;
  d3dManagerResetToken_ = token;
  return true;
}

bool H264Decoder::configure_input_type() {
  if (!dec_) return false;
  auto try_subtype = [&](const GUID& subtype, const char* subtypeName) -> bool {
    Microsoft::WRL::ComPtr<IMFMediaType> inType;
    if (FAILED(MFCreateMediaType(&inType)) || !inType) return false;
    (void)inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    (void)inType->SetGUID(MF_MT_SUBTYPE, subtype);
    (void)MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, width_, height_);
    (void)MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE, fps_, 1);
    (void)MFSetAttributeRatio(inType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    (void)inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    const HRESULT hr = dec_->SetInputType(0, inType.Get(), 0);
    if (FAILED(hr) && env_truthy_local("REMOTE60_NATIVE_DEBUG_CODEC")) {
      std::string line = "decoder configure input failed subtype=" + std::string(subtypeName) +
                         " hr=" + hr_to_hex(hr);
      codec_debug_log(line.c_str());
    }
    return SUCCEEDED(hr);
  };

  if (std::strcmp(backendName_, "amf_mft_h264dec") == 0) {
    if (try_subtype(MFVideoFormat_H264, "H264")) return true;
    for (DWORD i = 0; i < 16; ++i) {
      IMFMediaType* inTypeRaw = nullptr;
      const HRESULT hr = dec_->GetInputAvailableType(0, i, &inTypeRaw);
      if (hr == MF_E_NO_MORE_TYPES) break;
      if (FAILED(hr) || !inTypeRaw) continue;
      Microsoft::WRL::ComPtr<IMFMediaType> inType;
      inType.Attach(inTypeRaw);
      GUID st{};
      if (SUCCEEDED(inType->GetGUID(MF_MT_SUBTYPE, &st)) && env_truthy_local("REMOTE60_NATIVE_DEBUG_CODEC")) {
        std::string line = "decoder amf: try input subtype=" + guid_to_string(st);
        codec_debug_log(line.c_str());
      }
      if (SUCCEEDED(dec_->SetInputType(0, inType.Get(), 0))) {
        return true;
      }
    }
    return false;
  }

  return try_subtype(MFVideoFormat_H264, "H264");
}

bool H264Decoder::configure_output_type() {
  if (!dec_) return false;
  for (DWORD i = 0;; ++i) {
    IMFMediaType* outTypeRaw = nullptr;
    const HRESULT hr = dec_->GetOutputAvailableType(0, i, &outTypeRaw);
    if (hr == MF_E_NO_MORE_TYPES) return false;
    if (FAILED(hr) || !outTypeRaw) continue;
    Microsoft::WRL::ComPtr<IMFMediaType> outType;
    outType.Attach(outTypeRaw);
    GUID st{};
    if (FAILED(outType->GetGUID(MF_MT_SUBTYPE, &st)) || st != MFVideoFormat_NV12) continue;
    if (SUCCEEDED(dec_->SetOutputType(0, outType.Get(), 0))) {
      outputConfigured_ = true;
      return true;
    }
  }
}

bool H264Decoder::query_output_size(uint32_t* outWidth, uint32_t* outHeight) const {
  if (!dec_ || !outWidth || !outHeight) return false;
  Microsoft::WRL::ComPtr<IMFMediaType> outType;
  if (FAILED(dec_->GetOutputCurrentType(0, &outType)) || !outType) return false;
  UINT32 w = 0;
  UINT32 h = 0;
  if (FAILED(MFGetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, &w, &h))) return false;
  *outWidth = w;
  *outHeight = h;
  return true;
}

bool H264Decoder::initialize(uint32_t width, uint32_t height, uint32_t fps) {
  shutdown();
  codec_debug_log("decoder initialize: start");
  width_ = width;
  height_ = height;
  fps_ = std::max<uint32_t>(1, fps);
  sampleDurationHns_ = std::max<int64_t>(1, 10000000LL / static_cast<int64_t>(fps_));

  IMFTransform* decRaw = nullptr;
  codec_debug_log("decoder initialize: create transform");
  if (!create_h264_decoder_transform(&decRaw, &usingHardware_, &backendName_) || !decRaw) return false;
  codec_debug_log("decoder initialize: transform created");
  dec_.Attach(decRaw);
  (void)set_mf_attr_u32(dec_.Get(), MF_TRANSFORM_ASYNC_UNLOCK, 1);
  if (d3dManager_) {
    codec_debug_log("decoder initialize: set d3d manager");
    (void)dec_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                               reinterpret_cast<ULONG_PTR>(d3dManager_.Get()));
  }
  (void)set_mf_attr_u32(dec_.Get(), MF_LOW_LATENCY, 1);
  (void)set_codecapi_bool(dec_.Get(), CODECAPI_AVLowLatencyMode, true);

  const bool isAmfDecoder = (std::strcmp(backendName_, "amf_mft_h264dec") == 0);
  if (isAmfDecoder) {
    codec_debug_log("decoder initialize(amf): configure output first");
    outputConfigured_ = configure_output_type();
    codec_debug_log("decoder initialize(amf): configure input");
    if (!configure_input_type()) {
      codec_debug_log("decoder initialize(amf): configure input failed");
      shutdown();
      return false;
    }
    if (!outputConfigured_) {
      codec_debug_log("decoder initialize(amf): reconfigure output");
      outputConfigured_ = configure_output_type();
    }
  } else {
    codec_debug_log("decoder initialize: configure input");
    if (!configure_input_type()) {
      codec_debug_log("decoder initialize: configure input failed");
      shutdown();
      return false;
    }
    codec_debug_log("decoder initialize: configure output");
    outputConfigured_ = configure_output_type();
    if (!outputConfigured_) {
      codec_debug_log("decoder initialize: configure output pending/failed");
    }
  }

  (void)dec_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
  (void)dec_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
  codec_debug_log("decoder initialize: stream started");
  started_ = true;
  sampleIndex_ = 0;
  missingOutputTimestampCount_ = 0;
  pendingInputSampleTimesHns_.clear();
  return true;
}

bool H264Decoder::decode_access_unit(const std::vector<uint8_t>& annexb, bool keyFrame,
                                     int64_t inputSampleTimeHns,
                                     std::vector<DecodedFrameNv12>* outFrames) {
  if (!dec_ || !started_ || !outFrames) return false;
  outFrames->clear();

  Microsoft::WRL::ComPtr<IMFSample> sample;
  IMFSample* inputSample = nullptr;
  const int64_t sampleTime = (inputSampleTimeHns > 0)
                                 ? inputSampleTimeHns
                                 : (static_cast<int64_t>(sampleIndex_) * sampleDurationHns_);
  if (!create_input_sample(annexb, sampleTime, sampleDurationHns_, &inputSample)) {
    return false;
  }
  sample.Attach(inputSample);
  if (keyFrame) (void)sample->SetUINT32(MFSampleExtension_CleanPoint, 1);

  auto drain_outputs = [&]() -> bool {
    while (true) {
      if (!outputConfigured_) {
        outputConfigured_ = configure_output_type();
        if (!outputConfigured_) return true;
      }

      MFT_OUTPUT_STREAM_INFO osi{};
      if (FAILED(dec_->GetOutputStreamInfo(0, &osi))) return false;

      Microsoft::WRL::ComPtr<IMFSample> outSample;
      Microsoft::WRL::ComPtr<IMFMediaBuffer> outBuffer;
      MFT_OUTPUT_DATA_BUFFER odb{};
      odb.dwStreamID = 0;

      if ((osi.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == 0) {
        const uint32_t cb = (osi.cbSize > 0) ? static_cast<uint32_t>(osi.cbSize) : (4u << 20);
        if (FAILED(MFCreateSample(&outSample)) || !outSample) return false;
        if (FAILED(MFCreateMemoryBuffer(cb, &outBuffer)) || !outBuffer) return false;
        if (FAILED(outSample->AddBuffer(outBuffer.Get()))) return false;
        odb.pSample = outSample.Get();
      }

      DWORD status = 0;
      const HRESULT po = dec_->ProcessOutput(0, 1, &odb, &status);
      if (po == MF_E_TRANSFORM_NEED_MORE_INPUT) {
        if (odb.pEvents) odb.pEvents->Release();
        break;
      }
      if (po == MF_E_TRANSFORM_STREAM_CHANGE) {
        if (odb.pEvents) odb.pEvents->Release();
        outputConfigured_ = configure_output_type();
        continue;
      }
      if (FAILED(po)) {
        if (odb.pEvents) odb.pEvents->Release();
        return false;
      }

      IMFSample* produced = odb.pSample ? odb.pSample : outSample.Get();
      if (produced) {
        uint32_t outW = width_;
        uint32_t outH = height_;
        (void)query_output_size(&outW, &outH);
        int64_t mappedInputSampleTimeHns = 0;
        if (!pendingInputSampleTimesHns_.empty()) {
          mappedInputSampleTimeHns = pendingInputSampleTimesHns_.front();
          pendingInputSampleTimesHns_.pop_front();
        }

        std::vector<uint8_t> bytes;
        if (!sample_to_bytes(produced, &bytes) || bytes.empty()) {
          (void)sample_to_nv12_bytes_from_2d_buffer(produced, outW, outH, &bytes);
        }
        if (!bytes.empty()) {
          DecodedFrameNv12 frame{};
          frame.width = outW;
          frame.height = outH;

          int64_t outSampleTimeHns = 0;
          if (SUCCEEDED(produced->GetSampleTime(&outSampleTimeHns)) && outSampleTimeHns > 0) {
            frame.sampleTimeHns = outSampleTimeHns;
            frame.sampleTimeFromOutput = true;
          } else {
            frame.sampleTimeHns = mappedInputSampleTimeHns;
            frame.sampleTimeFromOutput = false;
            if (frame.sampleTimeHns > 0) {
              ++missingOutputTimestampCount_;
              if (env_truthy_local("REMOTE60_NATIVE_DEBUG_CODEC") &&
                  (missingOutputTimestampCount_ % 120ULL) == 1ULL) {
                std::string line = "decoder output sample_time missing, fallback count=" +
                                   std::to_string(missingOutputTimestampCount_);
                codec_debug_log(line.c_str());
              }
            }
          }
          frame.bytes = std::move(bytes);
          outFrames->push_back(std::move(frame));
        }
      }
      if (odb.pEvents) odb.pEvents->Release();
    }
    return true;
  };

  HRESULT inHr = dec_->ProcessInput(0, sample.Get(), 0);
  if (inHr == MF_E_NOTACCEPTING) {
    // Decoder queue is full: drain first so latest input can be accepted.
    if (!drain_outputs()) return false;
    inHr = dec_->ProcessInput(0, sample.Get(), 0);
  }
  if (FAILED(inHr)) return false;
  ++sampleIndex_;
  pendingInputSampleTimesHns_.push_back(sampleTime);
  constexpr size_t kPendingInputTimestampMax = 512;
  if (pendingInputSampleTimesHns_.size() > kPendingInputTimestampMax) {
    pendingInputSampleTimesHns_.pop_front();
  }

  return drain_outputs();
}

void H264Decoder::reset() {
  if (!dec_) return;
  (void)dec_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
  sampleIndex_ = 0;
  pendingInputSampleTimesHns_.clear();
}

void H264Decoder::shutdown() {
  if (dec_) {
    (void)dec_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    (void)dec_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
    dec_.Reset();
  }
  started_ = false;
  outputConfigured_ = false;
  sampleIndex_ = 0;
  usingHardware_ = false;
  backendName_ = "unknown";
  missingOutputTimestampCount_ = 0;
  pendingInputSampleTimesHns_.clear();
  d3dManager_.Reset();
  d3dManagerResetToken_ = 0;
}

}  // namespace remote60::native_poc
