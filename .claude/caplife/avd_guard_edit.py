p = 'C:/work/gnlink-wt/feat-apk-0233/apps/native_poc/src/mobile_abr_session_e2e_test.cpp'
s = open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
if crlf:
    s = s.replace('\r\n', '\n')
E = 'remote60::native_poc::e2e::'


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:100], s.count(a))
    s = s.replace(a, b)


rep('#include "e2e_isolation.hpp"\n', '#include "e2e_isolation.hpp"\n#include "e2e_injection_guard.hpp"\n')

rep('''                       L" --seconds " + std::to_wstring(hostSec) + L" --input-injection-mode none" +''',
    '''                       L" --seconds " + std::to_wstring(hostSec) +
                       // the shared guard (e2e_injection_guard.hpp): input injection AND host power management off
                       ''' + E + '''e2e_injection_off_arg(dir) +''')

rep('''    if (launched) {
      AssignProcessToJobObject(job, hostPi.hProcess);
      ResumeThread(hostPi.hThread);
    }
  }
  check("the host started", launched);''', '''    if (launched) {
      // resumed only inside a verified kill-on-close job, else INVALID (the shared guard)
      ''' + E + '''e2e_adopt_or_invalid(job, hostPi, "mobile_abr");
    }
  }
  check("the host started", launched);
  // fail closed (e2e_injection_guard.hpp): no client -- the APK on the AVD for directoryhost -- reaches a host that has
  // not said, in THIS launch's own output, both whole OFF lines (input injection none, host power management off)
  std::string injWhy;
  const bool injOff = launched && ''' + E + '''e2e_wait_injection_off(
      [&] { return ''' + E + '''e2e_read_text_file(hostLogPath); }, 20000, &injWhy);
  if (launched && !injOff) ''' + E + '''e2e_injection_setup_failed("mobile_abr", injWhy);
  check("the host said its input injection and its power management OFF before any client", injOff, injWhy);
  // the same judgement keeps running while the host lives (a contradiction -> INVALID, its job ended)
  std::unique_ptr<''' + E + '''E2eInjectionMonitor> injMon;
  if (launched && injOff) {
    injMon = std::make_unique<''' + E + '''E2eInjectionMonitor>(
        [&] { return ''' + E + '''e2e_read_text_file(hostLogPath); }, job, "mobile_abr");
  }
  // the end, for every path: polling stops, the host's job is ended (confirmed or not), the log read to EOF, the final
  // snapshot judged (end + EOF + both OFF lines + no contradiction, else INVALID); the job closes only after that
  auto endHostJudged = [&] {
    if (injMon) injMon->StopPolling();
    const bool hostEnded = ''' + E + '''e2e_end_job_confirmed(job);
    if (hostLog != INVALID_HANDLE_VALUE) {
      CloseHandle(hostLog);
      hostLog = INVALID_HANDLE_VALUE;
    }
    if (injMon) {
      injMon->Finalize(hostEnded, ''' + E + '''e2e_final_file(hostLogPath));
      injMon.reset();
    }
    if (job) {
      CloseHandle(job);
      job = nullptr;
    }
  };''')

# the directoryhost end
rep('''    if (hostPi.hProcess) {
      TerminateProcess(hostPi.hProcess, 0);
      WaitForSingleObject(hostPi.hProcess, 5000);
      CloseHandle(hostPi.hProcess);
    }
    if (hostPi.hThread) CloseHandle(hostPi.hThread);
    if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);
    CloseHandle(job);
    if (launched) {''', '''    endHostJudged();  // the shared guard's end (replaces a bare TerminateProcess of the host alone)
    if (hostPi.hProcess) {
      WaitForSingleObject(hostPi.hProcess, 5000);
      CloseHandle(hostPi.hProcess);
    }
    if (hostPi.hThread) CloseHandle(hostPi.hThread);
    if (launched) {''')
rep('''    if (!staging.Remove()) say("WARN  " + staging.why());
    say(std::string("\\nmobile_abr_session_e2e_test: ") + (gFailures == 0 ? "PASS" : "FAIL") +''',
    '''    if (!staging.Remove()) say("WARN  " + staging.why());
    ''' + E + '''e2e_injection_final_check_all();  // every launch judged before the result
    say(std::string("\\nmobile_abr_session_e2e_test: ") + (gFailures == 0 ? "PASS" : "FAIL") +''')

# the general end
rep('''  if (hostPi.hProcess) {
    if (WaitForSingleObject(hostPi.hProcess, 90000) != WAIT_OBJECT_0) {
      say("NOTE  host did not exit by itself; terminated");
      TerminateProcess(hostPi.hProcess, 0);
      WaitForSingleObject(hostPi.hProcess, 5000);
    }
    CloseHandle(hostPi.hProcess);
  }
  if (hostPi.hThread) CloseHandle(hostPi.hThread);
  if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);
  CloseHandle(job);''', '''  if (hostPi.hProcess && WaitForSingleObject(hostPi.hProcess, 90000) != WAIT_OBJECT_0) {
    say("NOTE  host did not exit by itself; its job is ended");
  }
  endHostJudged();  // the shared guard's end: the job (every process of it), the log to EOF, the final judgement
  if (hostPi.hProcess) {
    WaitForSingleObject(hostPi.hProcess, 5000);
    CloseHandle(hostPi.hProcess);
  }
  if (hostPi.hThread) CloseHandle(hostPi.hThread);''')
rep('''  if (!staging.Remove()) say("WARN  " + staging.why());
  say("\\n" + std::to_string(gChecks) + " checks, "''', '''  if (!staging.Remove()) say("WARN  " + staging.why());
  ''' + E + '''e2e_injection_final_check_all();  // every launch judged before the result
  say("\\n" + std::to_string(gChecks) + " checks, "''')

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
