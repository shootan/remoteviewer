p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/host_fatal_exception_test.cpp'
s = open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
if crlf:
    s = s.replace('\r\n', '\n')
NS = 'remote60::native_poc::e2e::'


def rep(a, b, n=1):
    global s
    assert s.count(a) == n, (a[:100], s.count(a))
    s = s.replace(a, b)


# ---- run(): the final output read to a confirmed end; an unconfirmed end or an incomplete read ends the test INVALID
rep('''  CloseHandle(h);
  SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", nullptr);
  *out = read_all(log);
  return code;
}''', '''  CloseHandle(h);
  SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", nullptr);
  *out = final_output_or_invalid(tag, log);  // r8 N1
  return code;
}''')
rep('''  CloseHandle(h);
  for (const auto& kv : env) SetEnvironmentVariableA(kv.first.c_str(), nullptr);
  *out = read_all(log);
  return code;
}''', '''  CloseHandle(h);
  for (const auto& kv : env) SetEnvironmentVariableA(kv.first.c_str(), nullptr);
  *out = final_output_or_invalid(tag, log);  // r8 N1
  return code;
}''')

# the helper, after fatal_invalid
rep('''// r5 F4: the host said its power management OFF, whole''', '''// r8 N1: a case's final output, read to a confirmed EOF after its producer's end -- an end not confirmed (the case is
// in gKeepLogs) or a log that cannot be read whole (missing, a read error part-way) ends the whole test INVALID here,
// before anything of it is judged or deleted.
std::string final_output_or_invalid(const std::wstring& tag, const std::wstring& log) {
  if (gKeepLogs.count(tag)) fatal_invalid(tag, "its producer's end is not confirmed (its output may still grow)");
  const ''' + NS + '''E2eFinalOutput fin = ''' + NS + '''e2e_final_file(log);
  if (!fin.complete) fatal_invalid(tag, "its final output is not known complete: " + fin.why);
  return fin.text;
}

// r8 N2: a startup-fault case judged whole (e2e_startup_fault_verdict): power OFF said, whole; no contradiction; the
// input line may be absent (the fault comes before the input path) -- said which. Anything else: the whole test INVALID.
void startup_fault_judged(const std::wstring& tag, const std::string& label, bool producerEnded,
                          const ''' + NS + '''E2eFinalOutput& fin) {
  bool inputSaid = false;
  const std::string why = ''' + NS + '''e2e_startup_fault_verdict(producerEnded, fin, &inputSaid);
  if (!why.empty()) fatal_invalid(tag, label + ": " + why);
  check((label + ": its whole output (read to EOF, " + std::to_string(fin.text.size()) +
         " bytes): power management OFF whole, no input / power contradiction; the input line " +
         (inputSaid ? "said OFF" : "absent (a startup fault before the input path -- the allowed exception)"))
            .c_str(),
        true);
}

// r5 F4: the host said its power management OFF, whole''')

# fatal_invalid / check must be declared before final_output_or_invalid: check is earlier? (verified by the build)

# ---- std / winrt: the startup-fault verdict instead of a check(false) + the contradiction check
for tag in ['std', 'winrt']:
    rep('''  check("''' + tag + ''': the host said its power management OFF (r5 F4, the record)", said_power_off(out));
  {
    std::string why;  // r7 N2: a contradiction ends the whole test INVALID (no further launch, logs kept)
    if (''' + NS + '''e2e_injection_contradicted(out, &why)) fatal_invalid(L"''' + tag + '''", why);
    check("''' + tag + ''': no input / power contradiction in its whole output (r6 G1)", true);
  }''', '''  // r8 N2: power OFF not said is no longer a mere FAIL: the whole test INVALID (like a contradiction)
  startup_fault_judged(L"''' + tag + '''", "''' + tag + '''", true, ''' + NS + '''e2e_final_pipe(''' + NS + '''E2eReadEnd::Eof, 0, out));''')

# ---- none (the normal control): both lines required (e2e_final_verdict)
rep('''  {
    std::string why;  // r7 N2: a contradiction ends the whole test INVALID (no further launch, logs kept)
    if (''' + NS + '''e2e_injection_contradicted(out, &why)) fatal_invalid(L"none", why);
    check("normal control: no input / power contradiction in its whole output (r6 G1)", true);
  }''', '''  {
    // r8 N1: the normal control's whole output (read to EOF): both OFF lines, whole, and no contradiction -- else INVALID
    const std::string why = ''' + NS + '''e2e_final_verdict(true, ''' + NS + '''e2e_final_pipe(''' + NS + '''E2eReadEnd::Eof, 0, out));
    if (!why.empty()) fatal_invalid(L"none", why);
    check("normal control: no input / power contradiction in its whole output (r6 G1)", true);
  }''')

# ---- the --prelogin worker
rep('''      else workerGone = end_child_confirmed(pi.hProcess, 99);  // r6 F-b: confirmed, or its logs are kept below''',
    '''      else workerGone = end_child_confirmed(pi.hProcess, 99);  // r6 F-b: confirmed, or its logs are kept below
      if (!ended && !workerGone) gKeepLogs.insert(L"prelogin");  // r8 N2: said as NOT confirmed if it ends the test''')
rep('''    check("...the worker said its power management OFF (r5 F4, the record)", said_power_off(read_all(log)));''',
    '''    // r8 N2: BEFORE anything of it is deleted, its stdout and its own log are read whole and judged: power OFF said,
    // no contradiction (the input line may be absent: it throws before the input path), the end confirmed --
    // otherwise the whole test ends INVALID here, every log kept, no further launch (probe_trap never starts).
    {
      const bool workerEnded = ended || (workerGone && gKeepLogs.count(L"prelogin") == 0);
      startup_fault_judged(L"prelogin", "--prelogin worker (its stdout)", workerEnded, ''' + NS + '''e2e_final_file(log));
      const ''' + NS + '''E2eFinalOutput own = ''' + NS + '''e2e_final_file(logs + L"\\\\prelogin_worker.log");
      if (!own.complete) fatal_invalid(L"prelogin", "--prelogin worker (its own log): " + own.why);
      std::string why;
      if (''' + NS + '''e2e_injection_contradicted(own.text, &why)) fatal_invalid(L"prelogin", "--prelogin worker (its own log): " + why);
      check("...its own log, read whole: no input / power contradiction", true);
    }''')

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
