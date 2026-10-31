p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/host_fatal_exception_test.cpp'
s = open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
if crlf:
    s = s.replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep('''  const std::wstring log = gRun + L"fatal_" + tag + L".log";
  SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", step.empty() ? nullptr : step.c_str());''',
    '''  const std::wstring log = gRun + L"fatal_" + tag + L".log";
  SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", step.empty() ? nullptr : step.c_str());
  // r10 D2 (verifier VERIFY_RECORD_ba2eaf2 r1): the stream build takes --codec h264 only with the encoded experiment on
  // (host_startup_config.cpp: "unsupported codec: h264 ... REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1", exit 11) -- set
  // here, in the environment the child's block is built from, so the normal control takes the real normal path
  SetEnvironmentVariableA("REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", "1");''')
rep('''  CloseHandle(h);
  SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", nullptr);
  *out = final_output_or_invalid(tag, log);  // r8 N1''', '''  CloseHandle(h);
  SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", nullptr);
  SetEnvironmentVariableA("REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", nullptr);
  *out = final_output_or_invalid(tag, log);  // r8 N1''')

rep('''  code = run(dir, L"none", "", &out, 30000);
  check("normal control (nothing injected): no FATAL line", out.find("FATAL:") == std::string::npos, "exit " + std::to_string(code));''',
    '''  code = run(dir, L"none", "", &out, 30000);
  // r10 D2: a normal control is a NORMAL START, not merely "no FATAL line": it got past its configuration (no codec
  // refusal, no early exit), said both OFF lines (below), reached "waiting client", and ended by this test's own
  // deadline (killed, its end confirmed) or by itself with 0 -- anything else means the control did not take the normal
  // path: INVALID, not a pass and not a product FAIL.
  if (out.find("unsupported codec") != std::string::npos || out.find("FATAL:") != std::string::npos ||
      out.find("[native-video-host] waiting client bindPort=") == std::string::npos ||
      !(code == 0xFFFFFFFFu || code == 0)) {
    fatal_invalid(L"none", "the normal control did not start normally (exit " + std::to_string(code) + ")");
  }
  check("normal control (nothing injected): a normal start -- configured, 'waiting client' reached, no FATAL line, ended "
        "by the test's deadline (or 0)",
        true, "exit " + std::to_string(code));''')

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
