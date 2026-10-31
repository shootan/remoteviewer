p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\client_managed_viewers.hpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c):
    global s
    assert s.count(a) == 1, (s.count(a), a[:100])
    s = s.replace(a, c)

rep('''//  - ending: Release / Shutdown are sent at once; the bounded wait (then TerminateJobObject on its own job) runs on
//    a thread, never on the UI thread.''', '''//  - ending: Release / Shutdown are sent at once; the bounded wait (then TerminateJobObject on its own job) runs on
//    a thread, never on the UI thread. A finished reaper is joined at the next ending (ka-pc-impl r2, Codex N5) -- not
//    only at the shell's exit -- so evicting and preparing PCs all day does not pile up threads.''')
rep('''  bool Activate(const std::string& hostId, uint64_t* op) {
    Child* c = Mutable(hostId);
    if (!c || c->ending || c->activeOp != 0) return false;
    c->activeOp = ++c->nextOp;
    *op = c->activeOp;
    return c->channel->Send(Stamp(*c, managed_ipc::Type::Activate, {{"op", std::to_string(c->activeOp)}}));
  }''', '''  bool Activate(const std::string& hostId, uint64_t* op) {
    Child* c = Mutable(hostId);
    if (!c || c->ending || c->activeOp != 0) return false;
    const uint64_t next = c->nextOp + 1;
    // ka-pc-impl r2 (Codex N6): an Activate that could not be sent leaves no activation behind (no activeOp that
    // nothing will ever end).
    if (!c->channel->Send(Stamp(*c, managed_ipc::Type::Activate, {{"op", std::to_string(next)}}))) return false;
    c->nextOp = next;
    c->activeOp = next;
    *op = next;
    return true;
  }''')
rep('''  // Shell exit: the reapers are joined, each bounded by its own wait (3 s) -- the process ends right after.
  void JoinReapers() {
    std::vector<std::thread> reapers;
    {
      std::lock_guard<std::mutex> lock(reapMu_);
      reapers.swap(reapers_);
    }
    for (auto& t : reapers)
      if (t.joinable()) t.join();
  }''', '''  // Shell exit: the reapers are joined, each bounded by its own wait -- the process ends right after.
  void JoinReapers() {
    std::vector<Reaper> reapers;
    {
      std::lock_guard<std::mutex> lock(reapMu_);
      reapers.swap(reapers_);
    }
    for (auto& r : reapers)
      if (r.thread.joinable()) r.thread.join();
  }

  // How many reaper threads are held (finished ones included until the next ending joins them). For tests.
  size_t ReaperCount() {
    std::lock_guard<std::mutex> lock(reapMu_);
    return reapers_.size();
  }''')
rep('''  void End(std::unique_ptr<Child> c, managed_ipc::Type how, const std::string& reason) {
    c->ending = true;
    c->channel->Send(Stamp(*c, how, {{"reason", reason}, {"deadlineMs", "3000"}}));
    log_("managed viewer ending host=" + c->hostId + " pid=" + std::to_string(c->pid) + " reason=" + reason);
    Child* raw = c.release();
    std::lock_guard<std::mutex> lock(reapMu_);
    reapers_.emplace_back([raw, log = log_] {
      std::unique_ptr<Child> owned(raw);
      const DWORD w = WaitForSingleObject(owned->process, 3000);
      if (w != WAIT_OBJECT_0) {
        TerminateJobObject(owned->job, 1);  // its own job: this child only
        WaitForSingleObject(owned->process, 2000);
        log("managed viewer did not end in 3 s; its job was ended host=" + owned->hostId);
      }''', '''  // ka-pc-impl r2: the child's own teardown (owner fence, WM_CLOSE, its session's bounded joins of 3 s + 3 s, the
  // lease's Release) is given this long before its job is ended.
  static constexpr DWORD kEndMs = 8000;

  void End(std::unique_ptr<Child> c, managed_ipc::Type how, const std::string& reason) {
    c->ending = true;
    c->channel->Send(Stamp(*c, how, {{"reason", reason}, {"deadlineMs", std::to_string(kEndMs)}}));
    log_("managed viewer ending host=" + c->hostId + " pid=" + std::to_string(c->pid) + " reason=" + reason);
    Child* raw = c.release();
    std::lock_guard<std::mutex> lock(reapMu_);
    // Finished reapers first (Codex N5): joined now, not at the shell's exit.
    for (auto it = reapers_.begin(); it != reapers_.end();) {
      if (it->done->load()) {
        if (it->thread.joinable()) it->thread.join();
        it = reapers_.erase(it);
      } else {
        ++it;
      }
    }
    auto done = std::make_shared<std::atomic<bool>>(false);
    Reaper r;
    r.done = done;
    r.thread = std::thread([raw, log = log_, done] {
      std::unique_ptr<Child> owned(raw);
      const DWORD w = WaitForSingleObject(owned->process, kEndMs);
      if (w != WAIT_OBJECT_0) {
        TerminateJobObject(owned->job, 1);  // its own job: this child only
        WaitForSingleObject(owned->process, 2000);
        log("managed viewer did not end in " + std::to_string(kEndMs) + " ms; its job was ended host=" + owned->hostId);
      }''')
rep('''      CloseHandle(owned->process);
      CloseHandle(owned->job);
    });
  }''', '''      CloseHandle(owned->process);
      CloseHandle(owned->job);
      owned.reset();
      done->store(true);
    });
    reapers_.push_back(std::move(r));
  }''')
rep('''  std::mutex reapMu_;
  std::vector<std::thread> reapers_;''', '''  struct Reaper {
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> done;
  };
  std::mutex reapMu_;
  std::vector<Reaper> reapers_;''')
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')
