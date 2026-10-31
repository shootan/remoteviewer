import io

BS = chr(92)
p = 'apps/native_poc/src/updater_scenarios_test.cpp'
d = io.open(p, encoding='utf-8').read()

# ------------------------------------------------------------------ the diagnosis itself
old = '''/**
 * Processes carrying this suite's names that could NOT be placed'''
new = '''/**
 * Who is holding `path`, asked of Windows rather than inferred.
 *
 * READ ONLY -- Restart Manager is being used purely as a question. It exists so an installer can
 * ask "what would I have to close to replace this file", and it answers with the processes that
 * have the file open or are running it, including ones a process snapshot cannot explain.
 *
 * This is here because a backup kept surviving a commit with no visible reason: the delete
 * returned ACCESS_DENIED on a plain archive file with nothing running it that could be found by
 * enumerating processes. Guessing produced three wrong answers in a row -- a running image, a
 * stale image section, an open process handle -- and each was tested and disproved. Asking is
 * cheaper than any of them, and it is the difference between a note that says "unexplained" and
 * one that names a process.
 */
std::vector<std::wstring> holders_of(const std::wstring& path) {
  std::vector<std::wstring> holders;
  DWORD session = 0;
  WCHAR key[CCH_RM_SESSION_KEY + 1]{};
  if (RmStartSession(&session, 0, key) != ERROR_SUCCESS) return holders;

  LPCWSTR files[1] = {path.c_str()};
  if (RmRegisterResources(session, 1, files, 0, nullptr, 0, nullptr) == ERROR_SUCCESS) {
    UINT needed = 0;
    UINT count = 32;
    std::vector<RM_PROCESS_INFO> info(count);
    DWORD reason = 0;
    const DWORD rc = RmGetList(session, &needed, &count, info.data(), &reason);
    if (rc == ERROR_MORE_DATA) {
      info.assign(needed, RM_PROCESS_INFO{});
      count = needed;
      if (RmGetList(session, &needed, &count, info.data(), &reason) != ERROR_SUCCESS) count = 0;
    } else if (rc != ERROR_SUCCESS) {
      count = 0;
    }
    for (UINT i = 0; i < count; ++i) {
      std::wstring who = info[i].strAppName;
      if (who.empty()) who = L"(unnamed)";
      who += L"#" + std::to_wstring(info[i].Process.dwProcessId);
      holders.push_back(who);
    }
  }
  RmEndSession(session);
  return holders;
}

/**
 * Processes carrying this suite's names that could NOT be placed'''
assert old in d, 'holders anchor'
d = d.replace(old, new, 1)

# ------------------------------------------------------------------ report holders when one survives
old = '''      for (const std::wstring& name : r.orphaned) {
        check("a surviving backup carries the reason the delete failed",
              r.effectsError.find(narrow(name)) != std::string::npos &&
                  r.effectsError.find("error ") != std::string::npos,
              r.effectsError);
      }'''
new = '''      for (const std::wstring& name : r.orphaned) {
        // Asked, not guessed. Whoever holds it is named here.
        std::string who;
        for (const std::wstring& holder : holders_of(install + L"\\\\" + name + L".gnlink-old")) {
          if (!who.empty()) who += ", ";
          who += narrow(holder);
        }
        if (who.empty()) who = "(Restart Manager names nobody)";
        check("a surviving backup carries the reason the delete failed",
              r.effectsError.find(narrow(name)) != std::string::npos &&
                  r.effectsError.find("error ") != std::string::npos,
              r.effectsError + " -- held by: " + who);
      }'''
assert old in d, 'orphan reason check'
d = d.replace(old, new, 1)

# ------------------------------------------------------------------ include the header
old = '''#include <windows.h>'''
new = '''#include <windows.h>

// Restart Manager, used read-only: it is the supported way to ask which processes hold a file.
#include <RestartManager.h>'''
assert old in d, 'windows include'
d = d.replace(old, new, 1)

# ------------------------------------------------------------------ a scenario that commits with an unowned client
old = '''  // ================================================================ control: whose process is it'''
new = '''  // ================================== 8. the shell-routed client, on a path that commits
  //
  // Scenario 2 with the owning launch taken away: the client comes back through the shell, the
  // host is healthy, and the update commits. This is the shape the surviving `.gnlink-old` was
  // first seen in, and it is reproduced here rather than left to a machine -- with Restart
  // Manager asked directly about anything that survives, so the answer is a name and not a guess.

  {
    Knobs k;
    k.stopped = {hostTarget, clientTarget};
    k.clientOwnable = false;  // shell route, no handle
    const Result r = run_scenario(k);
    check("8 shell-routed client: the update still stands",
          r.outcome.result == UpdateResult::Updated ||
              r.outcome.result == UpdateResult::UpdatedButNotRelaunched,
          std::string(result_name(r.outcome.result)) + " / " + r.outcome.detail);
    check("8 shell-routed client: the new bytes are in place", r.hostBytes == newBody,
          std::to_string(r.hostBytes.size()));
    // Whatever happens to the backup, it is accounted for -- deleted, or named with its reason
    // and its holder. The runner's own checks above assert that; this states the pairing.
    check("8 shell-routed client: a backup that survives is one that was reported",
          !r.clientBackupLeft || !r.orphaned.empty(),
          std::to_string(r.orphaned.size()) + " reported, left=" +
              std::to_string(r.clientBackupLeft ? 1 : 0));
  }

  // ================================================================ control: whose process is it'''
assert old in d, 'scenario 8 anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ------------------------------------------------------------------ link Restart Manager
p = 'apps/native_poc/CMakeLists.txt'
c = io.open(p, encoding='utf-8').read()
old = '''target_link_libraries(remote60_updater_scenarios_test PRIVATE'''
i = c.find(old)
assert i != -1, 'scenarios link line'
end = c.find(')', i)
line = c[i:end]
assert 'rstrtmgr' not in line
# rstrtmgr for the read-only holder question
c = c[:end] + ' rstrtmgr' + c[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(c)
print('Restart Manager asked directly; scenario 8 reproduces the shape')
