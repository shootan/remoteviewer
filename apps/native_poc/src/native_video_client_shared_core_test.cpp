#include <algorithm>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "native_socket.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_client_session.hpp"
#include "udp_video_nack.hpp"

namespace {

using remote60::native_poc::CaptureModeRequestState;
using remote60::native_poc::ClientControlMetricsSnapshot;
using remote60::native_poc::ClientControlScheduler;
using remote60::native_poc::ClientInputQueue;
using remote60::native_poc::ClientSessionConnectArgs;
using remote60::native_poc::ClientSessionController;
using remote60::native_poc::ClientSessionState;
using remote60::native_poc::ControlOutboundAction;
using remote60::native_poc::ControlOutboundActionKind;
using remote60::native_poc::ControlPingMessage;
using remote60::native_poc::ControlPongMessage;
using remote60::native_poc::ControlWindowListMessage;
using remote60::native_poc::KeyframeRequestState;
using remote60::native_poc::MessageHeader;
using remote60::native_poc::MessageType;
using remote60::native_poc::QueuedControlInputMessage;
using remote60::native_poc::RuntimeTuneState;
using remote60::native_poc::SocketHandle;
using remote60::native_poc::StreamStateControl;
using remote60::native_poc::UdpCodec;
using remote60::native_poc::UdpH264AssemblyDisposition;
using remote60::native_poc::UdpH264AssemblyStepResult;
using remote60::native_poc::UdpH264FrameAssembler;
using remote60::native_poc::kEncodedFrameFlagKeyFrame;
using remote60::native_poc::UdpHelloPacket;
using remote60::native_poc::UdpPacketKind;
using remote60::native_poc::UdpVideoChunkHeader;
using remote60::native_poc::WindowPanelStateModel;
using remote60::native_poc::kInvalidSocket;

bool expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "[shared-core-test] FAIL: " << message << "\n";
    return false;
  }
  return true;
}

bool wait_until(const std::function<bool()>& predicate, int timeoutMs) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return predicate();
}

struct FakeSessionServer {
  bool Start(bool closeControlAfterWindowList) {
    closeAfterWindowList = closeControlAfterWindowList;
    std::string error;
    if (!remote60::native_poc::initialize_sockets(&error)) {
      std::cerr << "[shared-core-test] socket init failed: " << error << "\n";
      return false;
    }

    tcpListen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    udpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (tcpListen == kInvalidSocket || udpSock == kInvalidSocket) {
      Stop();
      return false;
    }

    sockaddr_in loopback{};
    loopback.sin_family = AF_INET;
    loopback.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    sockaddr_in tcpAddr = loopback;
    tcpAddr.sin_port = 0;
    if (bind(tcpListen, reinterpret_cast<const sockaddr*>(&tcpAddr), sizeof(tcpAddr)) != 0 ||
        listen(tcpListen, 1) != 0) {
      Stop();
      return false;
    }

    sockaddr_in udpAddr = loopback;
    udpAddr.sin_port = 0;
    if (bind(udpSock, reinterpret_cast<const sockaddr*>(&udpAddr), sizeof(udpAddr)) != 0) {
      Stop();
      return false;
    }

    sockaddr_in boundTcp{};
    int boundTcpLen = sizeof(boundTcp);
    if (getsockname(tcpListen, reinterpret_cast<sockaddr*>(&boundTcp), &boundTcpLen) != 0) {
      Stop();
      return false;
    }
    sockaddr_in boundUdp{};
    int boundUdpLen = sizeof(boundUdp);
    if (getsockname(udpSock, reinterpret_cast<sockaddr*>(&boundUdp), &boundUdpLen) != 0) {
      Stop();
      return false;
    }

    controlPort = ntohs(boundTcp.sin_port);
    videoPort = ntohs(boundUdp.sin_port);

    tcpThread = std::thread([this]() { RunTcp(); });
    udpThread = std::thread([this]() { RunUdp(); });
    return true;
  }

  void Stop() {
    stop = true;
    remote60::native_poc::shutdown_socket(&acceptedTcp);
    remote60::native_poc::shutdown_socket(&tcpListen);
    remote60::native_poc::shutdown_socket(&udpSock);
    if (tcpThread.joinable()) tcpThread.join();
    if (udpThread.joinable()) udpThread.join();
  }

  ~FakeSessionServer() {
    Stop();
  }

  void RunUdp() {
    sockaddr_in peer{};
    int peerLen = sizeof(peer);
    UdpHelloPacket hello{};
    const int received = recvfrom(udpSock, reinterpret_cast<char*>(&hello), sizeof(hello), 0,
                                  reinterpret_cast<sockaddr*>(&peer), &peerLen);
    if (received >= static_cast<int>(sizeof(hello)) &&
        hello.magic == remote60::native_poc::kMagic &&
        hello.kind == static_cast<uint16_t>(UdpPacketKind::Hello)) {
      UdpHelloPacket ack{};
      ack.kind = static_cast<uint16_t>(UdpPacketKind::HelloAck);
      sendto(udpSock, reinterpret_cast<const char*>(&ack), sizeof(ack), 0,
             reinterpret_cast<const sockaddr*>(&peer), peerLen);
    }
  }

  void RunTcp() {
    sockaddr_in peer{};
    int peerLen = sizeof(peer);
    acceptedTcp = accept(tcpListen, reinterpret_cast<sockaddr*>(&peer), &peerLen);
    if (acceptedTcp == kInvalidSocket) return;
    remote60::native_poc::set_recv_timeout(acceptedTcp, 500);
    remote60::native_poc::set_tcp_nodelay(acceptedTcp);

    while (!stop.load(std::memory_order_acquire)) {
      MessageHeader header{};
      if (!remote60::native_poc::recv_all(acceptedTcp, &header, sizeof(header))) break;
      if (header.magic != remote60::native_poc::kMagic || header.size < sizeof(header)) break;

      const auto type = static_cast<MessageType>(header.type);
      if (type == MessageType::ControlPing && header.size == sizeof(ControlPingMessage)) {
        ControlPingMessage ping{};
        ping.header = header;
        if (!remote60::native_poc::recv_all(acceptedTcp, &ping.seq, sizeof(ping) - sizeof(ping.header))) break;
        ControlPongMessage pong{};
        pong.header.magic = remote60::native_poc::kMagic;
        pong.header.type = static_cast<uint16_t>(MessageType::ControlPong);
        pong.header.size = static_cast<uint16_t>(sizeof(pong));
        pong.seq = ping.seq;
        pong.clientSendQpcUs = ping.clientSendQpcUs;
        if (!remote60::native_poc::send_all(acceptedTcp, &pong, sizeof(pong))) break;
        continue;
      }

      if (type == MessageType::ControlWindowListRequest &&
          header.size == sizeof(remote60::native_poc::ControlWindowListRequestMessage)) {
        remote60::native_poc::ControlWindowListRequestMessage request{};
        request.header = header;
        if (!remote60::native_poc::recv_all(acceptedTcp, &request.seq, sizeof(request) - sizeof(request.header))) {
          break;
        }

        ControlWindowListMessage response{};
        response.header.magic = remote60::native_poc::kMagic;
        response.header.type = static_cast<uint16_t>(MessageType::ControlWindowList);
        response.header.size = static_cast<uint16_t>(sizeof(response));
        response.seq = request.seq;
        response.selectedWindowId = 0;
        response.itemCount = 2;
        response.items[0].id = 1001;
        std::snprintf(response.items[0].title, sizeof(response.items[0].title), "%s", "Desktop Mirror");
        response.items[1].id = 1002;
        std::snprintf(response.items[1].title, sizeof(response.items[1].title), "%s", "Editor");
        if (!remote60::native_poc::send_all(acceptedTcp, &response, sizeof(response))) break;
        windowListSent = true;
        if (closeAfterWindowList) {
          remote60::native_poc::shutdown_socket(&acceptedTcp);
          break;
        }
        continue;
      }

      const size_t discard = static_cast<size_t>(header.size - sizeof(header));
      if (!remote60::native_poc::recv_discard(acceptedTcp, discard)) break;
    }
  }

  int controlPort = 0;
  int videoPort = 0;
  std::atomic<bool> stop{false};
  std::atomic<bool> windowListSent{false};
  bool closeAfterWindowList = false;
  SocketHandle tcpListen = kInvalidSocket;
  SocketHandle acceptedTcp = kInvalidSocket;
  SocketHandle udpSock = kInvalidSocket;
  std::thread tcpThread;
  std::thread udpThread;
};

bool test_ping_and_metrics_order() {
  ClientControlScheduler scheduler;
  WindowPanelStateModel windowPanel;
  StreamStateControl streamState;
  CaptureModeRequestState captureMode;
  KeyframeRequestState keyframe(120000, 300000, 3);
  RuntimeTuneState runtimeTune(300000, 30000000, 250000, 1, 240);
  ClientInputQueue inputQueue;
  ControlOutboundAction action{};

  scheduler.Reset(1000, 1000);
  ClientControlMetricsSnapshot metrics{};
  metrics.updatedQpcUs = 1500;
  metrics.message.recvMbpsX1000 = 5000;

  if (!expect(scheduler.NextAction(1000, metrics, &windowPanel, &streamState, &captureMode, &keyframe,
                                   &runtimeTune, &inputQueue, &action),
              "initial ping action missing")) return false;
  if (!expect(action.kind == ControlOutboundActionKind::Ping, "first action should be ping")) return false;
  if (!expect(action.expectedResponseType.has_value() &&
                  *action.expectedResponseType == MessageType::ControlPong,
              "ping should expect pong")) return false;

  scheduler.OnPingCompleted(1100);

  if (!expect(scheduler.NextAction(1200, metrics, &windowPanel, &streamState, &captureMode, &keyframe,
                                   &runtimeTune, &inputQueue, &action),
              "metrics action missing after ping")) return false;
  if (!expect(action.kind == ControlOutboundActionKind::Metrics, "second action should be metrics")) return false;

  if (!expect(!scheduler.NextAction(1300, metrics, &windowPanel, &streamState, &captureMode, &keyframe,
                                    &runtimeTune, &inputQueue, &action),
              "metrics should not resend without updated timestamp")) return false;

  return true;
}

bool test_window_and_input_actions() {
  ClientControlScheduler scheduler;
  WindowPanelStateModel windowPanel;
  StreamStateControl streamState;
  CaptureModeRequestState captureMode;
  KeyframeRequestState keyframe(120000, 300000, 3);
  RuntimeTuneState runtimeTune(300000, 30000000, 250000, 1, 240);
  ClientInputQueue inputQueue;
  ControlOutboundAction action{};

  scheduler.Reset(1000, 0);
  scheduler.OnPingCompleted(0);

  windowPanel.RequestList("pending");
  if (!expect(scheduler.NextAction(100, {}, &windowPanel, &streamState, &captureMode, &keyframe,
                                   &runtimeTune, &inputQueue, &action),
              "window-list action missing")) return false;
  if (!expect(action.kind == ControlOutboundActionKind::WindowListRequest,
              "expected window-list request action")) return false;
  if (!expect(action.expectedResponseType.has_value() &&
                  *action.expectedResponseType == MessageType::ControlWindowList,
              "window-list request should expect response")) return false;

  ControlWindowListMessage list{};
  list.flags = 0;
  list.selectedWindowId = 77;
  list.itemCount = 1;
  list.items[0].id = 77;
  std::snprintf(list.items[0].title, sizeof(list.items[0].title), "App");
  windowPanel.ApplyWindowList(list, 4);
  if (!expect(windowPanel.RequestSelect(77, "select"), "window select should queue")) return false;

  if (!expect(scheduler.NextAction(200, {}, &windowPanel, &streamState, &captureMode, &keyframe,
                                   &runtimeTune, &inputQueue, &action),
              "window-select action missing")) return false;
  if (!expect(action.kind == ControlOutboundActionKind::WindowSelect,
              "expected window-select action")) return false;
  if (!expect(action.expectedResponseType.has_value() &&
                  *action.expectedResponseType == MessageType::ControlWindowSelected,
              "window-select should expect response")) return false;

  QueuedControlInputMessage input{};
  input.type = MessageType::ControlInputEvent;
  input.inputEvent.kind = 2;
  inputQueue.Enqueue(input);

  if (!expect(scheduler.NextAction(300, {}, &windowPanel, &streamState, &captureMode, &keyframe,
                                   &runtimeTune, &inputQueue, &action),
              "input action missing")) return false;
  if (!expect(action.kind == ControlOutboundActionKind::InputEvent,
              "expected input-event action")) return false;
  if (!expect(action.expectedResponseType.has_value() &&
                  *action.expectedResponseType == MessageType::ControlInputAck,
              "input event should expect ack")) return false;

  const uint64_t ackCount = scheduler.RecordInputAck(1);
  if (!expect(ackCount == 1, "input ack counter should increment")) return false;

  return true;
}

bool test_capture_runtime_and_keyframe_actions() {
  ClientControlScheduler scheduler;
  WindowPanelStateModel windowPanel;
  StreamStateControl streamState;
  CaptureModeRequestState captureMode;
  KeyframeRequestState keyframe(120000, 300000, 3);
  RuntimeTuneState runtimeTune(300000, 30000000, 250000, 1, 240);
  ClientInputQueue inputQueue;
  ControlOutboundAction action{};

  scheduler.Reset(1000, 0);
  scheduler.OnPingCompleted(0);

  captureMode.Request(2, 4200, 7300);
  if (!expect(scheduler.NextAction(100, {}, &windowPanel, &streamState, &captureMode, &keyframe,
                                   &runtimeTune, &inputQueue, &action),
              "capture-mode action missing")) return false;
  if (!expect(action.kind == ControlOutboundActionKind::CaptureMode,
              "expected capture-mode action")) return false;
  if (!expect(action.captureMode.mode == 2 &&
                  action.captureMode.xPermille == 4200 &&
                  action.captureMode.yPermille == 7300,
              "capture-mode payload mismatch")) return false;

  runtimeTune.Reset(0, 0);
  runtimeTune.SetEnabled(true);
  runtimeTune.MarkDirty();
  ClientControlMetricsSnapshot metrics{};
  metrics.message.recvMbpsX1000 = 6000;
  if (!expect(scheduler.NextAction(200, metrics, &windowPanel, &streamState, &captureMode, &keyframe,
                                   &runtimeTune, &inputQueue, &action),
              "runtime-tune action missing")) return false;
  if (!expect(action.kind == ControlOutboundActionKind::RuntimeTune,
              "expected runtime-tune action")) return false;
  if (!expect((action.runtimeTune.flags & 0x3u) == 0x3u,
              "runtime-tune flags should include bitrate and keyint")) return false;

  keyframe.Reset();
  const auto queued = keyframe.Request(3, 500);
  if (!expect(queued.queued, "keyframe request should queue")) return false;
  if (!expect(scheduler.NextAction(600, {}, &windowPanel, &streamState, &captureMode, &keyframe,
                                   &runtimeTune, &inputQueue, &action),
              "keyframe action missing")) return false;
  if (!expect(action.kind == ControlOutboundActionKind::KeyframeRequest,
              "expected keyframe action")) return false;
  if (!expect(action.keyframe.reason == 3, "keyframe reason mismatch")) return false;

  return true;
}

bool test_udp_assembler() {
  UdpH264FrameAssembler assembler;
  std::vector<uint8_t> datagram(sizeof(UdpVideoChunkHeader) + 4, 0);
  auto* header = reinterpret_cast<UdpVideoChunkHeader*>(datagram.data());
  header->magic = remote60::native_poc::kMagic;
  header->kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
  header->size = static_cast<uint16_t>(sizeof(UdpVideoChunkHeader));
  header->seq = 42;
  header->codec = static_cast<uint16_t>(UdpCodec::H264);
  header->flags = 0x1u | 0x2u | 0x4u;
  header->width = 1280;
  header->height = 720;
  header->payloadSize = 4;
  header->chunkOffset = 0;
  header->chunkSize = 4;
  header->chunkIndex = 0;
  header->chunkCount = 1;
  header->chunkStride = 4;
  datagram[sizeof(UdpVideoChunkHeader) + 0] = 1;
  datagram[sizeof(UdpVideoChunkHeader) + 1] = 2;
  datagram[sizeof(UdpVideoChunkHeader) + 2] = 3;
  datagram[sizeof(UdpVideoChunkHeader) + 3] = 4;

  auto result = assembler.PushDatagram(datagram.data(), datagram.size());
  if (!expect(result.disposition == UdpH264AssemblyDisposition::Completed,
              "udp assembler should complete single-chunk frame")) return false;
  if (!expect(result.frame.header.seq == 42, "udp assembler should preserve seq")) return false;
  if (!expect(result.frame.payload.size() == 4 && result.frame.payload[3] == 4,
              "udp assembler payload mismatch")) return false;

  assembler.Reset();
  header->flags = 0x2u;
  header->seq = 42;
  header->payloadSize = 8;
  header->chunkOffset = 0;
  header->chunkSize = 4;
  header->chunkIndex = 0;
  header->chunkCount = 2;
  header->chunkStride = 4;
  result = assembler.PushDatagram(datagram.data(), datagram.size());
  if (!expect(result.disposition == UdpH264AssemblyDisposition::Partial,
              "udp assembler should enter partial state")) return false;

  header->flags = 0x4u;
  header->seq = 42;
  header->chunkOffset = 4;
  header->chunkIndex = 1;
  result = assembler.PushDatagram(datagram.data(), datagram.size());
  if (!expect(result.disposition == UdpH264AssemblyDisposition::Completed,
              "udp assembler should complete a multi-chunk frame")) return false;

  // Data datagrams may be reordered by the network; offsets/indexes make this lossless.
  assembler.Reset();
  header->seq = 43;
  header->flags = 0x4u;
  header->payloadSize = 8;
  header->chunkOffset = 4;
  header->chunkIndex = 1;
  result = assembler.PushDatagram(datagram.data(), datagram.size());
  if (!expect(result.disposition == UdpH264AssemblyDisposition::Partial,
              "udp assembler should accept a reordered last chunk")) return false;
  header->flags = 0x2u;
  header->chunkOffset = 0;
  header->chunkIndex = 0;
  result = assembler.PushDatagram(datagram.data(), datagram.size());
  if (!expect(result.disposition == UdpH264AssemblyDisposition::Completed,
              "udp assembler should complete reordered chunks")) return false;

  // Drop one data chunk and recover it from the group's XOR parity packet.
  assembler.Reset();
  header->seq = 44;
  header->payloadSize = 12;
  header->chunkCount = 3;
  header->chunkStride = 4;
  header->flags = 0x2u;
  header->chunkIndex = 0;
  header->chunkOffset = 0;
  for (int i = 0; i < 4; ++i) datagram[sizeof(UdpVideoChunkHeader) + i] = static_cast<uint8_t>(i + 1);
  result = assembler.PushDatagram(datagram.data(), datagram.size());
  if (!expect(result.disposition == UdpH264AssemblyDisposition::Partial,
              "udp assembler fec first chunk should be partial")) return false;
  header->flags = 0x4u;
  header->chunkIndex = 2;
  header->chunkOffset = 8;
  for (int i = 0; i < 4; ++i) datagram[sizeof(UdpVideoChunkHeader) + i] = static_cast<uint8_t>(i + 9);
  result = assembler.PushDatagram(datagram.data(), datagram.size());
  if (!expect(result.disposition == UdpH264AssemblyDisposition::Partial,
              "udp assembler fec missing chunk should remain partial")) return false;
  header->flags = 0x10u;
  header->chunkIndex = 0;
  header->chunkOffset = 0;
  // [1..4] XOR [5..8] XOR [9..12].
  for (int i = 0; i < 4; ++i) {
    datagram[sizeof(UdpVideoChunkHeader) + i] =
        static_cast<uint8_t>((i + 1) ^ (i + 5) ^ (i + 9));
  }
  result = assembler.PushDatagram(datagram.data(), datagram.size());
  if (!expect(result.disposition == UdpH264AssemblyDisposition::Completed && result.fecRecovered,
              "udp assembler should recover one missing chunk with fec")) return false;
  if (!expect(result.frame.payload.size() == 12 && result.frame.payload[4] == 5 &&
                  result.frame.payload[7] == 8,
              "udp assembler recovered payload mismatch")) return false;

  assembler.Reset();
  header->seq = 45;
  header->flags = 0x2u;
  header->payloadSize = (16u * 1024u * 1024u) + 1u;
  header->chunkOffset = 0;
  header->chunkSize = 4;
  result = assembler.PushDatagram(datagram.data(), datagram.size());
  if (!expect(result.disposition == UdpH264AssemblyDisposition::Malformed && result.oversizePayload,
              "udp assembler should reject oversized payloads")) return false;
  if (!expect(result.rejectedPayloadSize == ((16u * 1024u * 1024u) + 1u),
              "udp assembler should report rejected payload size")) return false;

  return true;
}

// One data datagram of a multi-chunk AU: `stride`-byte chunks, the last one possibly shorter.
std::vector<uint8_t> make_video_chunk(uint32_t seq, uint16_t chunkIndex, uint32_t payloadSize,
                                      uint32_t stride, bool key, uint64_t generation = 1) {
  const uint16_t chunkCount = static_cast<uint16_t>((payloadSize + stride - 1u) / stride);
  const uint32_t offset = static_cast<uint32_t>(chunkIndex) * stride;
  const uint32_t chunkSize = std::min<uint32_t>(stride, payloadSize - offset);
  std::vector<uint8_t> datagram(sizeof(UdpVideoChunkHeader) + chunkSize, 0);
  auto* header = reinterpret_cast<UdpVideoChunkHeader*>(datagram.data());
  header->magic = remote60::native_poc::kMagic;
  header->kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
  header->size = static_cast<uint16_t>(sizeof(UdpVideoChunkHeader));
  header->seq = seq;
  header->codec = static_cast<uint16_t>(UdpCodec::H264);
  header->flags = static_cast<uint16_t>((key ? 0x1u : 0u) | (offset == 0 ? 0x2u : 0u) |
                                        (offset + chunkSize >= payloadSize ? 0x4u : 0u));
  header->width = 64;
  header->height = 64;
  header->payloadSize = payloadSize;
  header->chunkOffset = offset;
  header->chunkSize = chunkSize;
  header->chunkIndex = chunkIndex;
  header->chunkCount = chunkCount;
  header->chunkStride = stride;
  header->streamGeneration = generation;
  for (uint32_t i = 0; i < chunkSize; ++i) {
    datagram[sizeof(UdpVideoChunkHeader) + i] = static_cast<uint8_t>(seq * 16 + offset + i);
  }
  return datagram;
}

UdpH264AssemblyStepResult push_chunk(UdpH264FrameAssembler& a, uint32_t seq, uint16_t chunkIndex,
                                     uint32_t payloadSize, uint32_t stride, bool key, uint64_t nowUs) {
  const auto d = make_video_chunk(seq, chunkIndex, payloadSize, stride, key);
  return a.PushDatagram(d.data(), d.size(), nowUs);
}

// The shared NACK scheduler (udp_video_nack.hpp): hole vs tail grace, round spacing, the round
// cap, the keyframe-only rule while an IDR is awaited, and the restart when the blocker changes.
// The Windows viewer and the session controller both run this object. (Windows NACK wiring.)
bool test_video_nack_scheduler() {
  using remote60::native_poc::VideoNackScheduler;
  using remote60::native_poc::UdpVideoNackPacket;
  UdpH264FrameAssembler a;
  VideoNackScheduler s;  // defaults: gap 25 ms, tail 120 ms, round 25 ms, 3 rounds
  UdpVideoNackPacket pkt{};
  const uint64_t t0 = 1000000;

  // Nothing incomplete: no NACK.
  if (!expect(!s.Poll(a, true, t0, &pkt), "nack: nothing incomplete -> no packet")) return false;

  // AU 10, 3 chunks (12 bytes / stride 4): chunks 0 and 2 arrive, chunk 1 is a confirmed hole.
  (void)push_chunk(a, 10, 0, 12, 4, false, t0);
  (void)push_chunk(a, 10, 2, 12, 4, false, t0);
  if (!expect(!s.Poll(a, true, t0, &pkt), "nack: hole inside the reorder grace -> wait")) return false;
  if (!expect(!s.Poll(a, true, t0 + 24000, &pkt), "nack: 24 ms is still inside the grace")) return false;
  if (!expect(s.Poll(a, true, t0 + 25000, &pkt), "nack: hole past the grace -> round 0")) return false;
  if (!expect(pkt.seq == 10 && pkt.missingCount == 1 && pkt.missing[0] == 1 && pkt.round == 0 &&
                  pkt.chunkCount == 3,
              "nack: round 0 names the hole")) return false;
  if (!expect(!s.Poll(a, true, t0 + 40000, &pkt), "nack: inside the round spacing -> wait")) return false;
  if (!expect(s.Poll(a, true, t0 + 50000, &pkt) && pkt.round == 1, "nack: round 1 at +25 ms")) return false;
  if (!expect(s.Poll(a, true, t0 + 75000, &pkt) && pkt.round == 2, "nack: round 2 at +25 ms")) return false;
  if (!expect(!s.Poll(a, true, t0 + 100000, &pkt), "nack: rounds exhausted -> the IDR path takes over")) return false;
  if (!expect(s.stats().packetsSent == 3 && s.stats().chunksRequested == 3 &&
                  s.stats().roundsExhausted == 1,
              "nack: stats after three rounds")) return false;
  if (!expect(!s.Poll(a, true, t0 + 200000, &pkt) && s.stats().roundsExhausted == 1,
              "nack: exhaustion is counted once")) return false;

  // The hole is repaired: AU 10 completes, nothing is incomplete, the scheduler forgets it.
  auto r = push_chunk(a, 10, 1, 12, 4, false, t0 + 210000);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Completed, "nack: repaired AU completes")) return false;
  if (!expect(!s.Poll(a, true, t0 + 211000, &pkt) && s.current_seq() == 0, "nack: forgotten after completion")) return false;

  // Tail: AU 11, 4 chunks, only chunk 0 in -> the rest is still in flight (>= highWater).
  const uint64_t t1 = t0 + 300000;
  (void)push_chunk(a, 11, 0, 16, 4, false, t1);
  if (!expect(!s.Poll(a, true, t1 + 25000, &pkt), "nack: in-flight tail is not asked for at the hole grace")) return false;
  if (!expect(!s.Poll(a, true, t1 + 119000, &pkt), "nack: tail still inside its long grace")) return false;
  if (!expect(s.Poll(a, true, t1 + 120000, &pkt) && pkt.missingCount == 3 && pkt.missing[0] == 1 &&
                  pkt.missing[2] == 3 && pkt.round == 0,
              "nack: tail asked for after the long grace")) return false;

  // Mixed: AU 12, 5 chunks, chunks 0 and 3 in -> 1,2 are holes (< highWater 4), 4 is tail.
  a.Reset();
  s.Reset();
  const uint64_t t2 = t0 + 500000;
  (void)push_chunk(a, 12, 0, 20, 4, false, t2);
  (void)push_chunk(a, 12, 3, 20, 4, false, t2);
  if (!expect(s.Poll(a, true, t2 + 30000, &pkt) && pkt.missingCount == 2 && pkt.missing[0] == 1 &&
                  pkt.missing[1] == 2,
              "nack: only the holes at the short grace, the tail waits")) return false;
  if (!expect(s.Poll(a, true, t2 + 125000, &pkt) && pkt.missingCount == 3 && pkt.missing[2] == 4,
              "nack: the tail joins once its grace passed")) return false;

  // Waiting for a keyframe: a non-key incomplete AU is not chased, an incomplete IDR is.
  a.Reset();
  s.Reset();
  const uint64_t t3 = t0 + 800000;
  (void)push_chunk(a, 20, 0, 12, 4, false, t3);
  (void)push_chunk(a, 20, 2, 12, 4, false, t3);
  if (!expect(!s.Poll(a, false, t3 + 50000, &pkt), "nack: non-key AU is not repaired while an IDR is awaited")) return false;
  a.Reset();
  (void)push_chunk(a, 21, 0, 12, 4, true, t3);
  (void)push_chunk(a, 21, 2, 12, 4, true, t3);
  if (!expect(s.Poll(a, false, t3 + 50000, &pkt) && pkt.seq == 21,
              "nack: an incomplete IDR is repaired even while an IDR is awaited")) return false;

  // The tail phase has its own round budget: rounds spent on a hole while the tail was still in
  // flight do not leave the tail with none (Codex condition 2).
  a.Reset();
  s.Reset();
  const uint64_t t5 = t0 + 2000000;
  (void)push_chunk(a, 50, 0, 24, 4, false, t5);  // 6 chunks: 0 and 2 in, hole 1, tail 3..5
  (void)push_chunk(a, 50, 2, 24, 4, false, t5);
  if (!expect(s.Poll(a, true, t5 + 25000, &pkt) && pkt.missingCount == 1 && pkt.missing[0] == 1,
              "nack: hole round 0")) return false;
  if (!expect(s.busy(), "nack: busy while rounds remain")) return false;
  (void)s.Poll(a, true, t5 + 50000, &pkt);
  (void)s.Poll(a, true, t5 + 75000, &pkt);
  if (!expect(!s.Poll(a, true, t5 + 100000, &pkt) && !s.busy(), "nack: hole rounds spent, not busy")) return false;
  if (!expect(s.Poll(a, true, t5 + 120000, &pkt) && pkt.round == 0 && pkt.missingCount == 4 &&
                  pkt.missing[0] == 1 && pkt.missing[3] == 5,
              "nack: the tail phase starts with a fresh round 0 (hole + tail)")) return false;
  if (!expect(s.busy(), "nack: busy again in the tail phase")) return false;
  (void)s.Poll(a, true, t5 + 145000, &pkt);
  if (!expect(s.Poll(a, true, t5 + 170000, &pkt) && pkt.round == 2, "nack: tail round 2")) return false;
  if (!expect(!s.Poll(a, true, t5 + 195000, &pkt) && !s.busy(), "nack: tail rounds spent")) return false;

  // Blocker change restarts the schedule: AU 30 exhausted its rounds, AU 31 gets fresh ones.
  a.Reset();
  s.Reset();
  const uint64_t t4 = t0 + 1000000;
  (void)push_chunk(a, 30, 0, 12, 4, false, t4);
  (void)push_chunk(a, 30, 2, 12, 4, false, t4);
  (void)s.Poll(a, true, t4 + 25000, &pkt);
  (void)s.Poll(a, true, t4 + 50000, &pkt);
  (void)s.Poll(a, true, t4 + 75000, &pkt);
  if (!expect(!s.Poll(a, true, t4 + 100000, &pkt), "nack: AU 30 exhausted")) return false;
  r = push_chunk(a, 30, 1, 12, 4, false, t4 + 101000);  // repaired late: delivered
  (void)push_chunk(a, 31, 0, 12, 4, false, t4 + 102000);
  (void)push_chunk(a, 31, 2, 12, 4, false, t4 + 102000);
  if (!expect(!s.Poll(a, true, t4 + 110000, &pkt), "nack: AU 31 starts its own grace")) return false;
  if (!expect(s.Poll(a, true, t4 + 127000, &pkt) && pkt.seq == 31 && pkt.round == 0,
              "nack: AU 31 gets a fresh round schedule")) return false;
  return true;
}

// The assembler's in-order delivery hold (ConfigureInOrderHold / PopDelivery): a completed AU
// waits behind an older incomplete one until the hold expires; the keyframe-wait rule; the legacy
// immediate path is untouched. (Windows NACK wiring.)
// A04 give-up support in the assembler: lastProgressUs moves only on a NEW data chunk (a duplicate
// is not progress), and GiveUpIncomplete removes exactly the judged (generation, seq) -- not an
// "oldest" by arrival or by sequence -- and cancels itself if that AU completed meanwhile.
bool test_udp_assembler_progress_and_give_up() {
  UdpH264FrameAssembler a;
  a.ConfigureInOrderHold(120000, 8);
  UdpH264FrameAssembler::IncompleteAuInfo info{};
  const uint64_t t0 = 9000000;
  (void)push_chunk(a, 10, 0, 12, 4, false, t0);
  if (!expect(a.OldestIncomplete(nullptr, 0, &info) && info.seq == 10 && info.lastProgressUs == t0,
              "progress: the first chunk stamps lastProgressUs")) return false;
  (void)push_chunk(a, 10, 0, 12, 4, false, t0 + 10000);  // duplicate
  if (!expect(a.OldestIncomplete(nullptr, 0, &info) && info.lastProgressUs == t0,
              "progress: a duplicate chunk is not progress")) return false;
  (void)push_chunk(a, 10, 2, 12, 4, false, t0 + 20000);  // a new chunk
  if (!expect(a.OldestIncomplete(nullptr, 0, &info) && info.lastProgressUs == t0 + 20000,
              "progress: a new data chunk is progress")) return false;
  // A second incomplete AU behind it; give up 11 by identity: 10 stays, 11 goes.
  (void)push_chunk(a, 11, 0, 12, 4, false, t0 + 30000);
  if (!expect(a.PendingCount() == 2 && !a.AnyComplete(), "give-up: two incomplete AUs, none complete")) return false;
  if (!expect(!a.GiveUpIncomplete(1, 12), "give-up: an AU that is not held -> false")) return false;
  if (!expect(a.GiveUpIncomplete(1, 11) && a.PendingCount() == 1, "give-up: exactly seq 11 removed")) return false;
  if (!expect(a.OldestIncomplete(nullptr, 0, &info) && info.seq == 10, "give-up: seq 10 untouched")) return false;
  // 10 completes: a give-up judged earlier is cancelled (false, nothing removed) and it delivers.
  (void)push_chunk(a, 10, 1, 12, 4, false, t0 + 40000);
  if (!expect(a.AnyComplete(), "give-up: seq 10 completed")) return false;
  if (!expect(!a.GiveUpIncomplete(1, 10) && a.PendingCount() == 1, "give-up: a completed AU is not given up")) return false;
  UdpH264AssemblyStepResult out{};
  if (!expect(a.PopDelivery(t0 + 40000, true, &out) && out.frame.header.seq == 10 && out.frame.payload.size() == 12,
              "give-up: seq 10 delivered intact")) return false;
  return true;
}

// A04 follow-up: a given-up identity is tombstoned, so the chunks still arriving for it cannot
// re-create the assembly (which used to re-block the head and cause repeated give-ups). Only that
// exact (generation, seq): another generation restarts the seq space and must not be shadowed.
bool test_udp_assembler_abandoned_tombstone() {
  UdpH264FrameAssembler a;
  a.ConfigureInOrderHold(120000, 8);
  const uint64_t t0 = 12000000;
  (void)push_chunk(a, 30, 0, 12, 4, false, t0);
  if (!expect(a.GiveUpIncomplete(1, 30, t0 + 1000), "tombstone: seq 30 given up")) return false;
  if (!expect(a.IsAbandoned(1, 30), "tombstone: it is remembered")) return false;
  // A late chunk of it is stale traffic, not a new assembly.
  auto r = push_chunk(a, 30, 1, 12, 4, false, t0 + 3000);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Ignored && a.PendingCount() == 0,
              "tombstone: a late chunk of the abandoned AU is ignored")) return false;
  // No clock retires it: a host that keeps resending the abandoned AU for longer than any timeout
  // would otherwise get it re-assembled and abandoned again. Even 10 s later, with no delivery in
  // between, it is still ignored and still held.
  r = push_chunk(a, 30, 2, 12, 4, false, t0 + 10'000'000);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Ignored && a.PendingCount() == 0,
              "tombstone: a much later chunk of the abandoned AU is still ignored")) return false;
  if (!expect(a.AbandonedCount() == 1, "tombstone: still held while no delivery has passed it")) return false;
  // The next AU is unaffected and delivers -- and that retires the tombstone, because from here
  // the ordinary stale guard covers seq 30.
  r = push_chunk(a, 31, 0, 4, 4, true, t0 + 11'000'000);
  UdpH264AssemblyStepResult out{};
  if (!expect(a.PopDelivery(t0 + 11'000'000, true, &out) && out.frame.header.seq == 31,
              "tombstone: the next AU still delivers")) return false;
  if (!expect(a.AbandonedCount() == 0, "tombstone: retired once delivery passed it")) return false;
  r = push_chunk(a, 30, 0, 12, 4, false, t0 + 12'000'000);  // index 3 would be out of range (3 chunks)
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Ignored,
              "tombstone: after retirement the stale guard still ignores it")) return false;
  // Another generation with the same seq is a different AU: the tombstone must not shadow it.
  // (In a fresh assembler, so the ordinary "older than the last delivered seq" rule is not what
  // answers here.)
  {
    UdpH264FrameAssembler b;
    b.ConfigureInOrderHold(120000, 8);
    (void)push_chunk(b, 40, 0, 12, 4, false, t0);
    if (!expect(b.GiveUpIncomplete(1, 40, t0 + 1000), "tombstone: seq 40 of generation 1 given up")) return false;
    if (!expect(!b.IsAbandoned(2, 40), "tombstone: it is keyed by generation too")) return false;
    const auto d = make_video_chunk(40, 0, 12, 4, false, 2);
    const auto r2 = b.PushDatagram(d.data(), d.size(), t0 + 2000);
    if (!expect(r2.disposition == UdpH264AssemblyDisposition::Partial,
                "tombstone: the same seq in another generation assembles normally")) return false;
  }
  // Many identities abandoned with no delivery in between: nothing is forgotten. At the cap the
  // saturation episode takes over (test_udp_assembler_saturation_episode covers what it does);
  // what matters here is that every record survives, the first as much as the last.
  {
    UdpH264FrameAssembler d;
    d.ConfigureInOrderHold(120000, 32);
    for (uint32_t i = 0; i < 20; ++i) {
      (void)push_chunk(d, 100 + i, 0, 12, 4, false, t0 + i * 1000);
      (void)d.GiveUpIncomplete(1, 100 + i, t0 + i * 1000);  // refused once the episode bars new P frames
    }
    if (!expect(d.AbandonedCount() == 16 && d.saturated(),
                "tombstone: records kept up to the cap, then the episode (" + std::to_string(d.AbandonedCount()) + ")")) return false;
    if (!expect(d.IsAbandoned(1, 100) && d.IsAbandoned(1, 115),
                "tombstone: the first identity is remembered as well as the last")) return false;
  }
  a.Reset();
  if (!expect(!a.IsAbandoned(1, 30), "tombstone: Reset clears it")) return false;
  return true;
}

// A04 saturation episode: the abandoned list's cap starts a recovery episode instead of evicting
// records. Covers what Codex asked for: no release on a wrong generation (and no state moved by
// one), a refused generation changing nothing, the K=2 candidate slots and the floor, an old seq
// against a newer delivered one causing no reset, reordering, a live candidate surviving late
// duplicates, and P frames resuming after the accepted key.
bool test_udp_assembler_saturation_episode() {
  const uint64_t t0 = 20000000;
  auto fill_to_saturation = [&](UdpH264FrameAssembler& a, uint32_t firstSeq, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      const uint32_t seq = firstSeq + static_cast<uint32_t>(i);
      (void)push_chunk(a, seq, 0, 12, 4, false, t0 + i * 1000);
      (void)a.GiveUpIncomplete(1, seq, t0 + i * 1000);
    }
  };

  // The cap starts the episode; nothing is forgotten and the non-key assemblies go with it.
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 32);
    fill_to_saturation(a, 200, 15);
    (void)push_chunk(a, 300, 0, 12, 4, false, t0 + 90000);  // a non-key assembly still in flight
    if (!expect(!a.saturated() && a.PendingCount() == 1, "saturation: not yet at the cap")) return false;
    (void)push_chunk(a, 215, 0, 12, 4, false, t0 + 91000);
    (void)a.GiveUpIncomplete(1, 215, t0 + 91000);
    if (!expect(a.saturated(), "saturation: the cap started the episode")) return false;
    if (!expect(a.AbandonedCount() == 16, "saturation: every record is kept")) return false;
    if (!expect(a.PendingCount() == 0, "saturation: the non-key assemblies were dropped")) return false;
    // A non-key AU may not start an assembly now; a keyframe may.
    auto r = push_chunk(a, 400, 0, 12, 4, false, t0 + 92000);
    if (!expect(r.disposition == UdpH264AssemblyDisposition::Ignored && a.PendingCount() == 0,
                "saturation: a new P frame is refused")) return false;
    r = push_chunk(a, 401, 0, 12, 4, true, t0 + 93000);
    if (!expect(r.disposition == UdpH264AssemblyDisposition::Partial && a.saturation_candidates() == 1,
                "saturation: a keyframe becomes a candidate")) return false;
    // K = 2: a third key replaces the oldest candidate and the floor rises to it.
    r = push_chunk(a, 402, 0, 12, 4, true, t0 + 94000);
    if (!expect(a.saturation_candidates() == 2, "saturation: two candidate slots")) return false;
    r = push_chunk(a, 403, 0, 12, 4, true, t0 + 95000);
    if (!expect(a.saturation_candidates() == 2 && a.saturation_floor_set() && a.saturation_floor_seq() == 401,
                "saturation: the oldest candidate gave way and the floor rose to it")) return false;
    // The replaced candidate's late chunks cannot re-create it; the live ones keep repairing.
    r = push_chunk(a, 401, 1, 12, 4, true, t0 + 96000);
    if (!expect(r.disposition == UdpH264AssemblyDisposition::Ignored && a.saturation_candidates() == 2,
                "saturation: the replaced candidate stays gone")) return false;
    r = push_chunk(a, 402, 1, 12, 4, true, t0 + 97000);
    if (!expect(r.disposition == UdpH264AssemblyDisposition::Partial,
                "saturation: a live candidate keeps being repaired")) return false;
    r = push_chunk(a, 402, 1, 12, 4, true, t0 + 98000);  // a duplicate of a live candidate
    if (!expect(r.disposition == UdpH264AssemblyDisposition::Partial && a.saturation_candidates() == 2,
                "saturation: a late duplicate does not disturb the live candidate")) return false;
    // Delivery alone does not end the episode: only a key the caller ACCEPTS does, and only one
    // of this episode's candidates.
    r = push_chunk(a, 402, 2, 12, 4, true, t0 + 99000);
    UdpH264AssemblyStepResult out{};
    if (!expect(a.PopDelivery(t0 + 99000, true, &out) && out.frame.header.seq == 402,
                "saturation: the completed candidate is delivered")) return false;
    if (!expect(a.saturated(), "saturation: delivery alone does not end it")) return false;
    a.NoteKeyAccepted(1, 999);   // never a candidate
    if (!expect(a.saturated(), "saturation: an unknown key does not end it")) return false;
    // Delivering 402 already retired every record it covers (the ordinary watermark rule), so what
    // the wrong-generation call must prove is that it changes NOTHING further.
    const size_t recordsBefore = a.AbandonedCount();
    a.NoteKeyAccepted(2, 402);   // right seq, wrong generation
    if (!expect(a.saturated() && a.AbandonedCount() == recordsBefore && a.saturation_candidates() == 1 &&
                    a.saturation_floor_set() && a.saturation_floor_seq() == 401,
                "saturation: a wrong generation releases nothing and moves nothing")) return false;
    a.NoteKeyAccepted(1, 402);   // the accepted recovery key
    if (!expect(!a.saturated() && !a.saturation_floor_set(),
                "saturation: the accepted key ended the episode")) return false;
    if (!expect(a.AbandonedCount() == 0, "saturation: the records it covers were retired")) return false;
    // P frames flow again.
    r = push_chunk(a, 404, 0, 4, 4, false, t0 + 100000);
    if (!expect(r.disposition == UdpH264AssemblyDisposition::Queued ||
                    r.disposition == UdpH264AssemblyDisposition::Partial,
                "saturation: P frames resume after the accepted key")) return false;
  }

  // Releasing with records NEWER than the accepted key: the cap still holds, so the episode goes on.
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 32);
    // The records are made BEFORE the episode (inside it a failing candidate is another attempt,
    // not a new record) and all of them are newer than the recovery key that follows.
    fill_to_saturation(a, 500, 16);
    if (!expect(a.saturated(), "saturation(latch): episode started")) return false;
    // The recovery key is older than every record: accepting it retires none of them.
    (void)push_chunk(a, 400, 0, 4, 4, true, t0 + 600000);
    UdpH264AssemblyStepResult out{};
    if (!expect(a.PopDelivery(t0 + 600000, true, &out) && out.frame.header.seq == 400,
                "saturation(latch): the recovery key is delivered")) return false;
    if (!expect(a.AbandonedCount() >= 16, "saturation(latch): the newer records survive the delivery")) return false;
    a.NoteKeyAccepted(1, 400);
    if (!expect(a.saturated(), "saturation(latch): newer records at the cap keep the episode")) return false;
    if (!expect(!a.saturation_floor_set(), "saturation(latch): the floor was cleared with the release")) return false;
  }

  // A generation the caller refuses may not evict a candidate or raise the floor.
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 32);
    a.SetSaturationAdmitFilter([](uint64_t generation) { return generation == 1; });
    fill_to_saturation(a, 500, 16);
    if (!expect(a.saturated(), "saturation(admit): episode started")) return false;
    (void)push_chunk(a, 600, 0, 12, 4, true, t0 + 200000);
    (void)push_chunk(a, 601, 0, 12, 4, true, t0 + 201000);
    if (!expect(a.saturation_candidates() == 2, "saturation(admit): two accepted candidates")) return false;
    const auto refused = make_video_chunk(700, 0, 12, 4, true, 6);  // generation 6: refused
    const auto r = a.PushDatagram(refused.data(), refused.size(), t0 + 202000);
    if (!expect(r.disposition == UdpH264AssemblyDisposition::Ignored, "saturation(admit): refused")) return false;
    if (!expect(a.saturation_candidates() == 2 && !a.saturation_floor_set() && a.AbandonedCount() == 16,
                "saturation(admit): candidates, floor and records untouched")) return false;
    // The same seq from the admitted generation is accepted and does replace one.
    (void)push_chunk(a, 700, 0, 12, 4, true, t0 + 203000);
    if (!expect(a.saturation_candidates() == 2 && a.saturation_floor_set() && a.saturation_floor_seq() == 600,
                "saturation(admit): an admitted generation replaces the oldest")) return false;
  }

  // An older seq than the last delivered one is stale traffic, never a reason to reset anything;
  // reordering is unaffected (a lower seq arriving later still assembles when it is not retired).
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 32);
    (void)push_chunk(a, 100, 0, 4, 4, true, t0 + 300000);
    UdpH264AssemblyStepResult out{};
    if (!expect(a.PopDelivery(t0 + 300000, true, &out) && out.frame.header.seq == 100,
                "saturation(stale): seq 100 delivered")) return false;
    const auto old99 = make_video_chunk(99, 0, 12, 4, false, 1);
    const auto r = a.PushDatagram(old99.data(), old99.size(), t0 + 301000);
    if (!expect(r.disposition == UdpH264AssemblyDisposition::Ignored && !a.saturated() &&
                    a.PendingCount() == 0,
                "saturation(stale): an old seq is ignored, nothing is reset")) return false;
    // Reordering: 31 arrives before 30 -- but here nothing is retired, so 30 still assembles.
    UdpH264FrameAssembler b;
    b.ConfigureInOrderHold(120000, 32);
    (void)push_chunk(b, 31, 0, 12, 4, false, t0 + 400000);
    const auto r30 = push_chunk(b, 30, 0, 12, 4, false, t0 + 401000);
    if (!expect(r30.disposition == UdpH264AssemblyDisposition::Partial && b.PendingCount() == 2,
                "saturation(reorder): a later-arriving lower seq still assembles")) return false;
    // Giving up 31 (the arrival-order head) must not block 30 either.
    if (!expect(b.GiveUpIncomplete(1, 31, t0 + 402000), "saturation(reorder): 31 given up")) return false;
    const auto r30b = push_chunk(b, 30, 1, 12, 4, false, t0 + 403000);
    if (!expect(r30b.disposition == UdpH264AssemblyDisposition::Partial,
                "saturation(reorder): 30 is not blocked by the give-up of 31")) return false;
  }
  return true;
}

// A04 saturation, boundary behaviour (the six cases the verifying Claude found missing). Each one
// asserts product state directly, because a passing scenario run does not prove any of them.
bool test_udp_assembler_saturation_bounds() {
  bool ok = true;  // every case runs, so one failure does not hide the others
  const uint64_t t0 = 30000000;
  auto saturate = [&](UdpH264FrameAssembler& a, uint32_t firstSeq) {
    for (uint32_t i = 0; i < 16; ++i) {
      const uint32_t seq = firstSeq + i;
      (void)push_chunk(a, seq, 0, 12, 4, false, t0 + i * 1000);
      (void)a.GiveUpIncomplete(1, seq, t0 + i * 1000);
    }
  };

  // (1) A candidate that fails during the episode is another attempt at the same recovery, not a
  //     new retirement: twenty of them must not grow the record list by one.
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 8);
    saturate(a, 100);
    ok = expect(a.saturated() && a.AbandonedCount() == 16, "bounds(1): episode started") && ok;
    const size_t before = a.AbandonedCount();
    for (uint32_t i = 0; i < 20; ++i) {
      const uint32_t seq = 1000 + i;
      (void)push_chunk(a, seq, 0, 12, 4, true, t0 + 100000 + i * 1000);  // a key candidate
      (void)a.GiveUpIncomplete(1, seq, t0 + 100000 + i * 1000);          // ... which then fails
    }
    ok = expect(a.AbandonedCount() == before,
                "bounds(1): candidate failures add no records (" + std::to_string(a.AbandonedCount()) + ")") && ok;
    ok = expect(a.saturated(), "bounds(1): still one episode") && ok;
  }

  // (2) A key that is newer than the floor but OLDER than both candidates must not evict one --
  //     and the packet that is refused must not disturb the live assemblies (the erase used to
  //     invalidate the iterator the caller then compared).
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 8);
    saturate(a, 100);
    (void)push_chunk(a, 200, 0, 12, 4, true, t0 + 200000);
    (void)push_chunk(a, 202, 0, 12, 4, true, t0 + 201000);
    ok = expect(a.saturation_candidates() == 2, "bounds(2): two candidates") && ok;
    // Older than BOTH candidates (reordering), yet past the floor: it must not take a slot from a
    // candidate that is already being repaired.
    const auto r = push_chunk(a, 198, 0, 12, 4, true, t0 + 202000);
    ok = expect(r.disposition == UdpH264AssemblyDisposition::Ignored,
                "bounds(2): a key older than both candidates is refused") && ok;
    ok = expect(a.saturation_candidates() == 2 && !a.saturation_floor_set(),
                "bounds(2): candidates and floor untouched") && ok;
    // The live candidates still work, and a chunk for one of them is handled normally.
    const auto live = push_chunk(a, 202, 1, 12, 4, true, t0 + 203000);
    ok = expect(live.disposition == UdpH264AssemblyDisposition::Partial,
                "bounds(2): the live candidate still takes chunks") && ok;
    // A genuinely newer key does replace the oldest, and the floor rises to it.
    const auto newer = push_chunk(a, 203, 0, 12, 4, true, t0 + 204000);
    ok = expect(newer.disposition == UdpH264AssemblyDisposition::Partial &&
                    a.saturation_candidates() == 2 && a.saturation_floor_set() &&
                    a.saturation_floor_seq() == 200,
                "bounds(2): a newer key replaces the oldest and raises the floor") && ok;
  }

  // (3) The floor is a session-wide sequence, not a per-generation one: a late key of another
  //     generation at or below it is refused just the same.
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 8);
    saturate(a, 100);
    (void)push_chunk(a, 300, 0, 12, 4, true, t0 + 300000);
    (void)push_chunk(a, 302, 0, 12, 4, true, t0 + 301000);
    (void)push_chunk(a, 304, 0, 12, 4, true, t0 + 302000);  // replaces 300, floor = 300
    ok = expect(a.saturation_floor_set() && a.saturation_floor_seq() == 300, "bounds(3): floor at 300") && ok;
    const auto other = make_video_chunk(300, 0, 12, 4, true, 9);  // generation 9, same seq
    const auto r = a.PushDatagram(other.data(), other.size(), t0 + 303000);
    ok = expect(r.disposition == UdpH264AssemblyDisposition::Ignored,
                "bounds(3): another generation at the floor is refused too") && ok;
  }

  // (4) Retirement follows the session-wide sequence as well: records made under an older
  //     generation are retired by a newer generation's accepted key, so P frames resume.
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 8);
    saturate(a, 400);  // records 400..415 under generation 1
    const auto key = make_video_chunk(500, 0, 4, 4, true, 2);  // generation 2 recovery key
    const auto r = a.PushDatagram(key.data(), key.size(), t0 + 400000);
    ok = expect(r.disposition == UdpH264AssemblyDisposition::Queued, "bounds(4): the new generation's key completed") && ok;
    UdpH264AssemblyStepResult out{};
    ok = expect(a.PopDelivery(t0 + 400000, true, &out) && out.frame.header.seq == 500,
                "bounds(4): it is delivered") && ok;
    a.NoteKeyAccepted(2, 500);
    ok = expect(a.AbandonedCount() == 0, "bounds(4): the older generation's records are retired (" + std::to_string(a.AbandonedCount()) + ")") && ok;
    ok = expect(!a.saturated(), "bounds(4): the episode ended") && ok;
    const auto p = make_video_chunk(501, 0, 4, 4, false, 2);
    const auto rp = a.PushDatagram(p.data(), p.size(), t0 + 401000);
    ok = expect(rp.disposition == UdpH264AssemblyDisposition::Queued, "bounds(4): P frames resume") && ok;
  }

  // (5) The delivered-candidate list is bounded: fifty completed keys the caller never accepts
  //     must not make it grow without end.
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 8);
    saturate(a, 600);
    UdpH264AssemblyStepResult out{};
    for (uint32_t i = 0; i < 50; ++i) {
      const uint32_t seq = 700 + i;
      (void)push_chunk(a, seq, 0, 4, 4, true, t0 + 500000 + i * 1000);
      (void)a.PopDelivery(t0 + 500000 + i * 1000, true, &out);  // delivered, never accepted
    }
    ok = expect(a.delivered_key_candidates() <= 8,
                "bounds(5): the delivered-candidate list is bounded (" + std::to_string(a.delivered_key_candidates()) + ")") && ok;
  }

  // (6) Entering the episode trims the key assemblies to the two slots (the newest), raising the
  //     floor to what it drops; and a release that keeps the latch keeps the floor with it.
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 8);
    for (uint32_t i = 0; i < 4; ++i) {
      (void)push_chunk(a, 800 + i, 0, 12, 4, true, t0 + 600000 + i * 1000);  // four key assemblies
    }
    ok = expect(a.saturation_candidates() == 4, "bounds(6): four key assemblies before the episode") && ok;
    for (uint32_t i = 0; i < 16; ++i) {
      const uint32_t seq = 900 + i;
      (void)push_chunk(a, seq, 0, 12, 4, false, t0 + 700000 + i * 1000);
      (void)a.GiveUpIncomplete(1, seq, t0 + 700000 + i * 1000);
    }
    ok = expect(a.saturated(), "bounds(6): episode started") && ok;
    ok = expect(a.saturation_candidates() == 2,
                "bounds(6): trimmed to two slots (" + std::to_string(a.saturation_candidates()) + ")") && ok;
    ok = expect(a.saturation_floor_set() && a.saturation_floor_seq() == 801,
                "bounds(6): the floor rose to the newest one dropped") && ok;
  }

  // (6b) A release that leaves the list at the cap keeps both the latch and the floor.
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 8);
    saturate(a, 500);                     // records 500..515, episode starts
    (void)push_chunk(a, 400, 0, 12, 4, true, t0 + 800000);   // an older key candidate
    (void)push_chunk(a, 402, 0, 12, 4, true, t0 + 801000);
    (void)push_chunk(a, 404, 0, 12, 4, true, t0 + 802000);   // replaces 400: floor = 400
    ok = expect(a.saturation_floor_set() && a.saturation_floor_seq() == 400, "bounds(6b): floor at 400") && ok;
    (void)push_chunk(a, 402, 1, 12, 4, true, t0 + 803000);
    (void)push_chunk(a, 402, 2, 12, 4, true, t0 + 804000);
    UdpH264AssemblyStepResult out{};
    ok = expect(a.PopDelivery(t0 + 805000, true, &out) && out.frame.header.seq == 402,
                "bounds(6b): the candidate is delivered") && ok;
    a.NoteKeyAccepted(1, 402);
    ok = expect(a.saturated(), "bounds(6b): records above it keep the episode") && ok;
    ok = expect(a.saturation_floor_set() && a.saturation_floor_seq() == 400,
                "bounds(6b): the floor is kept while the episode lasts") && ok;
  }
  return ok;
}

bool test_udp_assembler_in_order_hold() {
  UdpH264FrameAssembler a;
  a.ConfigureInOrderHold(100000, 8);
  UdpH264AssemblyStepResult out{};
  const uint64_t t0 = 5000000;

  // AU 1 half in, AU 2 complete: AU 2 is queued, not delivered, while AU 1 may still be repaired.
  auto r = push_chunk(a, 1, 0, 8, 4, true, t0);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Partial, "hold: AU 1 partial")) return false;
  r = push_chunk(a, 2, 0, 4, 4, false, t0 + 5000);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Queued && !r.droppedPreviousIncomplete,
              "hold: a completed AU is queued, not delivered")) return false;
  if (!expect(!a.PopDelivery(t0 + 5000, true, &out), "hold: AU 2 waits behind AU 1")) return false;
  if (!expect(!a.PopDelivery(t0 + 99000, true, &out), "hold: still waiting inside the hold")) return false;
  if (!expect(a.PendingCount() == 2, "hold: both assemblies pending")) return false;
  // The retransmit lands: AU 1 completes and both go out in order, no gap reported.
  r = push_chunk(a, 1, 1, 8, 4, true, t0 + 60000);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Queued, "hold: repaired AU 1 queued")) return false;
  if (!expect(a.PopDelivery(t0 + 60000, true, &out) && out.frame.header.seq == 1 &&
                  !out.droppedPreviousIncomplete && out.frame.payload.size() == 8 &&
                  (out.frame.header.flags & kEncodedFrameFlagKeyFrame) != 0,
              "hold: AU 1 delivered first, intact")) return false;
  if (!expect(a.PopDelivery(t0 + 60000, true, &out) && out.frame.header.seq == 2 &&
                  !out.droppedPreviousIncomplete,
              "hold: AU 2 delivered second, no gap")) return false;
  if (!expect(!a.PopDelivery(t0 + 60000, true, &out) && a.PendingCount() == 0, "hold: drained")) return false;

  // Hold expiry: AU 3 never completes; AU 4 goes out once AU 3 outlived the hold, carrying the gap.
  const uint64_t t1 = t0 + 200000;
  (void)push_chunk(a, 3, 0, 8, 4, false, t1);
  r = push_chunk(a, 4, 0, 4, 4, false, t1 + 5000);
  if (!expect(!a.PopDelivery(t1 + 99000, true, &out), "hold: AU 4 held while AU 3 is young")) return false;
  if (!expect(a.PopDelivery(t1 + 100000, true, &out) && out.frame.header.seq == 4 &&
                  out.droppedPreviousIncomplete,
              "hold: expired -> AU 4 delivered with the gap")) return false;
  if (!expect(a.PendingCount() == 0, "hold: the given-up AU 3 is gone")) return false;

  // A late chunk of the given-up AU is stale traffic, not a new assembly.
  r = push_chunk(a, 3, 1, 8, 4, false, t1 + 110000);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Ignored && r.reorderDetected,
              "hold: late chunk of a given-up AU is ignored")) return false;

  // Waiting for an IDR: an incomplete NON-key head is not worth holding for -> released at once.
  const uint64_t t2 = t0 + 400000;
  (void)push_chunk(a, 5, 0, 8, 4, false, t2);
  (void)push_chunk(a, 6, 0, 4, 4, true, t2 + 1000);
  if (!expect(a.PopDelivery(t2 + 1000, false, &out) && out.frame.header.seq == 6 &&
                  out.droppedPreviousIncomplete,
              "hold: keyframe-wait releases past a non-key incomplete head")) return false;
  // ... but an incomplete KEY head is held even then (it is the only recovery point).
  const uint64_t t3 = t0 + 600000;
  (void)push_chunk(a, 7, 0, 8, 4, true, t3);
  (void)push_chunk(a, 8, 0, 4, 4, false, t3 + 1000);
  if (!expect(!a.PopDelivery(t3 + 50000, false, &out), "hold: an incomplete IDR head is held while an IDR is awaited")) return false;
  if (!expect(a.PopDelivery(t3 + 100000, false, &out) && out.frame.header.seq == 8 &&
                  out.droppedPreviousIncomplete,
              "hold: the IDR head is given up after the hold")) return false;

  // Sequence order beats arrival order: AU 10 arrives complete before AU 9's chunks; AU 9 goes first.
  const uint64_t t4 = t0 + 800000;
  (void)push_chunk(a, 10, 0, 4, 4, false, t4);
  (void)push_chunk(a, 9, 0, 8, 4, false, t4 + 1000);
  (void)push_chunk(a, 9, 1, 8, 4, false, t4 + 2000);
  if (!expect(a.PopDelivery(t4 + 2000, true, &out) && out.frame.header.seq == 9 &&
                  !out.droppedPreviousIncomplete,
              "hold: AU 9 delivered before AU 10 (contiguous with 8)")) return false;
  if (!expect(a.PopDelivery(t4 + 2000, true, &out) && out.frame.header.seq == 10 &&
                  !out.droppedPreviousIncomplete,
              "hold: then AU 10, contiguous")) return false;

  // The cap evicts the stuck oldest, not the newest: cap 2, AU 11 stuck, AU 12 and 13 arrive.
  UdpH264FrameAssembler b;
  b.ConfigureInOrderHold(100000, 2);
  (void)push_chunk(b, 11, 0, 8, 4, false, t0);
  (void)push_chunk(b, 12, 0, 8, 4, false, t0 + 1000);
  r = push_chunk(b, 13, 0, 4, 4, false, t0 + 2000);
  if (!expect(r.droppedPreviousIncomplete && b.PendingCount() == 2, "hold: cap evicts one")) return false;
  (void)push_chunk(b, 12, 1, 8, 4, false, t0 + 3000);
  if (!expect(b.PopDelivery(t0 + 3000, true, &out) && out.frame.header.seq == 12,
              "hold: the evicted one was AU 11 (oldest), AU 12 survived")) return false;

  // A complete IDR behind a broken chain is the recovery point: everything ahead of it is given
  // up at once and the IDR goes out immediately, carrying the gap (Codex condition 1).
  UdpH264FrameAssembler c;
  c.ConfigureInOrderHold(100000, 8);
  (void)push_chunk(c, 19, 0, 4, 4, false, t0 - 1000);   // something delivered before, so a gap is a gap
  if (!expect(c.PopDelivery(t0 - 1000, true, &out) && out.frame.header.seq == 19, "hold: AU 19 delivered")) return false;
  (void)push_chunk(c, 20, 0, 8, 4, false, t0);          // incomplete P
  (void)push_chunk(c, 21, 0, 4, 4, false, t0 + 1000);   // complete P behind it
  (void)push_chunk(c, 22, 0, 4, 4, true, t0 + 2000);    // complete IDR
  if (!expect(c.PopDelivery(t0 + 2000, true, &out) && out.frame.header.seq == 22 &&
                  out.droppedPreviousIncomplete && c.PendingCount() == 0,
              "hold: a complete IDR releases the broken chain ahead of it at once")) return false;
  if (!expect(!c.PopDelivery(t0 + 2000, true, &out), "hold: nothing left behind the IDR")) return false;

  // The byte cap: three 8-byte assemblies fit in 24 bytes, the fourth evicts the oldest.
  UdpH264FrameAssembler d;
  d.ConfigureInOrderHold(100000, 8, 24);
  (void)push_chunk(d, 30, 0, 8, 4, false, t0);
  (void)push_chunk(d, 31, 0, 8, 4, false, t0 + 1000);
  r = push_chunk(d, 32, 0, 8, 4, false, t0 + 2000);
  if (!expect(!r.droppedPreviousIncomplete && d.HeldBytes() == 24, "hold: within the byte cap")) return false;
  r = push_chunk(d, 33, 0, 8, 4, false, t0 + 3000);
  if (!expect(r.droppedPreviousIncomplete && d.HeldBytes() == 24 && d.PendingCount() == 3,
              "hold: the byte cap evicts the oldest")) return false;
  (void)push_chunk(d, 31, 1, 8, 4, false, t0 + 4000);
  if (!expect(d.PopDelivery(t0 + 4000, true, &out) && out.frame.header.seq == 31,
              "hold: AU 30 was the eviction, AU 31 survived")) return false;

  // Sequence wrap: 0xFFFFFFFF then 0 are contiguous, in that order.
  UdpH264FrameAssembler w;
  w.ConfigureInOrderHold(100000, 8);
  (void)push_chunk(w, 0u, 0, 4, 4, false, t0 + 1000);
  (void)push_chunk(w, 0xFFFFFFFFu, 0, 4, 4, false, t0);
  if (!expect(w.PopDelivery(t0 + 1000, true, &out) && out.frame.header.seq == 0xFFFFFFFFu,
              "hold: the pre-wrap AU goes first")) return false;
  if (!expect(w.PopDelivery(t0 + 1000, true, &out) && out.frame.header.seq == 0 &&
                  !out.droppedPreviousIncomplete,
              "hold: the wrapped AU follows without a gap")) return false;

  // A duplicate (retransmit of a chunk that did arrive) is absorbed, and the payload is intact.
  UdpH264FrameAssembler dup;
  dup.ConfigureInOrderHold(100000, 8);
  (void)push_chunk(dup, 40, 0, 8, 4, false, t0);
  r = push_chunk(dup, 40, 0, 8, 4, false, t0 + 1000);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Partial, "hold: duplicate chunk keeps the AU partial")) return false;
  r = push_chunk(dup, 40, 1, 8, 4, false, t0 + 2000);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Queued && dup.PopDelivery(t0 + 2000, true, &out) &&
                  out.frame.payload.size() == 8 && out.frame.payload[4] == static_cast<uint8_t>(40 * 16 + 4),
              "hold: the AU completes once and its payload is intact")) return false;

  // Legacy (no hold) is unchanged: AU 2 completing discards the half AU 1 and reports the gap.
  UdpH264FrameAssembler legacy;
  (void)push_chunk(legacy, 1, 0, 8, 4, false, t0);
  r = push_chunk(legacy, 2, 0, 4, 4, false, t0 + 1000);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Completed && !r.droppedPreviousIncomplete,
              "legacy: first delivery has no previous to drop")) return false;
  (void)push_chunk(legacy, 3, 0, 8, 4, false, t0 + 2000);
  r = push_chunk(legacy, 4, 0, 4, 4, false, t0 + 3000);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Completed && r.droppedPreviousIncomplete &&
                  legacy.PendingCount() == 0,
              "legacy: a newer completion discards the older incomplete AU at once")) return false;
  return true;
}

bool test_session_controller() {
  ClientSessionController controller;

  ClientSessionConnectArgs invalid{};
  invalid.host = "";
  invalid.videoPort = 43000;
  invalid.controlPort = 43001;
  if (!expect(!controller.Connect(invalid), "session connect should reject empty host")) return false;
  auto snapshot = controller.Snapshot();
  if (!expect(snapshot.state == ClientSessionState::Error, "invalid connect should set error state")) return false;
  if (!expect(snapshot.lastError == "host is required", "invalid connect should expose host error")) return false;

  FakeSessionServer successServer;
  if (!expect(successServer.Start(false), "fake session server should start")) return false;

  ClientSessionConnectArgs valid{};
  valid.host = "127.0.0.1";
  valid.videoPort = successServer.videoPort;
  valid.controlPort = successServer.controlPort;
  valid.controlIntervalMs = 50;
  if (!expect(controller.Connect(valid), "session connect should start worker")) return false;
  if (!expect(wait_until([&]() {
                const auto current = controller.Snapshot();
                return current.state == ClientSessionState::Connected &&
                       current.latestWindowListCount == 2 &&
                       current.controlLoopActive;
              }, 2000), "session should connect and receive window list")) {
    return false;
  }

  snapshot = controller.Snapshot();
  if (!expect(snapshot.host == "127.0.0.1", "snapshot should preserve host")) return false;
  if (!expect(snapshot.videoPort == successServer.videoPort &&
                  snapshot.controlPort == successServer.controlPort,
              "snapshot should preserve ports")) return false;
  if (!expect(snapshot.transport.tcpControlConnected && snapshot.transport.udpVideoReady,
              "snapshot should reflect transport readiness")) return false;
  if (!expect(snapshot.latestWindowListCount == 2, "window list count should update")) return false;
  if (!expect(snapshot.selectedWindowTitle == "desktop", "selected window title should summarize desktop")) {
    return false;
  }
  if (!expect(snapshot.status.find("window_list_received count=2") != std::string::npos,
              "connected status should include window list summary")) return false;

  controller.Disconnect();
  snapshot = controller.Snapshot();
  if (!expect(snapshot.state == ClientSessionState::Disconnected,
              "disconnect should return to disconnected state")) return false;
  if (!expect(!snapshot.sessionThreadActive && !snapshot.controlLoopActive,
              "disconnect should stop worker activity")) return false;

  FakeSessionServer failureServer;
  if (!expect(failureServer.Start(true), "failure server should start")) return false;
  valid.videoPort = failureServer.videoPort;
  valid.controlPort = failureServer.controlPort;
  if (!expect(controller.Connect(valid), "session connect should restart worker")) return false;
  if (!expect(wait_until([&]() {
                const auto current = controller.Snapshot();
                return current.state == ClientSessionState::Error &&
                       current.lastError == "control loop failed";
              }, 3000), "control socket close should surface as error")) {
    return false;
  }

  return true;
}

// The shared input-message builders (F-09): one event, masked buttons, sequence from the queue;
// text split into kControlInputTextMaxUtf16-unit chunks, sequenced after it, queued in order.
bool test_input_message_builders() {
  ClientInputQueue queue;
  const auto ev = remote60::native_poc::make_control_input_event(queue, 2, 0xFFu, 10, -20, 120,
                                                                 65, 777);
  if (!expect(ev.type == MessageType::ControlInputEvent, "builder: event type")) return false;
  if (!expect(ev.inputEvent.header.magic == remote60::native_poc::kMagic &&
                  ev.inputEvent.header.type == static_cast<uint16_t>(MessageType::ControlInputEvent) &&
                  ev.inputEvent.header.size == sizeof(ev.inputEvent),
              "builder: event header")) return false;
  if (!expect(ev.inputEvent.seq == 1 && ev.inputEvent.kind == 2 && ev.inputEvent.buttons == 0x7u &&
                  ev.inputEvent.x == 10 && ev.inputEvent.y == -20 && ev.inputEvent.wheelDelta == 120 &&
                  ev.inputEvent.keyCode == 65 && ev.inputEvent.clientSendQpcUs == 777,
              "builder: event fields (buttons masked to 3 bits, seq from the queue)")) return false;

  std::vector<uint16_t> text(130);  // 64 + 64 + 2
  for (size_t i = 0; i < text.size(); ++i) text[i] = static_cast<uint16_t>(0x3131 + i);
  if (!expect(remote60::native_poc::enqueue_control_input_text(queue, text.data(), text.size(), 900) == 3,
              "builder: 130 units split into 3 chunks")) return false;
  if (!expect(remote60::native_poc::enqueue_control_input_text(queue, nullptr, 5, 900) == 0 &&
                  remote60::native_poc::enqueue_control_input_text(queue, text.data(), 0, 900) == 0,
              "builder: empty text queues nothing")) return false;
  QueuedControlInputMessage out{};
  size_t seen = 0;
  size_t offset = 0;
  uint32_t expectSeq = 2;
  while (queue.TryDequeue(&out)) {
    if (!expect(out.type == MessageType::ControlInputText, "builder: queued item is text")) return false;
    if (!expect(out.inputText.seq == expectSeq++, "builder: chunks sequenced in order after the event")) return false;
    const size_t want = std::min<size_t>(text.size() - offset,
                                         remote60::native_poc::kControlInputTextMaxUtf16);
    if (!expect(out.inputText.utf16Count == want, "builder: chunk length")) return false;
    if (!expect(std::memcmp(out.inputText.utf16, text.data() + offset, want * sizeof(uint16_t)) == 0,
                "builder: chunk content")) return false;
    if (!expect(out.inputText.clientSendQpcUs == 900, "builder: chunk stamp")) return false;
    offset += want;
    ++seen;
  }
  return expect(seen == 3 && offset == text.size(), "builder: all three chunks dequeued");
}

}  // namespace

// P0 (#351/#354): the diagnosis relies on latest-wins move coalescing being counted, the newest
// move's local generatedUs surviving the coalesce, Reset clearing the counters, and NextAction
// carrying generatedUs to the action while the wire send time is (re)stamped at send.
bool test_input_coalesce_and_generated_us() {
  ClientInputQueue q;
  auto mkMove = [&q](int32_t x, uint64_t gen) {
    QueuedControlInputMessage m{};
    m.type = MessageType::ControlInputEvent;
    m.inputEvent.kind = 1;
    m.inputEvent.x = x;
    m.inputEvent.seq = q.NextSequence();
    m.inputEvent.clientSendQpcUs = gen;
    m.generatedUs = gen;
    return m;
  };
  q.Enqueue(mkMove(10, 1000));
  q.Enqueue(mkMove(20, 2000));  // latest-wins: replaces the first in place
  if (!expect(q.coalesced_move_count() == 1, "second move coalesces the first (count=1)")) return false;
  QueuedControlInputMessage out{};
  if (!expect(q.TryDequeue(&out), "one move remains after coalesce")) return false;
  if (!expect(out.inputEvent.x == 20 && out.generatedUs == 2000,
              "coalesce keeps the newest move and its generatedUs")) return false;
  if (!expect(!q.TryDequeue(&out), "queue empty after the single coalesced move")) return false;

  q.Reset();
  if (!expect(q.coalesced_move_count() == 0 && q.dropped_count() == 0, "Reset clears counters")) return false;

  ClientControlScheduler scheduler;
  WindowPanelStateModel windowPanel;
  StreamStateControl streamState;
  CaptureModeRequestState captureMode;
  KeyframeRequestState keyframe(120000, 300000, 3);
  RuntimeTuneState runtimeTune(300000, 30000000, 250000, 1, 240);
  scheduler.Reset(1000, 0);
  scheduler.OnPingCompleted(0);
  q.Enqueue(mkMove(5, 500));
  bool gotInput = false;
  ControlOutboundAction act{};
  for (int i = 0; i < 12 && !gotInput; ++i) {
    ControlOutboundAction a{};
    if (scheduler.NextAction(100000 + static_cast<uint64_t>(i) * 1000, {}, &windowPanel, &streamState,
                             &captureMode, &keyframe, &runtimeTune, &q, &a) &&
        a.kind == ControlOutboundActionKind::InputEvent) {
      act = a;
      gotInput = true;
    }
  }
  if (!expect(gotInput, "input action surfaced from the scheduler")) return false;
  if (!expect(act.inputGeneratedUs == 500, "action carries the local generatedUs")) return false;
  if (!expect(act.inputEvent.clientSendQpcUs != 500,
              "wire send time is stamped at send, not at generation")) return false;
  return true;
}

bool test_input_queue_preserves_key_edges_on_overflow() {
  using namespace remote60::native_poc;
  ClientInputQueue q;
  // A queued key-up (physical) must survive even when the queue floods past its cap with later input
  // -- otherwise a modifier strands down on the host. (Codex 4th review.)
  QueuedControlInputMessage up{};
  up.type = MessageType::ControlPhysicalKey;
  up.physicalKey.down = 0;
  up.physicalKey.scanCode = 0x1d;  // LCtrl
  q.Enqueue(up);
  for (int i = 0; i < 400; ++i) {  // non-coalescing key-down edges force overflow past kMaxInputQueueSize
    QueuedControlInputMessage k{};
    k.type = MessageType::ControlInputEvent;
    k.inputEvent.kind = 5;
    k.inputEvent.keyCode = static_cast<uint32_t>(0x41 + (i % 20));
    q.Enqueue(k);
  }
  int physUps = 0;
  QueuedControlInputMessage o{};
  while (q.TryDequeue(&o)) {
    if (o.type == MessageType::ControlPhysicalKey && o.physicalKey.down == 0) ++physUps;
  }
  if (physUps != 1) {
    std::cout << "FAIL: physical key-up dropped on queue overflow (physUps=" << physUps << ")\n";
    return false;
  }
  // A pure move flood, by contrast, coalesces and is safely bounded.
  ClientInputQueue q2;
  for (int i = 0; i < 500; ++i) {
    QueuedControlInputMessage mv{};
    mv.type = MessageType::ControlInputEvent;
    mv.inputEvent.kind = 1;
    mv.inputEvent.x = i;
    q2.Enqueue(mv);
  }
  int moves = 0;
  while (q2.TryDequeue(&o)) ++moves;
  if (moves > 8) {  // consecutive moves coalesce to ~1
    std::cout << "FAIL: moves not coalesced (moves=" << moves << ")\n";
    return false;
  }
  std::cout << "  ok input queue preserves key edges on overflow, coalesces moves\n";
  return true;
}

int main() {
  if (!test_ping_and_metrics_order()) return 1;
  if (!test_window_and_input_actions()) return 1;
  if (!test_input_coalesce_and_generated_us()) return 1;
  if (!test_input_queue_preserves_key_edges_on_overflow()) return 1;
  if (!test_input_message_builders()) return 1;
  if (!test_capture_runtime_and_keyframe_actions()) return 1;
  if (!test_udp_assembler()) return 1;
  if (!test_video_nack_scheduler()) return 1;
  if (!test_udp_assembler_in_order_hold()) return 1;
  if (!test_udp_assembler_progress_and_give_up()) return 1;
  if (!test_udp_assembler_abandoned_tombstone()) return 1;
  if (!test_udp_assembler_saturation_episode()) return 1;
  if (!test_udp_assembler_saturation_bounds()) return 1;
  if (!test_session_controller()) return 1;
  std::cout << "[shared-core-test] PASS\n";
  return 0;
}
