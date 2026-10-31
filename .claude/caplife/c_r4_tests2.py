p = r'C:\work\gnlink-wt\apk-inbox-c\apps\native_poc\src\android_decoder_inbox_prefix_test.cpp'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:90], s.count(a))
    s = s.replace(a, b)


# nobuf records when each stalled delta was sent; the AUs older than the budget when buffers came back
rep('''Verdict nobuf(bool prefix, int stallMs, std::string* st, uint64_t* passesDuring, uint64_t sel) {
  Run r(prefix, sel, sel * 10 + 1);
  input_available(false);
  const uint64_t p0 = field(r.sink.DebugStatus(), "passes");
  const auto t0 = Clock::now();
  int sent = 0;
  while (std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < stallMs) {
    r.deltas(1, 0);
    ++sent;
    sleep_ms(33);
  }
  *passesDuring = field(r.sink.DebugStatus(), "passes") - p0;
  input_available(true);''', '''std::vector<uint32_t> gStale;  // [P3] AUs whose residence was over the budget (+ margin) when the buffers came back
Verdict nobuf(bool prefix, int stallMs, std::string* st, uint64_t* passesDuring, uint64_t sel,
              std::vector<Fed>* fedOut = nullptr) {
  Run r(prefix, sel, sel * 10 + 1);
  input_available(false);
  const uint64_t p0 = field(r.sink.DebugStatus(), "passes");
  const auto t0 = Clock::now();
  std::vector<std::pair<uint32_t, Clock::time_point>> sentAt;
  while (std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < stallMs) {
    r.deltas(1, 0);
    sentAt.push_back({gSeq, Clock::now()});
    sleep_ms(33);
  }
  *passesDuring = field(r.sink.DebugStatus(), "passes") - p0;
  const auto back = Clock::now();
  gStale.clear();
  for (const auto& [seq, at] : sentAt) {
    if (std::chrono::duration_cast<std::chrono::milliseconds>(back - at).count() > 267 + 60) gStale.push_back(seq);
  }
  input_available(true);''')
rep('''  r.recover(5);
  *st = r.sink.DebugStatus();
  return r.verdict();
}
}  // namespace''', '''  r.recover(5);
  *st = r.sink.DebugStatus();
  if (fedOut) *fedOut = fed_from(r.q0);
  return r.verdict();
}
}  // namespace''')
rep('''  const Verdict a0 = nobuf(false, 700, &s0, &pp0, 130);
  const Verdict a1 = nobuf(true, 700, &s1, &pp1, 131);''', '''  std::vector<Fed> fa1;
  const Verdict a0 = nobuf(false, 700, &s0, &pp0, 130);
  const Verdict a1 = nobuf(true, 700, &s1, &pp1, 131, &fa1);
  const std::vector<uint32_t> stale1 = gStale;
  int staleFed = 0;
  for (const Fed& f : fa1) {
    if (std::find(stale1.begin(), stale1.end(), f.seq) != stale1.end()) ++staleFed;
  }
  check("[P3 prefix] no AU that had waited longer than the budget (267 ms + 60 ms margin) when the buffers came back was "
        "handed to the codec", !stale1.empty() && staleFed == 0,
        "stale AUs=" + std::to_string(stale1.size()) + " of them fed=" + std::to_string(staleFed));''')
# [P6b]: the hole right after the AU the codec path took (head != frontier + 1)
rep('''  std::puts("\\n--- [P7] sustained overload, then the current frame again");''', '''  std::puts("\\n--- [P6b] the AU right after the one in flight never delivered (head != frontier + 1)");
  for (const bool prefix : {false, true}) {
    Run r(prefix, 165 + prefix, 1651 + prefix * 10);
    hold(fake_ndk::kHoldDequeueInput);
    r.deltas(1, 0);
    wait_until(held_now, 2000);
    ++gSeq;  // never sent
    r.deltas(11, 1);
    sleep_ms(10);
    let_go();
    wait_until([&] { return field(r.sink.DebugStatus(), "inbox") == 0; }, 1500);
    sleep_ms(30);
    r.recover(3);
    const Verdict v = r.verdict();
    const std::string st = r.sink.DebugStatus();
    check(std::string("[P6b ") + (prefix ? "prefix" : "current") + "] the queue does not follow the frontier -> not "
          "provable -> today's case 3: none corrupt", v.corrupt == 0 && field(st, "prefixKept") == 0, show(v));
  }

  std::puts("\\n--- [P7] sustained overload, then the current frame again");''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
