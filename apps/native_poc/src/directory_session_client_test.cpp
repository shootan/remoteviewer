// Pins the parsing of what the directory replies with.
//
// These run against real server output rather than invented JSON, because the shapes here are a
// contract with apps/directory/server.js and the failure mode is quiet: a host list that comes
// back empty looks identical to an account with no PCs, and a candidate list that loses an entry
// looks identical to a network that dropped it.
//
// The transport is not covered -- that needs a server -- so what is checked is everything that
// happens to the bytes after they arrive.

#include <cstdio>
#include <string>
#include <vector>

// The parsing helpers are internal to the translation unit, so the test includes it directly
// rather than widening the header for testing's sake.
#include "directory_session_bootstrap.hpp"
#include "directory_session_client.cpp"

using namespace remote60::native_poc;

namespace {

int gFailures = 0;

void check(const char* name, bool cond, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ",
              detail.c_str());
  if (!cond) ++gFailures;
}

}  // namespace

int main() {
  // ---------------------------------------------------------------- the bootstrap's own refusal
  //
  // Through the real directory_session_open, not the rule in isolation: this is the path that used
  // to add one to the http port unconditionally. It returns before any socket is touched, so the
  // refusal is checkable here.
  //
  // The https half of the same branch is NOT reachable yet -- parse_directory_url still rejects
  // https before the port rule runs -- so it is not asserted here and must not be read as covered.
  // It becomes reachable in (C), and the assertion belongs with that change.
  {
    DirectorySessionRequest request{};
    request.url = "http://directory.example:65535";
    request.sessionToken = "t";
    request.hostId = "h";
    DirectorySessionResult session{};
    std::string error;
    const bool opened = directory_session_open(request, &session, &error);
    check("a directory on 65535 is refused rather than dialling port 0", !opened, error);
    check("...and the reason names the port, not the network",
          error.find("65535") != std::string::npos, error);
  }

  // ---------------------------------------------------------------- where observations go
  //
  // The clients used to derive this as httpPort + 1. Behind TLS on 443 that is 444, nothing
  // listens there, and a host whose observation fails skips its heartbeat -- so it never appears
  // in the list at all, and the relay cannot cover for it because the relay address only arrives
  // in a /api/connect response the viewer never reaches.
  {
    using directory::ObserveEndpoint;
    using directory::observe_port_for;
    using directory::parse_observe_metadata;

    ObserveEndpoint got;
    const bool read = parse_observe_metadata(R"({"sessionToken":"t","observe":{"port":29181}})", &got);
    const uint16_t readPort = got.port;
    check("the server's port is read",
          read && got.known && readPort == 29181 && got.host.empty(), std::to_string(readPort));

    check("and a host with it",
          parse_observe_metadata(R"({"observe":{"port":29181,"host":"udp.example"}})", &got) &&
              got.host == "udp.example",
          got.host);

    // Absent is the ordinary case for a server that has not been updated.
    check("no metadata is not an error, just absent",
          !parse_observe_metadata(R"({"sessionToken":"t"})", &got) && !got.known);

    // Out of range is treated as absent rather than clamped. A wrong port is worse than none:
    // none leaves the caller on a documented fallback, wrong sends it somewhere that will never
    // answer and looks like a network fault.
    check("port 0 is refused", !parse_observe_metadata(R"({"observe":{"port":0}})", &got));
    check("port 65536 is refused", !parse_observe_metadata(R"({"observe":{"port":65536}})", &got));
    check("a malformed observe object is refused",
          !parse_observe_metadata(R"({"observe":{"nope":1}})", &got));

    // The nested read matters: a flat search would find a "port" belonging to something else.
    // It happens to be unambiguous in today's responses, and would not stay that way.
    check("a port outside the observe object is not mistaken for it",
          !parse_observe_metadata(R"({"port":29181,"sessionToken":"t"})", &got));
    // Into variables first. check() takes the detail as an argument, and C++ does not promise it
    // is evaluated after the condition -- so a detail read from `got` can be the value from
    // BEFORE this parse. That has misled a reader of this suite before now.
    const bool nestedWins = parse_observe_metadata(R"({"port":1,"observe":{"port":29181}})", &got);
    const uint16_t nestedPort = got.port;
    check("the observe object wins over an unrelated port", nestedWins && nestedPort == 29181,
          std::to_string(nestedPort));

    // ---- the scheme decision, which used to be made in three places
    //
    // The parser compared the scheme case-sensitively while two callers lowercased first, so
    // `HTTPS://host` was "unsupported scheme" to one and "secure" to the others. That answer picks
    // the transport, so the disagreement was between encrypting and not.
    using directory::directory_url_is_secure;
    check("https is secure", directory_url_is_secure("https://rem.shotan.net"));
    check("http is not", !directory_url_is_secure("http://rem.shotan.net"));
    check("the scheme is case-insensitive", directory_url_is_secure("HTTPS://rem.shotan.net"));
    check("mixed case too", directory_url_is_secure("HtTpS://rem.shotan.net"));
    check("leading whitespace does not hide it",
          directory_url_is_secure("   https://rem.shotan.net"));
    check("HTTP in any case is still not secure", !directory_url_is_secure("HTTP://x"));
    check("a bare host is not secure", !directory_url_is_secure("rem.shotan.net"));
    check("a name that merely starts with https is not a scheme",
          !directory_url_is_secure("httpsx://x"));
    check("empty is not secure", !directory_url_is_secure(""));

    // ---- the advertised host, which the server does NOT validate
    //
    // It trims the configured value and sends it. A configuration slip therefore arrives here
    // looking like an address, and dialling it produces a timeout that reads as a network fault.
    // The client checks it, falls back to the directory host, and says that it did.
    using directory::observe_host_is_usable;
    check("an ordinary name is usable", observe_host_is_usable("udp.example.com"));
    check("an IPv4 literal is usable", observe_host_is_usable("192.168.0.6"));
    check("a scheme is not a host", !observe_host_is_usable("http://x"));
    check("a host:port is not a host", !observe_host_is_usable("x:1234"));
    check("a path is not a host", !observe_host_is_usable("x/y"));
    check("a space is not allowed", !observe_host_is_usable("a b"));
    check("a control character is not allowed", !observe_host_is_usable(std::string("a	b")));
    check("empty is not a host", !observe_host_is_usable(""));
    check("an empty label is refused", !observe_host_is_usable("a..b"));
    check("a trailing dot is refused", !observe_host_is_usable("a.b."));
    check("a label may not start with -", !observe_host_is_usable("-a.b"));
    check("a label may not end with -", !observe_host_is_usable("a.b-"));
    check("an absurdly long name is refused", !observe_host_is_usable(std::string(254, 'a')));

    {
      // The port survives a bad host: only the host is dropped, and the drop is recorded.
      ObserveEndpoint bad;
      const bool parsed =
          parse_observe_metadata(R"({"observe":{"port":29181,"host":"http://x"}})", &bad);
      check("a bad host does not throw away a good port",
            parsed && bad.known && bad.port == 29181 && bad.host.empty());
      check("...and the rejection is recorded rather than silent", bad.hostRejected);

      ObserveEndpoint blank;
      parse_observe_metadata(R"({"observe":{"port":29181,"host":"   "}})", &blank);
      check("a whitespace-only host is absent, not rejected",
            blank.host.empty() && !blank.hostRejected);

      ObserveEndpoint good;
      parse_observe_metadata(R"({"observe":{"port":29181,"host":" udp.example "}})", &good);
      check("a host is trimmed", good.host == "udp.example" && !good.hostRejected, good.host);
    }

    // A port that only looks like an integer. The shared getter matches [0-9]+ and would read
    // 29181 out of 29181.5 -- this server rejects such a value, another server is not bound by
    // that, and a client that trusts the server to validate breaks on the first one that does not.
    check("a fractional port is refused",
          !parse_observe_metadata(R"({"observe":{"port":29181.5}})", &got));
    check("a quoted port is refused",
          !parse_observe_metadata(R"({"observe":{"port":"29181"}})", &got));
    check("a negative port is refused",
          !parse_observe_metadata(R"({"observe":{"port":-1}})", &got));

    // ---- the rule
    ObserveEndpoint said;
    said.known = true;
    said.port = 29181;
    check("what the server said is used, on https",
          observe_port_for(said, 443, true) == 29181);
    check("...and on http too", observe_port_for(said, 8080, false) == 29181);

    // Plain http with nothing said keeps working exactly as every existing deployment does.
    check("http with no metadata keeps the +1 default",
          observe_port_for(ObserveEndpoint{}, 8080, false) == 8081);
    check("http on 65535 has nowhere to add one",
          observe_port_for(ObserveEndpoint{}, 65535, false) == 0);

    // THE case this exists for. 443 + 1 is not a fallback; it is a guess that cannot be right.
    check("https with no metadata refuses to guess 444",
          observe_port_for(ObserveEndpoint{}, 443, true) == 0);
    check("...and does not fall back to a fixed 29181 either",
          observe_port_for(ObserveEndpoint{}, 443, true) != 29181);
  }

  // Copied from an actual /api/hosts reply.
  const std::string hostsJson =
      R"({"hosts":[{"hostId":"87d843e41d8ed901","hostName":"Office PC","online":true,)"
      R"("lastSeen":1786010021341},{"hostId":"a1b2c3","hostName":"Home","online":false,)"
      R"("lastSeen":1786000000000}]})";

  const auto hostObjects = json_array_objects(hostsJson, "hosts");
  check("both hosts are found", hostObjects.size() == 2,
        "count=" + std::to_string(hostObjects.size()));

  std::string id;
  std::string name;
  bool online = false;
  if (hostObjects.size() == 2) {
    json_get_string(hostObjects[0], "hostId", &id);
    json_get_string(hostObjects[0], "hostName", &name);
    json_get_bool(hostObjects[0], "online", &online);
  }
  check("the first host's fields survive", id == "87d843e41d8ed901" && name == "Office PC" && online,
        "id=" + id + " name=" + name + " online=" + (online ? "1" : "0"));

  bool secondOnline = true;
  if (hostObjects.size() == 2) json_get_bool(hostObjects[1], "online", &secondOnline);
  check("an offline host is not reported as online", !secondOnline);

  // A name with a quote in it would break a naive scan; the server escapes it, and the depth
  // walk must not be thrown off by the brace-free content either way.
  const auto oneHost = json_array_objects(
      R"({"hosts":[{"hostId":"x","hostName":"Sam's \"Old\" PC","online":true}]})", "hosts");
  std::string quotedName;
  if (oneHost.size() == 1) json_get_string(oneHost[0], "hostName", &quotedName);
  check("a quoted name does not split the object", oneHost.size() == 1,
        "count=" + std::to_string(oneHost.size()));

  check("an empty list is empty, not a parse failure",
        json_array_objects(R"({"hosts":[]})", "hosts").empty());
  check("a missing key yields nothing rather than garbage",
        json_array_objects(R"({"other":[{"a":1}]})", "hosts").empty());

  // Copied from an actual /api/connect reply, relay candidate included.
  const std::string connectJson =
      R"({"hostPublicIp":"211.218.222.1","hostPublicUdpPort":43000,"candidates":[)"
      R"({"ip":"192.168.20.50","port":43000,"kind":"private"},)"
      R"({"ip":"211.218.222.1","port":43000,"kind":"public"},)"
      R"({"ip":"223.130.132.180","port":43000,"kind":"relay"}],)"
      R"("punchToken":"feb4d3adb8827d3141d6d909ae231fc5"})";

  const auto candidateObjects = json_array_objects(connectJson, "candidates");
  check("every candidate is found", candidateObjects.size() == 3,
        "count=" + std::to_string(candidateObjects.size()));

  // The relay kind is newer than this parser's enum, and dropping it would silently remove the
  // only route that works on a network with no direct path at all.
  std::string relayIp;
  uint32_t relayPort = 0;
  std::string relayKind;
  if (candidateObjects.size() == 3) {
    json_get_string(candidateObjects[2], "ip", &relayIp);
    json_get_u32(candidateObjects[2], "port", &relayPort);
    json_get_string(candidateObjects[2], "kind", &relayKind);
  }
  CandidateKind parsed = CandidateKind::Private;
  const bool known = candidate_kind_from_name(relayKind, &parsed);
  check("the relay candidate is kept even though its kind is unknown here",
        relayIp == "223.130.132.180" && relayPort == 43000 && !known,
        "ip=" + relayIp + " kind=" + relayKind);

  // Order is the server's preference, and it matters: the client punches all of them but falls
  // back to the first when none answer, so a reordering would change what a silent network does.
  std::string firstKind;
  if (!candidateObjects.empty()) json_get_string(candidateObjects[0], "kind", &firstKind);
  check("the LAN candidate is still first", firstKind == "private", "first=" + firstKind);

  std::string token;
  json_get_string(connectJson, "punchToken", &token);
  check("the capability comes through whole", token.size() == 32, "len=" +
        std::to_string(token.size()));

  std::printf(gFailures == 0 ? "\ndirectory_session_client_test: PASS\n"
                             : "\ndirectory_session_client_test: %d FAILED\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
