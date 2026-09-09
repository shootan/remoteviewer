#include "directory_session_bootstrap.hpp"

#include <random>
#include <sstream>

#include "directory_client.hpp"
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

  // The observation is UDP on its own port, so the url gives both halves: the host to dial and,
  // when the caller did not pin one, the port that sits one above the http port.
  std::string directoryHost;
  uint16_t directoryHttpPort = 0;
  if (!directory::parse_directory_url(request.url, &directoryHost, &directoryHttpPort, outError)) {
    return false;
  }
  // The rule lives in one place and this uses it, rather than adding one above the http port and
  // hoping. A pinned port still wins -- that is the caller's decision -- and otherwise it is what
  // the directory advertised, or the documented http default, or nothing at all.
  //
  // Nothing at all is the https case with a server that has not been told its observe port. It
  // used to become 444 here, which nothing answers; the viewer then returned before it ever
  // called /api/connect, so it never learned the relay address either. Failing with a reason is
  // the difference between "this server needs configuring" and a connection that just does not
  // work.
  bool directorySecure = false;
  {
    std::string lowered = request.url;
    for (char& c : lowered) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    while (!lowered.empty() && isspace(static_cast<unsigned char>(lowered.front()))) {
      lowered.erase(lowered.begin());
    }
    directorySecure = lowered.rfind("https://", 0) == 0;
  }
  const uint16_t observePort =
      request.directoryUdpPort != 0
          ? request.directoryUdpPort
          : directory::observe_port_for(request.advertised, directoryHttpPort, directorySecure);
  if (observePort == 0) {
    if (outError) {
      *outError = directorySecure
                      ? "this directory has not said where to send address observations; the "
                        "server needs REMOTE60_DIR_OBSERVE_PORT set (or use an http url)"
                      : "directory http port 65535 leaves no room for the observe port";
    }
    return false;
  }
  if (request.advertised.hostRejected && outError) {
    // Not fatal: the directory host is used instead. Recorded so a server-side mistake is
    // findable rather than showing up as a timeout.
    *outError = "note: the directory advertised an unusable observe host; using the directory "
                "host instead";
  }
  const std::string& observeHost =
      request.advertised.host.empty() ? directoryHost : request.advertised.host;

  // Owns the socket for the whole exchange. Released to the caller only once a candidate is
  // settled, so every early return closes it.
  DirectoryRendezvous rendezvous;
  const std::string observeToken = make_observe_token();
  std::string observed;
  if (!rendezvous.Observe(observeHost, observePort, observeToken, &observed, outError)) {
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
