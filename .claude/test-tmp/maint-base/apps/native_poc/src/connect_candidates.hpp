#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace remote60::native_poc {

// The set of addresses a client may try in order to reach a host.
//
// One published address cannot satisfy two networks whose constraints point in opposite
// directions, and this was learned the expensive way. A restrictive network limits which
// destination ports a client may dial outbound, which argues for a well-known port. A
// residential ISP limits what may arrive at the host inbound, and blocks exactly the well-known
// ports to stop people running servers at home. Moving the host to 443 opened the first and
// closed the second: a phone that had connected on 43000 stopped getting through entirely.
//
// No port number satisfies both, so the answer is not to choose one. Offer several and let the
// client find out which works, which is what ICE does and why the products that reach every
// network do not sit on a fixed inbound port at all.
//
// Kinds are ordered by how much they cost when they succeed, not by how likely they are: a
// private candidate keeps the traffic inside the LAN, so when it works it is strictly better
// than going out to the public address and back.

enum class CandidateKind : uint8_t {
  Private = 0,   // a LAN address; avoids the router entirely when both ends are on it
  Public = 1,    // the address the directory observed, which is the one NAT mapped
  PublicAlt = 2, // the same host on a second port, for networks that filter the first
};

struct ConnectCandidate {
  std::string ip;
  uint16_t port = 0;
  CandidateKind kind = CandidateKind::Public;

  bool operator==(const ConnectCandidate& other) const {
    return ip == other.ip && port == other.port;
  }
};

inline const char* candidate_kind_name(CandidateKind kind) {
  switch (kind) {
    case CandidateKind::Private: return "private";
    case CandidateKind::PublicAlt: return "public-alt";
    default: return "public";
  }
}

inline bool candidate_kind_from_name(const std::string& name, CandidateKind* out) {
  if (!out) return false;
  if (name == "private") { *out = CandidateKind::Private; return true; }
  if (name == "public-alt") { *out = CandidateKind::PublicAlt; return true; }
  if (name == "public") { *out = CandidateKind::Public; return true; }
  return false;
}

/** Loopback and link-local addresses can never reach a peer, so they are not candidates. */
inline bool is_usable_local_ipv4(const std::string& ip) {
  if (ip.empty()) return false;
  if (ip.rfind("127.", 0) == 0) return false;   // loopback
  if (ip.rfind("169.254.", 0) == 0) return false;  // link-local, means DHCP failed
  if (ip == "0.0.0.0") return false;
  return true;
}

/**
 * Builds the list a client should try, most-preferred first, with duplicates removed.
 *
 * The public address comes from the directory's observation, so it is the mapping NAT actually
 * made rather than anything the host guessed. `alternatePort` is a second port the host is also
 * listening on; it is only worth publishing when it differs from the observed one.
 *
 * A cap keeps a machine with many virtual adapters -- Hyper-V, WSL, emulators all add their own
 * -- from turning connect into a broadcast. Private addresses are first in preference but are
 * the ones dropped when the cap bites, because a public candidate that works everywhere is worth
 * more than the tenth LAN address that works nowhere.
 */
inline std::vector<ConnectCandidate> build_connect_candidates(
    const std::string& publicIp, uint16_t publicPort, uint16_t alternatePort,
    const std::vector<std::string>& privateIps, uint16_t privatePort, size_t maxCandidates = 6) {
  std::vector<ConnectCandidate> out;
  const auto add = [&out](const std::string& ip, uint16_t port, CandidateKind kind) {
    if (ip.empty() || port == 0) return;
    const ConnectCandidate candidate{ip, port, kind};
    if (std::find(out.begin(), out.end(), candidate) == out.end()) out.push_back(candidate);
  };

  for (const std::string& ip : privateIps) {
    if (is_usable_local_ipv4(ip)) add(ip, privatePort, CandidateKind::Private);
  }
  add(publicIp, publicPort, CandidateKind::Public);
  if (alternatePort != publicPort) add(publicIp, alternatePort, CandidateKind::PublicAlt);

  if (out.size() > maxCandidates) {
    // Keep every public candidate; they are the ones that work from outside the LAN.
    std::stable_partition(out.begin(), out.end(), [](const ConnectCandidate& c) {
      return c.kind != CandidateKind::Private;
    });
    out.resize(maxCandidates);
    // Restore preference order now that the surplus is gone.
    std::stable_partition(out.begin(), out.end(), [](const ConnectCandidate& c) {
      return c.kind == CandidateKind::Private;
    });
  }
  return out;
}

}  // namespace remote60::native_poc
