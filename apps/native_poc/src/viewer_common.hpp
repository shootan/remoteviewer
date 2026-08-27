#pragma once

// Shared prelude of the GNLinkViewer translation units.
//
// Role:    the include block and using-declarations native_video_client_main.cpp opened with, so
//          every viewer_* module compiles the moved code with the same names in scope.
// Thread:  none (declarations only).
// Input:   -
// Output:  namespace remote60::native_poc::viewer with the protocol/codec names imported.
// Callers: every viewer_*.cpp/.hpp and native_video_client_main.cpp.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-0).

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <imm.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <mfapi.h>
#include <wrl/client.h>

#include "client_macro_window.hpp"
#include "client_session_toolbar.hpp"
#include "directory_session_bootstrap.hpp"
#include "directory_session_client.hpp"
#include "input_macro.hpp"
#include "mf_h264_codec.hpp"
#include "json_profile.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_client_tcp_control.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

#pragma comment(lib, "Msimg32.lib")
#pragma comment(lib, "Imm32.lib")

#ifndef REMOTE60_NATIVE_ENCODED_EXPERIMENT
#define REMOTE60_NATIVE_ENCODED_EXPERIMENT 0
#endif

namespace remote60::native_poc::viewer {

using remote60::native_poc::ControlInputAckMessage;
using remote60::native_poc::ControlInputEventMessage;
using remote60::native_poc::ControlInputTextMessage;
using remote60::native_poc::ControlClientMetricsMessage;
using remote60::native_poc::ControlRequestKeyFrameMessage;
using remote60::native_poc::ControlRuntimeEncoderConfigMessage;
using remote60::native_poc::ControlCaptureModeRequestMessage;
using remote60::native_poc::ControlWindowEntry;
using remote60::native_poc::ControlWindowListMessage;
using remote60::native_poc::ControlWindowListRequestMessage;
using remote60::native_poc::ControlWindowSelectMessage;
using remote60::native_poc::ControlWindowSelectedMessage;
using remote60::native_poc::ControlPingMessage;
using remote60::native_poc::ControlPongMessage;
using remote60::native_poc::ClientInputQueue;
using remote60::native_poc::CaptureModeRequestState;
using remote60::native_poc::ClientControlMetricsSnapshot;
using remote60::native_poc::ClientControlScheduler;
using remote60::native_poc::DecodedFrameNv12;
using remote60::native_poc::EncodedFrameHeader;
using remote60::native_poc::H264Decoder;
using remote60::native_poc::KeyframeRequestState;
using remote60::native_poc::MessageHeader;
using remote60::native_poc::MessageType;
using remote60::native_poc::ControlOutboundAction;
using remote60::native_poc::ControlOutboundActionKind;
using remote60::native_poc::RawFrameHeader;
using remote60::native_poc::QueuedControlInputMessage;
using remote60::native_poc::RuntimeTuneState;
using remote60::native_poc::TcpControlResponse;
using remote60::native_poc::TcpControlResponseKind;
using remote60::native_poc::UdpH264AssemblyDisposition;
using remote60::native_poc::UdpH264FrameAssembler;
using remote60::native_poc::UdpCodec;
using remote60::native_poc::UdpHelloPacket;
using remote60::native_poc::UdpPacketKind;
using remote60::native_poc::UdpVideoChunkHeader;
using remote60::native_poc::VideoTransport;
using remote60::native_poc::WindowPanelSnapshot;
using remote60::native_poc::WindowPanelStateModel;
using remote60::native_poc::WindowTargetUiEntry;
using remote60::native_poc::nv12_to_bgra;
using remote60::native_poc::clamp_udp_mtu;
using remote60::native_poc::parse_video_transport;
using remote60::native_poc::qpc_now_us;
using remote60::native_poc::video_transport_name;
namespace json_profile = remote60::native_poc::json_profile;

}  // namespace remote60::native_poc::viewer
