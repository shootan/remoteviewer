# -*- coding: utf-8 -*-
import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d: %s' % (path, i, old[:70])
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)


edit('apps/native_poc/src/log_upload.hpp', [
 ('''/** Sends what it can once, stops the worker and resets the state. Safe when never started. */
void log_upload_stop();''',
  '''/**
 * Sends what it can once, stops the worker and resets the state. Safe when never started.
 *
 * Bounded: the drain stops STARTING new requests after kStopDrainBudgetMs, so a server that has
 * gone away cannot turn a queue into a shutdown that lasts as long as the queue is deep. One
 * request may already be in flight, and that one is waited out -- see log_upload_shutdown().
 * The uploader can be configured again afterwards; log_upload_shutdown() is the terminal form.
 */
void log_upload_stop();

/**
 * The terminal stop, for a process that is going away.
 *
 * Same as log_upload_stop(), plus two things it deliberately does not do: it latches, so a
 * configure arriving afterwards (a sign-in racing the close) cannot start a second worker for
 * the join to miss; and it drops the auth-rejected callback once the worker is joined, because
 * that callback runs on the worker thread and touches the caller's UI.
 *
 * Worst case wait = kStopDrainBudgetMs + one in-flight http_post. On this tree the log path uses
 * directory_client's 6 s timeout on connect and on receive, so the bound is 3 s + up to ~12 s
 * against a host that accepts and never answers; against an unreachable one, 3 s + ~6 s. Called
 * with no uploader or UI lock held -- see LogUploadShutdown.
 */
void log_upload_shutdown();

/**
 * The uploader's shutdown contract, as an object.
 *
 * The worker is a std::thread owned by a function-local static, and nothing in the product ever
 * called log_upload_stop() -- only the tests did, nine times, which is exactly why the gap kept
 * looking covered. At exit the static's destructor then destroyed a *joinable* thread, and that
 * is std::terminate() by definition: no exception, no stack, a process that dies while tidying
 * up. Declare one in each owner's entry function, before anything may configure the uploader and
 * outside whatever the worker needs to finish (sockets, in particular).
 */
class LogUploadShutdown {
 public:
  LogUploadShutdown() = default;
  ~LogUploadShutdown();
  LogUploadShutdown(const LogUploadShutdown&) = delete;
  LogUploadShutdown& operator=(const LogUploadShutdown&) = delete;
};'''),
])

edit('apps/native_poc/src/log_upload.cpp', [
 # ---------------------------------------------------------------- state
 ('''  bool running = false;
  bool stopping = false;
  std::thread worker;''',
  '''  bool running = false;
  bool stopping = false;
  // Latched by log_upload_shutdown(): the process is going away. Checked under the same lock that
  // starts the worker, so a configure racing the close cannot slip a second thread past the join.
  bool shutdown = false;
  // When the final drain must stop starting new requests. A stop is not free -- the worker sends
  // what it can and every send is a blocking http_post -- so without this the wait is as long as
  // the queue is deep times the http timeout, on whichever thread called stop.
  uint64_t stopDeadlineUs = 0;
  std::thread worker;'''),
 # ---------------------------------------------------------------- bounded drain
 ('''      seenWake = s.wakeSeq;
      ++cycles;
      s.workerCycles = cycles;
      finalPass = s.stopping;
      const uint64_t nowUs = steady_now_us();
      if (!next_job_locked(s, nowUs, &job)) {''',
  '''      seenWake = s.wakeSeq;
      ++cycles;
      s.workerCycles = cycles;
      finalPass = s.stopping;
      const uint64_t nowUs = steady_now_us();
      if (finalPass && s.stopDeadlineUs != 0 && nowUs >= s.stopDeadlineUs) {
        // The drain has had its budget. Whoever called stop is blocked in join() -- on the UI
        // thread, in the case this exists for -- and a server that accepts and never answers
        // would otherwise hold the window open for one http timeout per queued batch.
        uint64_t lines = 0, batches = 0;
        discard_all_locked(s, &lines, &batches);
        diag("worker exiting (stop budget spent) sent=" + std::to_string(s.sentBatches) +
             " discardedLines=" + std::to_string(lines) +
             " heldDiscarded=" + std::to_string(batches));
        return;
      }
      if (!next_job_locked(s, nowUs, &job)) {'''),
 # ---------------------------------------------------------------- configure refuses after shutdown
 ('''    std::lock_guard<std::mutex> lk(s.mu);
    if (s.running && !s.stopping) {''',
  '''    std::lock_guard<std::mutex> lk(s.mu);
    if (s.shutdown) {
      // Under the same lock as the start below, on purpose: checking earlier would leave a window
      // in which this call passes the check, the shutdown latches and joins, and then this one
      // starts a worker nobody will ever join.
      if (outReason) *outReason = "shutting down";
      return false;
    }
    if (s.running && !s.stopping) {'''),
 # ---------------------------------------------------------------- stop gets a deadline
 ('''void log_upload_stop() {
  UploaderState& s = state();
  std::thread worker;
  {
    std::lock_guard<std::mutex> lk(s.mu);
    if (!s.running) return;
    s.stopping = true;
    worker = std::move(s.worker);
  }''',
  '''void log_upload_stop() {
  UploaderState& s = state();
  std::thread worker;
  {
    std::lock_guard<std::mutex> lk(s.mu);
    if (!s.running) return;
    s.stopping = true;
    s.stopDeadlineUs = steady_now_us() + kStopDrainBudgetMs * 1000;
    worker = std::move(s.worker);
  }'''),
 ('''  s.lastTransientDiagUs = 0;
  s.workerCycles = 0;
}

bool log_upload_running() {''',
  '''  s.lastTransientDiagUs = 0;
  s.workerCycles = 0;
  s.stopDeadlineUs = 0;
}

void log_upload_shutdown() {
  UploaderState& s = state();
  {
    std::lock_guard<std::mutex> lk(s.mu);
    s.shutdown = true;
  }
  // No lock held here: the worker takes s.mu on every cycle, so joining while holding it would
  // hang the process instead of ending it. The caller must likewise not hold a UI lock -- the
  // auth-rejected callback runs on this thread and marshals to the UI with PostMessage.
  log_upload_stop();
  {
    // After the join, not before. Clearing it earlier would not stop a callback already running;
    // once the worker is joined, nothing can start another, so this is when the caller's UI stops
    // being reachable from here.
    std::lock_guard<std::mutex> lk(s.mu);
    s.authRejectedCallback = nullptr;
  }
}

LogUploadShutdown::~LogUploadShutdown() { log_upload_shutdown(); }

bool log_upload_running() {'''),
])

# the drain budget constant, next to the other tunables at the top of the anonymous namespace
d = io.open('apps/native_poc/src/log_upload.cpp', encoding='utf-8').read()
old = 'const char* outcome_name(Outcome o) {'
new = '''// How long the final drain may keep starting requests. Not a total: a request already in flight
// is waited out on top of this. Picked so a close is over within a couple of seconds of the last
// send that can still succeed, rather than however long the queue happens to be.
constexpr uint64_t kStopDrainBudgetMs = 3000;

const char* outcome_name(Outcome o) {'''
assert d.count(old) == 1, 'budget anchor'
d = d.replace(old, new, 1)
io.open('apps/native_poc/src/log_upload.cpp', 'w', encoding='utf-8', newline='').write(d)
print('ok budget')
