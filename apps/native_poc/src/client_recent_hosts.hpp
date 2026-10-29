#pragma once

// ka-pc-impl r1 (KA ③, proposal_ka_pc_r2.md §5, Codex §1): the PCs this device used last, per account and directory.
//
// The logical key of a PC is directory origin + stable account id + hostId -- never a token, an address or a display
// name. The order changes only on a user's successful connect or real reuse (Touch); a standby being prepared, its
// Ping or a list refresh never move it. Up to kKeep entries per (origin, account) are kept; older ones fall off.
//
// The file is "recent_hosts.v1" beside the shell's settings, written whole (tmp, then MoveFileExW REPLACE_EXISTING):
// a version line, then one record per line: seq \t origin \t account \t hostId, each field %-escaped ('%', '\t',
// '\n', '\r'), at most kMaxField bytes. `seq` is a monotonically increasing number -- the order is by it, never by
// a clock or a locale, so two touches in the same second cannot swap. A line that does not parse is dropped (and
// the file rewritten without it on the next Touch); an unreadable file is an empty history, not an error.

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace remote60::native_poc::shell {

constexpr size_t kRecentKeep = 3;
constexpr size_t kRecentMaxField = 512;
constexpr char kRecentHeader[] = "gnlink-recent-hosts v1";

struct RecentHost {
  uint64_t seq = 0;
  std::string origin;
  std::string account;
  std::string hostId;
};

inline std::string recent_escape(const std::string& v) {
  std::string out;
  for (char c : v) {
    if (c == '%') out += "%25";
    else if (c == '\t') out += "%09";
    else if (c == '\n') out += "%0A";
    else if (c == '\r') out += "%0D";
    else out += c;
  }
  return out;
}

inline bool recent_unescape(const std::string& v, std::string* out) {
  out->clear();
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] != '%') { out->push_back(v[i]); continue; }
    if (i + 2 >= v.size()) return false;
    const std::string h = v.substr(i + 1, 2);
    if (h == "25") out->push_back('%');
    else if (h == "09") out->push_back('\t');
    else if (h == "0A") out->push_back('\n');
    else if (h == "0D") out->push_back('\r');
    else return false;
    i += 2;
  }
  return out->size() <= kRecentMaxField;
}

inline std::wstring recent_wide(const std::string& utf8) {
  if (utf8.empty()) return std::wstring();
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &w[0], n);
  return w;
}

// Parses the file's text; records that do not parse are skipped.
inline std::vector<RecentHost> recent_parse(const std::string& text) {
  std::vector<RecentHost> out;
  std::istringstream in(text);
  std::string line;
  bool header = false;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!header) {
      if (line != kRecentHeader) return {};
      header = true;
      continue;
    }
    std::vector<std::string> f;
    size_t pos = 0;
    for (;;) {
      const size_t tab = line.find('\t', pos);
      f.push_back(line.substr(pos, tab == std::string::npos ? std::string::npos : tab - pos));
      if (tab == std::string::npos) break;
      pos = tab + 1;
    }
    if (f.size() != 4 || f[0].empty() || f[0].size() > 19) continue;
    RecentHost r;
    bool digits = true;
    for (char c : f[0]) digits = digits && c >= '0' && c <= '9';
    if (!digits) continue;
    r.seq = std::stoull(f[0]);
    if (!recent_unescape(f[1], &r.origin) || !recent_unescape(f[2], &r.account) || !recent_unescape(f[3], &r.hostId)) continue;
    if (r.origin.empty() || r.account.empty() || r.hostId.empty()) continue;
    out.push_back(std::move(r));
  }
  return out;
}

inline std::string recent_format(const std::vector<RecentHost>& all) {
  std::string out = std::string(kRecentHeader) + "\n";
  for (const auto& r : all) {
    out += std::to_string(r.seq) + "\t" + recent_escape(r.origin) + "\t" + recent_escape(r.account) + "\t" +
           recent_escape(r.hostId) + "\n";
  }
  return out;
}

inline std::vector<RecentHost> recent_load(const std::wstring& path) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return {};
  std::string text;
  char buf[4096];
  DWORD got = 0;
  while (ReadFile(h, buf, sizeof(buf), &got, nullptr) && got > 0 && text.size() < (1u << 20)) text.append(buf, got);
  CloseHandle(h);
  return recent_parse(text);
}

inline bool recent_save(const std::wstring& path, const std::vector<RecentHost>& all) {
  const std::string text = recent_format(all);
  const std::wstring tmp = path + L".tmp";
  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD put = 0;
  const bool wrote = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &put, nullptr) && put == text.size() &&
                     FlushFileBuffers(h);
  CloseHandle(h);
  if (!wrote) return false;
  return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

// The account's recent PCs on this directory, most recent first, at most kRecentKeep.
inline std::vector<std::string> recent_for(const std::vector<RecentHost>& all, const std::string& origin,
                                           const std::string& account) {
  std::vector<RecentHost> mine;
  for (const auto& r : all)
    if (r.origin == origin && r.account == account) mine.push_back(r);
  std::sort(mine.begin(), mine.end(), [](const RecentHost& a, const RecentHost& b) { return a.seq > b.seq; });
  std::vector<std::string> out;
  for (const auto& r : mine) {
    if (out.size() >= kRecentKeep) break;
    out.push_back(r.hostId);
  }
  return out;
}

// A successful connect / real reuse of hostId: it becomes the newest; the account keeps its kRecentKeep newest.
inline std::vector<RecentHost> recent_touch(std::vector<RecentHost> all, const std::string& origin,
                                            const std::string& account, const std::string& hostId) {
  uint64_t top = 0;
  for (const auto& r : all) top = (std::max)(top, r.seq);
  all.erase(std::remove_if(all.begin(), all.end(),
                           [&](const RecentHost& r) { return r.origin == origin && r.account == account && r.hostId == hostId; }),
            all.end());
  all.push_back(RecentHost{top + 1, origin, account, hostId});
  // trim this account's list to kRecentKeep (others untouched)
  std::vector<RecentHost> mine, rest;
  for (auto& r : all) (r.origin == origin && r.account == account ? mine : rest).push_back(r);
  std::sort(mine.begin(), mine.end(), [](const RecentHost& a, const RecentHost& b) { return a.seq > b.seq; });
  if (mine.size() > kRecentKeep) mine.resize(kRecentKeep);
  rest.insert(rest.end(), mine.begin(), mine.end());
  return rest;
}

}  // namespace remote60::native_poc::shell
