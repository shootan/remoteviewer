import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

# ---------------------------------------------------- the host's own observation
edit('apps/native_poc/src/directory_client.cpp', [
 ('''  const std::string text(reinterpret_cast<const char*>(bytes), len);
  std::string ip;
  uint32_t port = 0;
  if (!json_get_string(text, "ip", &ip) || !json_get_u32(text, "port", &port) || port == 0) {
    return true;  // it came from the directory, so do not hand it back to the media protocol
  }
  std::lock_guard<std::mutex> lock(mu_);
  observedIp_ = ip;
  observedPort_ = static_cast<uint16_t>(port);''',
  '''  const std::string text(reinterpret_cast<const char*>(bytes), len);
  std::string ip;
  uint32_t port = 0;
  // Checked in full before anything is stored. The loose reader used here before took the digits
  // in front of a '.' (so 29181.5 became 29181) and had no upper bound at all -- only `port == 0`
  // was refused -- so 65537 survived the cast to uint16_t as **1**. That is not a value anything
  // rejects downstream: it is a plausible port, published as this host's public one, and a host
  // nobody can reach looks exactly like a network fault. A refusal here leaves the previous
  // observation in place and the next probe replaces it.
  if (!json_get_string(text, "ip", &ip) || ip.empty() ||
      !json_get_exact_u32(text, "port", &port) || port == 0 || port > 65535) {
    return true;  // it came from the directory, so do not hand it back to the media protocol
  }
  std::lock_guard<std::mutex> lock(mu_);
  observedIp_ = ip;
  observedPort_ = static_cast<uint16_t>(port);'''),
])

# ------------------------------------- the viewer's and the phone's (one shared implementation)
edit('apps/native_poc/src/directory_rendezvous.cpp', [
 ('''  size_t cursor = json.find(':', portKey);
  if (cursor == std::string::npos) return false;
  ++cursor;
  while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\\t')) ++cursor;
  int port = 0;
  while (cursor < json.size() && json[cursor] >= '0' && json[cursor] <= '9') {
    port = port * 10 + (json[cursor] - '0');
    ++cursor;
  }
  if (port <= 0 || port > 65535) return false;''',
  '''  size_t cursor = json.find(':', portKey);
  if (cursor == std::string::npos) return false;
  ++cursor;
  while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\\t')) ++cursor;
  const size_t digitsBegin = cursor;
  long port = 0;
  while (cursor < json.size() && json[cursor] >= '0' && json[cursor] <= '9') {
    port = port * 10 + (json[cursor] - '0');
    // Stop accumulating well before an int can wrap. Anything this long is already out of range,
    // and signed overflow would be undefined rather than merely wrong.
    if (port > 65535) return false;
    ++cursor;
  }
  if (cursor == digitsBegin) return false;
  // What follows has to end the value. Without this, "29181.5" read as 29181 -- digits taken from
  // in front of a '.' -- which is a plausible port that nothing downstream refuses.
  if (cursor < json.size()) {
    const char after = json[cursor];
    if (after != ',' && after != '}' && after != ' ' && after != '\\t' && after != '\\r' &&
        after != '\\n') {
      return false;
    }
  }
  if (port <= 0 || port > 65535) return false;'''),
])
