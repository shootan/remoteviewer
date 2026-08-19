#include "directory_session_bootstrap.hpp"

#include <random>
#include <sstream>

#include "directory_rendezvous.hpp"
#include "directory_session_client.hpp"

namespace remote60::native_poc {

namespace {

/**
 * A fresh token per attempt, tying one observation to one connect.
 *
 * Reusing one would let a stale observation -- from a socket that has since closed, whose NAT
 * mapping has since gone -- answer for this attempt, and the host would punch at an address
 * nobody is listening on.
 */
std::string make_observe_token() {
  static std::mt19937_64 rng{std::random_device{}()};
  std::ostringstream oss;
  oss << std::hex << rng() << rng();
  return oss.str();
}

}  // namespace

bool directory_session_open(const DirectorySessionRequest& request, DirectorySessionResult* result,
                            std::string* outError) {
  if (!result) return false;
  *result = DirectorySessionResult{};
  if (request.url.empty() || request.sessionToken.empty() || request.hostId.empty()) {
    if (outError) *outError = "directory session needs a url, a session and a host";
    return false;
  }

  std::string directoryHost;
  {
    // Only the host part; the observation is UDP on its own port, not HTTP.
    const std::string stripped =
        request.url.rfind("http://", 0) == 0 ? request.url.substr(7) : request.url;
    const size_t colon = stripped.find(':');
    const size_t slash = stripped.find('/');
    const size_t end = std::min(colon == std::string::npos ? stripped.size() : colon,
                                slash == std::string::npos ? stripped.size() : slash);
    directoryHost = stripped.substr(0, end);
  }
  if (directoryHost.empty()) {
    if (outError) *outError = "could not read a host out of the directory url";
    return false;
  }

  // Owns the socket for the whole exchange. Released to the caller only once a candidate is
  // settled, so every early return closes it.
  DirectoryRendezvous rendezvous;
  const std::string observeToken = make_observe_token();
  std::string observed;
  if (!rendezvous.Observe(directoryHost, request.directoryUdpPort, observeToken, &observed,
                          outError)) {
    return false;
  }

  DirectoryConnectTarget target;
  if (!directory_connect(request.url, request.sessionToken, request.hostId, observeToken, &target,
                         outError)) {
    return false;
  }
  if (target.candidates.empty()) {
    if (outError) *outError = "the directory returned no address for this host";
    return false;
  }

  std::vector<RendezvousCandidate> candidates;
  candidates.reserve(target.candidates.size());
  for (const auto& candidate : target.candidates) {
    RendezvousCandidate entry{};
    entry.ip = candidate.ip;
    entry.port = candidate.port;
    entry.kind = candidate_kind_name(candidate.kind);
    candidates.push_back(std::move(entry));
  }

  RendezvousCandidate chosen{};
  std::string punchError;
  const bool answered =
      rendezvous.PunchAny(candidates, request.punchBudgetMs, &chosen, &punchError);
  // Falling back to the first candidate rather than giving up: some NATs drop the punch and pass
  // the hello that follows, and refusing here would turn a slow connection into no connection.
  const ConnectCandidate& fallback = target.candidates.front();

  result->answered = answered;
  if (answered) {
    result->chosen.ip = chosen.ip;
    result->chosen.port = chosen.port;
    // The string survives the race even when the enum has no name for it, which is the only
    // place relay is still distinguishable.
    result->relay = chosen.kind == "relay";
    (void)candidate_kind_from_name(chosen.kind, &result->chosen.kind);
  } else {
    // The fallback is the first candidate, and the relay is deliberately offered last.
    result->chosen = fallback;
  }
  result->punchToken = target.punchToken;
  result->socket = rendezvous.Release();
  if (result->socket == kInvalidSocket) {
    if (outError) *outError = "the prepared socket was lost";
    return false;
  }
  return true;
}

}  // namespace remote60::native_poc
