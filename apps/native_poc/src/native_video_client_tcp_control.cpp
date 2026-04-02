#include "native_video_client_tcp_control.hpp"

#include <vector>

namespace remote60::native_poc {

namespace {

bool recv_all(SOCKET s, void* out, size_t len) {
  auto* p = reinterpret_cast<uint8_t*>(out);
  size_t got = 0;
  while (got < len) {
    const int n = recv(s, reinterpret_cast<char*>(p + got), static_cast<int>(len - got), 0);
    if (n <= 0) return false;
    got += static_cast<size_t>(n);
  }
  return true;
}

bool send_all(SOCKET s, const void* data, size_t len) {
  const char* p = reinterpret_cast<const char*>(data);
  size_t sent = 0;
  while (sent < len) {
    const int n = send(s, p + sent, static_cast<int>(len - sent), 0);
    if (n <= 0) return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

bool recv_discard(SOCKET s, size_t len) {
  std::vector<uint8_t> scratch(1024);
  size_t left = len;
  while (left > 0) {
    const size_t chunk = (std::min)(left, scratch.size());
    if (!recv_all(s, scratch.data(), chunk)) return false;
    left -= chunk;
  }
  return true;
}

}  // namespace

bool send_tcp_control_action(SOCKET controlSock, const ControlOutboundAction& action) {
  switch (action.kind) {
    case ControlOutboundActionKind::Ping:
      return send_all(controlSock, &action.ping, sizeof(action.ping));
    case ControlOutboundActionKind::WindowListRequest:
      return send_all(controlSock, &action.windowListRequest, sizeof(action.windowListRequest));
    case ControlOutboundActionKind::WindowSelect:
      return send_all(controlSock, &action.windowSelect, sizeof(action.windowSelect));
    case ControlOutboundActionKind::CaptureMode:
      return send_all(controlSock, &action.captureMode, sizeof(action.captureMode));
    case ControlOutboundActionKind::Metrics:
      return send_all(controlSock, &action.metrics, sizeof(action.metrics));
    case ControlOutboundActionKind::KeyframeRequest:
      return send_all(controlSock, &action.keyframe, sizeof(action.keyframe));
    case ControlOutboundActionKind::RuntimeTune:
      return send_all(controlSock, &action.runtimeTune, sizeof(action.runtimeTune));
    case ControlOutboundActionKind::InputEvent:
      return send_all(controlSock, &action.inputEvent, sizeof(action.inputEvent));
    case ControlOutboundActionKind::InputText:
      return send_all(controlSock, &action.inputText, sizeof(action.inputText));
    case ControlOutboundActionKind::None:
    default:
      return false;
  }
}

bool recv_tcp_control_response(SOCKET controlSock, const ControlOutboundAction& action, TcpControlResponse* out) {
  if (!out) return false;
  *out = TcpControlResponse{};
  if (!action.expectedResponseType.has_value()) return true;

  MessageHeader header{};
  if (!recv_all(controlSock, &header, sizeof(header))) return false;
  if (header.magic != kMagic ||
      header.type != static_cast<uint16_t>(*action.expectedResponseType) ||
      header.size != action.expectedResponseSize) {
    return false;
  }

  switch (*action.expectedResponseType) {
    case MessageType::ControlPong:
      out->kind = TcpControlResponseKind::Pong;
      out->pong.header = header;
      return recv_all(controlSock, &out->pong.seq, sizeof(out->pong) - sizeof(MessageHeader));
    case MessageType::ControlWindowList:
      out->kind = TcpControlResponseKind::WindowList;
      out->windowList.header = header;
      return recv_all(controlSock, &out->windowList.seq, sizeof(out->windowList) - sizeof(MessageHeader));
    case MessageType::ControlWindowSelected:
      out->kind = TcpControlResponseKind::WindowSelected;
      out->windowSelected.header = header;
      return recv_all(controlSock, &out->windowSelected.seq,
                      sizeof(out->windowSelected) - sizeof(MessageHeader));
    case MessageType::ControlInputAck:
      out->kind = TcpControlResponseKind::InputAck;
      out->inputAck.header = header;
      return recv_all(controlSock, &out->inputAck.seq, sizeof(out->inputAck) - sizeof(MessageHeader));
    default:
      out->kind = TcpControlResponseKind::None;
      return recv_discard(controlSock, header.size - sizeof(MessageHeader));
  }
}

bool execute_tcp_control_action(SOCKET controlSock, const ControlOutboundAction& action, TcpControlResponse* out) {
  if (!send_tcp_control_action(controlSock, action)) return false;
  return recv_tcp_control_response(controlSock, action, out);
}

}  // namespace remote60::native_poc
