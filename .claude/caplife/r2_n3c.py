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

edit('host_backend_policy.hpp', [
    ('''#include "host_capture_device.hpp"''',
     '''#include "host_backend_owner.hpp"
#include "host_capture_device.hpp"'''),
])

edit('native_video_client_shared_core.hpp', [
    ('''  // Ask what runs now, changing nothing. False (nothing queued) toward a host that does not answer.
  bool Query();

private:
  std::atomic<bool> pending_{false};
  std::atomic<uint32_t> nextSeq_{0};
  std::atomic<uint16_t> backend_{2};
  std::atomic<bool> hostAnswers_{false};''',
     '''  // Ask what runs now, changing nothing. False (nothing queued) toward a host that does not answer.
  // capture-lifecycle r2 (Codex N3): a question never replaces a change. A change and a question pending
  // together go out as the change alone -- its answer (asked for toward a host that answers) IS the status; a
  // question taken first goes out first and a change after it is still sent. One message per ConsumePending.
  bool Query();

private:
  std::atomic<bool> pending_{false};       // a change (backend_) is waiting
  std::atomic<bool> queryPending_{false};  // r2 N3: a question is waiting -- kept apart from the change
  std::atomic<uint32_t> nextSeq_{0};
  std::atomic<uint16_t> backend_{2};
  std::atomic<bool> hostAnswers_{false};'''),
])

edit('native_video_client_shared_core.cpp', [
    ('''void DesktopBackendControl::Reset() {
  pending_.store(false, std::memory_order_relaxed);''',
     '''void DesktopBackendControl::Reset() {
  pending_.store(false, std::memory_order_relaxed);
  queryPending_.store(false, std::memory_order_relaxed);'''),
    ('''bool DesktopBackendControl::ConsumePending(PendingDesktopBackendRequest* out) {
  if (!out) return false;
  if (!pending_.exchange(false, std::memory_order_acq_rel)) return false;
  out->seq = nextSeq_.fetch_add(1, std::memory_order_relaxed) + 1;
  out->backend = backend_.load(std::memory_order_acquire);
  out->wantsStatus = hostAnswers_.load(std::memory_order_acquire);
  if (out->backend == 0 && !out->wantsStatus) return false;  // a question to a host that answers none
  return true;
}

bool DesktopBackendControl::Query() {
  if (!hostAnswers_.load(std::memory_order_acquire)) return false;
  backend_.store(0, std::memory_order_release);
  pending_.store(true, std::memory_order_release);
  return true;
}''',
     '''bool DesktopBackendControl::ConsumePending(PendingDesktopBackendRequest* out) {
  if (!out) return false;
  const bool answers = hostAnswers_.load(std::memory_order_acquire);
  if (pending_.exchange(false, std::memory_order_acq_rel)) {
    // The change first. Toward a host that answers, its answer is the status a pending question waited for.
    if (answers) queryPending_.store(false, std::memory_order_release);
    out->seq = nextSeq_.fetch_add(1, std::memory_order_relaxed) + 1;
    out->backend = backend_.load(std::memory_order_acquire);
    out->wantsStatus = answers;
    return true;
  }
  if (!queryPending_.exchange(false, std::memory_order_acq_rel)) return false;
  if (!answers) return false;  // a question to a host that answers none
  out->seq = nextSeq_.fetch_add(1, std::memory_order_relaxed) + 1;
  out->backend = 0;
  out->wantsStatus = true;
  return true;
}

bool DesktopBackendControl::Query() {
  if (!hostAnswers_.load(std::memory_order_acquire)) return false;
  queryPending_.store(true, std::memory_order_release);  // r2 N3: backend_ (a change waiting) stays as it is
  return true;
}'''),
])
print('ok')
