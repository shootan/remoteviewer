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
};

enum class UdpPacketKind : uint16_t {
  Hello = 300,
  HelloAck = 301,
  VideoChunk = 302,
};

enum class UdpCodec : uint16_t {
  Raw = 1,
  H264 = 2,
};

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
  uint64_t clientSendQpcUs = 0;
};

struct ControlRequestKeyFrameMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint16_t reason = 0;  // 1:catchup 2:udp_assembly_drop 3:waiting_keyframe 4:decode_fail
  uint16_t reserved = 0;
  uint64_t clientSendQpcUs = 0;
};

struct UdpHelloPacket {
  uint32_t magic = kMagic;
  uint16_t kind = static_cast<uint16_t>(UdpPacketKind::Hello);
  uint16_t size = static_cast<uint16_t>(sizeof(UdpHelloPacket));
  uint32_t version = 1;
  uint32_t reserved = 0;
};

struct UdpVideoChunkHeader {
  uint32_t magic = kMagic;
  uint16_t kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
  uint16_t size = static_cast<uint16_t>(sizeof(UdpVideoChunkHeader));
  uint32_t seq = 0;
  uint16_t codec = static_cast<uint16_t>(UdpCodec::H264);
  uint16_t flags = 0;  // bit0:keyFrame bit1:firstChunk bit2:lastChunk
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride = 0;
  uint32_t payloadSize = 0;
  uint32_t chunkOffset = 0;
  uint32_t chunkSize = 0;
  uint64_t captureQpcUs = 0;
  uint64_t encodeStartQpcUs = 0;
  uint64_t encodeEndQpcUs = 0;
  uint64_t sendQpcUs = 0;
};
#pragma pack(pop)

}  // namespace remote60::native_poc
