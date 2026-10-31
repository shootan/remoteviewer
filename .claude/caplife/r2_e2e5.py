src = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\apps\\native_poc\\src\\'

p = src + 'host_capture_lifecycle_ua_e2e_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

old_join = '''      Seen kk{};
      const bool joined = adoptedUs != 0 && wait_until([&] { return seen.KeyCapturedAfter(adoptedUs - 1, &kk); }, 3000);
      check(tag + ": ...THAT KEY IS OF THE NEW ATTACHMENT (captured at/after its first adopted frame)", joined,
            "desktopGen=" + std::to_string(gen) + " adoptedCaptureUs=" + std::to_string(adoptedUs) +
                (joined ? " keyCaptureUs=" + std::to_string(kk.captureUs) : seen.Describe(adoptedUs)));'''
new_join = '''      // The host sent that very frame as the new epoch's first key ("capture-switch first-frame ...
      // captureQpcUs=<adopted> auEpoch=E curEpoch=E"), and the viewer decoded a real picture captured at/after
      // it. (The decoded picture's capture time is the decoder OUTPUT's; the viewer's "key" mark is its INPUT
      // packet's -- an MF decoder may return a picture one call late, so the two are not joined on one Seen.)
      const std::string firstKey = "captureQpcUs=" + std::to_string(adoptedUs) + " size=";
      const bool hostKey = adoptedUs != 0 && host_says(firstKey, 3000);
      Seen kk{};
      const bool joined = adoptedUs != 0 && wait_until([&] { return seen.RealCapturedAtOrAfter(adoptedUs, &kk); }, 3000);
      check(tag + ": ...THE NEW ATTACHMENT'S FIRST FRAME WENT OUT AS THE NEW EPOCH'S KEY, AND WAS DECODED", hostKey && joined,
            "desktopGen=" + std::to_string(gen) + " adoptedCaptureUs=" + std::to_string(adoptedUs) +
                " hostFirstKey=" + (hostKey ? "1" : "0") +
                (joined ? " decodedCaptureUs=" + std::to_string(kk.captureUs) : seen.Describe(adoptedUs)));'''
assert s.count(old_join) == 2
s = s.replace(old_join, new_join)

old = '''  size_t CountSince(uint64_t fromUs) {'''
new = '''  // A real (not re-served) decoded picture captured at/after `fromCaptureUs`.
  bool RealCapturedAtOrAfter(uint64_t fromCaptureUs, Seen* out = nullptr) {
    std::lock_guard<std::mutex> lk(mu);
    for (const Seen& s : v) {
      if (!s.synthetic && s.captureUs >= fromCaptureUs) {
        if (out) *out = s;
        return true;
      }
    }
    return false;
  }
  size_t CountSince(uint64_t fromUs) {'''
assert s.count(old) == 1
s = s.replace(old, new)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')

