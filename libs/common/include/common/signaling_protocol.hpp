#pragma once

#include <optional>
#include <string>

namespace remote60::common::signaling {

enum class MessageType {
  Register,
  Registered,
  PeerState,
  Offer,
  Answer,
  Ice,
  Error,
  Hello,
  Unknown,
};

inline constexpr const char* kRoleHost = "host";
inline constexpr const char* kRoleClient = "client";

struct Message {
  MessageType type = MessageType::Unknown;
  std::string role;
  std::string from;
  std::string sdp;
  std::string candidate;
  std::string candidateMid;
  std::optional<int> candidateMLineIndex;
  std::string reason;
  std::string code;
  std::string requestId;
  std::string session;
  std::optional<bool> peerOnline;
};

const char* to_string(MessageType type);
MessageType message_type_from_string(const std::string& type);

bool is_valid_role(const std::string& role);
bool is_sdp_message(MessageType type);
bool is_client_to_server(MessageType type);

}  // namespace remote60::common::signaling
