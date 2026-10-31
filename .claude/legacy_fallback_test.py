# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/src/directory_retry_test.cpp'
d = io.open(p, encoding='utf-8').read()

# ---------------------------------------------------------------- a directory laid out like the deployed http one
old = '''  void Stop() {
    stopping_ = true;'''
new = '''  /**
   * Starts with the UDP socket at httpPort + 1 -- the layout a deployed http directory has, and
   * the only one the legacy fallback can reach.
   *
   * The other Start() deliberately takes an ephemeral observe port so that a heartbeat proves the
   * advertisement was used. This one is the opposite case: it proves the advertisement is not
   * REQUIRED where a derivable port already worked. Loopback only, never a fixed well-known
   * number, and retried because the OS may hand out the adjacent port to someone else.
   */
  bool StartLegacyAdjacent() {
    for (int attempt = 0; attempt < 32; ++attempt) {
      http_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      udp_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (http_ == INVALID_SOCKET || udp_ == INVALID_SOCKET) return false;
      if (!Bind(http_, &httpPort_) || httpPort_ >= 65535) {
        CloseSockets();
        continue;
      }
      if (!BindAt(udp_, static_cast<uint16_t>(httpPort_ + 1))) {
        CloseSockets();
        continue;
      }
      udpPort_ = static_cast<uint16_t>(httpPort_ + 1);
      if (::listen(http_, 8) != 0) return false;
      httpThread_ = std::thread([this] { ServeHttp(); });
      udpThread_ = std::thread([this] { ServeUdp(); });
      return true;
    }
    return false;
  }

  void Stop() {
    stopping_ = true;'''
assert d.count(old) == 1, 'Stop anchor'
d = d.replace(old, new, 1)

old = '''  static bool Bind(SOCKET s, uint16_t* outPort) {'''
new = '''  void CloseSockets() {
    if (http_ != INVALID_SOCKET) {
      closesocket(http_);
      http_ = INVALID_SOCKET;
    }
    if (udp_ != INVALID_SOCKET) {
      closesocket(udp_);
      udp_ = INVALID_SOCKET;
    }
  }

  static bool BindAt(SOCKET s, uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // loopback only
    addr.sin_port = htons(port);
    return bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
  }

  static bool Bind(SOCKET s, uint16_t* outPort) {'''
assert d.count(old) == 1, 'Bind anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- the cases
anchor = '''  WSACleanup();'''
added = '''  // ------------------------------------ asking must not cost anything where the old rule worked
  //
  // The health request is new, and new questions break old answers. On http the observe port has
  // always been derivable (httpPort + 1), and every currently deployed directory is laid out that
  // way -- so a health route that 500s, or that answers without saying anything, must leave that
  // path exactly as it was. If this pair ever goes red, the fix has made the common case worse
  // than the bug it repaired.
  {
    const struct {
      const char* label;
      const char* what;
      Reply health;
    } rows[] = {
        {"legacy-health-500", "a health route that fails",
         Reply{500, "{\\"error\\":\\"no health route here\\"}"}},
        {"legacy-health-silent", "a health route that says nothing",
         Reply{200, "{\\"ok\\":true}"}},
    };
    for (const auto& row : rows) {
      FakeDirectory dir;
      if (!dir.StartLegacyAdjacent()) {
        // Never silently skipped: an unavailable adjacent port would otherwise turn this into a
        // case that passes by not running.
        check(std::string(row.what) + ": could not lay out the fixture", false,
              "the adjacent udp port was not free after 32 tries");
        continue;
      }
      dir.Script("/healthz", {row.health});
      dir.Script("/api/host/heartbeat", {Reply{200, "{\\"ok\\":true}"}});
      dir.Script("/api/host/register", {Reply{500, "{\\"error\\":\\"must not be called\\"}"}});

      const std::string status = RunHost(dir, row.label, 1, 0, true);
      check(std::string(row.what) + " leaves the http fallback working",
            dir.Count("/api/host/heartbeat") >= 1,
            std::to_string(dir.Count("/api/host/heartbeat")));
      check("...and the observation really went to httpPort+1", dir.ObserveProbes() >= 1,
            std::to_string(dir.ObserveProbes()));
      check("...without registering again", dir.Count("/api/host/register") == 0,
            std::to_string(dir.Count("/api/host/register")));
      check("...and the address is published", status.find("public=") != std::string::npos,
            status);
      dir.Stop();
    }
  }

''' + anchor
assert d.count(anchor) == 1, 'anchor'
d = d.replace(anchor, added, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok legacy fallback cases')
