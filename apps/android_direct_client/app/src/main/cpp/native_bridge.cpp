#include <jni.h>

#include <mutex>
#include <string>

namespace {

std::mutex g_state_mutex;
bool g_connected = false;
std::string g_status = "disconnected";
std::string g_last_error;

bool is_valid_port(jint port) {
  return port > 0 && port <= 65535;
}

void set_error(const std::string& message) {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  g_last_error = message;
}

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

  if (host_value.empty()) {
    set_error("host is required");
    return JNI_FALSE;
  }
  if (!is_valid_port(video_port)) {
    set_error("video port is invalid");
    return JNI_FALSE;
  }
  if (!is_valid_port(control_port)) {
    set_error("control port is invalid");
    return JNI_FALSE;
  }

  std::lock_guard<std::mutex> lock(g_state_mutex);
  g_connected = true;
  g_last_error.clear();
  g_status = "connected(shell-stub): " + host_value + ":" + std::to_string(video_port) +
             " control=" + std::to_string(control_port);
  return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeDisconnect(
    JNIEnv* /* env */, jobject /* this */) {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  g_connected = false;
  g_status = "disconnected";
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetStatus(
    JNIEnv* env, jobject /* this */) {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  return to_jstring(env, g_status);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetLastError(
    JNIEnv* env, jobject /* this */) {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  return to_jstring(env, g_last_error);
}
