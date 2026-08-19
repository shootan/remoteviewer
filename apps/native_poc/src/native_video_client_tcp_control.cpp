#include "native_video_client_tcp_control.hpp"

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

}  // namespace remote60::native_poc
