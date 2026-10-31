import re

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


# ---- the keep set and the run-wide INVALID end
rep('''bool gKeepPreloginLog = false;  // r6 F-b''', '''bool gKeepPreloginLog = false;  // r6 F-b
// r7 N2: the cases whose child's end was NOT confirmed -- their logs are kept (never deleted at the end)
std::set<std::wstring> gKeepLogs;

// r7 N2: a safety contradiction (input / power) or an injection-gate refusal ends the WHOLE test here: no further
// launch, nothing of this run deleted (every log kept), RESULT: INVALID, exit 2. The case's own child has already been
// ended by its launcher (confirmed, or recorded as not confirmed in gKeepLogs, which is said).
[[noreturn]] void fatal_invalid(const std::wstring& tag, const std::string& why) {
  std::printf("FAIL  RUN %ls: %s -- its child's end %s; no further launch; this run's logs are kept in %ls\\n"
              "RESULT: INVALID\\n",
              tag.c_str(), why.c_str(), gKeepLogs.count(tag) ? "NOT confirmed" : "confirmed", gRun.c_str());
  std::fflush(stdout);
  std::_Exit(2);
}''')
if '#include <set>' not in s:
    s = s.replace('#include <sstream>\n', '#include <set>\n#include <sstream>\n', 1)

# ---- the unconfirmed ends are returned (kept), not discarded
n0 = s.count('(void)end_child_confirmed(pi.hProcess, ')
assert n0 == 4, n0
s = re.sub(r'\(void\)end_child_confirmed\(pi\.hProcess, ([^)]*)\);  // r6 F-c',
           r'if (!end_child_confirmed(pi.hProcess, \1)) gKeepLogs.insert(tag);  // r7 N2: not confirmed -> its log kept', s)

# ---- the run() cases: a contradiction ends the whole test
for case, label in [('std', 'std'), ('winrt', 'winrt')]:
    rep('''  check("''' + label + ''': no input / power contradiction in its whole output (r6 G1)", !''' + NS + '''e2e_injection_contradicted(out));''',
        '''  {
    std::string why;  // r7 N2: a contradiction ends the whole test INVALID (no further launch, logs kept)
    if (''' + NS + '''e2e_injection_contradicted(out, &why)) fatal_invalid(L"''' + case + '''", why);
    check("''' + label + ''': no input / power contradiction in its whole output (r6 G1)", true);
  }''')
rep('''  check("normal control: no input / power contradiction in its whole output (r6 G1)", !''' + NS + '''e2e_injection_contradicted(out));''',
    '''  {
    std::string why;  // r7 N2: a contradiction ends the whole test INVALID (no further launch, logs kept)
    if (''' + NS + '''e2e_injection_contradicted(out, &why)) fatal_invalid(L"none", why);
    check("normal control: no input / power contradiction in its whole output (r6 G1)", true);
  }''')

# ---- run_with_client: an INVALID case ends the whole test before the next launch
for tag in ['probe_trap', 'wrapper_trap', 'wgc_throw', 'wgc_false', 'normal']:
    pat = re.compile(r'(  code = run_with_client\(dir, L"' + tag + r'", [^\n]*\n)  check\("\.\.\.the injection gate held \(no Hello before the OFF line\)", code != kInjectionInvalid\);\n')
    m = pat.findall(s)
    assert len(m) == 1, (tag, len(m))
    s = pat.sub(lambda mm: mm.group(1) + '  if (code == kInjectionInvalid) fatal_invalid(L"' + tag + '", "the injection gate refused or the run contradicted it");  // r7 N2\n'
                + '  check("...the injection gate held (no Hello before the OFF line) and nothing contradicted it", true);\n', s)

# ---- the end: kept cases' logs stay
rep('''    delete_known_file(gRun + L"fatal_" + t + L".log");''', '''    if (gKeepLogs.count(t)) {  // r7 N2: its child's end was not confirmed -- its log stays, said
      std::printf("NOTE  kept (its child's end not confirmed): %lsfatal_%ls.log\\n", gRun.c_str(), t);
      continue;
    }
    delete_known_file(gRun + L"fatal_" + t + L".log");''')

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
