R = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'


def edit(name, reps, crlf_aware=True):
    p = R + name
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, a[:90], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', name)


edit('e2e_child_util.hpp', [
    ('#include <functional>\n', '#include <functional>\n#include <set>\n#include <string>\n'),
    ('''}  // namespace remote60::native_poc::e2e''', '''// r7 N2: a safety contradiction (or an injection-gate refusal) ends the WHOLE test: no further launch, nothing of
// this run deleted, RESULT: INVALID, exit 2. `keep` holds the cases whose child's end was not confirmed (said as such).
[[noreturn]] inline void e2e_case_invalid(const std::wstring& runFolder, const std::set<std::wstring>& keep,
                                          const std::wstring& tag, const std::string& why) {
  std::printf("FAIL  RUN %ls: %s -- its child's end %s; no further launch; this run's logs are kept in %ls\\n"
              "RESULT: INVALID\\n",
              tag.c_str(), why.c_str(), keep.count(tag) ? "NOT confirmed" : "confirmed", runFolder.c_str());
  std::fflush(stdout);
  std::_Exit(2);
}

}  // namespace remote60::native_poc::e2e'''),
])

edit('host_fatal_exception_test.cpp', [
    ('''[[noreturn]] void fatal_invalid(const std::wstring& tag, const std::string& why) {
  std::printf("FAIL  RUN %ls: %s -- its child's end %s; no further launch; this run's logs are kept in %ls\\n"
              "RESULT: INVALID\\n",
              tag.c_str(), why.c_str(), gKeepLogs.count(tag) ? "NOT confirmed" : "confirmed", gRun.c_str());
  std::fflush(stdout);
  std::_Exit(2);
}''', '''[[noreturn]] void fatal_invalid(const std::wstring& tag, const std::string& why) {
  remote60::native_poc::e2e::e2e_case_invalid(gRun, gKeepLogs, tag, why);  // e2e_child_util.hpp (its negative control:
                                                                           // e2e_guard_runtime_test)
}'''),
])
