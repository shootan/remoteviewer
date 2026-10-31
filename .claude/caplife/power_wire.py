R = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'
NS = 'remote60::native_poc::e2e::'


def edit(name, reps):
    p = R + name
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, a[:80], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', name)


PRE_OLD = '''                     // input injection off (e2e_injection_guard.hpp). Not for a --prelogin worker: no client ever connects
                     // to one here (its case checks that it stays offline), and its argument contract is the product's
                     (prelogin ? std::wstring() : ''' + NS + '''e2e_injection_off_arg(host->dir));'''
PRE_NEW = '''                     // input injection OFF and host power management OFF (e2e_injection_guard.hpp; Finding-3) -- for a
                     // --prelogin worker too: its power object exists from its first line, client or not
                     ''' + NS + '''e2e_injection_off_arg(host->dir);'''
edit('host_standby_admission_e2e_test.cpp', [(PRE_OLD, PRE_NEW)])
edit('host_punch_reply_e2e_test.cpp', [(PRE_OLD, PRE_NEW)])

edit('host_fatal_exception_test.cpp', [
    ('''    std::wstring cmd = L"\\"" + dir + L"GNLinkStreamMigrationTest.exe\\" --prelogin";''',
     '''    // input injection OFF and power management OFF from its first line (Finding-3), no client either way
    std::wstring cmd = L"\\"" + dir + L"GNLinkStreamMigrationTest.exe\\" --prelogin" + ''' + NS + '''e2e_injection_off_arg(dir);'''),
])

# record counter (record-only build only)
edit('os_input_effects.hpp', [
    ('''inline void record(const std::string& line) {
  static std::mutex mu;
  std::lock_guard<std::mutex> lock(mu);''', '''// How many effects were recorded, by API prefix -- for the record-only tests (never in a product build).
inline std::mutex& record_mu() {
  static std::mutex mu;
  return mu;
}
inline std::vector<std::string>& recorded() {
  static std::vector<std::string> v;
  return v;
}
inline void record(const std::string& line) {
  std::lock_guard<std::mutex> lock(record_mu());
  recorded().push_back(line);'''),
])
