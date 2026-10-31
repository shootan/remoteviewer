p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\ka_pc_managed_e2e_test.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

i = s.index("  // ---------------------------------------------------------------- the shell's ManagedViewers (product module)\n")
j = s.index('  hostA.End();\n  if (haveOld) {')
module = s[i:j]
s = s[:i] + s[j:]
# the module goes before "release, then a job close"
anchor = '  // ---------------------------------------------------------------- release, then a job close\n'
assert s.count(anchor) == 1
# the module needs A released first (one device = one entry at a time): it runs after A's Release, before C
rel_end = '''    check("...and the Host took the Release (not refused as Live)", hl.find("release refused") == std::string::npos,
          hl.find("release refused") == std::string::npos ? "" : "release refused seen");
  }
'''
assert s.count(rel_end) == 1
s = s.replace(rel_end, rel_end + module)
# after the job close: the relaunch after a crash, reported as a finding (not counted)
job_end = '''    check("closing the job's last handle ends the child (KILL_ON_JOB_CLOSE)", c.proc.Exited(5000),
          std::to_string(now_ms() - t0) + " ms");
  }
'''
assert s.count(job_end) == 1
finding = r'''  {
    // FINDING (reported, not counted): right after a crash (the child above ended by its job, its entry and relay
    // lease left behind), a NEW viewer for the same PC key. Observed 10-28: the relay forwards its standby Hellos
    // and the Host answers none (relay "no StandbyAck", h2c=0; no standby line in the host log), so it ends as
    // "unsupported". Shared Host / relay code (not this task's files): measured here, handed over.
    Managed d;
    d.key = url + "\ntester\n" + idA;
    d.inc = "3";
    d.epoch = "3";
    if (d.Start(url, idA, "D")) {
      d.ch.Send([&] {
        Message m;
        m.type = Type::Init;
        m.fields = {{"url", url}, {"session", session}, {"tokenVersion", "1"}, {"hostId", idA}, {"hostName", "KA-A"},
                    {"key", d.key}, {"inc", d.inc}, {"epoch", d.epoch}, {"genDir", genDir}};
        return m;
      }());
      const int okReady = d.WaitMsg(Type::State, 0, 45000, [](const Message& m) {
        return m.get("state") == "ready" || m.get("state") == "unsupported";
      });
      const std::string got = okReady >= 0 ? d.At(okReady).get("state") : std::string("none in 45 s");
      std::printf("FINDING  a new viewer for the same PC right after a crash: %s\n", got.c_str());
      d.Send(Type::Release, {{"reason", "test"}});
      d.proc.Exited(8000);
    }
  }
'''
s = s.replace(job_end, job_end + finding)
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')
