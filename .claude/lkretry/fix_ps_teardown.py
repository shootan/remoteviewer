p = r'C:\work\gnlink-wt\fix-capture-lifecycle\apps\native_poc\src\viewer_prelogin_login_e2e_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
def rep(old, new):
    global s
    assert s.count(old) == 1, old[:90]
    s = s.replace(old, new)
rep('''  if (ingress.joinable()) ingress.join();
  if (presentThread.joinable()) presentThread.join();''',
    '''  if (ingress.joinable()) ingress.join();
  if (presentThread.joinable()) presentThread.join();
  receiver.reset();  // capture-lifecycle r1 (PS): its decoder goes while Media Foundation is still up''')
rep('''  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;''',
    '''  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
  if (stillScreen && FAILED(MFStartup(MF_VERSION))) return 1;  // capture-lifecycle r1 (PS): the product decoder''')
rep('''  check("the scratch directory is cleaned up", staging.Remove(), staging.why());
  WSACleanup();''',
    '''  check("the scratch directory is cleaned up", staging.Remove(), staging.why());
  if (stillScreen) MFShutdown();
  WSACleanup();''')
rep('#include <optional>\n', '#include <optional>\n#include <mfapi.h>\n')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
