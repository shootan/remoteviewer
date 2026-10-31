p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\client_shell_main.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c, cnt=1):
    global s
    assert s.count(a) == cnt, (s.count(a), a[:80])
    s = s.replace(a, c)

rep('''std::set<std::string> gKeepUnconfirmed;          // this owner: a PC whose Host did not confirm standby (no retry now)''',
    '''// This owner: a PC whose Host did not confirm standby -- "not confirmed", not "an old Host" (Codex): one more try
// is allowed 60 s later (e.g. a Host that ignored a too-quick re-admission, ka-pc-impl r1 FINDING), then none.
struct KeepUnconfirmed {
  int times = 0;
  uint64_t retryAtMs = 0;
};
std::map<std::string, KeepUnconfirmed> gKeepUnconfirmed;''')
rep('''  if (gKeepUnconfirmed.count(hostId)) { *why = "the Host did not confirm standby"; return false; }''',
    '''  {
    const auto u = gKeepUnconfirmed.find(hostId);
    if (u != gKeepUnconfirmed.end() && (u->second.times >= 2 || GetTickCount64() < u->second.retryAtMs)) {
      *why = "the Host did not confirm standby";
      return false;
    }
  }''')
rep('''        gKeepUnconfirmed.insert(e.hostId);''', '''        {
          KeepUnconfirmed& u = gKeepUnconfirmed[e.hostId];
          ++u.times;
          u.retryAtMs = GetTickCount64() + 60000;
        }''')
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')
