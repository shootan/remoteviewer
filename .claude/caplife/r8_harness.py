import re

R = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'
NS = 'remote60::native_poc::e2e::'

OLD_STOP = '''    bool outputComplete = false;
    if (job) {
      outputComplete = remote60::native_poc::e2e::e2e_end_job_confirmed(job);
      CloseHandle(job);  // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE (already ended above when confirmed)
      job = nullptr;
    }
    if (!outputComplete) {
      reading = false;
      if (readEnd) {
        CloseHandle(readEnd);
        readEnd = nullptr;
      }
    }
    if (reader.joinable()) reader.join();
    if (readEnd) {
      CloseHandle(readEnd);
      readEnd = nullptr;
    }
    if (injMonitor) {
      injMonitor->Finalize(outputComplete);
      injMonitor.reset();
    }
'''
NEW_STOP = '''    // r8 N1: complete = the producer's end confirmed AND the reader's own EOF (not its join) AND the OFF lines in that
    // final text (e2e_final_verdict); the job stays open until the final judgement is made, then it closes.
    bool producerEnded = false;
    if (job) producerEnded = remote60::native_poc::e2e::e2e_end_job_confirmed(job);
    if (!producerEnded) {
      reading = false;
      if (readEnd) {
        CloseHandle(readEnd);
        readEnd = nullptr;
      }
    }
    if (reader.joinable()) reader.join();
    if (readEnd) {
      CloseHandle(readEnd);
      readEnd = nullptr;
    }
    if (injMonitor) {
      injMonitor->Finalize(producerEnded, remote60::native_poc::e2e::e2e_final_pipe(
                                              static_cast<remote60::native_poc::e2e::E2eReadEnd>(readResult.load()),
                                              readErr.load(), log()));
      injMonitor.reset();
    }
    if (job) {
      CloseHandle(job);  // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE (already ended above when confirmed)
      job = nullptr;
    }
'''
FIELDS = '''  std::atomic<bool> reading{false};
  // r8 N1: how the reader's read ended (E2eReadEnd) and its error -- EOF is the only "output complete"
  std::atomic<int> readResult{0};
  std::atomic<unsigned long> readErr{0};
'''


def edit(name, lock):
    p = R + name
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    assert s.count(OLD_STOP) == 1, name
    s = s.replace(OLD_STOP, NEW_STOP)
    a = '  std::atomic<bool> reading{false};\n'
    assert s.count(a) == 1, name
    s = s.replace(a, FIELDS)
    pat = re.compile(r'  host->reader = std::thread\(\[host\] \{\n    char buf\[4096\];\n    DWORD got = 0;\n    while \(host->reading\.load\(\) && host->readEnd &&\s*ReadFile\(host->readEnd, buf, sizeof\(buf\), &got, nullptr\) && got > 0\) \{\n      std::lock_guard<std::mutex> ' + lock + r'\(host->mu\);\n      host->output\.append\(buf, got\);\n    \}\n  \}\);\n')
    m = pat.findall(s)
    assert len(m) == 1, (name, len(m))
    s = pat.sub(lambda _: '''  host->reader = std::thread([host, h = host->readEnd] {
    // r8 N1: read to a confirmed end of output (EOF / broken pipe), any other end kept apart (e2e_read_to_end)
    DWORD err = 0;
    const auto end = remote60::native_poc::e2e::e2e_read_to_end(
        h, [host] { return host->reading.load(); },
        [host](const char* p, size_t n) {
          std::lock_guard<std::mutex> ''' + lock + '''(host->mu);
          host->output.append(p, n);
        },
        &err);
    host->readErr = err;
    host->readResult = static_cast<int>(end);
  });
''', s)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', name)


edit('host_migration_e2e_test.cpp', 'lock')
edit('host_punch_reply_e2e_test.cpp', 'lock')
edit('host_reboot_delegate_e2e_test.cpp', 'l')
edit('host_standby_admission_e2e_test.cpp', 'lock')

# ---- the flat (log file) harnesses: the final snapshot read with its end checked
for name in ['android_session_reboot_e2e_test.cpp', 'bwe_observe_e2e_test.cpp', 'host_control_deadline_e2e_test.cpp',
             'host_two_dispatcher_e2e_test.cpp']:
    p = R + name
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    a = '''  if (injMon) {
    injMon->Finalize(hostEnded);
    injMon.reset();
  }
  CloseHandle(job);'''
    assert s.count(a) == 1, name
    s = s.replace(a, '''  if (injMon) {
    // r8 N1: the final snapshot read to a confirmed EOF (an open failure / a read error is not complete), its OFF
    // lines judged; the job closes after
    injMon->Finalize(hostEnded, remote60::native_poc::e2e::e2e_final_file(hostLogPath));
    injMon.reset();
  }
  CloseHandle(job);''')
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', name)
