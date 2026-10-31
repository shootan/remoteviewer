import io

# ================================================================== header
p = 'apps/native_poc/src/directory_client.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''struct ObserveEndpoint {
  bool known = false;
  uint16_t port = 0;
  /** Empty means the directory host itself, which is the ordinary case. */
  std::string host;
};'''
new = '''struct ObserveEndpoint {
  bool known = false;
  uint16_t port = 0;
  /** Empty means the directory host itself, which is the ordinary case. */
  std::string host;
  /**
   * True when a host WAS advertised and was thrown away for being unusable.
   *
   * Kept because the fallback is silent otherwise. The server does not validate this value -- it
   * trims it and sends it -- so a configuration slip arrives here looking like an address, and
   * dialling it produces a timeout that reads as a network fault. Falling back is right; falling
   * back without saying so leaves nobody able to find the actual mistake.
   */
  bool hostRejected = false;
};

/**
 * Whether an advertised observe host is safe to dial.
 *
 * A hostname or an IPv4 literal, and nothing else: no scheme, no port, no path, no spaces, no
 * control characters. Anything with those in it is a configuration mistake rather than an
 * address, and resolving it would either fail slowly or -- worse -- succeed against something
 * unintended.
 */
bool observe_host_is_usable(const std::string& host);'''
assert old in d, 'struct'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ================================================================== implementation
p = 'apps/native_poc/src/directory_client.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''bool parse_observe_metadata(const std::string& json, ObserveEndpoint* out) {
  if (!out) return false;
  *out = ObserveEndpoint{};
  const std::string object = json_object_field(json, "observe");
  if (object.empty()) return false;

  uint32_t port = 0;
  if (!json_profile::json_get_u32(object, "port", &port)) return false;
  if (port < 1 || port > 65535) return false;  // absent, not clamped

  out->known = true;
  out->port = static_cast<uint16_t>(port);
  json_get_string(object, "host", &out->host);
  return true;
}'''
new = '''bool observe_host_is_usable(const std::string& host) {
  if (host.empty() || host.size() > 253) return false;
  bool labelHasChar = false;
  for (size_t i = 0; i < host.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(host[i]);
    if (c == '.') {
      if (!labelHasChar) return false;  // empty label: "a..b", ".a", trailing dot
      labelHasChar = false;
      continue;
    }
    // Letters, digits and '-' only. Everything a mistake tends to carry -- "://", ":1234",
    // "/path", spaces, control characters -- lands here and is refused.
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '-';
    if (!ok) return false;
    if (!labelHasChar && c == '-') return false;  // a label may not start with '-'
    labelHasChar = true;
  }
  if (!labelHasChar) return false;  // ended on a dot
  if (host.back() == '-') return false;
  return true;
}

namespace {

/**
 * Reads an integer field, refusing anything that only looks like one.
 *
 * The shared getter matches `[0-9]+`, so `"port": 29181.5` yields 29181 -- the fraction is simply
 * not seen. This server rejects such a value before sending it; another server, or an older one,
 * is not bound by that. A client that trusts the server to validate is a client that breaks the
 * moment it meets a different server.
 */
bool json_get_exact_u32(const std::string& text, const std::string& key, uint32_t* out) {
  const std::string needle = "\\"" + key + "\\"";
  size_t at = text.find(needle);
  if (at == std::string::npos) return false;
  at = text.find(':', at + needle.size());
  if (at == std::string::npos) return false;
  ++at;
  while (at < text.size() && isspace(static_cast<unsigned char>(text[at]))) ++at;
  const size_t begin = at;
  while (at < text.size() && isdigit(static_cast<unsigned char>(text[at]))) ++at;
  if (at == begin) return false;  // no digits: null, a string, a negative
  // What follows has to end the value. A '.' or another digit-ish character means this was never
  // the integer it appeared to be.
  if (at < text.size()) {
    const char after = text[at];
    if (after != ',' && after != '}' && !isspace(static_cast<unsigned char>(after))) return false;
  }
  const unsigned long long v = std::strtoull(text.substr(begin, at - begin).c_str(), nullptr, 10);
  if (v > 0xFFFFFFFFull) return false;
  if (out) *out = static_cast<uint32_t>(v);
  return true;
}

}  // namespace

bool parse_observe_metadata(const std::string& json, ObserveEndpoint* out) {
  if (!out) return false;
  *out = ObserveEndpoint{};
  const std::string object = json_object_field(json, "observe");
  if (object.empty()) return false;

  uint32_t port = 0;
  if (!json_get_exact_u32(object, "port", &port)) return false;
  if (port < 1 || port > 65535) return false;  // absent, not clamped

  out->known = true;
  out->port = static_cast<uint16_t>(port);

  std::string host;
  if (json_get_string(object, "host", &host)) {
    // Trimmed first, because whitespace-only is the same thing as absent.
    while (!host.empty() && isspace(static_cast<unsigned char>(host.front()))) {
      host.erase(host.begin());
    }
    while (!host.empty() && isspace(static_cast<unsigned char>(host.back()))) host.pop_back();
    if (!host.empty()) {
      if (observe_host_is_usable(host)) {
        out->host = host;
      } else {
        // The port is still good; only the host is not. Recorded so the fallback is visible.
        out->hostRejected = true;
      }
    }
  }
  return true;
}'''
assert old in d, 'parse fn'
d = d.replace(old, new, 1)

old = '''  const std::string& target = observeAdvertised_.host.empty() ? httpHost_ : observeAdvertised_.host;'''
new = '''  if (observeAdvertised_.hostRejected) {
    // Not fatal -- the directory's own hostname is the documented default and it is used below.
    // Said out loud because the value came from server configuration and nobody will find the
    // mistake by watching a timeout.
    SetStatus("the directory advertised an observe host that is not a usable name; using the "
              "directory host instead");
  }
  const std::string& target = observeAdvertised_.host.empty() ? httpHost_ : observeAdvertised_.host;'''
assert old in d, 'apply target'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('the client validates what the server sent')
