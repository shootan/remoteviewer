// Covers the option that decides which UDP port the host is reachable on. The interesting cases
// are the malformed ones: this parser sits in front of a bind, and a silent misparse would put
// the host on a port nobody dialled while every log line still looked healthy.

#include "bind_port_candidates.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using remote60::native_poc::parse_bind_port_candidates;

namespace {

int gFailures = 0;

std::string show(const std::vector<uint16_t>& ports) {
  std::string out = "[";
  for (size_t i = 0; i < ports.size(); ++i) {
    if (i) out += ",";
    out += std::to_string(ports[i]);
  }
  return out + "]";
}

void expect_ports(const std::string& input, const std::vector<uint16_t>& want) {
  const std::vector<uint16_t> got = parse_bind_port_candidates(input);
  if (got != want) {
    std::printf("  FAIL \"%s\" -> %s, wanted %s\n", input.c_str(), show(got).c_str(),
                show(want).c_str());
    ++gFailures;
  } else {
    std::printf("  ok   \"%s\" -> %s\n", input.c_str(), show(got).c_str());
  }
}

void TestSinglePortStillWorks() {
  std::printf("a bare port keeps behaving as it always did\n");
  expect_ports("43000", {43000});
}

void TestOrderedListIsPreserved() {
  std::printf("the list is a preference order, so it must not be sorted or reordered\n");
  expect_ports("443,3478,43000", {443, 3478, 43000});
  expect_ports("43000,443", {43000, 443});
}

void TestWhitespaceTolerated() {
  std::printf("a hand-edited list with spaces behaves as written\n");
  expect_ports("443, 3478 , 43000", {443, 3478, 43000});
}

// A duplicate would make the host try the same bind twice and report the same failure twice,
// which reads like two different ports being unavailable.
void TestDuplicatesCollapse() {
  std::printf("a repeated port is tried once\n");
  expect_ports("443,443,3478", {443, 3478});
}

// The whole point of dropping rather than failing: one typo must not leave the host with no
// port at all, because the fallback for an empty list is the default, not a dead socket.
void TestBadEntriesAreDroppedNotFatal() {
  std::printf("a bad entry is skipped and the good ones survive\n");
  expect_ports("443,notaport,3478", {443, 3478});
  expect_ports("443x,3478", {3478});
  expect_ports("0,443", {443});
  expect_ports("65536,443", {443});
  expect_ports("99999999999999999999,443", {443});
  expect_ports("-1,443", {443});
}

void TestEmptyMeansUseTheDefault() {
  std::printf("nothing usable yields an empty list, which the caller reads as the default\n");
  expect_ports("", {});
  expect_ports(",", {});
  expect_ports("   ", {});
  expect_ports("garbage", {});
}

void TestTrailingAndLeadingCommas() {
  std::printf("stray commas do not invent or swallow entries\n");
  expect_ports(",443,", {443});
  expect_ports("443,,3478", {443, 3478});
}

void TestBoundaryPortsAccepted() {
  std::printf("the ends of the valid range are usable\n");
  expect_ports("1,65535", {1, 65535});
}

}  // namespace

int main() {
  TestSinglePortStillWorks();
  TestOrderedListIsPreserved();
  TestWhitespaceTolerated();
  TestDuplicatesCollapse();
  TestBadEntriesAreDroppedNotFatal();
  TestEmptyMeansUseTheDefault();
  TestTrailingAndLeadingCommas();
  TestBoundaryPortsAccepted();

  if (gFailures != 0) {
    std::printf("bind_port_candidates_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("bind_port_candidates_test: PASS\n");
  return 0;
}
