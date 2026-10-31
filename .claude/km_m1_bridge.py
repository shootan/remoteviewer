import sys
root = sys.argv[1]

def edit(p, pairs):
    p = root + '/' + p
    raw = open(p, 'rb').read()
    bom = raw.startswith(b'\xef\xbb\xbf')
    s = raw.decode('utf-8-sig').replace('\r\n', '\n')
    for o, n in pairs:
        if s.count(o) != 1:
            raise SystemExit('count %d in %s: %r' % (s.count(o), p, o[:80]))
        s = s.replace(o, n)
    open(p, 'wb').write((b'\xef\xbb\xbf' if bom else b'') + s.replace('\n', '\r\n').encode('utf-8'))

edit('apps/android_direct_client/app/src/main/cpp/native_bridge.cpp', [
('''  int ParkPhaseCode() { return static_cast<int>(controller.ParkPhase()); }''',
 '''  int ParkPhaseCode() { return static_cast<int>(controller.ParkPhase()); }
  // keepalive-multi r2 M1: Connected = the authenticated Hello and the control link are up (WorkerMain sets it
  // only after both); Connect itself returns once the worker started.
  int Established() {
    const auto st = controller.Snapshot().state;
    if (st == remote60::native_poc::ClientSessionState::Connected) return 1;
    if (st == remote60::native_poc::ClientSessionState::Connecting) return 0;
    return -1;
  }
  uint64_t Generation() { return controller.ConnectionGeneration(); }'''),
('''void run_detached(std::function<void()> f) {
  try {
    std::thread(std::move(f)).detach();
  } catch (...) {
    f();
  }
}''', '''// The teardown away from the caller. The task is held by a shared_ptr, so a thread that cannot be created leaves it
// intact for the fallback (r2: std::thread(std::move(f)) may have moved it out before throwing). The fallback runs
// it in place -- under the table's transition lock (the closer is dispatched from inside it): a session's Close
// joins its own threads, none of which takes a table lock (their callbacks reach only the decoder's lock through
// the entry's adapter), so it cannot deadlock -- it only holds other switches until the join is done.
void run_detached(std::function<void()> f) {
  auto task = std::make_shared<std::function<void()>>(std::move(f));
  try {
    std::thread([task] { (*task)(); }).detach();
  } catch (...) {
    (*task)();
  }
}'''),
('''// The new connection is up: the eviction it reserved (if the limit needed one) happens now. The closed handles.
extern "C" JNIEXPORT jlongArray JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeCommit(
    JNIEnv* env, jobject /* this */, jlong handle) {
  std::vector<jlong> out;
  for (uint64_t h : g_table.Commit(static_cast<uint64_t>(handle), now_ms())) out.push_back(static_cast<jlong>(h));
  return to_jlongs(env, out);
}''', '''// keepalive-multi r2 M1: the new connection is up -- only once its session is established (authenticated Hello +
// control) on the generation `gen` its connect made. [status, closed...]: status 1 = committed (the closed handles
// follow: its reservation's eviction, now), 0 = pending (not established yet; nothing changed -- ask again),
// -1 = failed (it ended first, or another connection: given back, nothing evicted).
extern "C" JNIEXPORT jlongArray JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeCommit(
    JNIEnv* env, jobject /* this */, jlong handle, jlong gen) {
  using CR = remote60::native_poc::SessionTable<SessionEntry>::CommitResult;
  const auto r = g_table.Commit(static_cast<uint64_t>(handle), static_cast<uint64_t>(gen), now_ms());
  std::vector<jlong> out{r.kind == CR::Committed ? 1 : r.kind == CR::Pending ? 0 : -1};
  for (uint64_t h : r.closed) out.push_back(static_cast<jlong>(h));
  if (r.kind == CR::Failed) {
    std::fprintf(stderr, "[native-bridge] commit failed handle=%lld %s\\n", static_cast<long long>(handle), r.why.c_str());
  }
  return to_jlongs(env, out);
}'''),
('''  oss << "],\\"closing\\":" << g_table.PendingCloseCount() << ",\\"binding\\":" << g_video_decoder_sink.Binding() << "}";''',
 '''  const auto counts = g_table.CountsNow();
  oss << "],\\"closing\\":" << counts.closing << ",\\"live\\":" << counts.live << ",\\"reserved\\":" << counts.reserved
      << ",\\"marked\\":" << counts.marked << ",\\"binding\\":" << g_video_decoder_sink.Binding() << "}";'''),
])

edit('apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt', [
('''    /** The new connection is up: the handles its reservation evicted (LRU), closed now. */
    external fun nativeCommit(handle: Long): LongArray''',
 '''    /**
     * r2 M1: the new connection, made as generation [gen], is up -- [status, closed...]: 1 committed (the handles its
     * reservation evicted follow, closed now), 0 pending (its Hello is not answered yet: ask again), -1 failed (it
     * ended first: given back, nothing evicted). The eviction happens only for an established connection.
     */
    external fun nativeCommit(handle: Long, gen: Long): LongArray'''),
])
print('ok')
