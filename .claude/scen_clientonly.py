import io

p = 'apps/native_poc/src/updater_scenarios_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  // ================================================================ control: whose process is it'''
new = '''  // ============================================ 9. client-only: the host was not running at all
  //
  // Nothing required is in the plan, because the plan is built from what was actually stopped and
  // the host was never up. So there is nothing to bring back, nothing that writes a health report,
  // and nothing to wait for -- and waiting anyway would time out and roll back an update whose
  // files are perfectly correct, for the sole reason that a process was not running beforehand.

  {
    Knobs k;
    k.stopped = {clientTarget};  // the client only -- no host was running
    const Result r = run_scenario(k);
    check("9 client-only: the update completes", r.outcome.result == UpdateResult::Updated,
          std::string(result_name(r.outcome.result)) + " / " + r.outcome.detail);
    check("9 client-only: the new bytes are in place", r.hostBytes == newBody,
          std::to_string(r.hostBytes.size()));
    // No host was started, so nothing waited on a report nobody was going to write.
    check("9 client-only: nothing waited for a health report",
          r.log.find("nothing that reports health was relaunched") != std::string::npos ||
              r.log.find("health:") != std::string::npos,
          r.log);
    check("9 client-only: the host was never started, so it cannot have been duplicated",
          r.hostInstances == 0, std::to_string(r.hostInstances));
    // And the client did come back -- after the commit, as the logged-on user.
    check("9 client-only: the client came back", r.clientInstances >= 1,
          std::to_string(r.clientInstances) + " -- " + r.log);
  }

  // ================================================================ control: whose process is it'''
assert old in d, 'scenario 9 anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('client-only scenario on the real path')
