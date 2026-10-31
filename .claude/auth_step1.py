import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

# =============================================================== transport takes a credential
edit('apps/native_poc/src/update_http.hpp', [
 ('''FetchStatus https_get_text(const std::string& url, size_t maxBytes, std::string* out,
                           std::string* error);''',
  '''FetchStatus https_get_text(const std::string& url, size_t maxBytes, std::string* out,
                           std::string* error, const std::string& credentialHeader = {});'''),
 ('''FetchStatus https_get_file(const std::string& url, const std::wstring& destPath, uint64_t maxBytes,
                           std::string* error);''',
  '''FetchStatus https_get_file(const std::string& url, const std::wstring& destPath, uint64_t maxBytes,
                           std::string* error, const std::string& credentialHeader = {});'''),
 ('''//   * follow a redirect from https to http''',
  '''//   * follow a redirect from https to http
//   * follow ANY redirect on a request that carries a credential -- see below'''),
 ('''/**
 * Fetches a small document into memory.''',
  '''/**
 * `credentialHeader`, when non-empty, is one header line ("Name: value", no CRLF) sent with the
 * request. Two things change when it is present:
 *
 *   * redirects are turned off entirely for that request. A redirect is a request to send this
 *     again somewhere else, and "somewhere else" is exactly where a credential must not go. The
 *     https-to-http policy is not enough on its own: https://evil is still https.
 *   * nothing else. There is no configuration in which a credential goes out over http, because
 *     there is no configuration in which this client speaks http at all.
 *
 * Deciding WHETHER to pass one is not this file's job: the caller knows which origin it obtained
 * the credential from and where it is about to send it. This only guarantees that if it is
 * passed, it goes to that one request and no other.
 */

/**
 * Fetches a small document into memory.'''),
])

edit('apps/native_poc/src/update_http.cpp', [
 ('''FetchStatus open_request(const std::string& url, WinHttpHandle* session, WinHttpHandle* connect,''',
  '''FetchStatus open_request(const std::string& url, const std::string& credentialHeader,
                         WinHttpHandle* session, WinHttpHandle* connect,'''),
 ('''  if (!WinHttpSendRequest(request->h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0,
                          0, 0)) {''',
  '''  // A credentialed request follows nothing. A redirect asks for this request to be repeated at
  // another address, and that address is chosen by whoever answered -- which is the one party a
  // credential must not be handed to on request. Turned off at the handle rather than inspected
  // per hop, because an inspection is a decision that can be wrong.
  std::wstring headers;
  if (!credentialHeader.empty()) {
    DWORD disable = WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption(request->h, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));
    headers = widen(credentialHeader);
    headers += L"\\r\\n";
  }

  if (!WinHttpSendRequest(request->h,
                          headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                          headers.empty() ? 0 : static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0,
                          0, 0)) {'''),
 ('''FetchStatus https_get_text(const std::string& url, size_t maxBytes, std::string* out,
                           std::string* error) {''',
  '''FetchStatus https_get_text(const std::string& url, size_t maxBytes, std::string* out,
                           std::string* error, const std::string& credentialHeader) {'''),
 ('''FetchStatus https_get_file(const std::string& url, const std::wstring& destPath, uint64_t maxBytes,
                           std::string* error) {''',
  '''FetchStatus https_get_file(const std::string& url, const std::wstring& destPath, uint64_t maxBytes,
                           std::string* error, const std::string& credentialHeader) {'''),
])
