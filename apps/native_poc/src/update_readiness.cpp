#include "update_readiness.hpp"

#include <windows.h>
#include <bcrypt.h>

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

namespace {

std::wstring dir_of(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::string read_file(const std::wstring& path) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return {};
  std::string out;
  char buffer[4096];
  DWORD read = 0;
  while (ReadFile(h, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
    out.append(buffer, read);
    if (out.size() > 64 * 1024) break;  // a claim is a few hundred bytes; this is not a log
  }
  CloseHandle(h);
  return out;
}

bool write_file(const std::wstring& path, const std::string& text) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD wrote = 0;
  const bool ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &wrote, nullptr) &&
                  wrote == text.size() && FlushFileBuffers(h);
  CloseHandle(h);
  return ok;
}

}  // namespace

std::wstring readiness_dir_from_log(const std::wstring& healthLogPath) {
  return dir_of(healthLogPath);
}

std::wstring attempt_ticket_path(const std::wstring& healthLogPath) {
  const std::wstring dir = dir_of(healthLogPath);
  return dir.empty() ? std::wstring() : dir + L"\\update_attempt";
}

std::wstring readiness_claim_path(const std::wstring& healthLogPath) {
  const std::wstring dir = dir_of(healthLogPath);
  return dir.empty() ? std::wstring() : dir + L"\\host_readiness";
}

bool write_attempt_ticket(const std::wstring& path, const AttemptTicket& ticket) {
  if (path.empty() || ticket.nonce.empty()) return false;
  return write_file(path, "nonce=" + ticket.nonce + "\nversion=" + ticket.version + "\nend\n");
}

AttemptTicket read_attempt_ticket(const std::wstring& path) {
  AttemptTicket ticket;
  if (path.empty()) return ticket;
  const std::string text = read_file(path);
  if (text.size() < 4 || text.compare(text.size() - 4, 4, "end\n") != 0) return ticket;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("nonce=", 0) == 0) ticket.nonce = line.substr(6);
    else if (line.rfind("version=", 0) == 0) ticket.version = line.substr(8);
  }
  ticket.valid = !ticket.nonce.empty();
  return ticket;
}

bool write_claim_atomic(const std::wstring& path, const ReadinessClaim& claim) {
  if (path.empty()) return false;
  // Same directory, therefore the same volume, so the replace is a rename and not a copy. A reader
  // sees the old file or the new one and never a half-written one.
  const std::wstring temp = path + L".tmp";
  if (!write_file(temp, serialize_readiness(claim))) return false;
  if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    DeleteFileW(temp.c_str());
    return false;
  }
  return true;
}

ReadinessClaim read_claim(const std::wstring& path) {
  if (path.empty()) return ReadinessClaim{};
  return parse_readiness(read_file(path));
}

void remove_claim(const std::wstring& path) {
  if (!path.empty()) DeleteFileW(path.c_str());
}

std::string mint_attempt_nonce() {
  unsigned char bytes[8]{};
  if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
    // Not a reason to proceed with something guessable: an empty nonce is refused by the judge,
    // which fails the attempt closed rather than accepting whatever is on disk.
    return {};
  }
  static const char* kHex = "0123456789abcdef";
  std::string out;
  for (unsigned char b : bytes) {
    out.push_back(kHex[b >> 4]);
    out.push_back(kHex[b & 0xF]);
  }
  return out;
}

}  // namespace remote60::native_poc::update
