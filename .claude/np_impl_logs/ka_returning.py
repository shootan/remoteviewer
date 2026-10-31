p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\viewer_managed_standby.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c, cnt=1):
    global s
    assert s.count(a) == cnt, (s.count(a), a[:80])
    s = s.replace(a, c)

rep('''void log(const std::string& line) { std::cout << "[viewer][standby] " << line << std::endl; }
''', '''void log(const std::string& line) { std::cout << "[viewer][standby] " << line << std::endl; }

// How long after an activation's session ended this viewer still says "returning", not "ready". The Host ends a
// session when its control channel has been silent for 10 s (host_startup_control.cpp, UdpControlLink 10000) --
// the viewer's leaving is not announced -- and until then it still counts this device's entry as holding the
// session: a Release is refused (Live), a new admission of this device is Busy. A Pong only says the entry is
// alive, never that the grant is back (Codex model A), so "ready" is not claimed before the Host can have seen the
// end. A tap during "returning" still works: an activation with a newer generation is the device taking it back.
constexpr uint64_t kReturningMs = 12000;
''')
rep('''  std::string lastState;
  uint64_t lastStateAt = 0;
  std::string exitReason;
  auto publish = [&](bool force) {
    const StandbyRunner::Status s = runner.StatusOf(key);
    const std::string name = state_name(s);''', '''  std::string lastState;
  uint64_t lastStateAt = 0;
  uint64_t returningUntil = 0;
  std::string exitReason;
  auto publish = [&](bool force) {
    const StandbyRunner::Status s = runner.StatusOf(key);
    std::string name = state_name(s);
    if (name == "ready" && mono_ms() < returningUntil) name = "returning";''')
rep('''    if (taken) {
      runner.SessionOver(key, leaseOp);
    } else {''', '''    if (taken) {
      runner.SessionOver(key, leaseOp);
      returningUntil = mono_ms() + kReturningMs;
    } else {''')
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))

# the log line of an activation says what it was
p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\viewer_startup.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')
rep('''    std::cout << "[native-video-client][attempt] hello ok=" << (handshakeOk ? 1 : 0)''',
    '''    std::cout << "[native-video-client][attempt] " << (ctx.standby ? "activation ok=" : "hello ok=") << (handshakeOk ? 1 : 0)''')
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))

# shell: a tap while returning activates too; the page shows it
p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\client_shell_main.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')
rep('''        if (c->state == "ready") {
          uint64_t op = 0;''', '''        if (c->state == "ready" || c->state == "returning") {
          uint64_t op = 0;''')
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\ui\shell.html'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')
rep("""    ready: '준비됨', preparing: '준비 중', starting: '준비 중', retry: '다시 준비 중',""",
    """    ready: '준비됨', returning: '돌아오는 중', preparing: '준비 중', starting: '준비 중', retry: '다시 준비 중',""")
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')
