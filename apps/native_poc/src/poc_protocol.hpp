#pragma once

#include <cstdint>

namespace remote60::native_poc {

constexpr uint32_t kMagic = 0x31435052;  // "RPC1"

enum class MessageType : uint16_t {
  FrameTick = 1,
  Ack = 2,
  RawFrameBgra = 10,
  EncodedFrameH264 = 11,
  ControlPing = 20,
  ControlPong = 21,
  ControlInputEvent = 22,
  ControlInputAck = 23,
  ControlClientMetrics = 24,
  ControlRequestKeyFrame = 25,
  ControlRuntimeEncoderConfig = 26,
  ControlCaptureModeRequest = 27,
  ControlWindowListRequest = 28,
  ControlWindowList = 29,
  ControlWindowSelect = 30,
  ControlWindowSelected = 31,
  ControlInputText = 32,
  ControlStreamState = 33,
  ControlDesktopBackendRequest = 34,
  ControlWindowThumbnailRequest = 35,
  ControlWindowThumbnail = 36,
};

enum class UdpPacketKind : uint16_t {
  Hello = 300,
  HelloAck = 301,
  VideoChunk = 302,
  // Sent only to open a NAT mapping towards a peer whose address came from the directory
  // service. It carries no state and the receiver discards it; the real session still starts
  // with Hello/HelloAck.
  Punch = 303,
  // Control traffic carried over the media socket. A separate TCP control connection cannot
  // survive hole punching -- only the punched UDP path reaches a host behind NAT -- so the
  // control protocol is tunnelled through these, message-framed and made reliable.
  ControlData = 304,
  ControlAck = 305,
  ControlNack = 306,
};

// Control messages are numbered per direction so a peer can tell a retransmission from a new
// message. Stream ids are fixed by role rather than negotiated.
constexpr uint32_t kUdpControlStreamClientToHost = 1;
constexpr uint32_t kUdpControlStreamHostToClient = 2;
constexpr uint16_t kUdpControlMaxMissingPerNack = 64;

enum class UdpCodec : uint16_t {
  Raw = 1,
  H264 = 2,
};

constexpr uint32_t kControlWindowListMaxEntries = 64;
constexpr uint32_t kControlInputTextMaxUtf16 = 64;

// Window preview thumbnails. There is no version handshake on the control channel, so a peer
// that predates these messages must never be sent one: unknown opcodes are drained without a
// reply, which would stall the requester's strict request/response loop. The host therefore
// advertises support via kControlWindowListFlagThumbnails and clients only ask when it is set.
constexpr uint32_t kControlWindowListFlagSelectionLocked = 0x1u;
constexpr uint32_t kControlWindowListFlagThumbnails = 0x2u;
constexpr uint32_t kWindowThumbnailMaxWidth = 320;
constexpr uint32_t kWindowThumbnailMaxHeight = 320;
// BGRA, so the cap covers the largest thumbnail plus slack. Nothing else bounds payloadSize.
constexpr uint32_t kWindowThumbnailMaxPayloadBytes = 320u * 320u * 4u;

#pragma pack(push, 1)
struct MessageHeader {
  uint32_t magic = kMagic;
  uint16_t type = 0;
  uint16_t size = 0;
};

struct FrameTickMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint64_t sendQpcUs = 0;
};

struct AckMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint64_t hostSendQpcUs = 0;
  uint64_t clientRecvQpcUs = 0;
};

struct RawFrameHeader {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride = 0;
  uint32_t payloadSize = 0;
  uint64_t streamGeneration = 0;
  uint64_t captureQpcUs = 0;
  uint64_t encodeStartQpcUs = 0;
  uint64_t encodeEndQpcUs = 0;
  uint64_t sendQpcUs = 0;
};

struct EncodedFrameHeader {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t payloadSize = 0;
  uint32_t flags = 0;  // bit0: keyFrame
  uint64_t streamGeneration = 0;
  uint64_t captureQpcUs = 0;
  uint64_t encodeStartQpcUs = 0;
  uint64_t encodeEndQpcUs = 0;
  uint64_t sendQpcUs = 0;
};

struct ControlPingMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint64_t clientSendQpcUs = 0;
};

struct ControlPongMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint64_t clientSendQpcUs = 0;
  uint64_t hostRecvQpcUs = 0;
  uint64_t hostSendQpcUs = 0;
  uint32_t captureTargetPid = 0;
  uint32_t captureTargetFlags = 0;   // bit0: windowTargetEnabled, bit1: clientAreaOnly
  uint32_t captureRebindCount = 0;
  uint64_t captureTargetHwnd = 0;
  char captureTargetProcess[32] = {};
  char captureTargetTitle[96] = {};
};

struct ControlInputEventMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint16_t kind = 0;     // 1:mouse_move 2:mouse_down 3:mouse_up 4:wheel 5:key_down 6:key_up
  uint16_t buttons = 0;  // bit0:left bit1:right bit2:middle
  int32_t x = 0;         // client-local coordinates
  int32_t y = 0;
  int32_t wheelDelta = 0;
  uint32_t keyCode = 0;
  uint64_t clientSendQpcUs = 0;
};

struct ControlInputAckMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint64_t hostRecvQpcUs = 0;
  uint64_t hostSendQpcUs = 0;
};

struct ControlInputTextMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint16_t utf16Count = 0;
  uint16_t reserved = 0;
  uint16_t utf16[kControlInputTextMaxUtf16] = {};
  uint64_t clientSendQpcUs = 0;
};

struct ControlStreamStateMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t flags = 0;  // bit0: stream active
  uint64_t clientSendQpcUs = 0;
};

struct ControlClientMetricsMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t recvFpsX100 = 0;
  uint32_t decodedFpsX100 = 0;
  uint32_t recvMbpsX1000 = 0;
  uint32_t skippedFrames = 0;
  uint64_t avgLatencyUs = 0;
  uint64_t maxLatencyUs = 0;
  uint64_t avgDecodeTailUs = 0;
  uint64_t maxDecodeTailUs = 0;
  uint32_t congestionState = 0;           // 0:normal 1:recovering 2:congested
  uint32_t congestionTransitions = 0;     // cumulative state transitions
  uint32_t congestionRecoveryCount = 0;   // cumulative recoveries to normal
  uint32_t congestionRecoveryReq = 0;     // cumulative keyframe/recovery requests
  uint32_t congestionRecoveryMaxUs = 0;   // max recovery duration (us)
  uint32_t queueDepthMax = 0;             // max estimated queue depth (frames)
  uint32_t queueDepthH4p = 0;             // histogram bucket: 4+ frames
  uint32_t udpAssemblyDropPm = 0;         // latest assembly drop permille
  uint64_t clientSendQpcUs = 0;
  // What the viewer actually sees. Everything above describes frames arriving and decoding,
  // which stays healthy while playback visibly stutters -- the interval between frames
  // reaching the display is the only number that tracks perceived smoothness.
  uint32_t presentTargetIntervalUs = 0;   // 0 when the client does not report presentation
  uint32_t presentFpsX100 = 0;
  uint32_t presentGapP50Us = 0;
  uint32_t presentGapP95Us = 0;
  uint32_t presentGapMaxUs = 0;
  uint32_t presentOver1_5xCount = 0;      // frames late by half a period or more, this window
  uint32_t presentOver2xCount = 0;        // a whole missing frame, this window
  uint32_t presentSampleCount = 0;
  // Why the gaps look the way they do. A frame handed to the display with a schedule lands
  // where the playout clock put it; one released immediately landed wherever it happened to
  // arrive, which is what stutter is made of. Reanchors are the playout clock itself jumping.
  uint32_t presentScheduledCount = 0;
  uint32_t presentImmediateCount = 0;
  uint32_t presentReanchorCount = 0;
  // Counted by the view itself, once per frame it latched. Every other number here counts
  // frames handed to the display; this one counts frames that reached it.
  uint32_t presentDisplayedCount = 0;
};

struct ControlRequestKeyFrameMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint16_t reason = 0;  // 1:catchup 2:udp_assembly_drop 3:waiting_keyframe 4:decode_fail
  uint16_t reserved = 0;
  uint64_t clientSendQpcUs = 0;
};

struct ControlRuntimeEncoderConfigMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t bitrate = 0;  // bps
  uint32_t keyint = 0;   // frames
  uint32_t fps = 0;      // frames per second
  uint32_t flags = 0;    // bit0: bitrate valid, bit1: keyint valid, bit2: fps valid
  uint64_t clientSendQpcUs = 0;
};

struct ControlDesktopBackendRequestMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint16_t backend = 0;  // 1:dxgi, 2:wgc
  uint16_t flags = 0;
  uint64_t clientSendQpcUs = 0;
};

struct ControlCaptureModeRequestMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint16_t mode = 0;   // 1:overview-monitor, 2:focus-window-at-point
  uint16_t flags = 0;  // reserved
  uint32_t xPermille = 0;  // 0..10000
  uint32_t yPermille = 0;  // 0..10000
  uint64_t clientSendQpcUs = 0;
};

struct ControlWindowListRequestMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t flags = 0;
  uint64_t clientSendQpcUs = 0;
};

struct ControlWindowEntry {
  uint64_t id = 0;
  uint32_t pid = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t flags = 0;  // bit0:minimized
  char title[96] = {};
};

struct ControlWindowListMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t flags = 0;  // bit0: selection locked by config
  uint64_t selectedWindowId = 0;
  uint32_t itemCount = 0;
  uint32_t reserved = 0;
  ControlWindowEntry items[kControlWindowListMaxEntries] = {};
};

struct ControlWindowThumbnailRequestMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t flags = 0;
  uint64_t windowId = 0;  // 0 selects the desktop preview
  uint32_t maxWidth = 0;
  uint32_t maxHeight = 0;
  uint64_t clientSendQpcUs = 0;
};

// header.size describes only this fixed part; payloadSize bytes of BGRA follow, matching the
// RawFrameHeader/EncodedFrameHeader convention used on the video path.
struct ControlWindowThumbnailHeader {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t flags = 0;  // bit0: ok, bit1: unchanged since requested version
  uint64_t windowId = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride = 0;
  uint32_t payloadSize = 0;
  uint64_t version = 0;
};

struct ControlWindowSelectMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t flags = 0;
  uint64_t windowId = 0;
  uint64_t clientSendQpcUs = 0;
};

struct ControlWindowSelectedMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t flags = 0;  // bit0: ok, bit1: selection locked by config
  uint64_t windowId = 0;
  uint64_t streamGeneration = 0;
  char reason[64] = {};
  char title[96] = {};
  uint64_t hostSendQpcUs = 0;
};

// Capabilities each side advertises in its Hello. Unknown bits must be ignored, so a peer can
// gain a feature without breaking older peers. Media encryption is not implemented yet; the bit
// is claimed now so that adding it later is a field to fill in rather than a format change.
constexpr uint32_t kUdpFeatureEncryptedMedia = 0x1u;
constexpr uint32_t kUdpFeatureVideoFec = 0x2u;
constexpr uint32_t kUdpFeatureDirectoryAuth = 0x4u;
// Parity groups built from every Nth chunk instead of N consecutive ones. Same packet count,
// same bandwidth -- but Wi-Fi loses packets in bursts, and consecutive grouping puts a whole
// burst inside one group, where a single XOR parity repairs none of it. Interleaved, a burst
// hits each group once and every packet in it is recoverable. Negotiated rather than assumed
// so a host and viewer built at different times still agree on the layout.
constexpr uint32_t kUdpFeatureVideoFecInterleaved = 0x8u;
constexpr uint32_t kUdpProtocolVersion = 2u;

struct UdpHelloPacket {
  uint32_t magic = kMagic;
  uint16_t kind = static_cast<uint16_t>(UdpPacketKind::Hello);
  uint16_t size = static_cast<uint16_t>(sizeof(UdpHelloPacket));
  uint32_t version = kUdpProtocolVersion;
  uint32_t features = kUdpFeatureVideoFec | kUdpFeatureVideoFecInterleaved;
  // One-time capability returned by /api/connect and delivered independently to the host.
  // Empty preserves direct-LAN operation, but cannot authorize SYSTEM secure-desktop input.
  char authToken[33] = {};
};

// One fragment of a control message. Fragments of a message are sent back to back; the
// receiver asks for what is missing rather than the sender waiting for each piece.
struct UdpControlChunkHeader {
  uint32_t magic = kMagic;
  uint16_t kind = static_cast<uint16_t>(UdpPacketKind::ControlData);
  uint16_t size = static_cast<uint16_t>(sizeof(UdpControlChunkHeader));
  uint32_t streamId = 0;
  uint32_t messageSeq = 0;
  uint32_t totalSize = 0;
  uint16_t fragIndex = 0;
  uint16_t fragCount = 0;
  uint32_t fragOffset = 0;
  uint32_t fragSize = 0;
};

// Acknowledges a fully assembled message (kind ControlAck), or asks for the fragments listed
// in missing[] (kind ControlNack). Both carry the same shape so one handler covers them.
struct UdpControlAckPacket {
  uint32_t magic = kMagic;
  uint16_t kind = static_cast<uint16_t>(UdpPacketKind::ControlAck);
  uint16_t size = static_cast<uint16_t>(sizeof(UdpControlAckPacket));
  uint32_t streamId = 0;
  uint32_t messageSeq = 0;
  uint16_t missingCount = 0;
  uint16_t reserved = 0;
  uint16_t missing[kUdpControlMaxMissingPerNack] = {};
};

struct UdpVideoChunkHeader {
  uint32_t magic = kMagic;
  uint16_t kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
  uint16_t size = static_cast<uint16_t>(sizeof(UdpVideoChunkHeader));
  uint32_t seq = 0;
  uint16_t codec = static_cast<uint16_t>(UdpCodec::H264);
  // bit0:keyFrame bit1:firstChunk bit2:lastChunk bit3:reserved for encrypted payload
  // bit4:XOR parity packet -- covers the group beginning at chunkIndex
  // bit5:that parity group is interleaved, so chunkIndex names the group itself and the
  //      group holds chunks chunkIndex, chunkIndex+groupCount, chunkIndex+2*groupCount ...
  uint16_t flags = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride = 0;
  uint32_t payloadSize = 0;
  uint32_t chunkOffset = 0;
  uint32_t chunkSize = 0;
  uint16_t chunkIndex = 0;
  uint16_t chunkCount = 0;
  uint32_t chunkStride = 0;
  uint64_t streamGeneration = 0;
  uint64_t captureQpcUs = 0;
  uint64_t encodeStartQpcUs = 0;
  uint64_t encodeEndQpcUs = 0;
  uint64_t sendQpcUs = 0;
};
constexpr uint16_t kUdpVideoFecGroupSize = 8;
#pragma pack(pop)

}  // namespace remote60::native_poc
