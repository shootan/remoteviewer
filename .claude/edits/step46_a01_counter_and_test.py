import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
ROOT = r'D:\remote\remote'
S = ROOT + r'\apps\native_poc\src'
NL = chr(92) + 'n'   # literal \n for C++ strings

# --- workerCycles counter (status API, test seam) --------------------------------------------
edit(S + r'\log_upload.hpp', [
("  uint64_t lastRejectUs = 0;      // ... of the last 401\n};",
 "  uint64_t lastRejectUs = 0;      // ... of the last 401\n"
 "  uint64_t workerCycles = 0;      // worker loop iterations (wake-ups); a spin shows here first\n};"),
])
edit(S + r'\log_upload.cpp', [
("  uint64_t lastTransientDiagUs = 0;\n};",
 "  uint64_t lastTransientDiagUs = 0;\n  uint64_t workerCycles = 0;\n};"),
("      seenWake = s.wakeSeq;\n      finalPass = s.stopping;",
 "      seenWake = s.wakeSeq;\n      ++cycles;\n      s.workerCycles = cycles;\n      finalPass = s.stopping;"),
("        if (++cycles % 30 == 0) {",
 "        if (cycles % 30 == 0) {"),
("  s.lastTransientDiagUs = 0;\n}\n\nbool log_upload_running()",
 "  s.lastTransientDiagUs = 0;\n  s.workerCycles = 0;\n}\n\nbool log_upload_running()"),
("  st.lastRejectUs = s.lastRejectUs;",
 "  st.lastRejectUs = s.lastRejectUs;\n  st.workerCycles = s.workerCycles;"),
])

# --- test [10]: 401 pause with a queue past batchMaxBytes must not spin -----------------------
TEST = '''
// 10. A01 (stabilization audit U3 / probe Q1): paused on a 401 with MORE than batchMaxBytes queued.
//     The worker's wake condition "queue >= batch" was true forever while the pause forbade any
//     send, so the loop spun (one core, a diag line every 30 cycles: 772 KB/s of diag). Product
//     defaults for the batch size and the flush interval, because the [3] cap of 600 bytes never
//     reached the batch size and hid it. The pause must idle; a new token drains everything.
void test_401_pause_with_full_queue_does_not_spin(FakeLogServer& server) {
  std::printf("[10] 401 pause with queue >= batchMaxBytes: the worker idles, then drains on a new token" + std::string(1, 10));
  server.Clear();
  server.Script({401});
  LogUploadConfig c = base_config(server, "acct/machine-A");
  c.hostToken = "STALE";
  c.flushIntervalMs = 2000;              // product default
  c.batchMaxBytes = 192u * 1024u;         // product default
  c.queueCapBytes = 4u * 1024u * 1024u;   // product default
  std::string reason;
  CHECK(log_upload_configure(c, &reason));
  log_upload_enqueue("host", "first batch line");
  CHECK(server.WaitFor(1, 3000));
  CHECK(wait_until([] { return log_upload_status().authRejected; }, 3000));
  // ~200 KiB queued while paused: past batchMaxBytes, below the cap (nothing is dropped).
  const std::string line(99, 'x');
  for (int i = 0; i < 2100; ++i) log_upload_enqueue("host", line);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const LogUploadStatus before = log_upload_status();
  const size_t diagBefore = read_diag().size();
  FILETIME c0, e0, k0, u0;
  GetProcessTimes(GetCurrentProcess(), &c0, &e0, &k0, &u0);
  std::this_thread::sleep_for(std::chrono::milliseconds(2000));
  FILETIME c1, e1, k1, u1;
  GetProcessTimes(GetCurrentProcess(), &c1, &e1, &k1, &u1);
  const LogUploadStatus after = log_upload_status();
  const size_t diagAfter = read_diag().size();
  auto ft = [](const FILETIME& f) { return (static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
  const uint64_t cpuMs = ((ft(k1) - ft(k0)) + (ft(u1) - ft(u0))) / 10000;
  const uint64_t cyclesDelta = after.workerCycles - before.workerCycles;
  std::printf("  paused 2s: workerCycles +%llu diag +%zu bytes cpu %llu ms requests %zu" + std::string(1, 10),
              static_cast<unsigned long long>(cyclesDelta), diagAfter - diagBefore,
              static_cast<unsigned long long>(cpuMs), server.Count());
  CHECK(after.authRejected);
  CHECK(server.Count() == 1);              // still paused: nothing was sent
  CHECK(after.droppedLines == before.droppedLines);
  CHECK(cyclesDelta <= 5);                 // ~one wake per flush interval, not a spin
  CHECK(diagAfter - diagBefore <= 1024);   // no idle-alive flood
  CHECK(cpuMs < 500);                      // no busy loop on a core
  // A fresh token: the held batch goes out first, then the queue drains in batches.
  c.hostToken = "FRESH";
  CHECK(log_upload_configure(c, &reason));
  CHECK(reason.find("resumes after 401") != std::string::npos);
  CHECK(server.WaitFor(3, 6000));          // held batch + at least two queue batches
  const auto reqs = server.Requests();
  CHECK(reqs.size() >= 3);
  if (reqs.size() >= 3) {
    CHECK(reqs[0].hostToken() == "STALE");
    CHECK(reqs[1].hostToken() == "FRESH");
    CHECK(reqs[1].body.find("first batch line") != std::string::npos);
    CHECK(reqs[2].body.size() >= 100000);  // a full-size batch of the paused lines
  }
  CHECK(wait_until([] { return !log_upload_status().authRejected && log_upload_status().heldBatches == 0; }, 3000));
  log_upload_stop();
}
'''.replace('" + std::string(1, 10)', NL + '"')

edit(S + r'\log_upload_test.cpp', [
("void test_diag_has_no_token() {", TEST.lstrip('\n') + "\nvoid test_diag_has_no_token() {"),
("  test_diag_has_no_token();\n  server.Stop();",
 "  test_diag_has_no_token();\n  test_401_pause_with_full_queue_does_not_spin(server);\n  server.Stop();"),
])
print('A01 counter + test [10] ok')
