p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\managed_viewer_ipc.hpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c):
    global s
    assert s.count(a) == 1, (s.count(a), a[:80])
    s = s.replace(a, c)

rep('''  // Ends both threads: the write end is closed (the peer sees EOF), the reader is woken by closing the read end
  // and cancelling its synchronous read.
  void Close() {
    open_.store(false);
    cv_.notify_all();
    if (writer_.joinable()) {
      // The writer drains what is queued (a Bye, say). A peer that stopped reading must not hold this: after a
      // bounded wait its blocked write is cancelled.
      {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait_for(lock, std::chrono::milliseconds(1000), [&] { return writerDone_; });
        if (!writerDone_) CancelSynchronousIo(writer_.native_handle());
      }
      writer_.join();
    }
    if (write_) { CloseHandle(write_); write_ = nullptr; }
    if (reader_.joinable()) {
      CancelSynchronousIo(reader_.native_handle());
      if (read_) { CloseHandle(read_); read_ = nullptr; }
      reader_.join();
    }
    if (read_) { CloseHandle(read_); read_ = nullptr; }
  }
''', '''  // Ends both threads: the write end is closed (the peer sees EOF), the read end after the reader has stopped.
  //
  // ka-pc-impl r2 (Codex N5): bounded even when the peer stops reading. CancelSynchronousIo only cancels an I/O that
  // is ALREADY pending -- one call made a moment before the writer enters WriteFile finds nothing (ERROR_NOT_FOUND)
  // and the write that starts next blocks for good. So: past the drain deadline `abort_` is set (under mu_), every
  // WriteFile / ReadFile is started only after checking it, and the cancel is REPEATED until the thread says it is
  // done. Each thread can start at most one more I/O after abort is set, and that one is pending (cancellable) until
  // it ends, so the loop ends. A handle is closed only after the thread that used it has stopped -- never under it.
  void Close() {
    open_.store(false);
    cv_.notify_all();
    if (writer_.joinable()) {
      // The writer drains what is queued (a Bye, say) -- for up to a second.
      {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait_for(lock, std::chrono::milliseconds(1000), [&] { return writerDone_; });
        abort_ = true;
      }
      cancel_until_done(writer_, &writerDone_);
      writer_.join();
    }
    if (write_) { CloseHandle(write_); write_ = nullptr; }
    if (reader_.joinable()) {
      {
        std::lock_guard<std::mutex> lock(mu_);
        abort_ = true;
      }
      cancel_until_done(reader_, &readerDone_);
      reader_.join();
    }
    if (read_) { CloseHandle(read_); read_ = nullptr; }
  }
''')
rep('''  bool ReadExact(char* buf, DWORD n) {
    DWORD done = 0;
    while (done < n) {
      DWORD got = 0;
      if (!ReadFile(read_, buf + done, n - done, &got, nullptr) || got == 0) return false;
      done += got;
    }
    return true;
  }

  void ReadLoop() {
    for (;;) {''', '''  // Repeats CancelSynchronousIo on `t` until it reports done (see Close).
  void cancel_until_done(std::thread& t, bool* done) {
    for (;;) {
      {
        std::unique_lock<std::mutex> lock(mu_);
        if (cv_.wait_for(lock, std::chrono::milliseconds(10), [&] { return *done; })) return;
      }
      CancelSynchronousIo(t.native_handle());
    }
  }

  bool aborted() {
    std::lock_guard<std::mutex> lock(mu_);
    return abort_;
  }

  bool ReadExact(char* buf, DWORD n) {
    DWORD done = 0;
    while (done < n) {
      DWORD got = 0;
      if (aborted()) return false;
      if (!ReadFile(read_, buf + done, n - done, &got, nullptr) || got == 0) return false;
      done += got;
    }
    return true;
  }

  void ReadLoop() {
    struct Done {
      Channel* c;
      ~Done() {
        std::lock_guard<std::mutex> lock(c->mu_);
        c->readerDone_ = true;
        c->cv_.notify_all();
      }
    } readerDone{this};
    for (;;) {''')
rep('''      DWORD done = 0;
      while (done < frame.size()) {
        DWORD put = 0;
        if (!WriteFile(''', '''      DWORD done = 0;
      while (done < frame.size()) {
        DWORD put = 0;
        if (aborted()) return;  // past the drain deadline: no new write starts
        if (!WriteFile(''')
rep('''  bool writerDone_ = false;
''', '''  bool writerDone_ = false;
  bool readerDone_ = false;
  bool abort_ = false;
''')
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')
