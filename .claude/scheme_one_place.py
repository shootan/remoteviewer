import io

# ================================================================== one detector
p = 'apps/native_poc/src/directory_observe.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''/**
 * Where the server says address observations should be sent.'''
new = '''/**
 * Whether a directory URL asks for TLS.
 *
 * One implementation, because there were three and they did not agree. The parser tested the
 * scheme case-sensitively while two callers lowercased first, so `HTTPS://host` was "unsupported
 * scheme" to one and "secure" to the others. That disagreement decides whether the transport uses
 * TLS, so the two answers are "encrypt this" and "do not" -- not a cosmetic difference, and not
 * one that announces itself.
 *
 * Leading whitespace is ignored and the scheme is compared without case, which is what a URL
 * scheme actually is.
 */
bool directory_url_is_secure(const std::string& url);

/**
 * Where the server says address observations should be sent.'''
assert old in d, 'observe hpp'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

p = 'apps/native_poc/src/directory_client.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''bool parse_directory_url(const std::string& url, std::string* outHost, uint16_t* outPort,
                         std::string* outError) {
  std::string rest = trim(url);
  if (rest.rfind("https://", 0) == 0) {'''
new = '''bool directory_url_is_secure(const std::string& url) {
  size_t at = 0;
  while (at < url.size() && isspace(static_cast<unsigned char>(url[at]))) ++at;
  const std::string scheme = "https://";
  if (url.size() - at < scheme.size()) return false;
  for (size_t i = 0; i < scheme.size(); ++i) {
    if (tolower(static_cast<unsigned char>(url[at + i])) != scheme[i]) return false;
  }
  return true;
}

bool parse_directory_url(const std::string& url, std::string* outHost, uint16_t* outPort,
                         std::string* outError) {
  std::string rest = trim(url);
  if (directory_url_is_secure(rest)) {'''
assert old in d, 'parser head'
d = d.replace(old, new, 1)

old = '''  // Noted before the parse, because it decides what an absent advertisement means. The parser
  // still refuses https today -- that is (C)'s job -- and when it stops refusing, this is already
  // carrying the right answer rather than needing to be remembered.
  {
    std::string lowered = cfg_.url;
    for (char& c : lowered) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    while (!lowered.empty() && isspace(static_cast<unsigned char>(lowered.front()))) {
      lowered.erase(lowered.begin());
    }
    httpSecure_ = lowered.rfind("https://", 0) == 0;
  }'''
new = '''  // The same answer the parser uses, from the same function. It decides what an absent
  // advertisement means, and a second opinion here would be a second opinion about whether to
  // encrypt.
  httpSecure_ = directory_url_is_secure(cfg_.url);'''
assert old in d, 'httpSecure copy'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

p = 'apps/native_poc/src/directory_session_bootstrap.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''  bool directorySecure = false;
  {
    std::string lowered = request.url;
    for (char& c : lowered) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    while (!lowered.empty() && isspace(static_cast<unsigned char>(lowered.front()))) {
      lowered.erase(lowered.begin());
    }
    directorySecure = lowered.rfind("https://", 0) == 0;
  }'''
new = '''  const bool directorySecure = directory::directory_url_is_secure(request.url);'''
assert old in d, 'bootstrap copy'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('one scheme decision, three users')
