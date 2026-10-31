#include "url_origin.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace remote60::native_poc {
namespace {

std::string trimmed(const std::string& text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && isspace(static_cast<unsigned char>(text[begin]))) ++begin;
  while (end > begin && isspace(static_cast<unsigned char>(text[end - 1]))) --end;
  return text.substr(begin, end - begin);
}

/** How far a scheme reaches, or 0 when that is not the scheme there. Compared without case. */
size_t scheme_end(const std::string& url, const char* scheme) {
  const size_t n = std::strlen(scheme);
  if (url.size() < n) return 0;
  for (size_t i = 0; i < n; ++i) {
    if (tolower(static_cast<unsigned char>(url[i])) != scheme[i]) return 0;
  }
  return n;
}

std::string lowered(std::string text) {
  for (char& c : text) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return text;
}

}  // namespace

std::string url_origin_key(const std::string& url) {
  const std::string text = trimmed(url);
  bool secure = false;
  size_t at = scheme_end(text, "https://");
  if (at != 0) {
    secure = true;
  } else {
    at = scheme_end(text, "http://");
    if (at == 0) return lowered(text);  // no scheme we serve: only equal to itself
  }

  std::string rest = text.substr(at);
  const size_t slash = rest.find('/');
  if (slash != std::string::npos) rest = rest.substr(0, slash);

  unsigned long port = secure ? 443 : 80;
  const size_t colon = rest.rfind(':');
  if (colon != std::string::npos) {
    const std::string portText = rest.substr(colon + 1);
    if (!portText.empty() && portText.find_first_not_of("0123456789") == std::string::npos) {
      const unsigned long parsed = std::strtoul(portText.c_str(), nullptr, 10);
      // Out of range is left as the default rather than accepted: a port of 0 or 70000 is not a
      // port, and silently keeping it would make two unequal origins compare equal.
      if (parsed >= 1 && parsed <= 65535) {
        port = parsed;
        rest = rest.substr(0, colon);
      }
    }
  }
  if (rest.empty()) return lowered(text);

  return std::string(secure ? "https://" : "http://") + lowered(rest) + ":" +
         std::to_string(port);
}

}  // namespace remote60::native_poc
