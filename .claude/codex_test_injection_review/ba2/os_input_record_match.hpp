// The record-only host's evidence, matched to "this session's this input" (2026-10-28, Codex T1, verifier r5).
//
// The record-only host (os_input_effects.hpp) tags each recorded effect with the dispatch that caused it:
//   [os-input-record] ctx what=input|text|release seq=N epoch=E frag=K api=...
// and its dispatch logs each input it handled:
//   [native-video-host][input] injected seq=N ...        (what=input)
//   [native-video-host][input-text] injected seq=N ...   (what=text)
// e2e_match_dispatches() walks the dispatches in order from a cursor and checks, for each expected input:
//   * the next dispatch of that kind exists;
//   * the effects tagged with its (what, seq) all carry the session's epoch, with fragments 0..n-1 in order;
//   * the REQUIRED effects appear among them in the expected order;
//   * anything else among them is one of the ALLOWED extras (the IME preparation: AttachThreadInput /
//     ImmSetOpenStatus / ImmSetConversionStatus -- whether they happen depends on the real foreground's IME, a
//     read) -- any other effect fails.
// Effects tagged "ctx none" (the display wake, nothing dispatched) belong to no input and never match one.
// Pure: it reads text. Its negative controls are in os_input_record_match_test.cpp.
#ifndef REMOTE60_NATIVE_POC_OS_INPUT_RECORD_MATCH_HPP
#define REMOTE60_NATIVE_POC_OS_INPUT_RECORD_MATCH_HPP

#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace remote60::native_poc::e2e {

struct RecordedEffect {
  std::string what;  // "" for ctx none
  uint64_t seq = 0;
  uint64_t epoch = 0;
  int frag = -1;
  std::string api;   // from "api=" to the line end
};

struct Dispatched {
  std::string what;  // input / text
  uint64_t seq = 0;
};

struct ExpectedInput {
  std::string what;                   // input / text
  std::vector<std::string> required;  // substrings of the api text, in this order
};

inline uint64_t field_u64(const std::string& line, const char* key) {
  const size_t at = line.find(key);
  if (at == std::string::npos) return 0;
  return std::strtoull(line.c_str() + at + std::char_traits<char>::length(key), nullptr, 10);
}

inline std::vector<RecordedEffect> e2e_parse_records(const std::string& log) {
  std::vector<RecordedEffect> out;
  std::istringstream in(log);
  std::string line;
  while (std::getline(in, line)) {
    const size_t at = line.find("[os-input-record] ctx ");
    if (at == std::string::npos) continue;
    RecordedEffect r;
    const std::string rest = line.substr(at);
    const size_t api = rest.find("api=");
    if (api == std::string::npos) continue;
    r.api = rest.substr(api);
    while (!r.api.empty() && (r.api.back() == '\r' || r.api.back() == '\n')) r.api.pop_back();
    if (rest.find("ctx none ") == std::string::npos) {
      const size_t w = rest.find("what=");
      if (w == std::string::npos) continue;
      r.what = rest.substr(w + 5, rest.find(' ', w) - (w + 5));
      r.seq = field_u64(rest, "seq=");
      r.epoch = field_u64(rest, "epoch=");
      r.frag = static_cast<int>(field_u64(rest, "frag="));
    }
    out.push_back(r);
  }
  return out;
}

inline std::vector<Dispatched> e2e_parse_dispatches(const std::string& log) {
  std::vector<Dispatched> out;
  std::istringstream in(log);
  std::string line;
  while (std::getline(in, line)) {
    if (line.find("[native-video-host][input] injected seq=") != std::string::npos) {
      out.push_back({"input", field_u64(line, "injected seq=")});
    } else if (line.find("[native-video-host][input-text] injected seq=") != std::string::npos) {
      out.push_back({"text", field_u64(line, "injected seq=")});
    }
  }
  return out;
}

inline bool e2e_is_allowed_extra(const std::string& api) {
  return api.rfind("api=AttachThreadInput", 0) == 0 || api.rfind("api=ImmSetOpenStatus", 0) == 0 ||
         api.rfind("api=ImmSetConversionStatus", 0) == 0;
}

// The expected inputs, from the dispatch at index `fromDispatch` on, in this session (`epoch`).
inline bool e2e_match_dispatches(const std::string& log, size_t fromDispatch, uint64_t epoch,
                                 const std::vector<ExpectedInput>& expected, std::string* why) {
  const auto dispatches = e2e_parse_dispatches(log);
  const auto records = e2e_parse_records(log);
  size_t d = fromDispatch;
  for (size_t i = 0; i < expected.size(); ++i) {
    const ExpectedInput& want = expected[i];
    while (d < dispatches.size() && dispatches[d].what != want.what) ++d;
    if (d >= dispatches.size()) {
      if (why) *why = "expected input " + std::to_string(i) + " (" + want.what + ") was never dispatched";
      return false;
    }
    const Dispatched& got = dispatches[d++];
    std::vector<RecordedEffect> mine;
    for (const auto& r : records) {
      if (r.what == got.what && r.seq == got.seq) mine.push_back(r);
    }
    for (size_t k = 0; k < mine.size(); ++k) {
      if (mine[k].epoch != epoch) {
        if (why) *why = got.what + " seq=" + std::to_string(got.seq) + ": an effect of another session (epoch " +
                        std::to_string(mine[k].epoch) + ", this session " + std::to_string(epoch) + ")";
        return false;
      }
      if (mine[k].frag != static_cast<int>(k)) {
        if (why) *why = got.what + " seq=" + std::to_string(got.seq) + ": fragments out of order";
        return false;
      }
    }
    size_t r = 0;
    for (const auto& need : want.required) {
      while (r < mine.size() && mine[r].api.find(need) == std::string::npos) {
        if (!e2e_is_allowed_extra(mine[r].api)) {
          if (why) *why = got.what + " seq=" + std::to_string(got.seq) + ": unexpected effect before '" + need + "': " + mine[r].api;
          return false;
        }
        ++r;
      }
      if (r >= mine.size()) {
        if (why) *why = got.what + " seq=" + std::to_string(got.seq) + ": missing '" + need + "' (in order)";
        return false;
      }
      ++r;
    }
    for (; r < mine.size(); ++r) {
      if (!e2e_is_allowed_extra(mine[r].api)) {
        if (why) *why = got.what + " seq=" + std::to_string(got.seq) + ": unexpected trailing effect: " + mine[r].api;
        return false;
      }
    }
  }
  return true;
}

}  // namespace remote60::native_poc::e2e

#endif
