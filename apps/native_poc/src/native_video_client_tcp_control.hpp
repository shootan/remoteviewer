#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

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
  UnlockChallenge,
  UnlockAccepted,
  UnlockStatusResult,
};

struct TcpControlResponse {
  TcpControlResponseKind kind = TcpControlResponseKind::None;
  ControlPongMessage pong{};
  ControlWindowListMessage windowList{};
  ControlWindowSelectedMessage windowSelected{};
  ControlInputAckMessage inputAck{};
  ControlMonitorListMessage monitorList{};
  ControlUnlockChallengeMessage unlockChallenge{};
  ControlUnlockAcceptedMessage unlockAccepted{};
  ControlUnlockStatusResultMessage unlockStatusResult{};
};

// These take a ControlLink rather than a socket because the same exchange runs over TCP on a
// LAN and over the hole-punched UDP socket when the host was reached through the directory.
bool send_control_action(ControlLink& link, const ControlOutboundAction& action);
bool recv_control_response(ControlLink& link, const ControlOutboundAction& action,
                           TcpControlResponse* out);
bool execute_control_action(ControlLink& link, const ControlOutboundAction& action,
                            TcpControlResponse* out);

// One thumbnail exchange on the strict request/response control stream (viewer ledger F-09: the
// Android session and the Windows viewer each spoke this by hand). Asks for `windowId` (0 = the
// desktop) at most maxWidth x maxHeight, reads the header and the payload. False on a link
// failure -- the stream is then desynced and the session must drop; true otherwise, with
// `out->present` saying whether a picture came back. Pixels are left in wire order (BGRA,
// top-down); a caller whose bitmap wants another order converts them itself.
struct WindowThumbnailReply {
  bool present = false;
  uint32_t width = 0;
  uint32_t height = 0;
  uint64_t version = 0;  // host timestamp; changes when the preview content changes
  std::vector<uint8_t> bgra;
};
bool fetch_window_thumbnail(ControlLink& link, uint64_t windowId, uint32_t maxWidth,
                            uint32_t maxHeight, uint64_t nowUs, WindowThumbnailReply* out);

// The UDP video handshake (F-09, same story): Hello until a valid HelloAck or the budget is spent.
// Valid means the protocol version, the FEC feature and -- when a directory token was sent -- the
// DirectoryAuth feature that says the host accepted it. The socket's receive timeout is left at the
// last wait slice; the caller sets what it wants for the stream. `stop` (optional) aborts early.
struct UdpHelloOptions {
  std::string authToken;      // capability from /api/connect; empty on the LAN
  uint32_t budgetMs = 800;    // how long to keep re-sending Hello
  uint32_t sliceMaxMs = 250;  // longest single wait for an ack
  uint32_t retrySleepMs = 0;  // pause after a failed send or a bad ack; 0 = a failed send is fatal
};
bool udp_hello_handshake(SocketHandle sock, const UdpHelloOptions& options,
                         const std::atomic<bool>* stop, std::string* error);

}  // namespace remote60::native_poc
