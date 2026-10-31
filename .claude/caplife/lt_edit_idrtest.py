p = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/native_poc/src/android_decoder_idr_recovery_test.cpp'
s = open(p, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:70], s.count(a))
    s = s.replace(a, b)


rep('''  std::vector<Clock::time_point> keyRequests;
  std::atomic<bool> peerLost{false};''', '''  std::vector<Clock::time_point> keyRequests;
  std::vector<Clock::time_point> pings;  // the client's keepalive, as it reaches the host
  std::atomic<bool> peerLost{false};''')
rep('''      if (type == MessageType::ControlPing && h.size == sizeof(n::ControlPingMessage)) {
        const auto ping = decode_msg<n::ControlPingMessage>(p);''', '''      if (type == MessageType::ControlPing && h.size == sizeof(n::ControlPingMessage)) {
        {
          std::lock_guard<std::mutex> l(mu);
          pings.push_back(Clock::now());
        }
        const auto ping = decode_msg<n::ControlPingMessage>(p);''')
rep('''  size_t RequestsBetween(Clock::time_point a, Clock::time_point b) {''', '''  // The longest the client's keepalive was silent at the host within [a, b].
  int64_t PingGapMs(Clock::time_point a, Clock::time_point b) {
    std::lock_guard<std::mutex> l(mu);
    Clock::time_point last = a;
    int64_t gap = 0;
    for (auto t : pings) {
      if (t < a || t > b) continue;
      gap = std::max<int64_t>(gap, ms_between(last, t));
      last = t;
    }
    return std::max<int64_t>(gap, ms_between(last, b));
  }
  size_t RequestsBetween(Clock::time_point a, Clock::time_point b) {''')
rep('''  size_t asksBefore = 0, asksAfter = 0;
  int64_t queuedMs = -1, presentMs = -1;''', '''  size_t asksBefore = 0, asksAfter = 0;
  int64_t pingGapMs = -1;  // the longest keepalive silence at the host while the decoder was not ready
  int64_t queuedMs = -1, presentMs = -1;''')
rep('''  o.asksBefore = host.RequestsBetween(t0, ready);''', '''  o.asksBefore = host.RequestsBetween(t0, ready);
  o.pingGapMs = host.PingGapMs(t0, ready);''')
rep('''  o.detail = "asks before ready=" + std::to_string(o.asksBefore)''', '''  o.detail = "pingGap=" + std::to_string(o.pingGapMs) + "ms asks before ready=" + std::to_string(o.asksBefore)''')
rep('''  check(name + ": control stayed open on both ends (no peer-lost)", !o.controlLost, o.detail);''', '''  check(name + ": control stayed open on both ends (no peer-lost)", !o.controlLost, o.detail);
  check(name + ": the client's keepalive kept reaching the host while the decoder was not ready (gap < 1000 ms)",
        o.pingGapMs >= 0 && o.pingGapMs < 1000, o.detail);''')
open(p, 'w', encoding='utf-8').write(s)
print('ok')
