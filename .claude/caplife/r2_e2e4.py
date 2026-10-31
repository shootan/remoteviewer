src = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\apps\\native_poc\\src\\'

def edit(name, pairs):
    p = src + name
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == len_or_one(s, old), (name, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

def len_or_one(s, old):
    return 2 if old.startswith('            "desktopGen=" + std::to_string(gen) + " adoptedCaptureUs="') else 1

edit('host_capture_lifecycle_ua_e2e_test.cpp', [
    ('''  size_t CountSince(uint64_t fromUs) {''',
     '''  // What came at/after a capture time, for a failure's detail (up to 4: capture, key, synthetic).
  std::string Describe(uint64_t fromCaptureUs) {
    std::lock_guard<std::mutex> lk(mu);
    std::string out;
    int n = 0;
    for (const Seen& s : v) {
      if (s.captureUs + 1 < fromCaptureUs) continue;
      out += " [cap=" + std::to_string(s.captureUs) + " key=" + (s.key ? "1" : "0") + " syn=" + (s.synthetic ? "1" : "0") + "]";
      if (++n == 4) break;
    }
    return out.empty() ? std::string(" [nothing seen at/after]") : out;
  }
  size_t CountSince(uint64_t fromUs) {'''),
    ('''            "desktopGen=" + std::to_string(gen) + " adoptedCaptureUs=" + std::to_string(adoptedUs) +
                (joined ? " keyCaptureUs=" + std::to_string(kk.captureUs) : std::string()));''',
     '''            "desktopGen=" + std::to_string(gen) + " adoptedCaptureUs=" + std::to_string(adoptedUs) +
                (joined ? " keyCaptureUs=" + std::to_string(kk.captureUs) : seen.Describe(adoptedUs)));'''),
])
print('ok')
