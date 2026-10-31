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


rep('''    } else if (a == L"--host-exe" && more) {
      hostExe = argv[++i];''', '''    } else if (a == L"--host-exe" && more) {
      hostExe = argv[++i];
    } else if (a == L"--run-id" && more) {
      runId = argv[++i];''')
rep('''  std::wstring hostExe = L"GNLinkStream.exe";''', '''  std::wstring hostExe = L"GNLinkStream.exe";
  std::wstring runId;  // keepalive-r2 e (AVD guard r2 N1/N2): directoryhost's readiness names the run it belongs to''')

# directoryhost: a run id, and no readiness of an earlier run in --out
rep('''  const bool directoryHost = scenario == "directoryhost";
''', '''  const bool directoryHost = scenario == "directoryhost";
  // AVD guard r2 (Codex codex_avd_guard_src_r1.md N1/N2): directoryhost writes its readiness ONLY once its host passed
  // the guard, naming this run, into an --out folder that holds none yet -- anything else: INVALID, nothing started.
  auto setup_invalid = [&](const std::string& why) {
    say("FAIL  SETUP mobile_abr: " + why + " -- no readiness written, no client\\nRESULT: INVALID");
    return 2;
  };
  if (directoryHost) {
    bool plainRun = !runId.empty() && runId.size() <= 64;
    for (wchar_t ch : runId) {
      plainRun = plainRun && ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') ||
                              ch == L'-' || ch == L'_');
    }
    if (!plainRun) return setup_invalid("directoryhost needs --run-id (ASCII letters, digits, - _)");
    if (outDir.empty()) return setup_invalid("directoryhost needs --out");
    if (GetFileAttributesW((outDir + L"directoryhost_ready.txt").c_str()) != INVALID_FILE_ATTRIBUTES) {
      return setup_invalid("--out already holds a readiness file (an earlier run's): not reused");
    }
  }
''')

# not staged: INVALID before any window or host
rep('''  check("the host could be staged", staged);
''', '''  check("the host could be staged", staged);
  if (!staged) return setup_invalid("the host could not be staged");  // before any window, any host
''')

# not launched: INVALID before any client
rep('''  check("the host started", launched);
  // fail closed (e2e_injection_guard.hpp): no client''', '''  check("the host started", launched);
  if (!launched) {  // AVD guard r2 N1: not started is no gate passed -- no readiness, no client, no relay
    if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);
    CloseHandle(job);
    return setup_invalid("the host did not start");
  }
  // fail closed (e2e_injection_guard.hpp): no client''')

# readiness: only now, atomically, naming the run, the harness, the host and its exact log
rep('''    if (!outDir.empty()) {
      CreateDirectoryW(outDir.c_str(), nullptr);
      std::ofstream(outDir + L"directoryhost_ready.txt") << (launched ? "ready" : "failed") << "\\n";
    }
    say(std::string("DIRECTORY_HOST ") + (launched ? "ready" : "failed"));''', '''    {
      // launched and the guard's gate passed (else this run already ended INVALID): the readiness, written whole
      // (a temporary renamed into place -- a reader never sees half of it), naming this run, this harness, its host and
      // the host's exact log (ke_avd.py reads that path, never a guess by pid)
      const std::wstring tmp = outDir + L"directoryhost_ready.tmp";
      {
        std::ofstream r(tmp, std::ios::binary | std::ios::trunc);
        r << "ready run=" << std::string(runId.begin(), runId.end()) << " harness_pid=" << GetCurrentProcessId()
          << " host_pid=" << hostPi.dwProcessId << " host_log=" << std::string(hostLogPath.begin(), hostLogPath.end())
          << "\\n";
      }
      if (!MoveFileExW(tmp.c_str(), (outDir + L"directoryhost_ready.txt").c_str(), MOVEFILE_WRITE_THROUGH)) {
        endHostJudged();
        return setup_invalid("the readiness could not be written");
      }
    }
    say("DIRECTORY_HOST ready");''')

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
