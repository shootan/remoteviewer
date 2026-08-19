#include <jni.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <string>

#include "android_video_decoder.hpp"
#include "directory_rendezvous.hpp"
#include "input_macro.hpp"
#include "native_video_client_session.hpp"

namespace {

remote60::native_poc::ClientSessionController g_session_controller;
remote60::android_direct::AndroidVideoDecoderSink g_video_decoder_sink;
// Holds the punched socket between the two halves of the directory connect, which are split so
// that the HTTP calls can stay in the app where sessions and errors are already handled.
remote60::native_poc::DirectoryRendezvous g_rendezvous;
std::string g_rendezvous_error;
// Which of the offered addresses answered. Worth surfacing: "private" means the traffic never
// left the LAN, and a fallback after none answered is the case worth noticing in a bug report.
std::string g_chosen_candidate;

// One macro per session. Recording taps the same events the client is about to send, so what is
// captured is exactly what the host saw.
remote60::native_poc::InputMacro g_macro;

uint64_t now_ms() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

std::string jstring_to_string(JNIEnv* env, jstring value) {
  if (!value) return {};
  const char* chars = env->GetStringUTFChars(value, nullptr);
  std::string out = chars ? chars : "";
  if (chars) env->ReleaseStringUTFChars(value, chars);
  return out;
}

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
  oss << "],";
  oss << "\"hostSupportsMonitors\":" << (snapshot.hostSupportsMonitors ? "true" : "false") << ",";
  oss << "\"selectedMonitorId\":" << snapshot.selectedMonitorId << ",";
  oss << "\"monitors\":[";
  for (size_t i = 0; i < snapshot.monitors.size(); ++i) {
    const auto& mon = snapshot.monitors[i];
    if (i > 0) oss << ",";
    oss << "{\"id\":" << mon.id
        << ",\"x\":" << mon.x
        << ",\"y\":" << mon.y
        << ",\"width\":" << mon.width
        << ",\"height\":" << mon.height
        << ",\"primary\":" << (mon.primary ? "true" : "false") << "}";
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
  g_rendezvous.Close();
}

// Returns "ip:port" as the directory sees this device, or "" with the reason available from
// nativeDirectoryLastError. The socket stays open for the punch that follows.
extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeDirectoryObserve(
    JNIEnv* env, jobject /* this */, jstring directory_host, jint directory_udp_port,
    jstring observe_token) {
  std::string observed;
  g_rendezvous_error.clear();
  const bool ok = g_rendezvous.Observe(jstring_to_string(env, directory_host),
                                       directory_udp_port,
                                       jstring_to_string(env, observe_token), &observed,
                                       &g_rendezvous_error);
  return to_jstring(env, ok ? observed : std::string());
}

// Punches towards the host and starts the session on that same socket. A punch that times out
// is not fatal: the hello handshake still gets a chance, and reporting failure here would give
// up on NATs that would have worked.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeDirectoryConnect(
    JNIEnv* env, jobject /* this */, jstring host_ip, jint host_port, jint punch_budget_ms,
    jstring punch_token) {
  const std::string host = jstring_to_string(env, host_ip);
  g_rendezvous_error.clear();
  (void)g_rendezvous.Punch(host, host_port,
                           punch_budget_ms > 0 ? static_cast<uint32_t>(punch_budget_ms) : 4000u,
                           &g_rendezvous_error);

  const remote60::native_poc::SocketHandle prepared = g_rendezvous.Release();
  if (prepared == remote60::native_poc::kInvalidSocket) {
    if (g_rendezvous_error.empty()) g_rendezvous_error = "no prepared socket";
    return JNI_FALSE;
  }

  remote60::native_poc::ClientSessionConnectArgs args{};
  args.host = host;
  args.videoPort = host_port;
  // Never dialled on this path; the session only rejects a zero port.
  args.controlPort = host_port;
  args.requireTcpControl = false;
  args.controlOverUdp = true;
  args.peerAuthToken = jstring_to_string(env, punch_token);
  args.preparedUdpSocket = prepared;
  args.encodedFrameSink = &g_video_decoder_sink;
  return g_session_controller.Connect(args) ? JNI_TRUE : JNI_FALSE;
}

// Same as nativeDirectoryConnect, but given every address the directory offered instead of one.
//
// The client cannot tell from the inside which of them its own network permits: a company Wi-Fi
// blocks the high port outbound, a residential ISP blocks the well-known one inbound, and both
// look identical from here -- nothing arrives. So all of them are punched at once and whichever
// answers is used. Trying them in turn would multiply the wait by however many blocked addresses
// happen to come first in the list.
//
// `candidates` is "ip:port|kind" per entry, which keeps the JNI surface to a string array rather
// than a class the Kotlin side would have to mirror.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeDirectoryConnectAny(
    JNIEnv* env, jobject /* this */, jobjectArray candidates, jint punch_budget_ms,
    jstring punch_token) {
  g_rendezvous_error.clear();
  std::vector<remote60::native_poc::RendezvousCandidate> parsed;
  const jsize count = candidates ? env->GetArrayLength(candidates) : 0;
  for (jsize i = 0; i < count; ++i) {
    auto item = static_cast<jstring>(env->GetObjectArrayElement(candidates, i));
    if (!item) continue;
    const std::string text = jstring_to_string(env, item);
    env->DeleteLocalRef(item);
    const size_t colon = text.rfind(':');
    if (colon == std::string::npos) continue;
    const size_t bar = text.find('|', colon);
    const std::string portText = text.substr(colon + 1, bar == std::string::npos
                                                            ? std::string::npos
                                                            : bar - colon - 1);
    const int port = std::atoi(portText.c_str());
    if (port <= 0 || port > 65535) continue;
    remote60::native_poc::RendezvousCandidate candidate;
    candidate.ip = text.substr(0, colon);
    candidate.port = static_cast<uint16_t>(port);
    candidate.kind = bar == std::string::npos ? std::string() : text.substr(bar + 1);
    if (!candidate.ip.empty()) parsed.push_back(std::move(candidate));
  }
  if (parsed.empty()) {
    g_rendezvous_error = "no usable candidate";
    return JNI_FALSE;
  }

  remote60::native_poc::RendezvousCandidate chosen;
  const bool answered = g_rendezvous.PunchAny(
      parsed, punch_budget_ms > 0 ? static_cast<uint32_t>(punch_budget_ms) : 4000u, &chosen,
      &g_rendezvous_error);
  // A punch that answered tells us which address to use. When none did, fall back to the first
  // candidate and let the hello handshake try anyway -- some NATs pass it even after the punch
  // was dropped, and giving up here would refuse connections that would have worked.
  const remote60::native_poc::RendezvousCandidate& target = answered ? chosen : parsed.front();
  g_chosen_candidate = target.ip + ":" + std::to_string(target.port) + "|" +
                       (target.kind.empty() ? "unknown" : target.kind) +
                       (answered ? "" : " (no answer, trying anyway)");

  const remote60::native_poc::SocketHandle prepared = g_rendezvous.Release();
  if (prepared == remote60::native_poc::kInvalidSocket) {
    if (g_rendezvous_error.empty()) g_rendezvous_error = "no prepared socket";
    return JNI_FALSE;
  }

  remote60::native_poc::ClientSessionConnectArgs args{};
  args.host = target.ip;
  args.videoPort = target.port;
  args.controlPort = target.port;
  args.requireTcpControl = false;
  args.controlOverUdp = true;
  args.peerAuthToken = jstring_to_string(env, punch_token);
  args.preparedUdpSocket = prepared;
  args.encodedFrameSink = &g_video_decoder_sink;
  return g_session_controller.Connect(args) ? JNI_TRUE : JNI_FALSE;
}

// Which candidate answered, for the diagnostics log. Empty until a connect has been attempted.
extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeDirectoryChosenCandidate(
    JNIEnv* env, jobject /* this */) {
  return to_jstring(env, g_chosen_candidate);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeDirectoryLastError(
    JNIEnv* env, jobject /* this */) {
  return to_jstring(env, g_rendezvous_error);
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeSetSurface(
    JNIEnv* env, jobject /* this */, jobject surface) {
  g_video_decoder_sink.SetSurface(env, surface);
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeNotifyFrameDisplayed(
    JNIEnv* /* env */, jobject /* this */) {
  g_video_decoder_sink.NotifyFrameDisplayed();
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
Java_com_remote60_androiddirect_NativeSessionBridge_nativeSelectMonitor(
    JNIEnv* /* env */, jobject /* this */, jint monitor_id) {
  if (monitor_id < 0) return JNI_FALSE;
  return g_session_controller.RequestMonitorSelect(static_cast<uint32_t>(monitor_id))
             ? JNI_TRUE : JNI_FALSE;
}

// True while a UAC prompt or the lock screen is in front of the desktop, which is when nothing
// can be captured and the offer to unlock is worth making.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeIsHostScreenLocked(
    JNIEnv* /* env */, jobject /* this */) {
  return g_session_controller.HostSecureDesktopActive() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeRequestRuntimeConfig(
    JNIEnv* /* env */, jobject /* this */, jint bitrate_bps, jint fps) {
  if (fps > 0) {
    g_video_decoder_sink.SetTargetFps(
        static_cast<uint32_t>(std::clamp<jint>(fps, 1, 240)));
  }
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
  const uint16_t safe_kind = static_cast<uint16_t>(std::clamp<jint>(kind, 0, 0xffff));
  if (g_macro.IsRecording()) {
    remote60::native_poc::ControlInputEventMessage event{};
    event.kind = safe_kind;
    event.x = static_cast<int32_t>(x);
    event.y = static_cast<int32_t>(y);
    event.wheelDelta = static_cast<int32_t>(wheel_delta);
    event.keyCode = static_cast<uint32_t>(std::max<jint>(0, key_code));
    event.buttons = static_cast<uint16_t>(buttons & 0x7);
    g_macro.RecordEvent(event, now_ms());
  }
  return g_session_controller.QueueInputEvent(
             safe_kind,
             static_cast<int32_t>(x),
             static_cast<int32_t>(y),
             static_cast<int32_t>(wheel_delta),
             static_cast<uint32_t>(std::max<jint>(0, key_code)),
             static_cast<uint16_t>(buttons & 0x7))
             ? JNI_TRUE
             : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeMacroStartRecording(
    JNIEnv* /* env */, jobject /* this */) {
  g_macro.StartRecording(now_ms());
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeMacroStopRecording(
    JNIEnv* /* env */, jobject /* this */) {
  g_macro.StopRecording();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeMacroStartPlayback(
    JNIEnv* /* env */, jobject /* this */, jint timing_jitter_ms, jint position_jitter_px,
    jint repeat_count, jint gap_min_ms, jint gap_max_ms) {
  remote60::native_poc::MacroPlaybackOptions options;
  options.timingJitterMs = static_cast<uint32_t>(std::max<jint>(0, timing_jitter_ms));
  options.positionJitterPx = static_cast<uint32_t>(std::max<jint>(0, position_jitter_px));
  options.repeatCount = static_cast<uint32_t>(std::max<jint>(0, repeat_count));
  options.repeatGapMinMs = static_cast<uint32_t>(std::max<jint>(0, gap_min_ms));
  options.repeatGapMaxMs = static_cast<uint32_t>(std::max<jint>(0, gap_max_ms));
  const uint32_t seed = static_cast<uint32_t>(now_ms());
  return g_macro.StartPlayback(options, now_ms(), seed) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeMacroStopPlayback(
    JNIEnv* /* env */, jobject /* this */) {
  g_macro.StopPlayback();
}

/**
 * Sends whatever the macro says is due. Driven by the app's own ticker rather than a thread of
 * its own, so playback stops the moment the UI does and cannot outlive the session.
 */
extern "C" JNIEXPORT jint JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeMacroPump(
    JNIEnv* /* env */, jobject /* this */) {
  int sent = 0;
  remote60::native_poc::MacroStep step;
  const uint64_t now = now_ms();
  while (g_macro.PollDueStep(now, &step)) {
    g_session_controller.QueueInputEvent(step.kind, step.x, step.y, step.wheelDelta,
                                         step.keyCode, step.buttons);
    ++sent;
    if (sent > 64) break;   // one pump must not monopolise the UI thread
  }
  return sent;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeMacroState(
    JNIEnv* /* env */, jobject /* this */) {
  switch (g_macro.state()) {
    case remote60::native_poc::InputMacro::State::Recording: return 1;
    case remote60::native_poc::InputMacro::State::Playing: return 2;
    default: return 0;
  }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeMacroStepCount(
    JNIEnv* /* env */, jobject /* this */) {
  return static_cast<jint>(g_macro.StepCount());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeMacroCompletedRepeats(
    JNIEnv* /* env */, jobject /* this */) {
  return static_cast<jint>(g_macro.CompletedRepeats());
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeMacroClear(
    JNIEnv* /* env */, jobject /* this */) {
  g_macro.Clear();
}

/** The recorded actions as readable lines, newline separated. */
extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeMacroStepLines(
    JNIEnv* env, jobject /* this */) {
  const auto steps = g_macro.Steps();
  std::string out;
  for (size_t i = 0; i < steps.size(); ++i) {
    if (i > 0) out.push_back('\n');
    out += remote60::native_poc::InputMacro::DescribeStep(steps[i], i);
  }
  return to_jstring(env, out);
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeMacroSetPaused(
    JNIEnv* /* env */, jobject /* this */, jboolean paused) {
  g_macro.SetPaused(paused == JNI_TRUE, now_ms());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeMacroIsPaused(
    JNIEnv* /* env */, jobject /* this */) {
  return g_macro.IsPaused() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeMacroRemoveStep(
    JNIEnv* /* env */, jobject /* this */, jint index) {
  if (index < 0) return JNI_FALSE;
  return g_macro.RemoveStep(static_cast<size_t>(index)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeMacroUpdateStep(
    JNIEnv* /* env */, jobject /* this */, jint index, jint x, jint y, jint delay_ms) {
  if (index < 0) return JNI_FALSE;
  return g_macro.UpdateStep(static_cast<size_t>(index), static_cast<int32_t>(x),
                            static_cast<int32_t>(y),
                            static_cast<uint32_t>(std::max<jint>(0, delay_ms)))
             ? JNI_TRUE
             : JNI_FALSE;
}

/** One step as "kind x y wheel key buttons delay", for the edit dialog to prefill. */
extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeMacroStepFields(
    JNIEnv* env, jobject /* this */, jint index) {
  const auto steps = g_macro.Steps();
  if (index < 0 || static_cast<size_t>(index) >= steps.size()) return to_jstring(env, "");
  const auto& s = steps[static_cast<size_t>(index)];
  char buffer[128];
  std::snprintf(buffer, sizeof(buffer), "%u %d %d %d %u %u %u", s.kind, s.x, s.y, s.wheelDelta,
                s.keyCode, s.buttons, s.delayMs);
  return to_jstring(env, buffer);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeMacroSerialize(
    JNIEnv* env, jobject /* this */) {
  return to_jstring(env, g_macro.Serialize());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeMacroLoadSerialized(
    JNIEnv* env, jobject /* this */, jstring text) {
  if (!text) return JNI_FALSE;
  return g_macro.LoadSerialized(jstring_to_string(env, text)) ? JNI_TRUE : JNI_FALSE;
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
