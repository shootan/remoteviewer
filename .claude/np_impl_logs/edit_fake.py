import sys
p = sys.argv[1]
s = open(p, 'rb').read().decode('utf-8').replace('\r\n', '\n')
old = """  bool StartLegacyAdjacent() {
    for (int attempt = 0; attempt < 32; ++attempt) {
      http_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      udp_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (http_ == INVALID_SOCKET || udp_ == INVALID_SOCKET) return false;
      if (!Bind(http_, &httpPort_) || httpPort_ >= 65535) {
        CloseSockets();
        continue;
      }
      if (!BindAt(udp_, static_cast<uint16_t>(httpPort_ + 1))) {"""
new = """  bool StartLegacyAdjacent() {
    // maint r1: the pair is chosen at random in the dynamic range, not from the OS's own pick for the TCP port.
    // Windows hands out bind(0) TCP ports one after another (measured: 40 binds in a row gave 51809..51848), and
    // UDP has reserved ranges of its own (`netsh int ipv4 show excludedportrange protocol=udp` here: 60182-60381,
    // 62131-62230, 50000-50059). With the TCP cursor just below such a range, every "TCP P, UDP P+1" attempt in a
    // row failed together -- 32 of 32, on 10-28. A random base does not walk into that wall; the attempts are
    // independent. What the fixture is for is unchanged: http on one port, the observe UDP on the next one up, and
    // a loud failure (the caller's check) if no such pair can be had.
    std::mt19937 rng(std::random_device{}() ^ static_cast<uint32_t>(GetTickCount64()));
    std::uniform_int_distribution<int> base(49152, 65533);
    for (int attempt = 0; attempt < 256; ++attempt) {
      http_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      udp_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (http_ == INVALID_SOCKET || udp_ == INVALID_SOCKET) return false;
      httpPort_ = static_cast<uint16_t>(base(rng));
      if (!BindAt(http_, httpPort_)) {
        CloseSockets();
        continue;
      }
      if (!BindAt(udp_, static_cast<uint16_t>(httpPort_ + 1))) {"""
assert s.count(old) == 1
s = s.replace(old, new)
inc = '#include <atomic>\n'
assert s.count(inc) == 1
s = s.replace(inc, inc + '#include <cstdint>\n#include <random>\n', 1) if '#include <random>' not in s else s
open(p, 'wb').write(s.replace('\n', '\r\n').encode('utf-8'))
print('ok')
