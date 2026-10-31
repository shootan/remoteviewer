import io

p = 'apps/native_poc/src/updater_scenarios_test.cpp'
d = io.open(p, encoding='utf-8').read()

start = d.find('  // ============================================ 7. started, and nobody can say which process it is')
assert start != -1, 'scenario 7 start'
end = d.find('  // ================================== 8. the shell-routed client, on a path that commits')
assert end != -1 and end > start, 'scenario 7 end'

new = '''  // ================== 7. the unownable client can no longer be in the way of a rollback
  //
  // This scenario used to assert the opposite, and it was right at the time. The client came back
  // through the shell before the rollback, nothing could prove which process it was, and so the
  // rollback refused -- correctly, because restoring files a process may be holding produces an
  // installation that is part old and part new. The cost was real: the machine kept a build it
  // wanted to undo.
  //
  // The order removes the situation instead of handling it. The optional images start after the
  // commit, so at the moment a rollback is still possible there is no unownable process to be in
  // the way. Reclassifying after the fact could never have achieved this -- by the time the shell
  // fall-through is discovered the process already exists.

  {
    Knobs k;
    k.stopped = {hostTarget, clientTarget};
    k.clientOwnable = false;  // the token launch fails; the shell would take over
    k.newHealthOk = false;    // and the new build is unhealthy, so a rollback is wanted
    k.oldHealthOk = true;     // the version it goes back to is fine
    const Result r = run_scenario(k);
    check("7 unownable client: the rollback proceeds", r.outcome.result == UpdateResult::RolledBack,
          std::string(result_name(r.outcome.result)) + " / " + r.effectsError);
    check("7 unownable client: and it is NOT blocked", r.outcome.result != UpdateResult::RollbackFailed,
          result_name(r.outcome.result));
    check("7 unownable client: the old bytes really are back", r.hostBytes == oldBody,
          std::to_string(r.hostBytes.size()));
    // The reason it could proceed: nothing optional had been started when the decision was made.
    check("7 unownable client: no optional launch happened before the rollback",
          r.log.find("relaunch (optional)") == std::string::npos ||
              r.log.find("relaunch (optional)") > r.log.find("rollback"),
          r.log);
  }
  {
    // The counter-control for the ORDER, at the level where it can be seen: the same unownable
    // client on a path that commits. Here it does start -- after the commit -- and the update
    // stands. If the optional phase were still running before the commit, this and the case above
    // could not both hold.
    Knobs k;
    k.stopped = {hostTarget, clientTarget};
    k.clientOwnable = false;
    const Result r = run_scenario(k);
    check("7 counter-control: on a committing path the unownable client is started",
          r.log.find("started by the shell") != std::string::npos, r.log);
    check("7 counter-control: and the update stands",
          r.outcome.result == UpdateResult::Updated ||
              r.outcome.result == UpdateResult::UpdatedButNotRelaunched,
          std::string(result_name(r.outcome.result)) + " / " + r.outcome.detail);
  }

'''
d = d[:start] + new + d[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('scenario 7 now asserts the guarantee the order creates')
