p = 'C:\\work\\gnlink-wt\\ka-r3\\apps\\native_poc\\src\\host_standby_admission_e2e_test.cpp'
d = 'D:\\remote\\remote\\.claude\\caplife\\'
helpers = open(d + 'ka_realdir_helpers.inc', encoding='utf-8').read().replace('\r\n', '\n')
section = open(d + 'ka_realdir_section.inc', encoding='utf-8').read().replace('\r\n', '\n')
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''}  // namespace

int wmain(int argc, wchar_t** argv) {''', helpers + '''}  // namespace

int wmain(int argc, wchar_t** argv) {''')
rep('''  std::wstring oldHost;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::wstring(argv[i]) == L"--old-host") oldHost = argv[i + 1];
  }''', '''  std::wstring oldHost;
  bool realDirectory = false;
  for (int i = 1; i < argc; ++i) {
    if (i + 1 < argc && std::wstring(argv[i]) == L"--old-host") oldHost = argv[i + 1];
    if (std::wstring(argv[i]) == L"--real-directory") realDirectory = true;
  }''')
rep('''  // ================================================================== an older host''',
    section + '''  // ================================================================== an older host''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
