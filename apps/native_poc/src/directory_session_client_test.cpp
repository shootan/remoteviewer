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
  // The https half is asserted below now that the parser accepts https; the note that said it was
  // unreachable is gone with the rejection it described.
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

  // ------------------------------------------------- the https refusal, now that it is reachable
  //
  // This is the defect the observe work exists for: on an https directory the clients computed
  // 443 + 1 and sent their observation to 444. Nothing answers there, so the host never completed
  // a heartbeat and never appeared in anyone's list. Refusing with a reason is the difference
  // between a server that needs one setting and a product that does not work.
  //
  // Every case below returns before any socket, or fails at name resolution against a .invalid
  // name (RFC 2606, guaranteed not to resolve) -- so what is asserted is the decision, not a
  // network. The endpoint the code decided on is in that resolution error, which is how the port
  // it picked is visible at all.
  {
    DirectorySessionRequest base{};
    base.url = "https://directory.invalid";
    base.sessionToken = "t";
    base.hostId = "h";

    {
      DirectorySessionResult session{};
      std::string error;
      const bool opened = directory_session_open(base, &session, &error);
      check("https with no advertisement is refused, not dialled on 444", !opened, error);
      check("...and the reason points at the server's configuration",
            error.find("observations") != std::string::npos, error);
      check("...and it never got as far as a socket",
            error.find("resolve") == std::string::npos, error);
      check("...and 444 is nowhere in it", error.find("444") == std::string::npos, error);
    }

    // request.advertised is what the login response carried. A caller that fills it must get past
    // the refusal, and on the port the server named -- not on some default that happens to work.
    {
      DirectorySessionRequest request = base;
      request.advertised.known = true;
      request.advertised.port = 29181;
      DirectorySessionResult session{};
      std::string error;
      const bool opened = directory_session_open(request, &session, &error);
      check("an advertised port gets past the refusal", !opened && error.find("observations") == std::string::npos, error);
      check("...and it is the port that was advertised",
            error.find(":29181") != std::string::npos, error);
    }

    // The advertised host too, which is a separate field and was separately ignored.
    {
      DirectorySessionRequest request = base;
      request.advertised.known = true;
      request.advertised.port = 29181;
      request.advertised.host = "observe.invalid";
      DirectorySessionResult session{};
      std::string error;
      const bool opened = directory_session_open(request, &session, &error);
      check("the advertised host is the one dialled",
            !opened && error.find("observe.invalid:29181") != std::string::npos, error);
    }

    // A host the client refused (the server does not validate this field) falls back to the
    // directory's own host rather than to nothing.
    {
      DirectorySessionRequest request = base;
      request.advertised.known = true;
      request.advertised.port = 29181;
      request.advertised.hostRejected = true;  // parse_observe_metadata left the host empty
      DirectorySessionResult session{};
      std::string error;
      const bool opened = directory_session_open(request, &session, &error);
      check("a rejected observe host falls back to the directory host",
            !opened && error.find("directory.invalid:29181") != std::string::npos, error);
    }

    // The pin is the operator's decision and outranks both the advertisement and the default.
    {
      DirectorySessionRequest request = base;
      request.directoryUdpPort = 40000;
      DirectorySessionResult session{};
      std::string error;
      const bool opened = directory_session_open(request, &session, &error);
      check("a pinned port carries https past the refusal",
            !opened && error.find(":40000") != std::string::npos, error);
    }
    {
      DirectorySessionRequest request = base;
      request.directoryUdpPort = 40000;
      request.advertised.known = true;
      request.advertised.port = 29181;
      DirectorySessionResult session{};
      std::string error;
      const bool opened = directory_session_open(request, &session, &error);
      check("the pin outranks the advertisement",
            !opened && error.find(":40000") != std::string::npos, error);
      check("...and the advertised port is not the one used",
            error.find(":29181") == std::string::npos, error);
    }

    // http is untouched by all of this: the documented default is still one above the http port.
    {
      DirectorySessionRequest request{};
      request.url = "http://directory.invalid:29180";
      request.sessionToken = "t";
      request.hostId = "h";
      DirectorySessionResult session{};
      std::string error;
      const bool opened = directory_session_open(request, &session, &error);
      check("http still derives the port as http + 1",
            !opened && error.find("directory.invalid:29181") != std::string::npos, error);
    }
  }

  // ------------------------------------------------------- two spellings of the same server
  //
  // Whether a cached host token still belongs where it was issued was a string comparison against
  // the url as typed, so a trailing slash cost an unattended PC its credentials and a fresh
  // registration. Nothing unsafe -- but the fix must not go the other way either: http and https
  // are different origins and a token issued to one must not be sent to the other.
  {
    using directory::directory_origin_key;
    const std::string base = directory_origin_key("http://rem.example:8080");
    check("a trailing slash is the same server",
          directory_origin_key("http://rem.example:8080/") == base, base);
    check("a path is the same server",
          directory_origin_key("http://rem.example:8080/api") == base, base);
    check("surrounding space is the same server",
          directory_origin_key("  http://rem.example:8080  ") == base, base);
    check("the host case is the same server",
          directory_origin_key("http://REM.example:8080") == base, base);
    check("the scheme case is the same server",
          directory_origin_key("HTTP://rem.example:8080") == base, base);
    check("the written-out default port is the same server",
          directory_origin_key("http://rem.example:80") == directory_origin_key("http://rem.example"),
          directory_origin_key("http://rem.example"));
    check("and on https it is 443",
          directory_origin_key("https://rem.example:443") == directory_origin_key("https://rem.example"),
          directory_origin_key("https://rem.example"));

    check("a different port is a different server",
          directory_origin_key("http://rem.example:8081") != base);
    check("a different host is a different server",
          directory_origin_key("http://other.example:8080") != base);
    check("https is not http",
          directory_origin_key("https://rem.example:8080") != base,
          directory_origin_key("https://rem.example:8080"));
    check("an unusable url still equals itself",
          directory_origin_key("ftp://rem.example") == directory_origin_key("ftp://rem.example"));
    check("...and is not the same as a real one",
          directory_origin_key("ftp://rem.example") != base);
  }

  // ------------------------------------------------- where updates come from, with one address
  //
  // The manifest url used to come only from an environment variable, so a machine configured with
  // nothing but a server address could never check for updates: the address was there, the route
  // was there, and nothing joined them. It is derived from the directory origin now -- which
  // means the derivation must not invent a server, and must not move a deployment that set the
  // variable.
  {
    using directory::directory_update_manifest_url;
    using directory::update_manifest_url_for;

    check("an https directory names its own update route",
          directory_update_manifest_url("https://rem.example", "windows") ==
              "https://rem.example:443/api/update/manifest?platform=windows",
          directory_update_manifest_url("https://rem.example", "windows"));
    check("...on whatever port it is really on",
          directory_update_manifest_url("https://rem.example:8443", "windows") ==
              "https://rem.example:8443/api/update/manifest?platform=windows",
          directory_update_manifest_url("https://rem.example:8443", "windows"));
    check("...and the platform is the one asked for",
          directory_update_manifest_url("https://rem.example", "android") ==
              "https://rem.example:443/api/update/manifest?platform=android",
          directory_update_manifest_url("https://rem.example", "android"));

    // Built from the origin, so the spellings that mean one server produce one url. A string
    // paste would have carried the path and the trailing slash into the middle of this.
    check("a trailing slash does not end up inside the url",
          directory_update_manifest_url("https://rem.example/", "windows") ==
              directory_update_manifest_url("https://rem.example", "windows"),
          directory_update_manifest_url("https://rem.example/", "windows"));
    check("neither does a path",
          directory_update_manifest_url("https://REM.example/api/", "windows") ==
              directory_update_manifest_url("https://rem.example", "windows"),
          directory_update_manifest_url("https://REM.example/api/", "windows"));

    // The two refusals. Guessing https would invent a server nobody configured; following the
    // directory down to http would fetch an administrator-privileged artifact over a hop anyone
    // can rewrite.
    check("an http directory derives nothing",
          directory_update_manifest_url("http://rem.example", "windows").empty(),
          directory_update_manifest_url("http://rem.example", "windows"));
    check("...and is not quietly upgraded to https",
          directory_update_manifest_url("http://rem.example", "windows").find("https") ==
              std::string::npos);
    check("no directory means nothing to derive from",
          directory_update_manifest_url("", "windows").empty());

    // The override keeps working, including for the http deployment that has to use one.
    check("an explicit override wins over the derivation",
          update_manifest_url_for("https://updates.example/m?platform=windows",
                                  "https://rem.example", "windows") ==
              "https://updates.example/m?platform=windows");
    check("...and is the only answer an http directory has",
          update_manifest_url_for("https://updates.example/m", "http://rem.example", "windows") ==
              "https://updates.example/m");
    check("without either, there is no url and that is not an error",
          update_manifest_url_for("", "http://rem.example", "windows").empty());
    check("with only an address, there is one",
          update_manifest_url_for("", "https://rem.example", "windows") ==
              "https://rem.example:443/api/update/manifest?platform=windows");
  }

  // --------------------------------------------------------------- what the parser now accepts
  //
  // https used to be refused outright, which is why the 444 defect could not even be reproduced
  // through this path. The http branch was matched case-sensitively while the https branch was
  // not, inside the same function, so `HTTP://host` was "unsupported url scheme" -- a correctly
  // typed url refused for its capitals.
  {
    using directory::parse_directory_url;
    std::string host;
    uint16_t port = 0;
    bool secure = false;
    std::string error;

    check("https parses", parse_directory_url("https://rem.example", &host, &port, &error, &secure),
          error);
    check("...on 443 by default", port == 443, std::to_string(port));
    check("...and says so", secure);

    check("an explicit https port wins",
          parse_directory_url("https://rem.example:8443", &host, &port, &error, &secure) &&
              port == 8443 && secure,
          std::to_string(port));

    check("http parses", parse_directory_url("http://rem.example", &host, &port, &error, &secure),
          error);
    check("...on 80 by default", port == 80, std::to_string(port));
    check("...and says it is not secure", !secure);

    check("HTTP:// is the same scheme in capitals",
          parse_directory_url("HTTP://rem.example", &host, &port, &error, &secure), error);
    check("...with the same host", host == "rem.example", host);
    check("...the same port", port == 80, std::to_string(port));
    check("...and still not secure", !secure);

    check("HTTPS:// likewise",
          parse_directory_url("HTTPS://rem.example", &host, &port, &error, &secure) && secure &&
              port == 443,
          std::to_string(port));

    check("a scheme nobody serves is still refused",
          !parse_directory_url("ftp://rem.example", &host, &port, &error, &secure), error);
    // The out parameter is optional; older call sites pass four arguments and must still compile
    // and behave.
    check("the scheme is optional to ask for",
          parse_directory_url("http://rem.example:29180", &host, &port, &error) && port == 29180,
          error);
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
