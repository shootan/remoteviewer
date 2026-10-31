p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/os_input_record_e2e_test.cpp'
s = open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
if crlf:
    s = s.replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep('''    uint64_t epoch = 0;
    {
      const std::string out = log();
      const size_t at = out.rfind("[native-video-host][control] session epoch=");
      if (at != std::string::npos) epoch = e2e::field_u64(out.substr(at), "session epoch=");
    }
    check("this session's epoch, from the host's own line", epoch != 0, std::to_string(epoch));''',
    '''    // r10 D1 (verifier VERIFY_RECORD_ba2eaf2 r1): this session's epoch, as the host's source says it is. A LATER session
    // prints "[control] session epoch=N" (host_startup_control.cpp, AdmitHello with startsSession). The FIRST session --
    // this one: a token-less Hello on loopback -- is admitted in host_startup_connect.cpp without AdmitHello and without
    // an epoch line ("client connected transport=" only); its epoch is the initial clientSession.epoch, 1
    // (host_session.hpp: std::atomic<uint64_t> epoch{1}). Read so; anything else is not readable: INVALID, not FAIL.
    uint64_t epoch = 0;
    std::string epochFrom;
    {
      const std::string out = log();
      const size_t at = out.rfind("[native-video-host][control] session epoch=");
      size_t connected = 0;
      for (size_t i = out.find("[native-video-host] client connected transport="); i != std::string::npos;
           i = out.find("[native-video-host] client connected transport=", i + 1)) {
        ++connected;
      }
      if (at != std::string::npos) {
        epoch = e2e::field_u64(out.substr(at), "session epoch=");
        epochFrom = "a later session's [control] session epoch= line";
      } else if (connected == 1 && out.find("[native-video-host][standby] first session by activation") == std::string::npos) {
        epoch = 1;
        epochFrom = "the first session (one 'client connected', no epoch line): the initial epoch 1";
      }
    }
    if (epoch == 0) {
      staging.set_keep(true, "its host.log kept: this session's epoch could not be read");
      TerminateJobObject(job, 0);
      WaitForSingleObject(hostPi.hProcess, 10000);
      std::printf("FAIL  SETUP os_input_record: this session's epoch could not be read from the host's output -- the run "
                  "is INVALID (not a FAIL of the product); host.log kept at %ls\\nRESULT: INVALID\\n", hostLogPath.c_str());
      std::fflush(stdout);
      std::_Exit(2);
    }
    check("this session's epoch, as the host's source says it is", epoch != 0, std::to_string(epoch) + " (" + epochFrom + ")");''')

rep('''  CloseHandle(job);
  staging.Remove();
  WSACleanup();''', '''  CloseHandle(job);
  // r10 D1: a failed run keeps its staging (the host's own log is the evidence); a passed one is removed
  if (gFailures != 0) {
    staging.set_keep(true, "kept: the run failed");
    std::printf("NOTE  the run failed: its host.log is kept at %ls\\n", hostLogPath.c_str());
  }
  staging.Remove();
  WSACleanup();''')

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
