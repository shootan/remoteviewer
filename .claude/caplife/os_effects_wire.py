import re

R = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'


def edit(name, fn):
    p = R + name
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    s2 = fn(s)
    assert s2 != s, name
    if crlf:
        s2 = s2.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s2)
    print('ok', name)


def add_include(s):
    incs = [m.start() for m in re.finditer(r'^#include "[^"]+"\n', s, flags=re.M)]
    assert incs
    last = list(re.finditer(r'^#include "[^"]+"\n', s, flags=re.M))[-1]
    return s[:last.end()] + '#include "os_input_effects.hpp"\n' + s[last.end():]


def inject(s):
    n_si = len(re.findall(r'(?<![\w:])SendInput\(', s))
    n_pm = len(re.findall(r'(?<![\w:])PostMessageW\(', s))
    n_sc = len(re.findall(r'(?<![\w:])SetCursorPos\(', s))
    print('  host_input_inject: SendInput', n_si, 'PostMessageW', n_pm, 'SetCursorPos', n_sc)
    assert (n_si, n_pm, n_sc) == (4, 6, 3), (n_si, n_pm, n_sc)
    s = re.sub(r'(?<![\w:])SendInput\(', 'os_input::send_input(', s)
    s = re.sub(r'(?<![\w:])PostMessageW\(', 'os_input::post_message(', s)
    s = re.sub(r'(?<![\w:])SetCursorPos\(', 'os_input::set_cursor_pos(', s)
    return add_include(s)


edit('host_input_inject.cpp', inject)


def logon(s):
    n = len(re.findall(r'(?<![\w:])SendInput\(', s))
    assert n == 2, n
    s = re.sub(r'(?<![\w:])SendInput\(', 'os_input::send_input(', s)
    return add_include(s)


edit('logon_ui_target.cpp', logon)


def wake(s):
    a = '''  (void)SendInput(2, wake, sizeof(INPUT));
  (void)PostMessageW(HWND_BROADCAST, WM_SYSCOMMAND,'''
    assert s.count(a) == 1
    s = s.replace(a, '''  (void)os_input::send_input(2, wake, sizeof(INPUT));
  (void)os_input::post_message(HWND_BROADCAST, WM_SYSCOMMAND,''')
    return add_include(s)


edit('host_log.hpp', wake)


def broker(s):
    a = '''  std::lock_guard<std::mutex> lock(mu_);
  if (pipe_ != INVALID_HANDLE_VALUE) return true;
  if (!process_is_elevated()) {'''
    assert s.count(a) == 1
    s = s.replace(a, '''  // record-only test build (os_input_effects.hpp): no SYSTEM service is installed or connected
  if (os_input::broker_refused("install_connect")) {
    if (status) *status = "secure input: record-only test build";
    return false;
  }
  std::lock_guard<std::mutex> lock(mu_);
  if (pipe_ != INVALID_HANDLE_VALUE) return true;
  if (!process_is_elevated()) {''')
    a = '''bool SecureInputBrokerClient::WriteLocked(SecureInputMessage message) {
'''
    assert s.count(a) == 1
    s = s.replace(a, a + '''  if (os_input::broker_refused("write")) return false;  // record-only: nothing handed to a service
''')
    return add_include(s)


edit('secure_input_broker.cpp', broker)
