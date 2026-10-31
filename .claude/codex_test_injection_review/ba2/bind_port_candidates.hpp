#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace remote60::native_poc {

// Which UDP port the media socket should sit on, as an ordered list of candidates.
//
// The port is not a free choice in practice. Restrictive networks -- company Wi-Fi, guest
// networks, some hotel and campus links -- permit outbound UDP only to a whitelist of
// destination ports, so a host listening on 43000 is unreachable from them however healthy the
// rest of the path is. The failure is silent from both ends: the client's datagrams never
// arrive, the host sees nothing at all, and neither side can tell that apart from an offline
// host. 443 carries QUIC and 3478 carries STUN, so both are open almost everywhere.
//
// This only pays off when NAT preserves the port it is given, because what a restrictive
// firewall inspects is the destination port the client dials, which is the public one. Routers
// that preserve ports are the common case but not the rule, so the host reports the mapping it
// was actually given rather than assuming the request survived.

/**
 * Parses "443,3478,43000" into an ordered candidate list.
 *
 * Entries that are unparseable, out of range, or repeated are dropped rather than failing the
 * whole option: a single typo in a config file should not leave the host with no port to bind.
 * Surrounding whitespace is tolerated so a hand-edited "443, 3478" behaves as written.
 * Returns an empty vector when nothing usable was found, which callers read as "use the default".
 */
inline std::vector<uint16_t> parse_bind_port_candidates(const std::string& text) {
  std::vector<uint16_t> ports;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t comma = text.find(',', start);
    const size_t pieceEnd = (comma == std::string::npos) ? text.size() : comma;
    const size_t first = text.find_first_not_of(" \t", start);
    if (first != std::string::npos && first < pieceEnd) {
      const size_t last = text.find_last_not_of(" \t", pieceEnd - 1);
      const std::string piece = text.substr(first, last - first + 1);
      // strtoul stops at the first non-digit; require it to have consumed the whole token so
      // that "443x" is rejected outright instead of silently becoming 443.
      char* end = nullptr;
      const unsigned long value = std::strtoul(piece.c_str(), &end, 10);
      if (end && *end == '\0' && value > 0 && value <= 65535) {
        const auto port = static_cast<uint16_t>(value);
        if (std::find(ports.begin(), ports.end(), port) == ports.end()) ports.push_back(port);
      }
    }
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return ports;
}

}  // namespace remote60::native_poc
