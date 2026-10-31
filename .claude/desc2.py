import io

NL = chr(92) + 'n'

p = 'apps/native_poc/src/update_endpoint.cpp'
d = io.open(p, encoding='utf-8').read()

body = '''
namespace {

/** No value may contain a newline: the format is line-based, so one would forge a field. */
bool one_line(const std::string& value) {
  return value.find(chr_n) == std::string::npos && value.find('\\r') == std::string::npos;
}

}  // namespace

std::string encode_update_descriptor(const UpdateEndpoint& endpoint) {
  std::string out;
  out += "v=1";
  out += chr_n;
  out += "url=" + endpoint.url;
  out += chr_n;
  out += "origin=" + endpoint.origin;
  out += chr_n;
  out += "owner=" + endpoint.ownerKey;
  out += chr_n;
  out += "epoch=" + std::to_string(endpoint.ownerEpoch);
  out += chr_n;
  out += std::string("envelope=") + (endpoint.derived ? "1" : "0");
  out += chr_n;
  out += "cred=" + endpoint.credentialHeader;
  out += chr_n;
  return out;
}

bool decode_update_descriptor(const std::string& text, UpdateEndpoint* endpoint,
                              std::string* error) {
  if (!endpoint) return false;
  *endpoint = UpdateEndpoint{};
  bool sawVersion = false;
  bool sawUrl = false;
  bool sawEnvelope = false;
  size_t at = 0;
  while (at < text.size()) {
    const size_t end = text.find(chr_n, at);
    const std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
    at = end == std::string::npos ? text.size() : end + 1;
    if (line.empty()) continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
      if (error) *error = "a line without a key";
      return false;
    }
    const std::string key = line.substr(0, eq);
    const std::string value = line.substr(eq + 1);
    if (key == "v") {
      if (value != "1") {
        if (error) *error = "descriptor version " + value + " is not one this understands";
        return false;
      }
      sawVersion = true;
    } else if (key == "url") {
      endpoint->url = value;
      sawUrl = true;
    } else if (key == "origin") {
      endpoint->origin = value;
    } else if (key == "owner") {
      endpoint->ownerKey = value;
    } else if (key == "epoch") {
      endpoint->ownerEpoch = strtoull(value.c_str(), nullptr, 10);
    } else if (key == "envelope") {
      endpoint->derived = value == "1";
      sawEnvelope = true;
    } else if (key == "cred") {
      endpoint->credentialHeader = value;
    } else {
      // Refused rather than ignored. An unknown key means the sender and the receiver disagree
      // about what this frame is, and guessing which parts still apply is how the disagreement
      // gets buried.
      if (error) *error = "unknown field '" + key + "'";
      return false;
    }
  }
  if (!sawVersion || !sawUrl || !sawEnvelope) {
    if (error) *error = "the descriptor is missing a required field";
    return false;
  }
  return true;
}

'''

body = body.replace('chr_n', "'" + chr(92) + "n'")

marker = '}  // namespace remote60::native_poc::update'
assert d.count(marker) == 1
d = d.replace(marker, body + marker, 1)
if '#include <cstdlib>' not in d:
    d = d.replace('#include "url_origin.hpp"', '#include <cstdlib>\n\n#include "url_origin.hpp"', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok update_endpoint.cpp')
