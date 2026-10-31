import sys, os
R = r'C:\work\gnlink-wt\dirretry-r2'

def load(rel):
    b = open(os.path.join(R, rel), 'rb').read().decode('utf-8')
    return b.replace('\r\n', '\n'), ('\r\n' in b)

def save(rel, s, crlf):
    open(os.path.join(R, rel), 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))

def rep(s, a, b, cnt=1):
    assert s.count(a) == cnt, (s.count(a), a[:80])
    return s.replace(a, b)

# ---------------------------------------------------------------- directory_client.hpp
s, c = load(r'apps\native_poc\src\directory_client.hpp')
s = rep(s, """  /** Waits (bounded) until cycle `n` has completed; false on timeout or if the agent stopped. */
  bool WaitForCycleEnd(uint64_t n, int timeoutMs, CycleRecord* out);
#endif""", """  /** Waits (bounded) until cycle `n` has completed; false on timeout or if the agent stopped. */
  bool WaitForCycleEnd(uint64_t n, int timeoutMs, CycleRecord* out);
  /**
   * TEST BUILD ONLY (dirretry r2, Codex T1): a bounded barrier at the same loop boundary. Enabled before
   * Start, the loop stops right after recording cycle n until the test releases n (or the agent stops, or
   * `timeoutMs` passes -- then it goes on and CycleGateTimedOutForTest() says so, for a loud failure). While
   * it waits, nothing of cycle n+1 has happened: what the test reads of the directory is cycle n's, and a
   * script it installs is there before the next cycle asks.
   */
  void EnableCycleGateForTest(int timeoutMs);
  void ReleaseCycleForTest(uint64_t n);
  bool CycleGateTimedOutForTest() const;
#endif""")
s = rep(s, """  std::vector<CycleRecord> cycles_;  // guarded by mu_; every completed cycle, in order (bounded)
#endif""", """  std::vector<CycleRecord> cycles_;  // guarded by mu_; every completed cycle, in order (bounded)
  bool cycleGate_ = false;           // guarded by mu_ (set before Start)
  int cycleGateTimeoutMs_ = 0;
  uint64_t cycleReleased_ = 0;       // guarded by mu_
  bool cycleGateTimedOut_ = false;   // guarded by mu_
#endif""")
save(r'apps\native_poc\src\directory_client.hpp', s, c)

# ---------------------------------------------------------------- directory_client.cpp
s, c = load(r'apps\native_poc\src\directory_client.cpp')
s = rep(s, """      if (observedReady_) cycles_.back().status += " public=" + observedIp_ + ":" + std::to_string(observedPort_);
    }
  }
  cycleCv_.notify_all();
}
""", """      if (observedReady_) cycles_.back().status += " public=" + observedIp_ + ":" + std::to_string(observedPort_);
    }
  }
  cycleCv_.notify_all();
  // dirretry r2 (Codex T1): the barrier. Held here, after the record and before the loop's sleep and the next
  // cycle, so a test reads one moment -- this cycle's -- instead of a record from one and counters from later.
  std::unique_lock<std::mutex> lock(mu_);
  if (!cycleGate_) return;
  const bool released = cycleCv_.wait_for(lock, std::chrono::milliseconds(cycleGateTimeoutMs_), [&] {
    return cycleReleased_ >= record.n || !running_.load();
  });
  if (!released) cycleGateTimedOut_ = true;
}

void HostAgent::EnableCycleGateForTest(int timeoutMs) {
#if defined(REMOTE60_DIRECTORY_MUTANT_NO_CYCLE_GATE)
  (void)timeoutMs;  // MUTANT (test build only): the barrier is gone, the loop runs on as it did before r2
#else
  std::lock_guard<std::mutex> lock(mu_);
  cycleGate_ = true;
  cycleGateTimeoutMs_ = timeoutMs;
#endif
}

void HostAgent::ReleaseCycleForTest(uint64_t n) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (n > cycleReleased_) cycleReleased_ = n;
  }
  cycleCv_.notify_all();
}

bool HostAgent::CycleGateTimedOutForTest() const {
  std::lock_guard<std::mutex> lock(mu_);
  return cycleGateTimedOut_;
}
""")
save(r'apps\native_poc\src\directory_client.cpp', s, c)

# ---------------------------------------------------------------- CMakeLists
s, c = load(r'apps\native_poc\CMakeLists.txt')
s = rep(s, "foreach(_drf_mutant TIMEOUT_ONLY NO_LEGACY_ATTEMPT)", "foreach(_drf_mutant TIMEOUT_ONLY NO_LEGACY_ATTEMPT NO_CYCLE_GATE)")
s = rep(s, """# ...and two MUTANT builds of the same sources, each of which must FAIL the test: the old "timed out"
# overwrite, and "no advertisement, so do not try the legacy default". Never staged, never shipped.""",
"""# ...and MUTANT builds of the same sources, each of which must FAIL the test: the old "timed out"
# overwrite, "no advertisement, so do not try the legacy default", and (dirretry r2) no cycle barrier --
# the slow-observer stages then read counters from a later cycle. Never staged, never shipped.""")
save(r'apps\native_poc\CMakeLists.txt', s, c)
print('ok')
