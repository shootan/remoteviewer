// Pins the rules that decide which addresses a client will try. The measured failure behind this
// is that one published address cannot serve two networks with opposite constraints: with the
// host on 443 a phone on mobile data could not reach it at all, and with the host on 43000 a
// company Wi-Fi client cannot. Both have to be offered.

#include "connect_candidates.hpp"

#include <cstdio>
#include <string>

using remote60::native_poc::build_connect_candidates;
using remote60::native_poc::candidate_kind_name;
using remote60::native_poc::CandidateKind;
using remote60::native_poc::ConnectCandidate;
using remote60::native_poc::is_usable_local_ipv4;

namespace {

int gFailures = 0;

void show(const std::vector<ConnectCandidate>& list) {
  for (const auto& c : list) {
    std::printf("       %-16s:%-6u %s\n", c.ip.c_str(), c.port, candidate_kind_name(c.kind));
  }
}

void fail(const std::string& what, const std::vector<ConnectCandidate>& got) {
  std::printf("  FAIL %s\n", what.c_str());
  show(got);
  ++gFailures;
}

// The configuration measured on this machine: public 175.209.236.194:43000, a second listener on
// 3478, and a LAN address of 192.168.0.76.
void TestMeasuredHomeSetup() {
  std::printf("the measured setup offers LAN, public, and the alternate port\n");
  const auto list = build_connect_candidates("175.209.236.194", 43000, 3478, {"192.168.0.76"},
                                             43000);
  if (list.size() != 3) { fail("expected three candidates", list); return; }
  if (list[0].kind != CandidateKind::Private || list[0].ip != "192.168.0.76") {
    fail("the LAN address must come first -- it avoids the router entirely", list);
    return;
  }
  if (list[1].kind != CandidateKind::Public || list[1].port != 43000) {
    fail("the observed public address must be second", list);
    return;
  }
  if (list[2].kind != CandidateKind::PublicAlt || list[2].port != 3478) {
    fail("the alternate port must be offered for networks that filter the first", list);
    return;
  }
  std::printf("  ok\n");
  show(list);
}

// If the alternate port equals the observed one there is nothing extra to say, and repeating it
// would just make the client punch the same address twice.
void TestAlternatePortNotDuplicated() {
  std::printf("an alternate port equal to the public one is not published twice\n");
  const auto list = build_connect_candidates("1.2.3.4", 43000, 43000, {}, 43000);
  if (list.size() != 1 || list[0].port != 43000) {
    fail("expected exactly one candidate", list);
    return;
  }
  std::printf("  ok\n");
}

// Hyper-V, WSL and emulators each add an adapter. Without a cap, connecting would punch a dozen
// addresses that can never reach anything.
void TestVirtualAdapterFloodIsCapped() {
  std::printf("a machine full of virtual adapters does not turn connect into a broadcast\n");
  const std::vector<std::string> many = {"192.168.0.76", "172.30.80.1", "172.27.32.1",
                                         "172.20.1.1",   "10.0.0.5",    "10.1.1.1",
                                         "192.168.56.1"};
  const auto list = build_connect_candidates("1.2.3.4", 43000, 3478, many, 43000, 4);
  if (list.size() != 4) { fail("expected the cap to apply", list); return; }
  // Both public candidates have to survive; they are the ones that work from outside.
  int publicCount = 0;
  for (const auto& c : list) {
    if (c.kind != CandidateKind::Private) ++publicCount;
  }
  if (publicCount != 2) {
    fail("public candidates must survive the cap -- they work from anywhere", list);
    return;
  }
  if (list[0].kind != CandidateKind::Private) {
    fail("preference order must survive the cap", list);
    return;
  }
  std::printf("  ok  kept %zu with both public candidates intact\n", list.size());
  show(list);
}

void TestUnreachableLocalAddressesAreRejected() {
  std::printf("addresses that can never reach a peer are not candidates\n");
  struct Case { const char* ip; bool usable; };
  const Case cases[] = {
      {"192.168.0.76", true},   {"10.0.0.1", true},      {"172.30.80.1", true},
      {"127.0.0.1", false},     {"127.5.5.5", false},    {"169.254.10.2", false},
      {"0.0.0.0", false},       {"", false},
  };
  for (const Case& c : cases) {
    if (is_usable_local_ipv4(c.ip) != c.usable) {
      std::printf("  FAIL %s should be %s\n", c.ip, c.usable ? "usable" : "rejected");
      ++gFailures;
      return;
    }
  }
  // 169.254 in particular means DHCP failed; punching it wastes the whole connect budget.
  const auto list = build_connect_candidates("1.2.3.4", 43000, 0, {"169.254.1.1", "127.0.0.1"},
                                             43000);
  if (list.size() != 1 || list[0].kind != CandidateKind::Public) {
    fail("only the public candidate should survive", list);
    return;
  }
  std::printf("  ok\n");
}

void TestNoPublicAddressStillOffersLan() {
  std::printf("a host the directory could not observe is still reachable on the LAN\n");
  const auto list = build_connect_candidates("", 0, 0, {"192.168.0.76"}, 43000);
  if (list.size() != 1 || list[0].kind != CandidateKind::Private) {
    fail("expected the LAN candidate alone", list);
    return;
  }
  std::printf("  ok\n");
}

void TestDuplicateLocalAddressesCollapse() {
  std::printf("the same address reported twice is punched once\n");
  const auto list = build_connect_candidates("1.2.3.4", 43000, 0,
                                             {"192.168.0.76", "192.168.0.76"}, 43000);
  if (list.size() != 2) { fail("expected two candidates", list); return; }
  std::printf("  ok\n");
}

}  // namespace

int main() {
  TestMeasuredHomeSetup();
  TestAlternatePortNotDuplicated();
  TestVirtualAdapterFloodIsCapped();
  TestUnreachableLocalAddressesAreRejected();
  TestNoPublicAddressStillOffersLan();
  TestDuplicateLocalAddressesCollapse();

  if (gFailures != 0) {
    std::printf("connect_candidates_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("connect_candidates_test: PASS\n");
  return 0;
}
