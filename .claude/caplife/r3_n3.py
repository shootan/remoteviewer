src = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\apps\\native_poc\\src\\'
wt = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\apps\\native_poc\\'
NL = chr(92) + 'n'

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

for f in ('host_backend_status_board.hpp', 'host_backend_status_board_test.cpp', 'host_backend_owner_test.cpp'):
    p = src + f
    s = open(p, 'rb').read().decode('utf-8').replace('\r\n', '\n').replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

edit(src + 'host_capture_session.hpp', [
    ('''  std::atomic<uint64_t> backendStatusPacked{0};
  std::atomic<uint32_t> backendStatusChanges{0};
  std::atomic<uint64_t> backendAppliedKey{0};''',
     '''  // r3 (Codex N3-host): owner epoch, packed status, change count and handled key as ONE snapshot
  // (host_backend_status_board.hpp) -- replaces r2's separate backendStatusPacked / Changes / AppliedKey /
  // StatusEpoch atomics, which a reader could take from two different publishes.
  BackendStatusBoard backendStatus;'''),
])
p = src + 'host_capture_session.hpp'
s = open(p, 'rb').read().decode('utf-8')
import re
# drop the r2 statusEpoch atomic and its comment lines (keep backendChangePostedKey)
old_block_start = s.index('  // capture-lifecycle r2 (Codex N2/N3): the owner epoch backendStatusPacked was published for')
old_block_end = s.index('std::atomic<uint64_t> backendChangePostedKey{0};')
block = s[old_block_start:old_block_end]
nl = '\r\n' if '\r\n' in s else '\n'
new_block = ('  // capture-lifecycle r2 (Codex N3): the (epoch<<32 | seq) of the last backend CHANGE a control thread posted' + nl +
             '  // (a question is answered NotYetApplied while that one is not yet handled). Control thread only.' + nl + '  ')
s = s[:old_block_start] + new_block + s[old_block_end:]
if '#include "host_backend_status_board.hpp"' not in s:
    s = s.replace('#include <atomic>', '#include <atomic>' + nl + nl + '#include "host_backend_status_board.hpp"', 1)
open(p, 'wb').write(s.encode('utf-8'))
assert '#include "host_backend_status_board.hpp"' in s

edit(src + 'host_stage_backend.cpp', [
    ('''  if (capture.backendStatusPacked.exchange(packed, std::memory_order_acq_rel) != packed) {
    capture.backendStatusChanges.fetch_add(1, std::memory_order_acq_rel);
  }
  // r2 (Codex N3): ...of this owner (stored after the packed status: the control thread reads it around it).
  capture.backendStatusEpoch.store(backend.owner.ownerEpoch, std::memory_order_release);
}''',
     '''  // r3 (Codex N3-host): with its owner, in one snapshot.
  capture.backendStatus.Publish(backend.owner.ownerEpoch, packed);
}'''),
    ('''void publish_backend_status(HostContext& hx) {''',
     '''uint64_t backend_status_packed(HostContext& hx);
void publish_backend_status(HostContext& hx) {'''),
    ('''        publish_backend_status(hx);
        hx.capture.backendAppliedKey.store(key, std::memory_order_release);''',
     '''        // r3 (Codex N3-host): the handled key with the status it left, in one publish.
        hx.capture.backendStatus.PublishHandled(hx.backend.owner.ownerEpoch, backend_status_packed(hx), key);'''),
])

# split packed computation out
p = src + 'host_stage_backend.cpp'
s = open(p, 'rb').read().decode('utf-8')
nl = '\r\n' if '\r\n' in s else '\n'
s2 = s.replace('\r\n', '\n')
old = '''uint64_t backend_status_packed(HostContext& hx);
void publish_backend_status(HostContext& hx) {
  auto& backend = hx.backend;
  auto& capture = hx.capture;
  uint16_t flags'''
assert s2.count(old) == 1
new = '''uint64_t backend_status_packed(HostContext& hx) {
  auto& backend = hx.backend;
  auto& capture = hx.capture;
  uint16_t flags'''
s2 = s2.replace(old, new)
old2 = '''                          (static_cast<uint64_t>(flags) << 48);
  // r3 (Codex N3-host): with its owner, in one snapshot.
  capture.backendStatus.Publish(backend.owner.ownerEpoch, packed);
}'''
assert s2.count(old2) == 1
new2 = '''                          (static_cast<uint64_t>(flags) << 48);
  return packed;
}
void publish_backend_status(HostContext& hx) {
  // r3 (Codex N3-host): with its owner, in one snapshot.
  hx.capture.backendStatus.Publish(hx.backend.owner.ownerEpoch, backend_status_packed(hx));
}'''
s2 = s2.replace(old2, new2)
if nl == '\r\n':
    s2 = s2.replace('\n', '\r\n')
open(p, 'wb').write(s2.encode('utf-8'))

edit(src + 'host_control_session.cpp', [
    ('''        const bool isChange = req.backend == 1 || req.backend == 2 || req.backend == 3;
        const uint64_t until = GetTickCount64() + 250;
        bool applied = false;
        uint64_t packed = 0;
        for (;;) {
          const uint64_t statusEpoch = capture.backendStatusEpoch.load(std::memory_order_acquire);
          packed = capture.backendStatusPacked.load(std::memory_order_acquire);
          const uint64_t posted = capture.backendChangePostedKey.load(std::memory_order_acquire);
          const uint64_t handled = capture.backendAppliedKey.load(std::memory_order_acquire);
          const uint64_t wanted = isChange ? ((servedEpoch << 32) | req.seq) : posted;
          const bool changeDone = isChange ? handled == wanted : ((posted >> 32) != servedEpoch || handled == posted);
          // The epoch read again after the packed status: both of the same publish (one owner).
          applied = statusEpoch == servedEpoch && changeDone &&
                    capture.backendStatusEpoch.load(std::memory_order_acquire) == statusEpoch;
          if (applied || GetTickCount64() >= until ||
              clientSession.epoch.load(std::memory_order_acquire) != servedEpoch) {
            break;
          }
          Sleep(5);
        }''',
     '''        // r3 (Codex N3-host): every look is ONE snapshot (owner, status, handled key -- host_backend_status_board.hpp),
        // and a snapshot of another owner answers nothing for this session (fields 0, NotYetApplied).
        const bool isChange = req.backend == 1 || req.backend == 2 || req.backend == 3;
        const uint64_t requestKey = (servedEpoch << 32) | req.seq;
        const uint64_t posted = capture.backendChangePostedKey.load(std::memory_order_acquire);
        const uint64_t until = GetTickCount64() + 250;
        BackendStatusAnswer answer;
        for (;;) {
          answer = DecideBackendStatusAnswer(capture.backendStatus.Read(), servedEpoch, isChange, requestKey, posted);
          if (answer.applied || GetTickCount64() >= until ||
              clientSession.epoch.load(std::memory_order_acquire) != servedEpoch) {
            break;
          }
          Sleep(5);
        }
        const bool applied = answer.applied;'''),
    ('''        st.requested = static_cast<uint16_t>(packed & 0xffffu);
        st.effective = static_cast<uint16_t>((packed >> 16) & 0xffffu);
        st.active = static_cast<uint16_t>((packed >> 32) & 0xffffu);
        st.flags = static_cast<uint16_t>((packed >> 48) & 0xffffu);
        if (!applied) st.flags |= kDesktopBackendStatusFlagNotYetApplied;
        st.changeCount = capture.backendStatusChanges.load(std::memory_order_acquire);''',
     '''        st.requested = answer.requested;
        st.effective = answer.effective;
        st.active = answer.active;
        st.flags = answer.flags;
        if (!applied) st.flags |= kDesktopBackendStatusFlagNotYetApplied;
        st.changeCount = answer.changes;'''),
])

edit(wt + 'CMakeLists.txt', [
    ('''target_include_directories(remote60_host_backend_owner_test PRIVATE src)''',
     '''target_include_directories(remote60_host_backend_owner_test PRIVATE src)

# capture-lifecycle r3 (Codex N3-host): request 34's answer from one status snapshot.
add_executable(remote60_host_backend_status_board_test
  src/host_backend_status_board_test.cpp
)
target_include_directories(remote60_host_backend_status_board_test PRIVATE src)'''),
])
print('ok')
