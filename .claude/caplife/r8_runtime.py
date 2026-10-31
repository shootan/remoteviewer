p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/e2e_guard_runtime_test.cpp'
s = open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
if crlf:
    s = s.replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:100], s.count(a))
    s = s.replace(a, b)


rep('''//   N2:  a case's contradiction ends the WHOLE test''', '''//   r8 N1: the final judgement needs the producer's end AND a confirmed EOF AND the OFF lines in that final text: a
//        missing log file, a read error part-way (a message pipe's ERROR_MORE_DATA), a cut reader, a complete but
//        empty output, an unconfirmed producer end -- each INVALID; a log file / pipe read to EOF with the OFF lines
//        passes (the positive controls). The job is borrowed: dropped at Finalize / destruction.
//   r8 N2: the startup-fault verdict (fatal's --prelogin worker): the input line may be absent, power OFF may not;
//        any contradiction, an incomplete read or an unconfirmed end refuses.
//   N2:  a case's contradiction ends the WHOLE test''')

# ---- helpers in the child: files and pipes
rep('''// ---- the child modes
int sleeper() {''', '''// r8: a file of this test (its own temp folder) with `text`; empty path on failure
std::wstring temp_file(const char* tag, const std::string& text) {
  wchar_t dir[MAX_PATH] = {};
  GetTempPathW(MAX_PATH, dir);
  const std::wstring path = std::wstring(dir) + L"e2e_guard_runtime_" + std::wstring(tag, tag + strlen(tag)) + L"_" +
                            std::to_wstring(GetCurrentProcessId()) + L".log";
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
  if (h == INVALID_HANDLE_VALUE) return {};
  DWORD put = 0;
  const bool ok = text.empty() || (WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &put, nullptr) && put == text.size());
  CloseHandle(h);
  return ok ? path : std::wstring();
}

// r8: a message-mode pipe holding one message larger than a read -- ReadFile hands part of it with ERROR_MORE_DATA
// (a read error after some bytes that is not an end of output)
e::E2eFinalOutput partial_pipe_read(const std::string& message, e::E2eReadEnd* endOut, DWORD* errOut) {
  const std::wstring name = L"\\\\.\\pipe\\e2e_guard_runtime_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
                            std::to_wstring(GetTickCount64());
  HANDLE srv = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_OUTBOUND, PIPE_TYPE_MESSAGE | PIPE_WAIT, 1, 1 << 16, 1 << 16, 0, nullptr);
  HANDLE cli = CreateFileW(name.c_str(), GENERIC_READ | FILE_WRITE_ATTRIBUTES, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  DWORD mode = PIPE_READMODE_MESSAGE;
  SetNamedPipeHandleState(cli, &mode, nullptr, nullptr);
  DWORD put = 0;
  WriteFile(srv, message.data(), static_cast<DWORD>(message.size()), &put, nullptr);
  std::string text;
  DWORD err = 0;
  const e::E2eReadEnd end = e::e2e_read_to_end(cli, nullptr, [&](const char* p, size_t n) { text.append(p, n); }, &err);
  CloseHandle(cli);
  CloseHandle(srv);
  if (endOut) *endOut = end;
  if (errOut) *errOut = err;
  return e::e2e_final_pipe(end, err, text);
}

// r8: an anonymous pipe written then its writer closed -- read to its real end (broken pipe = EOF)
e::E2eFinalOutput eof_pipe_read(const std::string& text, e::E2eReadEnd* endOut) {
  HANDLE rd = nullptr, wr = nullptr;
  CreatePipe(&rd, &wr, nullptr, 1 << 16);
  DWORD put = 0;
  WriteFile(wr, text.data(), static_cast<DWORD>(text.size()), &put, nullptr);
  CloseHandle(wr);
  std::string got;
  DWORD err = 0;
  const e::E2eReadEnd end = e::e2e_read_to_end(rd, nullptr, [&](const char* p, size_t n) { got.append(p, n); }, &err);
  CloseHandle(rd);
  if (endOut) *endOut = end;
  return e::e2e_final_pipe(end, err, got);
}

// ---- the child modes
int sleeper() {''')

rep('''  if (kind == "tail_finalize" || kind == "tail_live" || kind == "abandoned" || kind == "incomplete" || kind == "quiet") {''',
    '''  // r8 N1: the final snapshot itself not confirmed / not holding the OFF lines -- each INVALID at Finalize
  if (kind == "missing_file" || kind == "empty_file" || kind == "pipe_partial" || kind == "pipe_cut" ||
      kind == "producer_unconfirmed" || kind == "quiet_file" || kind == "quiet_pipe") {
    auto mon = std::make_unique<e::E2eInjectionMonitor>(get, job, "runtime-" + kind);
    Sleep(300);
    mon->StopPolling();
    std::printf("PHASE_POLLING_STOPPED\\n");  // polling saw nothing wrong: only the final judgement can refuse
    std::fflush(stdout);
    if (kind == "missing_file") {
      mon->Finalize(true, e::e2e_final_file(L"C:\\\\e2e-guard-runtime-no-such-dir\\\\host.log"));  // Codex's reproduction
    } else if (kind == "empty_file") {
      const std::wstring f = temp_file("empty", "");  // opens and reads to EOF -- 0 bytes, no OFF line
      const e::E2eFinalOutput fin = e::e2e_final_file(f);
      DeleteFileW(f.c_str());
      std::printf("EMPTY_COMPLETE=%d BYTES=%zu\\n", fin.complete ? 1 : 0, fin.text.size());
      mon->Finalize(true, fin);
    } else if (kind == "pipe_partial") {
      e::E2eReadEnd end = e::E2eReadEnd::Running;
      DWORD err = 0;
      const e::E2eFinalOutput fin = partial_pipe_read(offLines() + std::string(20000, 'x'), &end, &err);
      std::printf("PARTIAL end=%d err=%lu bytes=%zu\\n", static_cast<int>(end), err, fin.text.size());
      mon->Finalize(true, fin);  // the OFF lines are IN the part read -- still not complete
    } else if (kind == "pipe_cut") {
      mon->Finalize(true, e::e2e_final_pipe(e::E2eReadEnd::Cut, 0, get()));  // a reader stopped by its owner (joined)
    } else if (kind == "producer_unconfirmed") {
      mon->Finalize(false, e::e2e_final_pipe(e::E2eReadEnd::Eof, 0, get()));  // the text complete, its producer not ended
    } else {
      e::E2eFinalOutput fin;
      if (kind == "quiet_file") {
        const std::wstring f = temp_file("quiet", offLines());
        fin = e::e2e_final_file(f);
        DeleteFileW(f.c_str());
      } else {
        e::E2eReadEnd end = e::E2eReadEnd::Running;
        fin = eof_pipe_read(offLines(), &end);
        std::printf("EOF_PIPE end=%d\\n", static_cast<int>(end));
      }
      mon->Finalize(true, fin);
      e::e2e_injection_final_check_all();
      std::printf("NO_CONTRADICTION\\n");
      TerminateJobObject(job, 0);
      return 0;
    }
    std::printf("MISSED\\n");
    return 0;
  }
  if (kind == "tail_finalize" || kind == "tail_live" || kind == "abandoned" || kind == "quiet") {''')

rep('''      mon->Finalize(true);        // the complete output judged''',
    '''      mon->Finalize(true, e::e2e_final_pipe(e::E2eReadEnd::Eof, 0, get()));  // the complete output judged''')
rep('''    } else if (kind == "incomplete") {
      mon->Finalize(false);       // the end of its output not confirmed
    } else {  // quiet''', '''    } else {  // quiet''')
rep('''      mon->Finalize(true);
      e::e2e_injection_final_check_all();''', '''      mon->Finalize(true, e::e2e_final_pipe(e::E2eReadEnd::Eof, 0, get()));
      e::e2e_injection_final_check_all();''')

# ---- the parent: invalid_run variants
rep('''bool invalid_run(const Run& r) {
  return r.code == 2 && has(r, "FAIL  RUN runtime-") && has(r, "RESULT: INVALID") && !has(r, "MISSED") &&
         has(r, "ended (confirmed)") && sleeper_gone(r.out);
}''', '''bool invalid_run(const Run& r) {
  return r.code == 2 && has(r, "FAIL  RUN runtime-") && has(r, "RESULT: INVALID") && !has(r, "MISSED") &&
         has(r, "ended (confirmed)") && sleeper_gone(r.out);
}
// r8 N1: a launch whose monitor is gone no longer holds its job (borrowed): INVALID, the end said NOT confirmed by the
// guard -- the child still goes with the job's last handle (kill-on-close at the test's exit)
bool invalid_run_job_released(const Run& r) {
  return r.code == 2 && has(r, "FAIL  RUN runtime-") && has(r, "RESULT: INVALID") && !has(r, "MISSED") &&
         has(r, "NOT confirmed ended") && sleeper_gone(r.out);
}''')

rep('''    check("a launch destroyed without its final judgement: INVALID at the pre-result check",
          invalid_run(r) && has(r, "never judged on its complete output"), "exit " + std::to_string(r.code));
  }
  {
    const Run r = run_self(L"--monitor-case incomplete");
    check("an output not known complete (end / EOF not confirmed): INVALID",
          invalid_run(r) && has(r, "not known complete"), "exit " + std::to_string(r.code));
  }''', '''    check("a launch destroyed without its final judgement: INVALID at the pre-result check (its borrowed job no longer "
          "touched: said NOT confirmed; the child goes with the job's close)",
          invalid_run_job_released(r) && has(r, "never judged on its complete output"), "exit " + std::to_string(r.code));
  }

  std::printf("\\n--- r8 N1: the final snapshot -- producer end, EOF, OFF lines, all confirmed ---\\n");
  {
    const Run r = run_self(L"--monitor-case missing_file");
    check("a log file that does not exist (Codex's reproduction: initial OFF 1, final 0 bytes): INVALID, not \\"complete\\"",
          invalid_run(r) && has(r, "could not be opened") && !has(r, "judged on their complete"), "exit " + std::to_string(r.code));
  }
  {
    const Run r = run_self(L"--monitor-case empty_file");
    check("a log read to EOF but empty (0 bytes, no OFF line): INVALID -- complete is not enough",
          invalid_run(r) && has(r, "EMPTY_COMPLETE=1 BYTES=0") && has(r, "does not hold the initial OFF lines"),
          "exit " + std::to_string(r.code));
  }
  {
    const Run r = run_self(L"--monitor-case pipe_partial");
    check("a read error part-way (message pipe, ERROR_MORE_DATA after 8192 bytes; the OFF lines in the part read): INVALID",
          invalid_run(r) && has(r, "PARTIAL end=2 err=234 bytes=8192") && has(r, "read error (error 234) after 8192 bytes"),
          "exit " + std::to_string(r.code));
  }
  {
    const Run r = run_self(L"--monitor-case pipe_cut");
    check("a reader cut by its owner (joined, no EOF): INVALID -- a join is not an end of output",
          invalid_run(r) && has(r, "read cut by its owner"), "exit " + std::to_string(r.code));
  }
  {
    const Run r = run_self(L"--monitor-case producer_unconfirmed");
    check("the text complete but its producer's end not confirmed: INVALID",
          invalid_run(r) && has(r, "producer's end is not confirmed"), "exit " + std::to_string(r.code));
  }
  {
    const Run f = run_self(L"--monitor-case quiet_file");
    check("positive control: a log file read to EOF holding the OFF lines -- passes, not stopped",
          f.code == 0 && has(f, "NO_CONTRADICTION") && !has(f, "RESULT: INVALID") && has(f, "1 judged on their complete output"),
          "exit " + std::to_string(f.code));
    const Run p = run_self(L"--monitor-case quiet_pipe");
    check("positive control: an anonymous pipe read to its broken-pipe end (EOF) holding the OFF lines -- passes",
          p.code == 0 && has(p, "EOF_PIPE end=1") && has(p, "NO_CONTRADICTION") && !has(p, "RESULT: INVALID"),
          "exit " + std::to_string(p.code));
  }
  {
    // the read helper's own ends, in this process (no guard exit involved)
    const std::wstring wf = temp_file("writeonly", "abc");
    HANDLE w = CreateFileW(wf.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    DWORD err = 0;
    std::string got;
    const e::E2eReadEnd end = e::e2e_read_to_end(w, nullptr, [&](const char* p, size_t n) { got.append(p, n); }, &err);
    if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
    DeleteFileW(wf.c_str());
    check("a handle that cannot be read (write-only): Error with its code, never EOF",
          end == e::E2eReadEnd::Error && err != 0, "end=" + std::to_string(static_cast<int>(end)) + " err=" + std::to_string(err));
    const e::E2eFinalOutput miss = e::e2e_final_file(L"C:\\\\e2e-guard-runtime-no-such-dir\\\\host.log");
    check("e2e_final_file on a missing file: not complete, 0 bytes, why said", !miss.complete && miss.text.empty() &&
          miss.why.find("could not be opened") != std::string::npos, miss.why);
    HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);  // any open handle: the owner stops before a read
    const e::E2eReadEnd cut = e::e2e_read_to_end(ev, [] { return false; }, [](const char*, size_t) {}, &err);
    CloseHandle(ev);
    check("a read whose owner says stop: Cut", cut == e::E2eReadEnd::Cut);
  }

  std::printf("\\n--- r8 N2: the startup-fault verdict (fatal's --prelogin worker) ---\\n");
  {
    const std::string power = "[native-video-host] " + e::e2e_power_off_line() + "\\n";
    const std::string fault = "FATAL: std::exception \\"x\\" at step=startup_configure_from_env -- exit 49\\n";
    auto fin = [](const std::string& t) { return e::e2e_final_pipe(e::E2eReadEnd::Eof, 0, t); };
    bool inputSaid = true;
    std::string v = e::e2e_startup_fault_verdict(true, fin(power + fault), &inputSaid);
    check("power OFF said, the input line absent (a fault before the input path): accepted, input absent said",
          v.empty() && !inputSaid, v);
    v = e::e2e_startup_fault_verdict(true, fin(offLines() + fault), &inputSaid);
    check("power OFF and input OFF both said: accepted, input said", v.empty() && inputSaid, v);
    struct Neg {
      const char* what;
      bool ended;
      e::E2eFinalOutput f;
      const char* why;
    };
    const Neg negs[] = {
        {"no power line (0 bytes, a missing / empty log)", true, fin(""), "does not say power management OFF"},
        {"only the input line, no power line", true, fin("[native-video-host] " + std::string(e::e2e_injection_off_line()) + "\\n" + fault),
         "does not say power management OFF"},
        {"power ON", true, fin(std::string(kPowerOn) + fault), "not OFF"},
        {"power OFF then ON", true, fin(power + kPowerOn), "not OFF"},
        {"another input mode", true, fin(power + "[native-video-host] input injection disabled (unsupported mode) mode=bogus\\n"),
         "contradict"},
        {"input taken with injection on", true, fin(power + "x injectionEnabled=1 y\\n"), "injectionEnabled=1"},
        {"a read error part-way", true, e::e2e_final_pipe(e::E2eReadEnd::Error, 234, power), "not known complete"},
        {"a missing file", true, e::e2e_final_file(L"C:\\\\e2e-guard-runtime-no-such-dir\\\\w.log"), "could not be opened"},
        {"the worker's end not confirmed", false, fin(power + fault), "producer's end is not confirmed"},
    };
    for (const Neg& n : negs) {
      const std::string w = e::e2e_startup_fault_verdict(n.ended, n.f, nullptr);
      check((std::string("startup fault refused: ") + n.what).c_str(), !w.empty() && w.find(n.why) != std::string::npos, w);
    }
  }''')

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
