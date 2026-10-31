src = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\apps\\native_poc\\src\\'

def edit(name, pairs):
    p = src + name
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (name, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

edit('host_capture_lifecycle_ua_e2e_test.cpp', [
    ('''  if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);
  if (job) CloseHandle(job);
  if (secure) CloseHandle(secure);
  check("the scratch directory is cleaned up", staging.Remove(), staging.why());''',
     '''  if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);
  {
    // r2: the whole host log, kept for the evidence where the caller says (REMOTE60_E2E_HOST_LOG_COPY=<file>).
    wchar_t keep[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"REMOTE60_E2E_HOST_LOG_COPY", keep, MAX_PATH) > 0) CopyFileW(hostLogPath.c_str(), keep, FALSE);
  }
  if (job) CloseHandle(job);
  if (secure) CloseHandle(secure);
  check("the scratch directory is cleaned up", staging.Remove(), staging.why());'''),
])
print('ok')
