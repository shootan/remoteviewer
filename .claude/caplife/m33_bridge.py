cpp = 'C:/work/gnlink-wt/feat-apk-0233/apps/android_direct_client/app/src/main/cpp/native_bridge.cpp'
kt = 'C:/work/gnlink-wt/feat-apk-0233/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt'

s = open(cpp, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
for old, new in [
    ('''Java_com_remote60_androiddirect_NativeSessionBridge_nativeQueryDesktopCaptureBackend(
    JNIEnv* /* env */, jobject /* this */) {
  // ua2-apk r1: ask what runs now (changes nothing); false toward a host that does not answer.
  return g_session_controller.QueryDesktopCaptureBackend() ? JNI_TRUE : JNI_FALSE;
}''', '''Java_com_remote60_androiddirect_NativeSessionBridge_nativeQueryDesktopCaptureBackend(
    JNIEnv* /* env */, jobject /* this */, jlong binding) {
  // ua2-apk r1: ask what runs now (changes nothing); false toward a host that does not answer.
  // keepalive-r2 merge: of the entry on screen (binding), like every other per-PC request.
  auto e = bound_entry(binding);
  if (!e) return JNI_FALSE;
  return e->controller.QueryDesktopCaptureBackend() ? JNI_TRUE : JNI_FALSE;
}'''),
    ('''Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetDesktopBackendStatus(
    JNIEnv* env, jobject /* this */) {
  const auto s = g_session_controller.DesktopCaptureBackendStatus();
  const auto i = g_session_controller.DesktopCaptureBackendIntent();
  // ua2-apk r3 (N3): known and answers from one read of the host's role
  const auto cap = g_session_controller.DesktopBackendCapability();''', '''Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetDesktopBackendStatus(
    JNIEnv* env, jobject /* this */, jlong binding) {
  // keepalive-r2 merge: the status of the entry on screen (binding); none bound = null (the screen says "not connected")
  auto e = bound_entry(binding);
  if (!e) return nullptr;
  const auto s = e->controller.DesktopCaptureBackendStatus();
  const auto i = e->controller.DesktopCaptureBackendIntent();
  // ua2-apk r3 (N3): known and answers from one read of the host's role
  const auto cap = e->controller.DesktopBackendCapability();'''),
]:
    assert s.count(old) == 1, old[:70]
    s = s.replace(old, new)
if crlf:
    s = s.replace('\n', '\r\n')
open(cpp, 'wb').write(s.encode('utf-8'))

k = open(kt, 'rb').read().decode('utf-8')
c2 = '\r\n' in k
k = k.replace('\r\n', '\n')
a = k.index('<<<<<<< HEAD\n')
m = k.index('=======\n', a)
b = k.index('>>>>>>> 6d85ce2\n', m)
theirs = k[m + len('=======\n'):b]
theirs = theirs.replace('''    external fun nativeRequestDesktopCaptureBackend(binding: Long, backend: Int): Boolean
''', '''    external fun nativeRequestDesktopCaptureBackend(binding: Long, backend: Int): Boolean
    /** ua2-apk r1: ask the host what runs now (false: nothing asked -- a host that does not answer). */
    external fun nativeQueryDesktopCaptureBackend(binding: Long): Boolean
    /** ua2-apk r1: DesktopBackendPresentation.HostStatus.of(...) reads it (null: no entry on screen). */
    external fun nativeGetDesktopBackendStatus(binding: Long): LongArray?
''')
k = k[:a] + theirs + k[b + len('>>>>>>> 6d85ce2\n'):]
assert '<<<<<<<' not in k
if c2:
    k = k.replace('\n', '\r\n')
open(kt, 'wb').write(k.encode('utf-8'))
print('ok')
