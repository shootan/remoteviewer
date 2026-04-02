#pragma once

#include <winsock2.h>

#include <cstdint>

#include "native_video_client_shared_core.hpp"

namespace remote60::native_poc {

enum class TcpControlResponseKind : uint8_t {
  None = 0,
  Pong,
  WindowList,
  WindowSelected,
  InputAck,
};

struct TcpControlResponse {
  TcpControlResponseKind kind = TcpControlResponseKind::None;
  ControlPongMessage pong{};
  ControlWindowListMessage windowList{};
  ControlWindowSelectedMessage windowSelected{};
  ControlInputAckMessage inputAck{};
};

bool send_tcp_control_action(SOCKET controlSock, const ControlOutboundAction& action);
bool recv_tcp_control_response(SOCKET controlSock, const ControlOutboundAction& action, TcpControlResponse* out);
bool execute_tcp_control_action(SOCKET controlSock, const ControlOutboundAction& action, TcpControlResponse* out);

}  // namespace remote60::native_poc
