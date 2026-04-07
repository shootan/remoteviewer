#include <cstdio>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

#include "native_socket.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_client_session.hpp"

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
using remote60::native_poc::UdpH264FrameAssembler;
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
  result = assembler.PushDatagram(datagram.data(), datagram.size());
  if (!expect(result.disposition == UdpH264AssemblyDisposition::Partial,
              "udp assembler should enter partial state")) return false;

  header->flags = 0x4u;
  header->seq = 43;
  header->chunkOffset = 4;
  result = assembler.PushDatagram(datagram.data(), datagram.size());
  if (!expect(result.disposition == UdpH264AssemblyDisposition::Dropped && result.reorderDetected,
              "udp assembler should detect reorder/drop")) return false;

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
                       current.lastError == "tcp control loop failed";
              }, 3000), "control socket close should surface as error")) {
    return false;
  }

  return true;
}

}  // namespace

int main() {
  if (!test_ping_and_metrics_order()) return 1;
  if (!test_window_and_input_actions()) return 1;
  if (!test_capture_runtime_and_keyframe_actions()) return 1;
  if (!test_udp_assembler()) return 1;
  if (!test_session_controller()) return 1;
  std::cout << "[shared-core-test] PASS\n";
  return 0;
}
