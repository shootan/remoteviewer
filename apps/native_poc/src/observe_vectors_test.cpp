// Checks the C++ side of the observe-endpoint contract against the shared vectors.
//
// The point of reading apps/shared/observe_endpoint_vectors.txt rather than hard-coding the cases
// is that the same file is read by the Android client's Kotlin test and by the directory server's
// JS test. Three implementations, one set of expected answers -- and this particular rule is one
// all three had wrong in the same way, which is exactly the shape a shared file exists for: every
// suite passed while the product could not connect at all, because every suite made the same
// assumption the code did.
//
// The vectors path comes from CMake (REMOTE60_OBSERVE_VECTORS_PATH) and can be overridden by
// argv[1], so the test can be pointed at a scratch file while working on it.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "directory_observe.hpp"

#ifndef REMOTE60_OBSERVE_VECTORS_PATH
#define REMOTE60_OBSERVE_VECTORS_PATH "apps/shared/observe_endpoint_vectors.txt"
#endif

namespace {

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool ok, const std::string& detail) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

std::vector<std::string> split(const std::string& line, char sep) {
  std::vector<std::string> out;
  std::string field;
  std::istringstream is(line);
  while (std::getline(is, field, sep)) out.push_back(field);
  // getline drops a trailing empty field; the format ends with one on parse rows.
  if (!line.empty() && line.back() == sep) out.push_back("");
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string path = argc > 1 ? argv[1] : REMOTE60_OBSERVE_VECTORS_PATH;
  std::ifstream file(path);
  if (!file.is_open()) {
    std::cout << "FAIL  the shared vectors could not be opened  " << path << "\n";
    return 1;
  }

  using namespace remote60::native_poc::directory;

  int parsed = 0;
  int ports = 0;
  std::string line;
  int lineNumber = 0;
  while (std::getline(file, line)) {
    ++lineNumber;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;

    const std::vector<std::string> f = split(line, '|');
    const std::string where = path + ":" + std::to_string(lineNumber);

    if (f[0] == "parse") {
      if (f.size() < 6) {
        check("a parse row has six fields", false, where);
        continue;
      }
      ObserveEndpoint got;
      parse_observe_metadata(f[1], &got);
      const bool wantKnown = f[2] == "1";
      const uint16_t wantPort = static_cast<uint16_t>(std::stoul(f[3]));
      const bool wantRejected = f[5] == "1";
      const std::string detail = f[1] + " -> known=" + std::to_string(got.known) + " port=" +
                                 std::to_string(got.port) + " host='" + got.host + "' rejected=" +
                                 std::to_string(got.hostRejected);
      check("parse " + f[1],
            got.known == wantKnown && got.port == wantPort && got.host == f[4] &&
                got.hostRejected == wantRejected,
            detail);
      ++parsed;
    } else if (f[0] == "port") {
      if (f.size() < 6) {
        check("a port row has six fields", false, where);
        continue;
      }
      ObserveEndpoint advertised;
      advertised.known = f[1] == "1";
      advertised.port = static_cast<uint16_t>(std::stoul(f[2]));
      const uint16_t httpPort = static_cast<uint16_t>(std::stoul(f[3]));
      const bool secure = f[4] == "1";
      const uint16_t want = static_cast<uint16_t>(std::stoul(f[5]));
      const uint16_t got = observe_port_for(advertised, httpPort, secure);
      check("port known=" + f[1] + " advertised=" + f[2] + " http=" + f[3] + " secure=" + f[4],
            got == want, "got " + std::to_string(got) + " want " + f[5]);
      ++ports;
    } else {
      check("an unknown row kind", false, where + "  " + f[0]);
    }
  }

  // A file that stopped being read would otherwise pass with nothing in it, which is the failure
  // mode a shared-vector test has: green because it checked nothing.
  check("the vectors file had parse rows", parsed >= 15, std::to_string(parsed));
  check("the vectors file had port rows", ports >= 8, std::to_string(ports));

  std::cout << "\n" << gChecks << " checks, " << gFailures << " failed\n";
  return gFailures == 0 ? 0 : 1;
}
