import sys
root = sys.argv[1]

def edit(path, pairs):
    p = root + '/' + path
    with open(p, 'rb') as f:
        s = f.read().decode('utf-8')
    for old, new in pairs:
        old = old.replace('\n', '\r\n')
        new = new.replace('\n', '\r\n')
        if s.count(old) != 1:
            raise SystemExit('not unique in %s: %r' % (path, old[:60]))
        s = s.replace(old, new)
    with open(p, 'wb') as f:
        f.write(s.encode('utf-8'))

edit('apps/native_poc/src/client_park_state.hpp', [
    ("    if (phase_ == ClientParkPhase::Parked && nowUs > beganUs_ + kParkedLimitUs) phase_ = ClientParkPhase::Expired;",
     "    if (phase_ == ClientParkPhase::Parked && nowUs > beganUs_ + parkedLimitUs_) phase_ = ClientParkPhase::Expired;"),
    ("  ClientParkPhase phase() const { return phase_; }\n",
     "  ClientParkPhase phase() const { return phase_; }\n"
     "  // keepalive-multi r1: the park limit is a policy value (KeepalivePolicy::parkTtlMs); kParkedLimitUs by default.\n"
     "  void SetParkedLimitUs(uint64_t us) { parkedLimitUs_ = us ? us : kParkedLimitUs; }\n"
     "  uint64_t parked_limit_us() const { return parkedLimitUs_; }\n"),
    ("  uint32_t reReleases_ = 0;\n};",
     "  uint32_t reReleases_ = 0;\n  uint64_t parkedLimitUs_ = kParkedLimitUs;\n};"),
])

edit('apps/native_poc/src/native_video_client_session.hpp', [
    ("  bool BeginPark();\n  bool ResumeFromPark();\n  void EndPark();\n",
     "  bool BeginPark();\n  bool ResumeFromPark();\n  void EndPark();\n"
     "  // keepalive-multi r1: this connection's park limit (the policy's TTL), enforced by its own control loop.\n"
     "  void SetParkLimitMs(uint64_t ms) {\n"
     "    std::lock_guard<std::mutex> lk(parkMu_);\n"
     "    park_.SetParkedLimitUs(ms * 1000ull);\n"
     "  }\n"),
])
print('ok')
