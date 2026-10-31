p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/e2e_injection_guard.hpp'
s = open(p, encoding='utf-8').read()
i = s.index('// r6 G1: after the initial gate, the same judgement keeps running')
j = s.index('// The tests whose very subject is an injection the OS performs')
new = '''// r6 G1 / r7 N1: after the initial gate, the same judgement keeps running, in three steps of its own:
//   1. polling -- the launch's whole output every 200 ms while the host lives (constructor; poll=false: none);
//   2. StopPolling() -- before the host is ended (nothing judged yet: its last output may still come);
//   3. Finalize(outputComplete) -- AFTER the producer ended and the reader reached its end of output: the final
//      snapshot judged; output not complete (an end or EOF not confirmed) is INVALID too.
// A contradiction at any step -> its job ended (confirmed or said not), FAIL RUN ..., RESULT: INVALID, exit 2.
// Every launch stays registered for the test's life: e2e_injection_final_check_all() (before the result) judges each
// one -- a finalized launch by its final verdict, a live one by its output now, an abandoned one (destroyed without
// Finalize) is INVALID. "No live monitor" is never taken as "all judged". Reported apart from the initial gate.
class E2eInjectionMonitor {
 public:
  struct State {
    std::mutex mu;
    std::function<std::string()> output;  // released at Finalize / destruction (the producer's owner may go)
    HANDLE job = nullptr;
    std::string where;
    bool finalized = false;
    bool abandoned = false;
  };
  struct Reg {
    std::mutex mu;
    std::vector<std::shared_ptr<State>> all;
  };
  static Reg& Registry() {
    static Reg r;
    return r;
  }

  E2eInjectionMonitor(std::function<std::string()> output, HANDLE job, std::string where, bool poll = true)
      : st_(std::make_shared<State>()) {
    st_->output = std::move(output);
    st_->job = job;
    st_->where = std::move(where);
    {
      std::lock_guard<std::mutex> lock(Registry().mu);
      Registry().all.push_back(st_);
    }
    if (poll) {
      thread_ = std::thread([this] {
        while (!stop_.load()) {
          Judge(*st_);
          for (int i = 0; i < 4 && !stop_.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
      });
    }
  }
  ~E2eInjectionMonitor() {
    StopPolling();
    std::lock_guard<std::mutex> lock(st_->mu);
    if (!st_->finalized) st_->abandoned = true;  // never judged on its complete output
    st_->output = nullptr;
  }
  E2eInjectionMonitor(const E2eInjectionMonitor&) = delete;
  E2eInjectionMonitor& operator=(const E2eInjectionMonitor&) = delete;

  void StopPolling() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
  }
  // the producer has ended and its output was read to the end (outputComplete) -- the final snapshot judged
  void Finalize(bool outputComplete) {
    StopPolling();
    if (!outputComplete) {
      e2e_injection_run_invalid(st_->where.c_str(), "the final output is not known complete (the host's end or the "
                                "reader's end of output not confirmed)", st_->job);
    }
    Judge(*st_);
    std::lock_guard<std::mutex> lock(st_->mu);
    st_->finalized = true;
    st_->output = nullptr;
  }

  // one judgement of a launch's output now (a released output: nothing to judge)
  static void Judge(State& st) {
    std::string out, where;
    HANDLE job = nullptr;
    {
      std::lock_guard<std::mutex> lock(st.mu);
      if (!st.output) return;
      out = st.output();
      where = st.where;
      job = st.job;
    }
    std::string why;
    if (e2e_injection_contradicted(out, &why)) e2e_injection_run_invalid(where.c_str(), why, job);
  }

 private:
  std::shared_ptr<State> st_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
};

// Before a test prints its result: every launch of the run judged (r6 G1 / r7 N1).
inline int e2e_injection_final_check_all() {
  std::vector<std::shared_ptr<E2eInjectionMonitor::State>> v;
  {
    std::lock_guard<std::mutex> lock(E2eInjectionMonitor::Registry().mu);
    v = E2eInjectionMonitor::Registry().all;
  }
  int finalized = 0, live = 0;
  for (auto& st : v) {
    bool fin = false, aband = false;
    std::string where;
    {
      std::lock_guard<std::mutex> lock(st->mu);
      fin = st->finalized;
      aband = st->abandoned;
      where = st->where;
    }
    if (aband) e2e_injection_run_invalid(where.c_str(), "a launch was never judged on its complete output", st->job);
    if (fin) {
      ++finalized;
    } else {
      E2eInjectionMonitor::Judge(*st);  // still running: judged on its output now
      ++live;
    }
  }
  std::printf("PASS  input / power state: no contradiction during the run (%zu launch(es): %d judged on their complete "
              "output, %d still running judged now)\\n", v.size(), finalized, live);
  return static_cast<int>(v.size());
}

'''
s = s[:i] + new + s[j:]
if '#include <memory>' not in s:
    s = s.replace('#include <mutex>\n', '#include <memory>\n#include <mutex>\n', 1)
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('ok')
