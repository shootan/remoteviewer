#include "update_readiness.hpp"

#include <cstdlib>
#include <sstream>
#include <string>

namespace remote60::native_poc::update {
namespace {

/** Reads "key=value" and hands back the value, or an empty string. */
std::string value_of(const std::string& line, const char* key) {
  const std::string prefix = std::string(key) + "=";
  if (line.rfind(prefix, 0) != 0) return {};
  return line.substr(prefix.size());
}

}  // namespace

const char* readiness_verdict_name(ReadinessVerdict v) {
  switch (v) {
    case ReadinessVerdict::Accept: return "accept";
    case ReadinessVerdict::Malformed: return "malformed";
    case ReadinessVerdict::WrongNonce: return "wrong-attempt";
    case ReadinessVerdict::WrongVersion: return "wrong-version";
    case ReadinessVerdict::WrongProcess: return "wrong-process";
    case ReadinessVerdict::Stale: return "stale";
    case ReadinessVerdict::NotAlive: return "not-alive";
  }
  return "unknown";
}

ReadinessVerdict readiness_judge(const ReadinessClaim& claim, const ReadinessExpectation& expect) {
  if (!claim.valid) return ReadinessVerdict::Malformed;

  // An empty expected nonce would make every claim match, including one left on disk by a previous
  // attempt. There is no configuration in which that is what the caller wanted.
  if (expect.nonce.empty() || claim.nonce != expect.nonce) return ReadinessVerdict::WrongNonce;

  if (!expect.version.empty() && claim.version != expect.version) {
    return ReadinessVerdict::WrongVersion;
  }

  // pid and creation time together. A pid on its own is reused, sometimes within seconds, and a
  // reused one carrying the right nonce is exactly the shape a stale file takes after a restart.
  if (expect.pid != 0 && claim.pid != expect.pid) return ReadinessVerdict::WrongProcess;
  if (expect.createTimeQw != 0 && claim.createTimeQw != expect.createTimeQw) {
    return ReadinessVerdict::WrongProcess;
  }

  // Freshness is judged forwards and backwards. A claim from the future is not this attempt's
  // either -- it means the clock moved or the file is not what we think it is.
  if (expect.maxAgeMs > 0) {
    if (claim.writtenAtMs > expect.nowMs) return ReadinessVerdict::Stale;
    if (expect.nowMs - claim.writtenAtMs > expect.maxAgeMs) return ReadinessVerdict::Stale;
  }

  // Asked now, not when the claim was written. A Host that reported readiness and then exited is
  // the case the gate exists to catch, and a file cannot retract itself.
  if (!expect.processAlive) return ReadinessVerdict::NotAlive;

  return ReadinessVerdict::Accept;
}

std::string serialize_readiness(const ReadinessClaim& claim) {
  std::ostringstream out;
  // The marker is last on purpose: a torn write leaves the file without it, and parse_readiness
  // refuses anything that does not end complete. Atomic replacement makes this unnecessary on the
  // happy path; it is here for the paths that are not happy.
  out << "nonce=" << claim.nonce << "\n"
      << "version=" << claim.version << "\n"
      << "pid=" << claim.pid << "\n"
      << "created=" << claim.createTimeQw << "\n"
      << "writtenMs=" << claim.writtenAtMs << "\n"
      << "end\n";
  return out.str();
}

ReadinessClaim parse_readiness(const std::string& text) {
  ReadinessClaim claim;
  // The marker has to be a COMPLETE line, terminator included. Relying on the loop below to see
  // "end" is not enough: std::getline hands back a final partial line that has no newline after
  // it, so a write cut one byte short of the end still looked finished. The test found that.
  const auto ends_with = [&text](const char* suffix) {
    const size_t n = std::char_traits<char>::length(suffix);
    return text.size() >= n && text.compare(text.size() - n, n, suffix) == 0;
  };
  if (!ends_with("end\n") && !ends_with("end\r\n")) return claim;

  std::istringstream in(text);
  std::string line;
  bool sawEnd = false;
  int fields = 0;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line == "end") { sawEnd = true; break; }
    if (!value_of(line, "nonce").empty()) { claim.nonce = value_of(line, "nonce"); ++fields; continue; }
    if (!value_of(line, "version").empty()) { claim.version = value_of(line, "version"); ++fields; continue; }
    if (!value_of(line, "pid").empty()) {
      claim.pid = static_cast<uint32_t>(std::strtoul(value_of(line, "pid").c_str(), nullptr, 10));
      ++fields;
      continue;
    }
    if (!value_of(line, "created").empty()) {
      claim.createTimeQw = std::strtoull(value_of(line, "created").c_str(), nullptr, 10);
      ++fields;
      continue;
    }
    if (!value_of(line, "writtenMs").empty()) {
      claim.writtenAtMs = std::strtoull(value_of(line, "writtenMs").c_str(), nullptr, 10);
      ++fields;
      continue;
    }
  }
  // Every field, and the marker. A claim missing one of them is not a claim with a default in it:
  // a zero pid or an empty nonce would each make the judge's comparisons vacuous.
  claim.valid = sawEnd && fields == 5 && !claim.nonce.empty() && !claim.version.empty() &&
                claim.pid != 0 && claim.createTimeQw != 0;
  return claim;
}

}  // namespace remote60::native_poc::update
