p = 'C:/work/gnlink-wt/feat-apk-0233/apps/android_direct_client/app/src/main/cpp/native_bridge.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep('#include "standby_runner.hpp"\n', '#include "standby_runner.hpp"\n#include "standby_session_link.hpp"\n')
rep("""  std::atomic<uint64_t> observes{0};
  std::atomic<uint64_t> punches{0};
""", """  std::atomic<uint64_t> observes{0};
  std::atomic<uint64_t> punches{0};
  // keepalive-r2 ② e (kept model A): the lease activation this entry's session came from, if any -- its SessionOver is
  // said right after every Disconnect of the entry (standby_session_link.hpp), so the lease pings again.
  remote60::native_poc::StandbySessionLink standby;
""")
rep("""  void Close() {
    gate.CloseExplicit([this] { controller.Disconnect(); }, [this] { rendezvous.Close(); },""",
    """  void Close() {
    gate.CloseExplicit([this] {
                         controller.Disconnect();
                         standby.Over();  // after Disconnect RETURNED: the worker and the video reader are joined
                       },
                       [this] { rendezvous.Close(); },""")
# CloseIfOwn's disconnect: the same pairing
i = s.index("Java_com_remote60_androiddirect_NativeSessionBridge_nativeCloseIfOwn(")
j = s.index("}\n", s.index("CloseIfOwn(static_cast<int64_t>(op)", i))
seg = s[i:j]
assert seg.count("[&e] { e->controller.Disconnect(); }") == 1, seg
s = s[:i] + seg.replace("[&e] { e->controller.Disconnect(); }",
                        "[&e] {\n                              e->controller.Disconnect();\n                              e->standby.Over();\n                            }") + s[j:]

s = s.rstrip('\n') + """

// keepalive-r2 ② e (kept model A, codex_keepalive_r2_kept_model.md): a tap on a PC whose lease is ready -- this NEW
// entry's session by that lease's activation, through the entry's gate under `op` (standby_session_link.hpp): no
// directory, observe, punch or ordinary Hello. The generation it made (0 = none; why: nativeDirectoryLastError --
// "standby_not_ready", "stale_operation", "connect_failed"). Off the UI thread (it may wait for the pump's read in flight).
extern "C" JNIEXPORT jlong JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeConnectStandby(JNIEnv* env, jobject /* this */, jlong handle,
                                                                         jstring key, jlong op) {
  auto e = entry_of(handle);
  if (!e) return 0;
  auto* r = standby_runner();
  if (!r) {
    e->SetError("standby_not_initialized");
    return 0;
  }
  SessionEntry* entry = e.get();
  const auto res = remote60::native_poc::standby_adopt(
      e->gate, static_cast<int64_t>(op), *r, jstring_to_string(env, key), e->controller, e->standby,
      [entry](remote60::native_poc::ClientSessionConnectArgs& args) {
        args.encodedFrameSink = &entry->sink;
        entry->controller.SetClipboardSyncEnabled(g_clipboard_sync_enabled.load());
      });
  {
    std::lock_guard<std::mutex> lk(e->infoMu);
    e->chosenCandidate = "standby";
  }
  e->SetError(res.generation != 0 ? std::string() : res.why);
  return static_cast<jlong>(res.generation);
}
"""
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

k = 'C:/work/gnlink-wt/feat-apk-0233/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt'
t = open(k, 'rb').read().decode('utf-8')
crlf = '\r\n' in t
t = t.replace('\r\n', '\n')
a = "    external fun nativeStandbyReleaseAll()\n"
assert t.count(a) == 1
t = t.replace(a, a + """    /**
     * keepalive-r2 ② e (kept model A): the reserved entry [handle]'s session by the ready lease [key]'s activation,
     * under connect operation [op]. The generation it made (0 = none: nativeDirectoryLastError says why). Off the UI thread.
     */
    external fun nativeConnectStandby(handle: Long, key: String, op: Long): Long
""")
if crlf:
    t = t.replace('\n', '\r\n')
open(k, 'wb').write(t.encode('utf-8'))
print('ok')
