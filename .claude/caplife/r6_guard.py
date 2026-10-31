p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/e2e_injection_guard.hpp'
s = open(p, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:90], s.count(a))
    s = s.replace(a, b)


# F-c: the adoption's recovery reports what it confirmed
rep('''  if (why.empty()) return;
  if (pi.hProcess) {
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess);
    pi.hProcess = nullptr;
  }
  if (pi.hThread) {
    CloseHandle(pi.hThread);
    pi.hThread = nullptr;
  }
  std::printf("FAIL  SETUP %s: %s -- the child was ended before it ran\\nRESULT: INVALID\\n", where, why.c_str());''',
    '''  if (why.empty()) return;
  // r6 F-c: what the recovery confirmed, said as such -- never "ended" unless the wait saw it end
  const char* ended = "no child handle";
  if (pi.hProcess) {
    const BOOL terminated = TerminateProcess(pi.hProcess, 1);
    const DWORD waited = WaitForSingleObject(pi.hProcess, 10000);
    ended = waited == WAIT_OBJECT_0 ? "the child ended before it ran (confirmed)"
            : terminated            ? "terminate asked, its end NOT confirmed within 10 s"
                                    : "terminate failed, its end NOT confirmed";
    CloseHandle(pi.hProcess);
    pi.hProcess = nullptr;
  }
  if (pi.hThread) {
    CloseHandle(pi.hThread);
    pi.hThread = nullptr;
  }
  std::printf("FAIL  SETUP %s: %s -- %s\\nRESULT: INVALID\\n", where, why.c_str(), ended);''')

# G1: the contradiction rule = the full state again, plus a host that took input with injection on
rep('''// Rule 4: a host that took input while injection was on is a contradiction -- the run is invalid.
inline bool e2e_injection_contradicted(const std::string& hostOutput) {
  return hostOutput.find("injectionEnabled=1") != std::string::npos ||
         hostOutput.find("input injection enabled") != std::string::npos;
}''', '''// Rule 4 (r6 G1): a contradiction anywhere in the launch's WHOLE output so far -- the full state judged again (any
// power / input state line that is not the explicit OFF, before or after one that is: a later power ON included),
// or a host that took input with injection on (injectionEnabled=1). "Not yet said" is not a contradiction here --
// the initial gate already required both lines.
inline bool e2e_injection_contradicted(const std::string& hostOutput, std::string* why = nullptr) {
  std::string w;
  if (e2e_injection_state(hostOutput, &w) < 0) {
    if (why) *why = w;
    return true;
  }
  if (hostOutput.find("injectionEnabled=1") != std::string::npos) {
    if (why) *why = "the host took input with injection on (injectionEnabled=1)";
    return true;
  }
  return false;
}

// Ends a job's processes and says whether that was confirmed (its active process count reached 0 within 10 s).
inline bool e2e_end_job_confirmed(HANDLE job) {
  if (!job) return false;
  if (!TerminateJobObject(job, 2)) return false;
  for (int i = 0; i < 200; ++i) {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION acc{};
    if (QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &acc, sizeof(acc), nullptr) &&
        acc.ActiveProcesses == 0) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}

// The run is INVALID: its own children (the job) are ended -- confirmed or said not to be -- and the test stops.
[[noreturn]] inline void e2e_injection_run_invalid(const char* where, const std::string& why, HANDLE job) {
  const bool ended = e2e_end_job_confirmed(job);
  std::printf("FAIL  RUN %s: input / power state contradicted during the run (%s) -- its children %s\\n"
              "RESULT: INVALID\\n", where, why.c_str(), ended ? "ended (confirmed)" : "NOT confirmed ended");
  std::fflush(stdout);
  std::_Exit(2);
}

// r6 G1: after the initial gate, the same judgement keeps running -- a poll of the launch's whole output every
// 200 ms while the host lives, one more when it is stopped (Stop) and one before the test prints its result
// (e2e_injection_final_check_all). A contradiction -> e2e_injection_run_invalid (its job ended, INVALID). This is
// separate from the initial gate's PASS line: the gate says "OFF before any client", the monitor says "and nothing
// contradicted it while it ran". One per launched host.
class E2eInjectionMonitor {
 public:
  E2eInjectionMonitor(std::function<std::string()> output, HANDLE job, std::string where)
      : output_(std::move(output)), job_(job), where_(std::move(where)) {
    {
      std::lock_guard<std::mutex> lock(Registry().mu);
      Registry().live.push_back(this);
    }
    thread_ = std::thread([this] {
      while (!stop_.load()) {
        Check();
        for (int i = 0; i < 4 && !stop_.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    });
  }
  ~E2eInjectionMonitor() { Stop(); }
  E2eInjectionMonitor(const E2eInjectionMonitor&) = delete;
  E2eInjectionMonitor& operator=(const E2eInjectionMonitor&) = delete;

  // the last look at this launch, then no more polling (call before the host's output goes away)
  void Stop() {
    if (stopped_.exchange(true)) return;
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    Check();
    std::lock_guard<std::mutex> lock(Registry().mu);
    auto& v = Registry().live;
    for (auto it = v.begin(); it != v.end(); ++it) {
      if (*it == this) {
        v.erase(it);
        break;
      }
    }
  }
  void Check() {
    std::string why;
    if (e2e_injection_contradicted(output_(), &why)) e2e_injection_run_invalid(where_.c_str(), why, job_);
    ++checks_;
  }
  int checks() const { return checks_.load(); }

  struct Reg {
    std::mutex mu;
    std::vector<E2eInjectionMonitor*> live;
  };
  static Reg& Registry() {
    static Reg r;
    return r;
  }

 private:
  std::function<std::string()> output_;
  HANDLE job_;
  std::string where_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> stopped_{false};
  std::atomic<int> checks_{0};
};

// Before a test prints its result: every host still monitored is judged once more (r6 G1). Returns how many.
inline int e2e_injection_final_check_all() {
  std::vector<E2eInjectionMonitor*> v;
  {
    std::lock_guard<std::mutex> lock(E2eInjectionMonitor::Registry().mu);
    v = E2eInjectionMonitor::Registry().live;
  }
  for (auto* m : v) m->Check();
  std::printf("PASS  input / power state: no contradiction during the run (%zu host(s) still monitored, judged again)\\n",
              v.size());
  return static_cast<int>(v.size());
}''')
for inc in ('#include <atomic>\n', '#include <mutex>\n', '#include <vector>\n'):
    if inc not in s:
        s = s.replace('#include <chrono>\n', '#include <chrono>\n' + inc, 1)
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('ok')
