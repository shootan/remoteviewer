#pragma once

#include <cstdint>

#include "native_socket.hpp"
#include "native_video_client_shared_core.hpp"
#include "udp_control_channel.hpp"

namespace remote60::native_poc {

enum class TcpControlResponseKind : uint8_t {
  None = 0,
  Pong,
  WindowList,
  WindowSelected,
  InputAck,
  MonitorList,
};

struct TcpControlResponse {
  TcpControlResponseKind kind = TcpControlResponseKind::None;
  ControlPongMessage pong{};
  ControlWindowListMessage windowList{};
  ControlWindowSelectedMessage windowSelected{};
  ControlInputAckMessage inputAck{};
  ControlMonitorListMessage monitorList{};
};

// These take a ControlLink rather than a socket because the same exchange runs over TCP on a
// LAN and over the hole-punched UDP socket when the host was reached through the directory.
bool send_control_action(ControlLink& link, const ControlOutboundAction& action);
bool recv_control_response(ControlLink& link, const ControlOutboundAction& action,
                           TcpControlResponse* out);
bool execute_control_action(ControlLink& link, const ControlOutboundAction& action,
                            TcpControlResponse* out);

}  // namespace remote60::native_poc
