base = r'C:\work\gnlink-wt\old-apk-lane\apps\native_poc\src'


def edit(name, reps):
    p = base + '\\' + name
    raw = open(p, 'rb').read()
    crlf = b'\r\n' in raw
    s = raw.decode('utf-8').replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, s.count(a), a[:90])
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


edit('ka_standby_holder_env.hpp', [("#include <cwctype>\n", "#include <cwchar>\n#include <cwctype>\n")])

edit('ka_standby_holder_tool.cpp', [
("""#include "ka_standby_holder_verdict.hpp\"""", """#include "ka_standby_holder_env.hpp"
#include "ka_standby_holder_verdict.hpp\""""),
("""// Exit (r3, ka_standby_holder_verdict.hpp):""", """// r5: the viewer's own environment block carries REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1 (h264) and the window seam's
// X / Y explicitly (ka_standby_holder_env.hpp) -- nothing is left to the caller's environment. The device-free check
// (automation/old_apk_lane/oa_holder_viewer_check.ps1) uses two options of its own:
//   --host-id <id>              Init names this id; no directory host lookup (a check with no Host);
//   --omit-viewer-encoded-env 1 NEGATIVE CONTRAST ONLY: the viewer launched as r4 did (no FORCE in its block).
// Exit (r3, ka_standby_holder_verdict.hpp):"""),
("""  const int maxSec = a.count("--max-sec") ? std::atoi(a["--max-sec"].c_str()) : 900;""",
 """  const int maxSec = a.count("--max-sec") ? std::atoi(a["--max-sec"].c_str()) : 900;
  const std::string givenHostId = a["--host-id"];                        // r5: the device-free check only
  const bool omitEncodedEnv = a["--omit-viewer-encoded-env"] == "1";     // r5: the negative contrast only"""),
("""  const uint64_t listUntil = now_ms() + 60000;
  while (now_ms() < listUntil && hostId.empty()) {""", """  const uint64_t listUntil = now_ms() + 60000;
  if (!givenHostId.empty()) {
    hostId = givenHostId;  // r5: no lookup -- signed in only (Init carries a real session)
    for (int i = 0; i < 20 && session.empty(); ++i) {
      if (!directory_login(url, account, password, &session, &err, &adv)) Sleep(500);
    }
  }
  while (now_ms() < listUntil && hostId.empty()) {"""),
("""  if (hostId.empty()) {
    note("SETUP host \\"" + hostName + "\\" not online for the account within 60 s");
    return 2;
  }""", """  if (hostId.empty() || session.empty()) {
    note(hostId.empty() ? "SETUP host \\"" + hostName + "\\" not online for the account within 60 s"
                        : std::string("SETUP could not sign in"));
    return 2;
  }"""),
("""  SetEnvironmentVariableW(L"REMOTE60_VIEWER_TEST_WINDOW_X", std::to_wstring(right + 200).c_str());
  SetEnvironmentVariableW(L"REMOTE60_VIEWER_TEST_WINDOW_Y", std::to_wstring(GetSystemMetrics(SM_YVIRTUALSCREEN) + 40).c_str());
  const auto env = e2e::e2e_isolated_environment(vdir + L"lad");""", """  // r5: written INTO the viewer's block (not this process's environment, not inherited from the caller)
  std::vector<std::pair<std::wstring, std::wstring>> viewerEnv = {
      {L"REMOTE60_VIEWER_TEST_WINDOW_X", std::to_wstring(right + 200)},
      {L"REMOTE60_VIEWER_TEST_WINDOW_Y", std::to_wstring(GetSystemMetrics(SM_YVIRTUALSCREEN) + 40)}};
  if (!omitEncodedEnv) viewerEnv.push_back({L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1"});
  const auto env = ka_standby::env_block_with(e2e::e2e_isolated_environment(vdir + L"lad"), viewerEnv);
  note(std::string("viewer env: REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=") +
       (ka_standby::env_block_value(env, L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE") == L"1" ? "1" : "(absent)") +
       (omitEncodedEnv ? " -- NEGATIVE CONTRAST (--omit-viewer-encoded-env)" : ""));"""),
])

edit('ka_standby_holder_verdict_test.cpp', [
("""#include "ka_standby_holder_verdict.hpp\"""", """#include "ka_standby_holder_env.hpp"
#include "ka_standby_holder_verdict.hpp\""""),
("""  std::printf("\\n%d checks, %d failed\\nRESULT: %s\\n", gChecks, gFailures, gFailures ? "FAIL" : "PASS");""",
 """  // r5: the viewer's environment block -- the variables set replace what the base had, the rest is kept
  {
    std::vector<wchar_t> base;
    for (const wchar_t* e : {L"PATH=C:\\\\x", L"remote60_native_encoded_experiment_force=0", L"=C:=C:\\\\", L"LOCALAPPDATA=L"}) {
      base.insert(base.end(), e, e + wcslen(e));
      base.push_back(L'\\0');
    }
    base.push_back(L'\\0');
    const auto out = env_block_with(base, {{L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1"}, {L"REMOTE60_VIEWER_TEST_WINDOW_X", L"9999"}});
    int forceEntries = 0;
    for (size_t i = 0; i < out.size() && out[i] != L'\\0';) {
      const std::wstring entry(&out[i]);
      i += entry.size() + 1;
      if (_wcsnicmp(entry.c_str(), L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=", 41) == 0) ++forceEntries;
    }
    check("r5 viewer env: FORCE=1 replaces the caller's value (one entry), X added, PATH / =C: / LOCALAPPDATA kept",
          env_block_value(out, L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE") == L"1" && forceEntries == 1 &&
              env_block_value(out, L"REMOTE60_VIEWER_TEST_WINDOW_X") == L"9999" && env_block_value(out, L"PATH") == L"C:\\\\x" &&
              env_block_value(out, L"LOCALAPPDATA") == L"L" && out.size() >= 2 && out[out.size() - 1] == L'\\0' &&
              out[out.size() - 2] == L'\\0');
    const auto bare = env_block_with(base, {});
    check("r5 viewer env: with nothing set, FORCE is what the base had (the r4 way: the caller decides)",
          env_block_value(bare, L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE") == L"0");
  }
  std::printf("\\n%d checks, %d failed\\nRESULT: %s\\n", gChecks, gFailures, gFailures ? "FAIL" : "PASS");"""),
])
print('ok')
