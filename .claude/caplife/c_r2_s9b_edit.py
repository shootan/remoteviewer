p = r'C:\work\gnlink-wt\apk-inbox-c\apps\native_poc\src\android_decoder_inbox_session_test.cpp'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')
a = s.index('  std::puts("\\n--- [S9]')
b = s.index('  std::printf("\\nandroid_decoder_inbox_session_test: %d checks')
new = r'''  std::puts("\n--- [S9] C r2 N2: a publish is ONE section -- the scheduler cannot take the action between the limiter and the descriptor");
  {
    Rig r;
    const bool up = r.Up(91);
    check("[S9] connected and ready", up);
    if (up) {
      r.host.ignoreKeyRequests = true;
      // a publish from THIS (a third) thread -- not the control thread, whose own pass could never race its own
      // scheduler. Held inside the publish up to 400 ms: one section -> the scheduler's take waits for us (nothing
      // cancelled meanwhile); a split section -> the scheduler takes the action before its descriptor -> cancelled.
      const auto me = std::this_thread::get_id();
      std::atomic<int> fired{0};
      std::atomic<uint64_t> cancelledDuring{0};
      const uint64_t cancel0 = r.c.KeyRequestsDroppedCancelled();
      r.c.SetTestKeyRequestHook([&](const char* point) {
        if (std::strcmp(point, "publish") != 0 || std::this_thread::get_id() != me || fired.fetch_add(1) != 0) return;
        const auto w0 = Clock::now();
        while (r.c.KeyRequestsDroppedCancelled() == cancel0 && ms_between(w0, Clock::now()) < 400)
          std::this_thread::sleep_for(std::chrono::milliseconds(2));
        cancelledDuring = r.c.KeyRequestsDroppedCancelled() - cancel0;
      });
      const auto t0 = Clock::now();
      const bool queued = r.c.TestRequestKey(kKeyCausePipeline);
      std::this_thread::sleep_for(std::chrono::milliseconds(1500));
      const auto reqs = r.host.RequestsSinceMs(t0);
      check("[S9] a pipeline ask published from another thread goes WITH its descriptor: nothing cancelled while the "
            "publish was open; the host got it",
            queued && fired.load() == 1 && cancelledDuring.load() == 0 && r.c.KeyRequestsDroppedCancelled() == cancel0 &&
                !reqs.empty(),
            "queued=" + std::to_string(queued ? 1 : 0) + " hook=" + std::to_string(fired.load()) + " cancelledDuring=" +
                std::to_string(cancelledDuring.load()) + " cancelled+" +
                std::to_string(r.c.KeyRequestsDroppedCancelled() - cancel0) + " host requests=" + list(reqs));
      r.c.SetTestKeyRequestHook(nullptr);
    }
    r.Down();
  }

'''
s = s[:a] + new + s[b:]
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
