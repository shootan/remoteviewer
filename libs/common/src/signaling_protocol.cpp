#include "common/signaling_protocol.hpp"

namespace remote60::common::signaling {

const char* to_string(MessageType type) {
  switch (type) {
    case MessageType::Register:
      return "register";
    case MessageType::Registered:
      return "registered";
    case MessageType::PeerState:
      return "peer_state";
    case MessageType::Offer:
      return "offer";
    case MessageType::Answer:
      return "answer";
    case MessageType::Ice:
      return "ice";
    case MessageType::Error:
      return "error";
    case MessageType::Hello:
      return "hello";
    default:
      return "unknown";
  }
}

MessageType message_type_from_string(const std::string& type) {
  if (type == "register") return MessageType::Register;
  if (type == "registered") return MessageType::Registered;
  if (type == "peer_state") return MessageType::PeerState;
  if (type == "offer") return MessageType::Offer;
  if (type == "answer") return MessageType::Answer;
  if (type == "ice") return MessageType::Ice;
  if (type == "error") return MessageType::Error;
  if (type == "hello") return MessageType::Hello;
  return MessageType::Unknown;
}

bool is_valid_role(const std::string& role) {
  return role == kRoleHost || role == kRoleClient;
}

bool is_sdp_message(MessageType type) {
  return type == MessageType::Offer || type == MessageType::Answer || type == MessageType::Ice;
}

bool is_client_to_server(MessageType type) {
  switch (type) {
    case MessageType::Register:
    case MessageType::Offer:
    case MessageType::Answer:
    case MessageType::Ice:
      return true;
    default:
      return false;
  }
}

}  // namespace remote60::common::signaling
