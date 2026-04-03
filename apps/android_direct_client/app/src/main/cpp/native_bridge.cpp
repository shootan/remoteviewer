#include <jni.h>

#include <string>

#include "native_video_client_session.hpp"

namespace {

remote60::native_poc::ClientSessionController g_session_controller;

jstring to_jstring(JNIEnv* env, const std::string& value) {
  return env->NewStringUTF(value.c_str());
}

}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeConnect(
    JNIEnv* env, jobject /* this */, jstring host, jint video_port, jint control_port) {
  const char* host_chars = env->GetStringUTFChars(host, nullptr);
  const std::string host_value = host_chars ? host_chars : "";
  if (host_chars) {
    env->ReleaseStringUTFChars(host, host_chars);
  }

  remote60::native_poc::ClientSessionConnectArgs args{};
  args.host = host_value;
  args.videoPort = video_port;
  args.controlPort = control_port;
  return g_session_controller.Connect(args) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeDisconnect(
    JNIEnv* /* env */, jobject /* this */) {
  g_session_controller.Disconnect();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetStatus(
    JNIEnv* env, jobject /* this */) {
  return to_jstring(env, g_session_controller.Snapshot().status);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetLastError(
    JNIEnv* env, jobject /* this */) {
  return to_jstring(env, g_session_controller.Snapshot().lastError);
}
