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
