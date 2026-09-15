#pragma once

#include <cstring>
#include <string>
#include "poc_protocol.hpp"
#include "product_version.hpp"
#include "udp_control_channel.hpp"

namespace remote60::native_poc {
inline std::string local_product_version() {
  std::string result;
  for (wchar_t c : kProductVersion) {
    if (c == 0) break;
    if (c > 127 || result.size() >= 31) return "unknown";
    result.push_back(static_cast<char>(c));
  }
  return result;
}

// The remote string is untrusted. Never echo control bytes, whitespace or unterminated input.
inline std::string reported_peer_version(const char (&version)[32]) {
  std::string result;
  for (char c : version) {
    if (c == 0) return result.empty() ? "unknown-invalid" : result;
    const bool safe = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                      (c >= 'A' && c <= 'Z') || c == '.' || c == '-' || c == '+';
    if (!safe) return "unknown-invalid";
    result.push_back(c);
  }
  return "unknown-invalid";
}

inline ControlVersionMessage make_version_message(MessageType type, uint32_t seq) {
  ControlVersionMessage message{};
  message.header.type = static_cast<uint16_t>(type);
  message.header.size = sizeof(message);
  message.seq = seq;
  const auto version = local_product_version();
  std::memcpy(message.productVersion, version.data(), version.size());
  return message;
}

// The host's capability is the compatibility gate: never send an unknown opcode to old hosts.
inline bool exchange_peer_version(ControlLink& link, bool supported, uint32_t seq,
                                  std::string* peer) {
  *peer = "unknown-unsupported";
  if (!supported) return true;
  *peer = "unknown-exchange-failed";
  const auto request = make_version_message(MessageType::ControlVersionRequest, seq);
  if (!link.Write(&request, sizeof(request)) || !link.EndMessage()) return false;
  ControlVersionMessage response{};
  if (!link.Read(&response.header, sizeof(response.header))) return false;
  if (response.header.magic != kMagic ||
      response.header.type != static_cast<uint16_t>(MessageType::ControlVersionResponse) ||
      response.header.size != sizeof(response)) return false;
  if (!link.Read(&response.seq, sizeof(response) - sizeof(response.header)) || response.seq != seq)
    return false;
  *peer = reported_peer_version(response.productVersion);
  return true;
}
}  // namespace remote60::native_poc
