#pragma once

// The H.264 decoder and the codec/transport choice it serves (Phase 1-13 state struct).
//
// Role:    which codec/transport the session runs, Media Foundation start flag, the H264Decoder,
//          its readiness and size, the optional D3D11 device shared with the presenter (DXGI
//          decode-surface opt-in), the keyframe-wait latch, and the selection epoch the recv loop
//          last reset the decoder for.
// Thread:  main creates/configures before the threads start and shuts down after they join; the
//          recv thread is the only user in between (MFT submit/drain on one thread).
// Input:   args (codec/transport), startup env, encoded frames.
// Output:  decoded NV12 frames / surfaces for FrameBuffer.
// Callers: main() (startup/shutdown), recv thread.
//
// Fields are the former main() locals useRaw / useH264 / transport / mfStarted / decoder /
// decoderReady / waitForKeyFrame / decoderW / decoderH / decD3dDevice / decD3dContext and the
// recv lambda's recvSelectionEpoch, initial values unchanged (viewer split refactor Phase 1-13):
// waitForKeyFrame = useH264 and recvSelectionEpoch are assigned where the locals were initialised.

#include "viewer_common.hpp"

namespace remote60::native_poc::viewer {

struct DecoderState {
  bool useRaw = false;
  bool useH264 = false;
  VideoTransport transport = VideoTransport::Tcp;
  bool mfStarted = false;
  H264Decoder decoder;
  bool decoderReady = false;
  uint32_t decoderW = 0;
  uint32_t decoderH = 0;
  Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext;
  // Which selection generation the recv loop has already reset the decoder for. A bump by
  // begin_pc_target_selection() on the UI thread makes the next frame flush stale references.
  uint64_t recvSelectionEpoch = 0;  // set from gSel.epoch at thread start
};

}  // namespace remote60::native_poc::viewer
