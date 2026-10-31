#include "payload_name.hpp"

#include <algorithm>
#include <set>

namespace remote60::native_poc::update {
namespace {

// Long enough for anything the product ships, short enough to stay clear of MAX_PATH once the
// install directory is prepended.
constexpr size_t kMaxPayloadNameLength = 120;

bool is_separator(wchar_t c) { return c == L'\\' || c == L'/'; }

std::wstring to_lower(std::wstring s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](wchar_t c) { return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + 32) : c; });
  return s;
}

/**
 * Names Windows resolves to devices rather than files, with or without an extension.
 *
 * Opening one of these does not create a file at all -- it opens the console, a printer port, or
 * the bit bucket -- so a payload named CON would make a swap silently succeed while writing
 * nothing.
 */
bool is_reserved_device(const std::wstring& component) {
  // The stem, before the first dot: "NUL.txt" is still NUL.
  const size_t dot = component.find(L'.');
  const std::wstring stem = to_lower(dot == std::wstring::npos ? component : component.substr(0, dot));
  static const std::set<std::wstring> kReserved = {
      L"con", L"prn", L"aux", L"nul",
      L"com1", L"com2", L"com3", L"com4", L"com5", L"com6", L"com7", L"com8", L"com9",
      L"lpt1", L"lpt2", L"lpt3", L"lpt4", L"lpt5", L"lpt6", L"lpt7", L"lpt8", L"lpt9",
  };
  return kReserved.count(stem) != 0;
}

PayloadNameVerdict check_normalised(const std::wstring& name) {
  if (name.empty()) return PayloadNameVerdict::Empty;
  if (name.size() > kMaxPayloadNameLength) return PayloadNameVerdict::TooLong;

  if (is_separator(name[0])) return PayloadNameVerdict::Absolute;

  // A colon is either a drive specifier or an alternate data stream. Neither is a payload name.
  const size_t colon = name.find(L':');
  if (colon != std::wstring::npos) {
    // "C:\..." is absolute; "C:file" resolves against that drive's own current directory, which
    // is a different surprise but no less of one.
    if (colon == 1 && name.size() > 2 && is_separator(name[2])) return PayloadNameVerdict::Absolute;
    if (colon == 1) return PayloadNameVerdict::DriveRelative;
    return PayloadNameVerdict::AlternateStream;
  }

  for (wchar_t c : name) {
    if (c < 32) return PayloadNameVerdict::ControlCharacter;
    if (c == L'*' || c == L'?') return PayloadNameVerdict::Wildcard;
    if (c == L'"' || c == L'<' || c == L'>' || c == L'|') return PayloadNameVerdict::ControlCharacter;
  }

  // Split on EITHER separator. Splitting on backslash alone would read "ui/.." as one component
  // and reject it for ending in a dot -- a refusal, but for the wrong reason, which makes the
  // diagnostic misleading and would hide a real traversal behind a cosmetic complaint.
  const auto next_separator = [&name](size_t from) {
    const size_t back = name.find(L'\\', from);
    const size_t fwd = name.find(L'/', from);
    return std::min(back, fwd);
  };

  size_t start = 0;
  while (start <= name.size()) {
    const size_t sep = next_separator(start);
    const std::wstring component =
        name.substr(start, (sep == std::wstring::npos ? name.size() : sep) - start);

    if (component.empty()) return PayloadNameVerdict::EmptyComponent;
    if (component == L"..") return PayloadNameVerdict::Traversal;
    if (component == L".") return PayloadNameVerdict::CurrentDir;
    // Windows strips a trailing dot or space, so "a." and "a" name the same file -- two payload
    // entries could then collide without looking like duplicates.
    if (component.back() == L'.' || component.back() == L' ') {
      return PayloadNameVerdict::TrailingDotOrSpace;
    }
    if (component.front() == L' ') return PayloadNameVerdict::TrailingDotOrSpace;
    if (is_reserved_device(component)) return PayloadNameVerdict::ReservedDeviceName;

    if (sep == std::wstring::npos) break;
    start = sep + 1;
  }

  return PayloadNameVerdict::Ok;
}

}  // namespace

const char* payload_name_verdict_name(PayloadNameVerdict verdict) {
  switch (verdict) {
    case PayloadNameVerdict::Ok: return "Ok";
    case PayloadNameVerdict::Empty: return "Empty";
    case PayloadNameVerdict::Absolute: return "Absolute";
    case PayloadNameVerdict::DriveRelative: return "DriveRelative";
    case PayloadNameVerdict::Traversal: return "Traversal";
    case PayloadNameVerdict::CurrentDir: return "CurrentDir";
    case PayloadNameVerdict::AlternateStream: return "AlternateStream";
    case PayloadNameVerdict::Wildcard: return "Wildcard";
    case PayloadNameVerdict::ControlCharacter: return "ControlCharacter";
    case PayloadNameVerdict::TrailingDotOrSpace: return "TrailingDotOrSpace";
    case PayloadNameVerdict::ReservedDeviceName: return "ReservedDeviceName";
    case PayloadNameVerdict::TooLong: return "TooLong";
    case PayloadNameVerdict::EmptyComponent: return "EmptyComponent";
    case PayloadNameVerdict::Duplicate: return "Duplicate";
    case PayloadNameVerdict::NonAscii: return "NonAscii";
  }
  return "?";
}

std::wstring normalise_payload_name(const std::wstring& name) {
  std::wstring out = name;
  std::replace(out.begin(), out.end(), L'/', L'\\');
  return to_lower(out);
}

PayloadNameVerdict check_payload_name(const std::wstring& name) {
  // Judged as given first, then again after folding forward slashes. A name that looks harmless
  // in one separator form and hostile in the other must not be able to pass by being checked in
  // only one of them.
  const PayloadNameVerdict asGiven = check_normalised(name);
  if (asGiven != PayloadNameVerdict::Ok) return asGiven;

  std::wstring folded = name;
  std::replace(folded.begin(), folded.end(), L'/', L'\\');
  return check_normalised(folded);
}

PayloadNameVerdict check_payload_name_utf8(const std::string& name) {
  std::wstring wide;
  wide.reserve(name.size());
  for (unsigned char c : name) {
    // Refused, not decoded. Every name this product ships is ASCII, and admitting more would mean
    // reasoning about normalisation and homoglyphs on a string about to become a path.
    if (c >= 0x80) return PayloadNameVerdict::NonAscii;
    wide.push_back(static_cast<wchar_t>(c));
  }
  return check_payload_name(wide);
}

bool check_payload_names_utf8(const std::vector<std::string>& names, size_t* badIndex,
                              PayloadNameVerdict* verdict) {
  std::vector<std::wstring> wide;
  wide.reserve(names.size());
  for (size_t i = 0; i < names.size(); ++i) {
    const PayloadNameVerdict v = check_payload_name_utf8(names[i]);
    if (v != PayloadNameVerdict::Ok) {
      if (badIndex) *badIndex = i;
      if (verdict) *verdict = v;
      return false;
    }
    std::wstring w;
    for (unsigned char c : names[i]) w.push_back(static_cast<wchar_t>(c));
    wide.push_back(std::move(w));
  }
  return check_payload_names(wide, badIndex, verdict);
}

bool check_payload_names(const std::vector<std::wstring>& names, size_t* badIndex,
                         PayloadNameVerdict* verdict) {
  const auto fail = [&](size_t index, PayloadNameVerdict why) {
    if (badIndex) *badIndex = index;
    if (verdict) *verdict = why;
    return false;
  };

  std::set<std::wstring> seen;
  for (size_t i = 0; i < names.size(); ++i) {
    const PayloadNameVerdict v = check_payload_name(names[i]);
    if (v != PayloadNameVerdict::Ok) return fail(i, v);

    // Duplicates are refused because the swap moves every named file aside before writing any of
    // them: the same name twice would have the second move-aside overwrite the first backup, and
    // a rollback would restore the wrong bytes.
    const std::wstring key = normalise_payload_name(names[i]);
    if (!seen.insert(key).second) return fail(i, PayloadNameVerdict::Duplicate);
  }
  if (badIndex) *badIndex = 0;
  if (verdict) *verdict = PayloadNameVerdict::Ok;
  return true;
}

}  // namespace remote60::native_poc::update
