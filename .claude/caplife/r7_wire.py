R = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'
NS = 'remote60::native_poc::e2e::'


def load(name):
    p = R + name
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    return p, s.replace('\r\n', '\n'), crlf


def save(p, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', p.rsplit('/', 1)[-1])


def one(s, a, b):
    assert s.count(a) == 1, (a[:100], s.count(a))
    return s.replace(a, b)


# ---- guard: Finalize idempotent
p, s, crlf = load('e2e_injection_guard.hpp')
s = one(s, '''  void Finalize(bool outputComplete) {
    StopPolling();''', '''  void Finalize(bool outputComplete) {
    StopPolling();
    {
      std::lock_guard<std::mutex> lock(st_->mu);
      if (st_->finalized) return;  // once
    }''')
save(p, s, crlf)

STOP_FIRST = '    if (injMonitor) injMonitor->Stop();  // r6 G1: its last judgement, before it goes\n'
STOP_FIRST_NEW = '    if (injMonitor) injMonitor->StopPolling();  // r7 N1: polling ends; the final judgement comes after the host and its reader\n'

ORDER = '''    // r7 N1: the producer is ended (its job: every process of it, confirmed), its output read to the end (the reader
    // gets EOF once the last writer is gone), THEN the final snapshot is judged. An end not confirmed: the read is cut
    // and the output is NOT complete -- the final judgement says so (INVALID).
    bool outputComplete = false;
    if (job) {
      outputComplete = ''' + NS + '''e2e_end_job_confirmed(job);
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

# standby: reading=false, the dump block, then job / readEnd / join
p, s, crlf = load('host_standby_admission_e2e_test.cpp')
s = one(s, STOP_FIRST + '    reading = false;\n', STOP_FIRST_NEW)
s = one(s, '''    if (job) {
      CloseHandle(job);  // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE takes the host with it
      job = nullptr;
    }
    if (readEnd) {
      CloseHandle(readEnd);
      readEnd = nullptr;
    }
    if (reader.joinable()) reader.join();
''', ORDER)
save(p, s, crlf)

p, s, crlf = load('host_punch_reply_e2e_test.cpp')
s = one(s, STOP_FIRST + '''    reading = false;
    if (job) {
      CloseHandle(job);  // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE takes the host with it
      job = nullptr;
    }
    if (readEnd) {
      CloseHandle(readEnd);
      readEnd = nullptr;
    }
    if (reader.joinable()) reader.join();
''', STOP_FIRST_NEW + ORDER)
save(p, s, crlf)

p, s, crlf = load('host_reboot_delegate_e2e_test.cpp')
s = one(s, STOP_FIRST + '''    reading = false;
    if (job) {
      CloseHandle(job);
      job = nullptr;
    }
    if (readEnd) {
      CloseHandle(readEnd);
      readEnd = nullptr;
    }
    if (reader.joinable()) reader.join();
''', STOP_FIRST_NEW + ORDER)
save(p, s, crlf)

# migration: job close, then the process wait, then readEnd / join
p, s, crlf = load('host_migration_e2e_test.cpp')
s = one(s, STOP_FIRST + '''    reading = false;
    if (job) {
      CloseHandle(job);  // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE takes the host with it
      job = nullptr;
    }
    bool exited = true;
    if (pi.hProcess) {
      exited = WaitForSingleObject(pi.hProcess, 20000) == WAIT_OBJECT_0;
      CloseHandle(pi.hProcess);
    }
    if (readEnd) {
      CloseHandle(readEnd);
      readEnd = nullptr;
    }
    if (reader.joinable()) reader.join();
''', STOP_FIRST_NEW + ORDER + '''    bool exited = true;
    if (pi.hProcess) {
      exited = WaitForSingleObject(pi.hProcess, 20000) == WAIT_OBJECT_0;
      CloseHandle(pi.hProcess);
    }
''')
save(p, s, crlf)

# single-host (log file): polling ends, the job ended (confirmed), the complete log judged, then the job closes
for f in ['android_session_reboot_e2e_test.cpp', 'bwe_observe_e2e_test.cpp', 'host_control_deadline_e2e_test.cpp',
          'host_two_dispatcher_e2e_test.cpp']:
    p, s, crlf = load(f)
    s = one(s, '''  if (injMon) injMon->Stop();  // r6 G1: its last judgement, before the job closes
  CloseHandle(job);
''', '''  // r7 N1: polling ends, the host's job is ended and that end confirmed (its log file then complete), THEN the final
  // snapshot is judged; then the job closes
  if (injMon) injMon->StopPolling();
  const bool hostEnded = ''' + NS + '''e2e_end_job_confirmed(job);
  if (injMon) {
    injMon->Finalize(hostEnded);
    injMon.reset();
  }
  CloseHandle(job);
''')
    save(p, s, crlf)
