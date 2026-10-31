import sys
p = sys.argv[1]
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old)
    s = s.replace(old, new, 1)

rep('const std::wstring kSecret = L"Pw-7Qz \\"!ä가x";\n',
    'const std::wstring kSecret = L"Pw-7Qz \\"!ä가x";\n'
    '// lk-ux-retry r1: the second password of the same connection\'s explicit retry (the first stands for the wrong one).\n'
    '// The same "Pw-7Qz" head, so every "no password in any log" check below covers it too.\n'
    'const std::wstring kSecret2 = L"Pw-7Qz-retry 나2";\n')

rep('''  const std::string u8 = utf8(kSecret);
  const std::string u16(reinterpret_cast<const char*>(kSecret.data()), kSecret.size() * sizeof(wchar_t));
''', '''  const std::string u8 = utf8(kSecret);
  const std::string u16(reinterpret_cast<const char*>(kSecret.data()), kSecret.size() * sizeof(wchar_t));
  const std::string u8b = utf8(kSecret2);  // lk-ux-retry r1
  const std::string u16b(reinterpret_cast<const char*>(kSecret2.data()), kSecret2.size() * sizeof(wchar_t));
''')
rep('''      } else if (file_contains(d + n, u8) || file_contains(d + n, u16)) {''',
    '''      } else if (file_contains(d + n, u8) || file_contains(d + n, u16) || file_contains(d + n, u8b) ||
                 file_contains(d + n, u16b)) {''')

rep('''  const auto operate_prompt = [&](OperatorRun* run, const wchar_t* cls, bool cancel) {''',
    '''  const auto operate_prompt = [&](OperatorRun* run, const wchar_t* cls, bool cancel, const std::wstring& secret = kSecret) {''')
rep('''      for (wchar_t c : kSecret) PostMessageW(edit, WM_CHAR, c, 1);
      run->typedAll = wait_until([&] { return GetWindowTextLengthW(edit) == static_cast<int>(kSecret.size()); }, 3000);''',
    '''      for (wchar_t c : secret) PostMessageW(edit, WM_CHAR, c, 1);
      run->typedAll = wait_until([&] { return GetWindowTextLengthW(edit) == static_cast<int>(secret.size()); }, 3000);''')

anchor = '''    // ---- r2 (Codex L5): a result of an EARLIER control connection, still queued, is not shown
'''
block = open(sys.argv[2], 'rb').read().decode('utf-8').replace('\r\n', '\n')
rep(anchor, block + anchor)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok crlf=%s' % crlf)
