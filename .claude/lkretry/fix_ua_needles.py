p = r'C:\work\gnlink-wt\fix-capture-lifecycle\apps\native_poc\src\host_capture_lifecycle_ua_e2e_test.cpp'
s = open(p, encoding='utf-8').read()
def rep(old, new):
    global s
    assert s.count(old) == 1, old[:80]
    s = s.replace(old, new)
rep('''  const auto round_trip = [&](const char* who, int i, SeenLog& seen, const std::string& onSecure, const std::string& onBack) {''',
    '''  // *Needle: a host log line each edge must add; *What: how the check names it.
  const auto round_trip = [&](const char* who, int i, SeenLog& seen, const std::string& onSecure, const std::string& onSecureWhat,
                              const std::string& onBack, const std::string& onBackWhat) {''')
rep('''      check(tag + ": ...and " + onSecure, wait_until([&] { return host_count(onSecure) > secureNeedleBefore; }, 4000));''',
    '''      check(tag + ": ...and " + onSecureWhat, wait_until([&] { return host_count(onSecure) > secureNeedleBefore; }, 4000),
            onSecure);''')
rep('''    check(tag + ": ...and " + onBack, wait_until([&] { return host_count(onBack) > backNeedleBefore; }, 8000));''',
    '''    check(tag + ": ...and " + onBackWhat, wait_until([&] { return host_count(onBack) > backNeedleBefore; }, 8000), onBack);''')
rep('''      round_trip("UA-1 WGC", i, phoneSeen, "the host rebuilt its WGC capture (wgc-rebuild ok=1)",
                 "the host rebuilt its WGC capture (wgc-rebuild ok=1)");''',
    '''      round_trip("UA-1 WGC", i, phoneSeen, "attempt=1 ok=1 active=wgc", "THE HOST REBUILT ITS WGC CAPTURE for the secure desktop",
                 "attempt=1 ok=1 active=wgc", "THE HOST REBUILT ITS WGC CAPTURE for Default again");''')
rep('''        round_trip("UA-1 DXGI", i, pcSeen, "DXGI fell back to WGC (fallback_reason=dxgi_access_lost)",
                   "the host climbed back to DXGI (desktop-backend-restored from=wgc to=dxgi)");''',
    '''        round_trip("UA-1 DXGI", i, pcSeen, "fallback_reason=dxgi_access_lost_secure_desktop", "DXGI fell back to WGC on the secure desktop",
                   "desktop-backend-restored from=wgc to=dxgi", "THE HOST CLIMBED BACK TO DXGI once Default was back");''')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('ok')
