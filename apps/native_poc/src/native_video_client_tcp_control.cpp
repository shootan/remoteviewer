#include "native_video_client_tcp_control.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

namespace remote60::native_poc {

bool send_control_action(ControlLink& link, const ControlOutboundAction& action) {
  switch (action.kind) {
    case ControlOutboundActionKind::Ping:
      return link.Write(&action.ping, sizeof(action.ping));
    case ControlOutboundActionKind::WindowListRequest:
      return link.Write(&action.windowListRequest, sizeof(action.windowListRequest));
    case ControlOutboundActionKind::WindowSelect:
      return link.Write(&action.windowSelect, sizeof(action.windowSelect));
    case ControlOutboundActionKind::MonitorListRequest:
      return link.Write(&action.monitorListRequest, sizeof(action.monitorListRequest));
    case ControlOutboundActionKind::MonitorSelect:
      return link.Write(&action.monitorSelect, sizeof(action.monitorSelect));
    case ControlOutboundActionKind::StreamState:
      return link.Write(&action.streamState, sizeof(action.streamState));
    case ControlOutboundActionKind::CaptureMode:
      return link.Write(&action.captureMode, sizeof(action.captureMode));
    case ControlOutboundActionKind::Metrics:
      return link.Write(&action.metrics, sizeof(action.metrics));
    case ControlOutboundActionKind::KeyframeRequest:
      return link.Write(&action.keyframe, sizeof(action.keyframe));
    case ControlOutboundActionKind::RuntimeTune:
      return link.Write(&action.runtimeTune, sizeof(action.runtimeTune));
    case ControlOutboundActionKind::DesktopBackend:
      return link.Write(&action.desktopBackend, sizeof(action.desktopBackend));
    case ControlOutboundActionKind::InputEvent:
      return link.Write(&action.inputEvent, sizeof(action.inputEvent));
    case ControlOutboundActionKind::InputText:
      return link.Write(&action.inputText, sizeof(action.inputText));
    case ControlOutboundActionKind::None:
    default:
      return false;
  }
}

bool recv_control_response(ControlLink& link, const ControlOutboundAction& action,
                           TcpControlResponse* out) {
  if (!out) return false;
  *out = TcpControlResponse{};
  if (!action.expectedResponseType.has_value()) return true;

  MessageHeader header{};
  if (!link.Read(&header, sizeof(header))) return false;
  if (header.magic != kMagic ||
      header.type != static_cast<uint16_t>(*action.expectedResponseType) ||
      header.size != action.expectedResponseSize) {
    return false;
  }

  switch (*action.expectedResponseType) {
    case MessageType::ControlPong:
      out->kind = TcpControlResponseKind::Pong;
      out->pong.header = header;
      return link.Read(&out->pong.seq, sizeof(out->pong) - sizeof(MessageHeader));
    case MessageType::ControlWindowList:
      out->kind = TcpControlResponseKind::WindowList;
      out->windowList.header = header;
      return link.Read(&out->windowList.seq, sizeof(out->windowList) - sizeof(MessageHeader));
    case MessageType::ControlWindowSelected:
      out->kind = TcpControlResponseKind::WindowSelected;
      out->windowSelected.header = header;
      return link.Read(&out->windowSelected.seq,
                       sizeof(out->windowSelected) - sizeof(MessageHeader));
    case MessageType::ControlInputAck:
      out->kind = TcpControlResponseKind::InputAck;
      out->inputAck.header = header;
      return link.Read(&out->inputAck.seq, sizeof(out->inputAck) - sizeof(MessageHeader));
    case MessageType::ControlMonitorList:
      out->kind = TcpControlResponseKind::MonitorList;
      out->monitorList.header = header;
      return link.Read(&out->monitorList.seq, sizeof(out->monitorList) - sizeof(MessageHeader));
    default:
      out->kind = TcpControlResponseKind::None;
      return link.Discard(header.size - sizeof(MessageHeader));
  }
}

bool execute_control_action(ControlLink& link, const ControlOutboundAction& action,
                            TcpControlResponse* out) {
  // One request is one message; the boundary matters to UDP and is free over TCP.
  if (!send_control_action(link, action) || !link.EndMessage()) return false;
  return recv_control_response(link, action, out);
}

bool fetch_window_thumbnail(ControlLink& link, uint64_t windowId, uint32_t maxWidth,
                            uint32_t maxHeight, uint64_t nowUs, WindowThumbnailReply* out) {
  if (out) *out = WindowThumbnailReply{};
  ControlWindowThumbnailRequestMessage req{};
  req.header.magic = kMagic;
  req.header.type = static_cast<uint16_t>(MessageType::ControlWindowThumbnailRequest);
  req.header.size = static_cast<uint16_t>(sizeof(req));
  req.windowId = windowId;
  req.maxWidth = maxWidth;
  req.maxHeight = maxHeight;
  req.clientSendQpcUs = nowUs;
  // One request is one message; EndMessage() draws the boundary UDP needs and TCP ignores.
  if (!link.Write(&req, sizeof(req)) || !link.EndMessage()) return false;
  ControlWindowThumbnailHeader rsp{};
  if (!link.Read(&rsp, sizeof(rsp))) return false;
  if (rsp.header.magic != kMagic ||
      rsp.header.type != static_cast<uint16_t>(MessageType::ControlWindowThumbnail) ||
      rsp.payloadSize > kWindowThumbnailMaxPayloadBytes) {
    return false;
  }
  std::vector<uint8_t> payload(rsp.payloadSize);
  if (rsp.payloadSize > 0 && !link.Read(payload.data(), payload.size())) return false;
  if (out && (rsp.flags & 0x1u) != 0 && rsp.width > 0 && rsp.height > 0 &&
      payload.size() == static_cast<size_t>(rsp.width) * rsp.height * 4u) {
    out->present = true;
    out->width = rsp.width;
    out->height = rsp.height;
    out->version = rsp.version;
    out->bgra = std::move(payload);
  }
  return true;
}

bool udp_hello_handshake(SocketHandle sock, const UdpHelloOptions& options,
                         const std::atomic<bool>* stop, std::string* error) {
  UdpHelloPacket hello{};
  std::snprintf(hello.authToken, sizeof(hello.authToken), "%s", options.authToken.c_str());
  const auto stopped = [stop]() { return stop && stop->load(std::memory_order_acquire); };
  const auto now_ms = []() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
  };
  const uint64_t deadlineMs = now_ms() + std::max<uint32_t>(1, options.budgetMs);
  while (!stopped() && now_ms() < deadlineMs) {
    const int sent = send(sock, reinterpret_cast<const char*>(&hello), sizeof(hello), 0);
    if (sent != static_cast<int>(sizeof(hello))) {
      if (options.retrySleepMs == 0) {
        if (error) *error = "udp hello send failed";
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(options.retrySleepMs));
      continue;
    }
    // Wait at most one slice so a stop request or the deadline is noticed between tries.
    const uint64_t nowMs = now_ms();
    const uint64_t remainingMs = deadlineMs > nowMs ? deadlineMs - nowMs : 0;
    const uint32_t sliceMs = static_cast<uint32_t>(
        std::clamp<uint64_t>(remainingMs, 1, std::max<uint32_t>(1, options.sliceMaxMs)));
    (void)set_recv_timeout(sock, sliceMs);
    UdpHelloPacket ack{};
    const int received = recv(sock, reinterpret_cast<char*>(&ack), sizeof(ack), 0);
    const bool validAck =
        received >= static_cast<int>(sizeof(UdpHelloPacket)) && ack.magic == kMagic &&
        ack.kind == static_cast<uint16_t>(UdpPacketKind::HelloAck) &&
        ack.version == kUdpProtocolVersion && (ack.features & kUdpFeatureVideoFec) != 0;
    const bool directoryAuthorized =
        options.authToken.empty() || (ack.features & kUdpFeatureDirectoryAuth) != 0;
    if (validAck && directoryAuthorized) {
      if (error) error->clear();
      return true;
    }
    if (options.retrySleepMs > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(options.retrySleepMs));
    }
  }
  if (error) *error = "udp hello ack failed";
  return false;
}

}  // namespace remote60::native_poc
