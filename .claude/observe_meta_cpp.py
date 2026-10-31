import io

BS = chr(92)

# ================================================================== header
p = 'apps/native_poc/src/directory_client.hpp'
d = io.open(p, encoding='utf-8').read()

old = '''bool parse_directory_url(const std::string& url, std::string* outHost, uint16_t* outPort,'''
new = '''/**
 * Where the server says address observations should be sent.
 *
 * The clients used to work this out themselves as httpPort + 1, which held only while the
 * directory was reached directly on its own two ports. Behind a TLS terminator on 443 that
 * becomes 444 -- nothing listens there, observation fails, and a host whose observation fails
 * skips its heartbeat and so never appears in the list at all. The relay cannot stand in for it
 * either: the relay address arrives in the /api/connect response, and the viewer gives up before
 * it ever calls connect.
 *
 * So the server tells us. `known` false means it did not, and the caller decides what that means
 * for the scheme it is using -- see observe_port_for().
 */
struct ObserveEndpoint {
  bool known = false;
  uint16_t port = 0;
  /** Empty means the directory host itself, which is the ordinary case. */
  std::string host;
};

/**
 * Reads the optional `observe` metadata out of a login or register response.
 *
 * Returns false when the server said nothing, or said something unusable. A port outside
 * 1..65535 is treated as absent rather than clamped: a wrong port is worse than no port, because
 * no port leaves the caller on a documented fallback while a wrong one sends it somewhere that
 * will never answer and looks like a network fault.
 */
bool parse_observe_metadata(const std::string& json, ObserveEndpoint* out);

/**
 * The port to send observations to, or 0 when there is no safe answer.
 *
 * `secure` is whether the directory URL is https. The rules differ by scheme on purpose:
 *
 *   * The server said so -> use it, whatever the scheme.
 *   * Plain http and no metadata -> httpPort + 1, which is what every existing deployment does
 *     and what an unchanged server still expects.
 *   * https and no metadata -> 0. NOT 443 + 1. That address is not a fallback, it is a guess
 *     that cannot be right, and dialling it turns "this server needs configuring" into a silent
 *     timeout. The caller reports it instead.
 */
uint16_t observe_port_for(const ObserveEndpoint& advertised, uint16_t httpPort, bool secure);

bool parse_directory_url(const std::string& url, std::string* outHost, uint16_t* outPort,'''
assert old in d, 'hpp anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ================================================================== implementation
p = 'apps/native_poc/src/directory_client.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''bool parse_directory_url(const std::string& url, std::string* outHost, uint16_t* outPort,'''
new = '''namespace {

/**
 * The text of a nested JSON object, by key. Empty when it is not there.
 *
 * Enough for one flat object inside another, which is all this metadata is. Written rather than
 * reaching for the flat getters because those find the FIRST key of that name anywhere in the
 * document -- so a top-level "port" belonging to something else would be read as the observe
 * port. It happens to be unambiguous today; it would not stay that way, and the failure would be
 * a wrong port rather than an error.
 */
std::string json_object_field(const std::string& text, const std::string& key) {
  const std::string needle = "\\"" + key + "\\"";
  size_t at = text.find(needle);
  if (at == std::string::npos) return {};
  at = text.find(':', at + needle.size());
  if (at == std::string::npos) return {};
  while (at < text.size() && (text[at] == ':' || isspace(static_cast<unsigned char>(text[at])))) {
    ++at;
  }
  if (at >= text.size() || text[at] != '{') return {};
  int depth = 0;
  for (size_t i = at; i < text.size(); ++i) {
    if (text[i] == '{') ++depth;
    if (text[i] == '}') {
      --depth;
      if (depth == 0) return text.substr(at, i - at + 1);
    }
  }
  return {};
}

}  // namespace

bool parse_observe_metadata(const std::string& json, ObserveEndpoint* out) {
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
}

uint16_t observe_port_for(const ObserveEndpoint& advertised, uint16_t httpPort, bool secure) {
  if (advertised.known) return advertised.port;
  if (secure) return 0;  // 443 + 1 is not a fallback; see the header
  if (httpPort >= 65535) return 0;
  return static_cast<uint16_t>(httpPort + 1);
}

bool parse_directory_url(const std::string& url, std::string* outHost, uint16_t* outPort,'''
assert old in d, 'cpp anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('observe metadata parsing and the port rule')
