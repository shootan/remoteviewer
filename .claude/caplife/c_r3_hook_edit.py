p = r'C:\work\gnlink-wt\apk-inbox-c\apps\native_poc\src\native_video_client_session.hpp'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')
a = '''  // TEST BUILD ONLY (apk-inbox C r2): one key request with these causes, through the same section every path uses.'''
assert s.count(a) == 1
s = s.replace(a, '''  // TEST BUILD ONLY (apk-inbox C r3): a whole descriptor (epoch / episode / token as a producer took it) published
  // through the same section every path uses -- e.g. an older one published after a newer one.
  bool TestPublishKey(const DecoderKeyRequest& r) {
    return RequestKey(r, static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                   std::chrono::steady_clock::now().time_since_epoch()).count()));
  }
''' + a)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
