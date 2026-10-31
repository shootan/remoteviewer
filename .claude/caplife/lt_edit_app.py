import glob
A = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/android_direct_client/app/'
G = glob.glob(A + 'build.gradle*')[0]


def edit(path, reps):
    s = open(path, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        n = s.count(a)
        assert n == 1, (path, a[:80], n)
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(path, 'w', encoding='utf-8', newline='').write(s)
    print('ok', path)


edit(G, [
    ('''  for (property in listOf("gnlink.testDirectoryUrl", "gnlink.testMigratableOrigins", "gnlink.testKeepParkTtlMs",
                          "gnlink.testStandbyAutoPrepare")) {''', '''  for (property in listOf("gnlink.testDirectoryUrl", "gnlink.testMigratableOrigins", "gnlink.testKeepParkTtlMs",
                          "gnlink.testStandbyAutoPrepare", "gnlink.livenessTrace")) {'''),
    ('''    buildConfigField("boolean", "STANDBY_AUTO_PREPARE", "false")
''', '''    buildConfigField("boolean", "STANDBY_AUTO_PREPARE", "false")
    // apk-decoder-liveness r2: the session's progress trace, logged by the app twice a second. OFF in every shipped
    // build; only a debug TEST build turns it on (below), with its native half (GNLINK_LIVENESS_TRACE).
    buildConfigField("boolean", "LIVENESS_TRACE", "false")
'''),
    ('''      if (project.findProperty("gnlink.testStandbyAutoPrepare") == "true") {
        buildConfigField("boolean", "STANDBY_AUTO_PREPARE", "true")
      }
''', '''      if (project.findProperty("gnlink.testStandbyAutoPrepare") == "true") {
        buildConfigField("boolean", "STANDBY_AUTO_PREPARE", "true")
      }
      // TEST BUILDS ONLY (apk-decoder-liveness r2, Codex design r2 (2)): -Pgnlink.livenessTrace=true -- the session's
      // receive / control progress, the owed key, the last keyframe request and the control transport's counters,
      // logged as `liveness_trace` twice a second. Metadata only. Refused for a release (above); native marker
      // "liveness-trace", which the release gate refuses.
      if (project.findProperty("gnlink.livenessTrace") == "true") {
        buildConfigField("boolean", "LIVENESS_TRACE", "true")
        externalNativeBuild {
          cmake {
            cppFlags += "-DGNLINK_LIVENESS_TRACE=1"
          }
        }
      }
'''),
])

edit(A + 'src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt', [
    ('''    external fun nativeGetVideoDebugStatus(): String
''', '''    external fun nativeGetVideoDebugStatus(): String
    // apk-decoder-liveness r2: the session's + the decoder's progress trace ("" unless a liveness-trace test build).
    external fun nativeLivenessTrace(handle: Long): String
'''),
])

edit(A + 'src/main/cpp/native_bridge.cpp', [
    ('''extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetVideoDebugStatus(
    JNIEnv* env, jobject /* this */) {
  return to_jstring(env, g_video_decoder_sink.DebugStatus());
}
''', '''extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetVideoDebugStatus(
    JNIEnv* env, jobject /* this */) {
  return to_jstring(env, g_video_decoder_sink.DebugStatus());
}

// apk-decoder-liveness r2: test builds only record it (GNLINK_LIVENESS_TRACE); "" otherwise. No lock of the decoder.
extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeLivenessTrace(
    JNIEnv* env, jobject /* this */, jlong handle) {
#ifdef GNLINK_LIVENESS_TRACE
  std::string line;
  if (auto e = entry_of(handle)) line = e->controller.LivenessTraceLine();
  else line = "session none";
  line += " | " + g_video_decoder_sink.LivenessTraceLine();
  return to_jstring(env, line);
#else
  (void)handle;
  return to_jstring(env, std::string());
#endif
}
'''),
])
