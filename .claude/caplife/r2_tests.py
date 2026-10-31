wt = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\apps\\native_poc\\'
src = wt + 'src\\'

def edit(path, pairs):
    s = open(path, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (path, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(path, 'wb').write(s.encode('utf-8'))

# owner test: real names; CRLF like neighbours
p = src + 'host_backend_owner_test.cpp'
s = open(p, 'rb').read().decode('utf-8').replace('\r\n', '\n')
s = s.replace('HostMainLoopMailbox mailbox;', 'MainLoopMailbox mailbox;').replace('r.mailbox.StaleDropped()', 'r.mailbox.StaleDroppedCount()')
open(p, 'wb').write(s.replace('\n', '\r\n').encode('utf-8'))
p = src + 'host_backend_owner.hpp'

edit(wt + 'CMakeLists.txt', [
    ('''add_executable(remote60_host_kick_test
  src/host_kick_test.cpp
)
target_include_directories(remote60_host_kick_test PRIVATE src)''',
     '''add_executable(remote60_host_kick_test
  src/host_kick_test.cpp
)
target_include_directories(remote60_host_kick_test PRIVATE src)

# capture-lifecycle r2 (Codex N2): the owner of a viewer's desktop-backend choice and the mailbox take under it,
# on the epoch boundary orders (host_backend_owner.hpp).
add_executable(remote60_host_backend_owner_test
  src/host_backend_owner_test.cpp
)
target_include_directories(remote60_host_backend_owner_test PRIVATE src)'''),
])

edit(src + 'native_video_client_shared_core_test.cpp', [
    ('''int main() {
  if (!test_control_keepalive_interval()) return 1;''',
     '''// capture-lifecycle r2 (Codex N3): a question never replaces a change waiting to go out, in either order.
bool test_desktop_backend_control() {
  using remote60::native_poc::DesktopBackendControl;
  using remote60::native_poc::PendingDesktopBackendRequest;
  {
    DesktopBackendControl c;
    c.SetHostAnswers(true);
    c.Request(2);
    if (!expect(c.Query(), "backend: a question toward a host that answers is queued")) return false;
    PendingDesktopBackendRequest out{};
    if (!expect(c.ConsumePending(&out) && out.backend == 2 && out.wantsStatus,
                "backend: Request then Query -> the WGC change goes out (asking for the status)")) return false;
    if (!expect(!c.ConsumePending(&out), "backend: ...and its answer is the question's: nothing more")) return false;
  }
  {
    DesktopBackendControl c;
    c.SetHostAnswers(true);
    c.Query();
    PendingDesktopBackendRequest q{};
    if (!expect(c.ConsumePending(&q) && q.backend == 0 && q.wantsStatus, "backend: Query alone -> a question")) return false;
    c.Request(2);
    PendingDesktopBackendRequest out{};
    if (!expect(c.ConsumePending(&out) && out.backend == 2 && out.seq == q.seq + 1,
                "backend: Query taken, then Request -> the change still goes out after it")) return false;
  }
  {
    DesktopBackendControl c;
    c.SetHostAnswers(true);
    c.Query();
    c.Request(2);
    PendingDesktopBackendRequest out{};
    if (!expect(c.ConsumePending(&out) && out.backend == 2, "backend: Query then Request -> the change goes out")) return false;
    if (!expect(!c.ConsumePending(&out), "backend: one message for both")) return false;
  }
  {
    DesktopBackendControl c;  // an old host: no answers
    c.Request(2);
    if (!expect(!c.Query(), "backend: no question toward a host that answers none")) return false;
    PendingDesktopBackendRequest out{};
    if (!expect(c.ConsumePending(&out) && out.backend == 2 && !out.wantsStatus,
                "backend: old host -> the change, without asking for an answer")) return false;
    if (!expect(!c.ConsumePending(&out), "backend: old host -> never a backend-0 question")) return false;
  }
  return true;
}

int main() {
  if (!test_desktop_backend_control()) return 1;
  if (!test_control_keepalive_interval()) return 1;'''),
])
print('ok')
