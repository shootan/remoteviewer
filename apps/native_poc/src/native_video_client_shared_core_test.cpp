#include <cstdio>
#include <iostream>
#include <string>

#include "native_video_client_shared_core.hpp"
#include "native_video_client_session.hpp"

namespace {

using remote60::native_poc::CaptureModeRequestState;
using remote60::native_poc::ClientControlMetricsSnapshot;
using remote60::native_poc::ClientControlScheduler;
using remote60::native_poc::ClientInputQueue;
using remote60::native_poc::ControlOutboundAction;
using remote60::native_poc::ControlOutboundActionKind;
using remote60::native_poc::ControlWindowListMessage;
using remote60::native_poc::ControlWindowSelectedMessage;
using remote60::native_poc::KeyframeRequestState;
using remote60::native_poc::ClientSessionConnectArgs;
using remote60::native_poc::ClientSessionController;
using remote60::native_poc::ClientSessionState;
using remote60::native_poc::MessageType;
using remote60::native_poc::QueuedControlInputMessage;
using remote60::native_poc::RuntimeTuneState;
using remote60::native_poc::UdpCodec;
using remote60::native_poc::UdpH264AssemblyDisposition;
using remote60::native_poc::UdpH264FrameAssembler;
using remote60::native_poc::UdpPacketKind;
using remote60::native_poc::UdpVideoChunkHeader;
using remote60::native_poc::WindowPanelStateModel;

bool expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "[shared-core-test] FAIL: " << message << "\n";
    return false;
  }
  return true;
}

bool test_ping_and_metrics_order() {
  ClientControlScheduler scheduler;
  WindowPanelStateModel windowPanel;
  CaptureModeRequestState captureMode;
  KeyframeRequestState keyframe(120000, 300000, 3);
  RuntimeTuneState runtimeTune(300000, 30000000, 250000, 1, 240);
  ClientInputQueue inputQueue;
  ControlOutboundAction action{};

  scheduler.Reset(1000, 1000);
  ClientControlMetricsSnapshot metrics{};
  metrics.updatedQpcUs = 1500;
  metrics.message.recvMbpsX1000 = 5000;

  if (!expect(scheduler.NextAction(1000, metrics, &windowPanel, &captureMode, &keyframe,
                                   &runtimeTune, &inputQueue, &action),
              "initial ping action missing")) return false;
  if (!expect(action.kind == ControlOutboundActionKind::Ping, "first action should be ping")) return false;
  if (!expect(action.expectedResponseType.has_value() &&
                  *action.expectedResponseType == MessageType::ControlPong,
              "ping should expect pong")) return false;

  scheduler.OnPingCompleted(1100);

  if (!expect(scheduler.NextAction(1200, metrics, &windowPanel, &captureMode, &keyframe,
                                   &runtimeTune, &inputQueue, &action),
              "metrics action missing after ping")) return false;
  if (!expect(action.kind == ControlOutboundActionKind::Metrics, "second action should be metrics")) return false;

  if (!expect(!scheduler.NextAction(1300, metrics, &windowPanel, &captureMode, &keyframe,
                                    &runtimeTune, &inputQueue, &action),
              "metrics should not resend without updated timestamp")) return false;

  return true;
}

bool test_window_and_input_actions() {
  ClientControlScheduler scheduler;
  WindowPanelStateModel windowPanel;
  CaptureModeRequestState captureMode;
  KeyframeRequestState keyframe(120000, 300000, 3);
  RuntimeTuneState runtimeTune(300000, 30000000, 250000, 1, 240);
  ClientInputQueue inputQueue;
  ControlOutboundAction action{};

  scheduler.Reset(1000, 0);
  scheduler.OnPingCompleted(0);

  windowPanel.RequestList("pending");
  if (!expect(scheduler.NextAction(100, {}, &windowPanel, &captureMode, &keyframe,
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

  if (!expect(scheduler.NextAction(200, {}, &windowPanel, &captureMode, &keyframe,
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

  if (!expect(scheduler.NextAction(300, {}, &windowPanel, &captureMode, &keyframe,
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
  CaptureModeRequestState captureMode;
  KeyframeRequestState keyframe(120000, 300000, 3);
  RuntimeTuneState runtimeTune(300000, 30000000, 250000, 1, 240);
  ClientInputQueue inputQueue;
  ControlOutboundAction action{};

  scheduler.Reset(1000, 0);
  scheduler.OnPingCompleted(0);

  captureMode.Request(2, 4200, 7300);
  if (!expect(scheduler.NextAction(100, {}, &windowPanel, &captureMode, &keyframe,
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
  if (!expect(scheduler.NextAction(200, metrics, &windowPanel, &captureMode, &keyframe,
                                   &runtimeTune, &inputQueue, &action),
              "runtime-tune action missing")) return false;
  if (!expect(action.kind == ControlOutboundActionKind::RuntimeTune,
              "expected runtime-tune action")) return false;
  if (!expect((action.runtimeTune.flags & 0x3u) == 0x3u,
              "runtime-tune flags should include bitrate and keyint")) return false;

  keyframe.Reset();
  const auto queued = keyframe.Request(3, 500);
  if (!expect(queued.queued, "keyframe request should queue")) return false;
  if (!expect(scheduler.NextAction(600, {}, &windowPanel, &captureMode, &keyframe,
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

  ClientSessionConnectArgs valid{};
  valid.host = "192.168.0.10";
  valid.videoPort = 43000;
  valid.controlPort = 43001;
  if (!expect(controller.Connect(valid), "session connect should accept valid args")) return false;
  snapshot = controller.Snapshot();
  if (!expect(snapshot.state == ClientSessionState::Connected, "valid connect should set connected state")) return false;
  if (!expect(snapshot.host == "192.168.0.10", "snapshot should preserve host")) return false;
  if (!expect(snapshot.videoPort == 43000 && snapshot.controlPort == 43001,
              "snapshot should preserve ports")) return false;

  controller.Disconnect();
  snapshot = controller.Snapshot();
  if (!expect(snapshot.state == ClientSessionState::Disconnected,
              "disconnect should return to disconnected state")) return false;

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
