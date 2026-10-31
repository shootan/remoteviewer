R = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'
NS = 'remote60::native_poc::e2e::'
for f in ['host_standby_admission_e2e_test.cpp', 'host_punch_reply_e2e_test.cpp']:
    p = R + f
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    a = '''std::wstring isolation_args(const std::wstring& stagingDir) {
  return L" --directory-cache \\"" + stagingDir + L"host_cache.json\\"" +
         ''' + NS + '''e2e_injection_off_arg(stagingDir);
}'''
    assert s.count(a) == 1, f
    s = s.replace(a, '''std::wstring isolation_args(const std::wstring& stagingDir) {
  return L" --directory-cache \\"" + stagingDir + L"host_cache.json\\"";
}''')
    # the command: an ordinary host gets the injection-off config; a --prelogin worker refuses any --config by design
    # (host_args.cpp preloginRefusedConfig) -- and no client ever connects to one here (its case checks it stays offline)
    import re
    m = re.search(r'(isolation_args\(host->dir\));', s)
    assert m, f
    s = s.replace(m.group(0), '''isolation_args(host->dir) +
                     // input injection off (e2e_injection_guard.hpp). Not for a --prelogin worker: it refuses any --config
                     // by design (host_args.cpp preloginRefusedConfig), and no client ever connects to one here -- its
                     // case checks that it stays offline
                     (prelogin ? std::wstring() : ''' + NS + '''e2e_injection_off_arg(host->dir));''', 1)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', f)
