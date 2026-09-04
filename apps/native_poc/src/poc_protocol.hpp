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
  // Desktop mode used to mean "the primary monitor", which is only the whole desktop when there
  // is one. These name the others so a client can ask for the second screen.
  ControlMonitorListRequest = 37,
  ControlMonitorList = 38,
  ControlMonitorSelect = 39,
  // Sealed unlock v1 (lock-screen password over an encrypted, replay-bound challenge). Advertised
  // via kCaptureFlagUnlockSealedV1; an old peer is never sent these.
  ControlUnlockChallengeRequest = 40,
  ControlUnlockChallenge = 41,
  ControlUnlockSealedRequest = 42,
  ControlUnlockAccepted = 43,
  ControlUnlockStatusRequest = 44,
  ControlUnlockStatusResult = 45,
  // Host-side IME v1: raw physical key (vk+scan+flags) so the host IME composes live. Advertised via
  // kCaptureFlagHostImeV1; sent only when the viewer opts in and the host advertised support.
  ControlPhysicalKey = 46,
  // Host-side IME v2: the viewer asks the host to align its IME (to English) for the current target
  // and report the authoritative state; the host answers with ControlImeStateResponse. Gated on
  // kCaptureFlagHostImePulseStateV2 so an old v1 host never sees these (it would drop them silently).
  ControlImeStateRequest = 47,
  ControlImeStateResponse = 48,
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
  // Hardware-cursor position report (UdpCursorPosPacket). Latest-wins and unreliable by design;
  // an old viewer drops the unknown kind before its video-size guard, so both directions stay
  // compatible without a handshake.
  CursorPos = 307,
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
// Same reasoning as thumbnails: the monitor messages are newer than some hosts, and an unknown
// opcode is drained without a reply, which would hang a client waiting for one. The window list
// is fetched on every connect, so it is where support gets advertised.
constexpr uint32_t kControlWindowListFlagMonitors = 0x4u;
// Two or three screens is the normal case and a dozen is somebody's video wall; the cap only
// bounds the message.
constexpr uint32_t kControlMonitorListMaxEntries = 8;
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

// EncodedFrameHeader::flags. bit1 marks a frame the host produced by re-encoding its cached
// picture -- a trailing-edge kick or the 1Hz static refresh -- rather than from new capture. The
// pixels are right, but the frame says nothing about the pipeline: its capture stamp is the kick
// time, so a viewer that folds it into latency / decode-tail averages, the catch-up trigger or the
// "real" fps reads a static desktop as a stalled one (avgLatency 0.6~0.8s in the field), and the
// host's ABR -- which takes those averages as input -- can demote quality for nothing. (Viewer
// ledger F-10 / host H-13.) On the UDP wire the same fact rides UdpVideoChunkHeader bit6.
constexpr uint32_t kEncodedFrameFlagKeyFrame = 0x1u;
constexpr uint32_t kEncodedFrameFlagSynthetic = 0x2u;
constexpr uint16_t kUdpVideoChunkFlagSynthetic = 0x40u;

struct EncodedFrameHeader {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t payloadSize = 0;
  uint32_t flags = 0;  // bit0: keyFrame  bit1: synthetic refresh (kEncodedFrameFlagSynthetic)
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

// Bits of ControlPongMessage::captureTargetFlags.
constexpr uint32_t kCaptureFlagWindowTargetEnabled = 0x1u;
constexpr uint32_t kCaptureFlagClientAreaOnly = 0x2u;
constexpr uint32_t kCaptureFlagSecureDesktopActive = 0x4u;
// The host advertises sealed-unlock v1 support here so a client only sends the unlock messages to a
// host that understands them (an old host drains an unknown control opcode without replying, which
// would hang the client's strict request/response loop). Control-level so TCP and UDP both see it.
constexpr uint32_t kCaptureFlagUnlockSealedV1 = 0x8u;
// The host advertises host-side IME (raw physical-key injection) support here.
constexpr uint32_t kCaptureFlagHostImeV1 = 0x10u;
// Host-side IME v2: host understands the make-only (Hangul/Hanja pulse) physical-key flag and the
// ControlImeStateRequest/Response handshake. The default-on host-IME path uses the v2 pulse/state
// features only when this is advertised; a v1-only host falls back to legacy client-side IME so the
// new viewer never strands a make-only key or blocks on a state request an old host cannot answer.
constexpr uint32_t kCaptureFlagHostImePulseStateV2 = 0x20u;

struct ControlPongMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint64_t clientSendQpcUs = 0;
  uint64_t hostRecvQpcUs = 0;
  uint64_t hostSendQpcUs = 0;
  uint32_t captureTargetPid = 0;
  // bit0: windowTargetEnabled, bit1: clientAreaOnly, bit2: secureDesktopActive
  //
  // bit2 says the input desktop is not the ordinary one -- a UAC consent prompt or the lock
  // screen is in front. Nothing in the product can capture that desktop yet, so the picture is
  // frozen or blank while it is set, and a viewer that says so is worth a great deal more than a
  // stalled rectangle the operator cannot explain. Carried in the existing flags word on purpose:
  // growing the struct would change its size, which older peers validate.
  uint32_t captureTargetFlags = 0;
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

// One screen. Coordinates are the virtual-screen rectangle, so a monitor left of or above the
// primary one has a negative origin -- the same convention the secure-input mapping already uses.
struct ControlMonitorEntry {
  uint32_t id = 0;      // index within this host session; stable until the layout changes
  int32_t x = 0;
  int32_t y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t flags = 0;   // bit0: primary
  char name[64] = {};
};

constexpr uint32_t kControlMonitorFlagPrimary = 0x1u;

struct ControlMonitorListRequestMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint64_t clientSendQpcUs = 0;
};

// Also the reply to a select, so the client learns the new selection from the same shape it
// already knows how to read.
struct ControlMonitorListMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t flags = 0;
  uint32_t selectedMonitorId = 0;
  uint32_t itemCount = 0;
  ControlMonitorEntry items[kControlMonitorListMaxEntries] = {};
};

struct ControlMonitorSelectMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t monitorId = 0;
  uint64_t clientSendQpcUs = 0;
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

// Hardware-cursor position for the DXGI desktop backend, which does not composite the pointer
// into captured frames: on a still screen the remote cursor otherwise never moves and the whole
// session looks frozen (the field case: a static game map "revived" only by dragging it). Sent
// over the media socket at <=30Hz, latest-wins -- a lost sample is superseded a frame later, so
// no reliability machinery. x/y are pixels in the capture (monitor) space whose dimensions are
// captureW/H, letting the viewer map into its letterboxed video rect even across resizes.
struct UdpCursorPosPacket {
  uint32_t magic = kMagic;
  uint16_t kind = static_cast<uint16_t>(UdpPacketKind::CursorPos);
  uint16_t size = static_cast<uint16_t>(sizeof(UdpCursorPosPacket));
  uint16_t flags = 0;  // bit0: cursor visible
  uint16_t reserved = 0;
  int32_t x = 0;
  int32_t y = 0;
  uint32_t captureW = 0;
  uint32_t captureH = 0;
  // Generation fence: the sample is only meaningful for the stream generation it was captured
  // under. Without it, a cursor sampled on the old target could paint over a freshly selected
  // window for up to the stale-hide timeout.
  uint64_t streamGeneration = 0;
  uint64_t hostQpcUs = 0;
};
static_assert(sizeof(UdpCursorPosPacket) == 44, "cursor packet wire layout must not drift");

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
  // bit6:synthetic refresh frame (see kEncodedFrameFlagSynthetic); preserved by the chunker,
  //      which only rewrites bits 1/2/4
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
// --- Sealed unlock v1 messages ------------------------------------------------------------------
// Wire array sizes mirror sealed_unlock.hpp (literals kept here so this wire header stays standalone):
//   pub point 64 (P-256 X||Y), salt 32, challengeId 16, nonce 12, tag 16, padded plaintext 258.
// Layout is deterministic under the file's #pragma pack(1); static_asserts below pin it against drift.

enum class UnlockStage : uint16_t {
  Unknown = 0,
  RejectedPolicy = 1,        // not locked / not authorized / cooldown
  RejectedStaleTopology = 2, // the session topology changed between challenge and execution
  ChallengeIssued = 3,
  DecryptFailed = 4,         // bad tag / bad challenge binding
  TargetResolved = 5,
  ConnectStarted = 6,
  WtsConnectAccepted = 7,
  Injected = 8,              // Winlogon fallback keystrokes queued (not proof of unlock)
  SessionUnlocked = 9,       // authoritative success: the WTS unlock transition
  AuthFailed = 10,           // wrong password / account restriction
  Timeout = 11,
  InternalError = 12,
};

// client -> host: the user pressed Unlock. The host replies with a fresh one-shot challenge.
struct ControlUnlockChallengeRequestMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t requestId = 0;
  uint64_t clientSendQpcUs = 0;
};
static_assert(sizeof(ControlUnlockChallengeRequestMessage) == 24, "unlock challenge-req wire drift");

// host -> client: the one-shot challenge and every binding field (mirrors sealed_unlock::UnlockContext).
struct ControlUnlockChallengeMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t requestId = 0;
  uint16_t status = 0;  // UnlockStage: ChallengeIssued(3) on success, else a rejection stage
  uint16_t reserved = 0;
  uint8_t challengeId[16] = {};
  uint8_t hostPub[64] = {};
  uint8_t salt[32] = {};
  uint64_t hostId = 0;
  uint64_t clientSessionCookie = 0;
  uint64_t accountId = 0;
  uint32_t requesterSession = 0;
  uint32_t consoleSession = 0;
  uint32_t lockGeneration = 0;
  uint32_t topologyGeneration = 0;
  uint64_t issuedMs = 0;
  uint64_t expiresMs = 0;
  uint64_t clientSendQpcUs = 0;
};
static_assert(sizeof(ControlUnlockChallengeMessage) == 196, "unlock challenge wire drift");

// client -> host: the sealed password for that challenge.
struct ControlUnlockSealedRequestMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t requestId = 0;
  uint8_t challengeId[16] = {};
  uint8_t clientPub[64] = {};
  uint8_t nonce[12] = {};
  uint8_t tag[16] = {};
  uint8_t cipher[258] = {};  // AES-256-GCM over the padded plaintext block
  uint64_t clientSendQpcUs = 0;
};
static_assert(sizeof(ControlUnlockSealedRequestMessage) == 390, "unlock sealed-req wire drift");

// host -> client: the unlock job was accepted (NOT completed). Poll status for the terminal result.
struct ControlUnlockAcceptedMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t requestId = 0;
  uint32_t jobId = 0;
  uint16_t accepted = 0;  // 1 = accepted, 0 = rejected
  uint16_t stage = 0;     // UnlockStage on immediate rejection
  uint64_t clientSendQpcUs = 0;
};
static_assert(sizeof(ControlUnlockAcceptedMessage) == 32, "unlock accepted wire drift");

// client -> host: poll a job's result.
struct ControlUnlockStatusRequestMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t requestId = 0;
  uint32_t jobId = 0;
  uint32_t reserved = 0;
  uint64_t clientSendQpcUs = 0;
};
static_assert(sizeof(ControlUnlockStatusRequestMessage) == 32, "unlock status-req wire drift");

// host -> client: a job's current/terminal result. Terminal results are cached and idempotent, so a
// duplicate status request never re-runs the unlock.
struct ControlUnlockStatusResultMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t requestId = 0;
  uint32_t jobId = 0;
  uint16_t stage = 0;      // UnlockStage
  uint16_t terminal = 0;   // 1 once stage is final
  uint32_t win32Error = 0; // GetLastError from WTSConnectSession / injection when relevant
  uint64_t clientSendQpcUs = 0;
};
static_assert(sizeof(ControlUnlockStatusResultMessage) == 36, "unlock status-result wire drift");

// Host-side IME: one physical key transition, carrying the scan code so the host can inject with
// KEYEVENTF_SCANCODE (layout/IME independent) and let the host IME compose live. down=1 keydown.
struct ControlPhysicalKeyMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint16_t down = 0;      // 1 = keydown, 0 = keyup
  uint16_t vk = 0;        // virtual key (diagnostic / fallback)
  uint16_t scanCode = 0;  // hardware scan code from the client's WM_KEY* lParam
  uint16_t flags = 0;     // bit0: extended (E0), bit1: repeat, bit2: make-only (Hangul/Hanja pulse)
  uint64_t clientSendQpcUs = 0;
};
static_assert(sizeof(ControlPhysicalKeyMessage) == 28, "physical-key wire drift");

// Host-side IME v2 handshake. The viewer sends a request when it (re)enters host-IME mode for a
// target; the host aligns/queries its IME under a target fence and answers with a response echoing
// the seq and targetGeneration so the viewer can discard a stale/crossed reply.
struct ControlImeStateRequestMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint32_t targetGeneration = 0;  // viewer's host-IME session generation; bumped on reconnect/retarget
  uint16_t action = 0;            // 0: query only, 1: set to English (open=false) then query
  uint16_t reserved = 0;
  uint64_t clientSendQpcUs = 0;
};
static_assert(sizeof(ControlImeStateRequestMessage) == 28, "ime state-req wire drift");

struct ControlImeStateResponseMessage {
  MessageHeader header{};
  uint32_t seq = 0;               // echoes the request seq
  uint32_t targetGeneration = 0;  // echoes the request targetGeneration
  uint16_t status = 0;            // 0: known, 1: unknown, 2: stale-target, 3: no-target/error
  uint16_t open = 0;              // IME open? 1=composing(KR-ish) 0=alphanumeric(EN); valid iff status==0
  uint32_t conversionMode = 0;    // raw IMC conversion bits (NATIVE/FULLSHAPE...), diagnostic
  uint64_t hostQpcUs = 0;
};
static_assert(sizeof(ControlImeStateResponseMessage) == 32, "ime state-resp wire drift");

constexpr uint16_t kUdpVideoFecGroupSize = 8;
#pragma pack(pop)

}  // namespace remote60::native_poc
