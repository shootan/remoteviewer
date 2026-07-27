#include <jni.h>

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>

#include "android_video_decoder.hpp"
#include "native_video_client_session.hpp"

namespace {

remote60::native_poc::ClientSessionController g_session_controller;
remote60::android_direct::AndroidVideoDecoderSink g_video_decoder_sink;

jstring to_jstring(JNIEnv* env, const std::string& value) {
  return env->NewStringUTF(value.c_str());
}

void append_json_escaped(std::ostringstream& oss, const std::string& value) {
  for (const unsigned char ch : value) {
    switch (ch) {
      case '\\':
        oss << "\\\\";
        break;
      case '"':
        oss << "\\\"";
        break;
      case '\b':
        oss << "\\b";
        break;
      case '\f':
        oss << "\\f";
        break;
      case '\n':
        oss << "\\n";
        break;
      case '\r':
        oss << "\\r";
        break;
      case '\t':
        oss << "\\t";
        break;
      default:
        if (ch < 0x20u) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(ch));
          oss << buf;
        } else {
          oss << static_cast<char>(ch);
        }
        break;
    }
  }
}

std::string window_panel_snapshot_json(const remote60::native_poc::WindowPanelSnapshot& snapshot) {
  std::ostringstream oss;
  oss << "{";
  oss << "\"selectedId\":" << snapshot.selectedId << ",";
  oss << "\"selectedTitle\":\"";
  append_json_escaped(oss, snapshot.selectedTitle);
  oss << "\",";
  oss << "\"selectedWidth\":" << snapshot.selectedWidth << ",";
  oss << "\"selectedHeight\":" << snapshot.selectedHeight << ",";
  oss << "\"selectionLocked\":" << (snapshot.selectionLocked ? "true" : "false") << ",";
  oss << "\"status\":\"";
  append_json_escaped(oss, snapshot.status);
  oss << "\",";
  oss << "\"lastSelectSeq\":" << snapshot.lastSelectSeq << ",";
  oss << "\"lastSelectOk\":" << (snapshot.lastSelectOk ? "true" : "false") << ",";
  oss << "\"lastSelectWindowId\":" << snapshot.lastSelectWindowId << ",";
  oss << "\"lastSelectStreamGeneration\":" << snapshot.lastSelectStreamGeneration << ",";
  oss << "\"lastSelectHostSendQpcUs\":" << snapshot.lastSelectHostSendQpcUs << ",";
  oss << "\"items\":[";
  for (size_t i = 0; i < snapshot.items.size(); ++i) {
    const auto& item = snapshot.items[i];
    if (i > 0) oss << ",";
    oss << "{";
    oss << "\"id\":" << item.id << ",";
    oss << "\"pid\":" << item.pid << ",";
    oss << "\"width\":" << item.width << ",";
    oss << "\"height\":" << item.height << ",";
    oss << "\"minimized\":" << (item.minimized ? "true" : "false") << ",";
    oss << "\"thumbVersion\":" << g_session_controller.WindowThumbnailVersion(item.id) << ",";
    oss << "\"title\":\"";
    append_json_escaped(oss, item.title);
    oss << "\"}";
  }
  oss << "]";
  oss << "}";
  return oss.str();
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
  args.encodedFrameSink = &g_video_decoder_sink;
  return g_session_controller.Connect(args) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeDisconnect(
    JNIEnv* /* env */, jobject /* this */) {
  g_session_controller.Disconnect();
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeSetSurface(
    JNIEnv* env, jobject /* this */, jobject surface) {
  g_video_decoder_sink.SetSurface(env, surface);
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

extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetVideoDebugStatus(
    JNIEnv* env, jobject /* this */) {
  return to_jstring(env, g_video_decoder_sink.DebugStatus());
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetVideoSizePacked(
    JNIEnv* /* env */, jobject /* this */) {
  return static_cast<jlong>(g_video_decoder_sink.VideoSizePacked());
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativePrepareVideoSwitch(
    JNIEnv* /* env */, jobject /* this */, jlong selection_generation) {
  g_video_decoder_sink.PrepareForWindowSelection(static_cast<uint64_t>(selection_generation));
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeAbortVideoSwitch(
    JNIEnv* /* env */, jobject /* this */) {
  g_video_decoder_sink.AbortWindowSelection();
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetReadySelectionGeneration(
    JNIEnv* /* env */, jobject /* this */) {
  return static_cast<jlong>(g_video_decoder_sink.ReadySelectionGeneration());
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetLastOutputPresentationUs(
    JNIEnv* /* env */, jobject /* this */) {
  return static_cast<jlong>(g_video_decoder_sink.LastOutputPresentationUs());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeRequestWindowList(
    JNIEnv* /* env */, jobject /* this */) {
  return g_session_controller.RequestWindowList() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeSelectWindow(
    JNIEnv* /* env */, jobject /* this */, jlong window_id) {
  return g_session_controller.RequestWindowSelect(static_cast<uint64_t>(window_id)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeSelectDesktopMode(
    JNIEnv* /* env */, jobject /* this */) {
  return g_session_controller.RequestDesktopMode() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeRequestRuntimeConfig(
    JNIEnv* /* env */, jobject /* this */, jint bitrate_bps, jint fps) {
  return g_session_controller.RequestRuntimeConfig(
             static_cast<uint32_t>(std::max<jint>(0, bitrate_bps)),
             static_cast<uint32_t>(std::max<jint>(0, fps)))
             ? JNI_TRUE
             : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeRequestDesktopCaptureBackend(
    JNIEnv* /* env */, jobject /* this */, jint backend) {
  return g_session_controller.RequestDesktopCaptureBackend(
             static_cast<uint16_t>(std::clamp<jint>(backend, 0, 0xffff)))
             ? JNI_TRUE
             : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeRequestStreamActive(
    JNIEnv* /* env */, jobject /* this */, jboolean active) {
  return g_session_controller.RequestStreamActive(active == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeQueueInputEvent(
    JNIEnv* /* env */, jobject /* this */, jint kind, jint x, jint y, jint wheel_delta,
    jint key_code, jint buttons) {
  return g_session_controller.QueueInputEvent(
             static_cast<uint16_t>(std::clamp<jint>(kind, 0, 0xffff)),
             static_cast<int32_t>(x),
             static_cast<int32_t>(y),
             static_cast<int32_t>(wheel_delta),
             static_cast<uint32_t>(std::max<jint>(0, key_code)),
             static_cast<uint16_t>(buttons & 0x7))
             ? JNI_TRUE
             : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeQueueInputText(
    JNIEnv* env, jobject /* this */, jstring text) {
  if (!text) return JNI_FALSE;
  const jsize length = env->GetStringLength(text);
  if (length <= 0) return JNI_FALSE;
  const jchar* chars = env->GetStringChars(text, nullptr);
  if (!chars) return JNI_FALSE;
  const bool ok = g_session_controller.QueueInputText(
      reinterpret_cast<const uint16_t*>(chars), static_cast<size_t>(length));
  env->ReleaseStringChars(text, chars);
  return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetWindowPanelJson(
    JNIEnv* env, jobject /* this */) {
  return to_jstring(env, window_panel_snapshot_json(g_session_controller.WindowPanelSnapshotCopy()));
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeResetVideoStream(
    JNIEnv* /* env */, jobject /* this */) {
  g_video_decoder_sink.OnVideoStreamReset();
}

// Preview pixels for one target card. Layout: [width:int32][height:int32][rgba bytes...],
// little-endian, RGBA byte order ready for Bitmap.copyPixelsFromBuffer. Null if no preview
// has been fetched yet for this window id (0 = desktop).
extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetWindowThumbnail(
    JNIEnv* env, jobject /* this */, jlong window_id) {
  remote60::native_poc::ClientSessionController::WindowThumbnail thumb;
  if (!g_session_controller.CopyWindowThumbnail(static_cast<uint64_t>(window_id), &thumb)) {
    return nullptr;
  }
  const jsize total = static_cast<jsize>(8 + thumb.rgba.size());
  jbyteArray out = env->NewByteArray(total);
  if (!out) return nullptr;
  const int32_t dims[2] = {static_cast<int32_t>(thumb.width), static_cast<int32_t>(thumb.height)};
  env->SetByteArrayRegion(out, 0, 8, reinterpret_cast<const jbyte*>(dims));
  env->SetByteArrayRegion(out, 8, static_cast<jsize>(thumb.rgba.size()),
                          reinterpret_cast<const jbyte*>(thumb.rgba.data()));
  return out;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetSessionBytesReceived(
    JNIEnv* /* env */, jobject /* this */) {
  return static_cast<jlong>(g_session_controller.SessionBytesReceived());
}
