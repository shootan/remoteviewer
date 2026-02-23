#include "audio_runtime.hpp"

#include <Audioclient.h>
#include <Mmdeviceapi.h>
#include <comdef.h>
#include <opus/opus.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "common/opus_rtp_packetizer.hpp"

namespace remote60::host {
namespace {

bool is_supported_opus_rate(uint32_t r) {
  return r == 8000 || r == 12000 || r == 16000 || r == 24000 || r == 48000;
}

struct SampleLayout {
  bool ok = false;
  bool isFloat = false;
  uint16_t bitsPerSample = 0;
  uint16_t channels = 0;
};

SampleLayout parse_layout(const WAVEFORMATEX* wfx) {
  SampleLayout out;
  if (!wfx) return out;
  out.channels = wfx->nChannels;
  if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && wfx->wBitsPerSample == 32) {
    out.ok = true;
    out.isFloat = true;
    out.bitsPerSample = 32;
    return out;
  }
  if (wfx->wFormatTag == WAVE_FORMAT_PCM && wfx->wBitsPerSample == 16) {
    out.ok = true;
    out.isFloat = false;
    out.bitsPerSample = 16;
    return out;
  }
  if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
    const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
    if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT && wfx->wBitsPerSample == 32) {
      out.ok = true;
      out.isFloat = true;
      out.bitsPerSample = 32;
      return out;
    }
    if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_PCM && wfx->wBitsPerSample == 16) {
      out.ok = true;
      out.isFloat = false;
      out.bitsPerSample = 16;
      return out;
    }
  }
  return out;
}

void append_stereo_float(const BYTE* data, UINT32 frames, const SampleLayout& layout, std::vector<float>& outStereo) {
  if (!data || frames == 0 || !layout.ok || layout.channels == 0) return;
  const uint16_t ch = layout.channels;

  if (layout.isFloat) {
    const auto* p = reinterpret_cast<const float*>(data);
    for (UINT32 i = 0; i < frames; ++i) {
      const float l = p[i * ch + 0];
      const float r = (ch >= 2) ? p[i * ch + 1] : l;
      outStereo.push_back(l);
      outStereo.push_back(r);
    }
  } else {
    const auto* p = reinterpret_cast<const int16_t*>(data);
    constexpr float kScale = 1.0f / 32768.0f;
    for (UINT32 i = 0; i < frames; ++i) {
      const float l = static_cast<float>(p[i * ch + 0]) * kScale;
      const float r = (ch >= 2) ? static_cast<float>(p[i * ch + 1]) * kScale : l;
      outStereo.push_back(l);
      outStereo.push_back(r);
    }
  }
}

void resample_linear_stereo(const std::vector<float>& inStereo, uint32_t inRate, uint32_t outRate,
                            std::vector<float>& outStereo) {
  if (inStereo.empty() || inRate == 0 || outRate == 0) return;
  if (inRate == outRate) {
    outStereo.insert(outStereo.end(), inStereo.begin(), inStereo.end());
    return;
  }

  const size_t inFrames = inStereo.size() / 2;
  const double ratio = static_cast<double>(outRate) / static_cast<double>(inRate);
  const size_t outFrames = static_cast<size_t>(std::floor(static_cast<double>(inFrames) * ratio));
  outStereo.reserve(outStereo.size() + outFrames * 2);

  for (size_t i = 0; i < outFrames; ++i) {
    const double srcPos = static_cast<double>(i) / ratio;
    size_t idx0 = static_cast<size_t>(srcPos);
    size_t idx1 = (std::min)(idx0 + 1, inFrames - 1);
    const float a = static_cast<float>(srcPos - static_cast<double>(idx0));

    const float l0 = inStereo[idx0 * 2 + 0];
    const float r0 = inStereo[idx0 * 2 + 1];
    const float l1 = inStereo[idx1 * 2 + 0];
    const float r1 = inStereo[idx1 * 2 + 1];
    outStereo.push_back(l0 + (l1 - l0) * a);
    outStereo.push_back(r0 + (r1 - r0) * a);
  }
}

}  // namespace

AudioProbeStats run_audio_loopback_probe_seconds(int seconds) {
  AudioProbeStats out;

  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool comInited = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
  const bool shouldUninitialize = SUCCEEDED(hr);
  if (!comInited) {
    out.detail = "com_init_failed";
    return out;
  }

  IMMDeviceEnumerator* enumerator = nullptr;
  IMMDevice* device = nullptr;
  IAudioClient* audioClient = nullptr;
  IAudioCaptureClient* captureClient = nullptr;
  WAVEFORMATEX* mix = nullptr;

  OpusEncoder* enc = nullptr;
  int opusErr = OPUS_OK;

  do {
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr) || !enumerator) {
      out.detail = "mmdevice_enum_failed";
      break;
    }

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr) || !device) {
      out.detail = "default_render_device_failed";
      break;
    }
    out.deviceOk = true;

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&audioClient));
    if (FAILED(hr) || !audioClient) {
      out.detail = "audio_client_activate_failed";
      break;
    }

    hr = audioClient->GetMixFormat(&mix);
    if (FAILED(hr) || !mix) {
      out.detail = "get_mix_format_failed";
      break;
    }
    out.sampleRate = mix->nSamplesPerSec;
    out.channels = mix->nChannels;

    const auto layout = parse_layout(mix);
    if (!layout.ok) {
      out.detail = "unsupported_pcm_format";
      break;
    }

    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, mix, nullptr);
    if (FAILED(hr)) {
      out.detail = "audio_client_init_loopback_failed";
      break;
    }
    out.clientOk = true;

    hr = audioClient->GetService(IID_PPV_ARGS(&captureClient));
    if (FAILED(hr) || !captureClient) {
      out.detail = "capture_client_get_service_failed";
      break;
    }

    const uint32_t opusRate = is_supported_opus_rate(out.sampleRate) ? out.sampleRate : 48000;
    const int opusFrame = static_cast<int>(opusRate / 50);  // 20ms
    enc = opus_encoder_create(static_cast<opus_int32>(opusRate), 2, OPUS_APPLICATION_AUDIO, &opusErr);
    if (!enc || opusErr != OPUS_OK) {
      out.detail = "opus_encoder_create_failed";
      break;
    }
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(64000));
    opus_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));

    hr = audioClient->Start();
    if (FAILED(hr)) {
      out.detail = "audio_start_failed";
      break;
    }
    out.loopbackStarted = true;

    const auto t0 = std::chrono::steady_clock::now();
    const auto until = t0 + std::chrono::seconds(seconds);

    std::vector<float> pcmStereoQueue;
    pcmStereoQueue.reserve(static_cast<size_t>(opusRate) * 4);

    remote60::common::OpusRtpPacketizer rtp;
    uint16_t seq = 1000;
    uint32_t ts = opusRate;
    std::vector<uint8_t> opusOut(4000);

    while (std::chrono::steady_clock::now() < until) {
      UINT32 packetFrames = 0;
      hr = captureClient->GetNextPacketSize(&packetFrames);
      if (FAILED(hr)) {
        out.detail = "get_next_packet_failed";
        break;
      }

      if (packetFrames == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        continue;
      }

      BYTE* data = nullptr;
      UINT32 numFrames = 0;
      DWORD flags = 0;
      hr = captureClient->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
      if (FAILED(hr)) {
        out.detail = "get_buffer_failed";
        break;
      }

      out.packets++;
      out.pcmFrames += numFrames;

      std::vector<float> chunkStereo;
      chunkStereo.reserve(static_cast<size_t>(numFrames) * 2);
      if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0) {
        append_stereo_float(data, numFrames, layout, chunkStereo);
      } else {
        chunkStereo.resize(static_cast<size_t>(numFrames) * 2, 0.0f);
      }

      if (out.sampleRate == opusRate) {
        pcmStereoQueue.insert(pcmStereoQueue.end(), chunkStereo.begin(), chunkStereo.end());
      } else {
        resample_linear_stereo(chunkStereo, out.sampleRate, opusRate, pcmStereoQueue);
      }

      while (pcmStereoQueue.size() >= static_cast<size_t>(opusFrame * 2)) {
        const int encoded = opus_encode_float(enc, pcmStereoQueue.data(), opusFrame, opusOut.data(),
                                              static_cast<opus_int32>(opusOut.size()));
        if (encoded <= 0) {
          out.detail = "opus_encode_failed";
          break;
        }

        out.opusFrames20ms++;
        out.opusBytes += static_cast<uint64_t>(encoded);

        std::vector<uint8_t> payload(opusOut.begin(), opusOut.begin() + encoded);
        auto pkt = rtp.packetize_20ms_frame(payload, seq, ts);
        out.rtpPackets++;
        if (out.rtpPackets == 1) {
          out.firstSeq = pkt.sequence;
          out.firstTimestamp = pkt.timestamp;
        }
        out.lastSeq = pkt.sequence;
        out.lastTimestamp = pkt.timestamp;

        pcmStereoQueue.erase(pcmStereoQueue.begin(), pcmStereoQueue.begin() + (opusFrame * 2));
      }

      captureClient->ReleaseBuffer(numFrames);
      if (out.detail == "opus_encode_failed") break;
    }

    const auto t1 = std::chrono::steady_clock::now();
    out.elapsedSec = std::chrono::duration<double>(t1 - t0).count();
    if (out.detail.empty()) out.detail = "ok";

  } while (false);

  if (audioClient && out.loopbackStarted) audioClient->Stop();
  if (enc) opus_encoder_destroy(enc);
  if (mix) CoTaskMemFree(mix);
  if (captureClient) captureClient->Release();
  if (audioClient) audioClient->Release();
  if (device) device->Release();
  if (enumerator) enumerator->Release();

  if (shouldUninitialize) CoUninitialize();
  return out;
}

}  // namespace remote60::host
