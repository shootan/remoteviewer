#include <jni.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "android_video_decoder.hpp"
#include "connect_op_gate.hpp"
#include "directory_rendezvous.hpp"
#include "entry_sink_adapter.hpp"
#include "input_macro.hpp"
#include "native_video_client_session.hpp"
#include "session_table.hpp"
#include "utf8_bounded.hpp"

namespace {

// keepalive-multi r1 (codex_keepalive_multi_r1.md; docs/keepalive_multi_resource_table.md): one entry per PC
// connection the phone keeps. What 0.2.30 held in globals -- the session, the rendezvous socket, its error and chosen
// candidate, the observe / punch counts, the connect gate -- is the entry's own. The decoder and its Surface stay
// single; each entry reaches it only through its EntrySinkAdapter, whose binding token is non-zero only while that
// entry is on screen (SessionTable::Activate), so a parked or closing PC's late callback does nothing to the picture.
remote60::android_direct::AndroidVideoDecoderSink g_video_decoder_sink;

struct SessionEntry {
  explicit SessionEntry(uint64_t h) : handle(h), sink(&g_video_decoder_sink) {}
  const uint64_t handle;
  // Declared before the controller: members are destroyed in reverse order, so the controller (and the threads it
  // joins) is gone before the adapter it calls into.
  remote60::android_direct::EntrySinkAdapter sink;
  remote60::native_poc::ClientSessionController controller;
  // Holds the punched socket between the two halves of the directory connect, which are split so that the HTTP
  // calls can stay in the app where sessions and errors are already handled.
  remote60::native_poc::DirectoryRendezvous rendezvous;
  // keepalive r3 R2-a / r4 / r5 T1, now per PC: the entry's connect operation (numbers are process-unique, the
  // cancel is this entry's -- a connect to B does not stop a parked A's steps).
  remote60::native_poc::ConnectOpGate gate;
  std::mutex infoMu;
  std::string lastError;        // the last observe / punch error of this entry
  // Which of the offered addresses answered. "private" means the traffic never left the LAN, and a fallback after
  // none answered is the case worth noticing in a bug report.
  std::string chosenCandidate;
  // keepalive r1: how many observes and punches this entry ran -- the reuse of a parked one must add none (nor a
  // session Connect / UDP Hello: the controller's own counters).
  std::atomic<uint64_t> observes{0};
  std::atomic<uint64_t> punches{0};

  void SetError(const std::string& e) {
    std::lock_guard<std::mutex> lk(infoMu);
    lastError = e;
  }
  std::string Error() {
    std::lock_guard<std::mutex> lk(infoMu);
    return lastError;
  }

  // SessionTable's entry interface (session_table.hpp).
  int ParkPhaseCode() { return static_cast<int>(controller.ParkPhase()); }
  // Not ended: a session still finishing its handshake right after Connect (state Connecting) is alive -- only one
  // that is Disconnected / Error ended on its own (the first AVD run reaped B here as "peer_lost" while it connected).
  bool Alive() {
    const auto st = controller.Snapshot().state;
    return st == remote60::native_poc::ClientSessionState::Connected ||
           st == remote60::native_poc::ClientSessionState::Connecting;
  }
  bool BeginPark() { return controller.BeginPark(); }
  bool ResumeFromPark() { return controller.ResumeFromPark(); }
  void SetParkLimitMs(uint64_t ms) { controller.SetParkLimitMs(ms); }
  void SetBindingToken(uint64_t t) { sink.SetToken(t); }
  uint64_t BindingToken() { return sink.Token(); }
  // Off every table lock (the closer's thread): the session under the adoption lock, the rendezvous socket under
  // the step lock, each once (r5 T1).
  void Close() {
    gate.CloseExplicit([this] { controller.Disconnect(); }, [this] { rendezvous.Close(); },
                       [](const std::function<void()>& closeRendezvous) { closeRendezvous(); });
  }
};

void run_detached(std::function<void()> f) {
  try {
    std::thread(std::move(f)).detach();
  } catch (...) {
    f();
  }
}

remote60::native_poc::SessionTable<SessionEntry> g_table(
    remote60::native_poc::SessionTable<SessionEntry>::Binder{
        [](uint64_t token) { g_video_decoder_sink.Bind(token); },
        [](uint64_t token) { return g_video_decoder_sink.Unbind(token); }},
    [](std::function<void()> close) { run_detached(std::move(close)); });

// The clipboard switch is the user's, not a PC's: applied to every kept connection and to each new one.
std::atomic<bool> g_clipboard_sync_enabled{true};

uint64_t now_ms();

std::shared_ptr<SessionEntry> entry_of(jlong handle) {
  return handle > 0 ? g_table.Find(static_cast<uint64_t>(handle)) : nullptr;
}
// The entry on screen with the binding the app captured when the action was taken (0 / stale = none).
std::shared_ptr<SessionEntry> bound_entry(jlong binding) {
  return binding > 0 ? g_table.FindByBinding(static_cast<uint64_t>(binding)) : nullptr;
}

remote60::native_poc::ConnectOpGate::Steps session_adopt_steps(SessionEntry& e,
                                                               remote60::native_poc::ClientSessionConnectArgs& args) {
  remote60::native_poc::ConnectOpGate::Steps steps;
  steps.connect = [&e, &args] {
    e.controller.SetClipboardSyncEnabled(g_clipboard_sync_enabled.load());
    return e.controller.Connect(args);
  };
  steps.generation = [&e] { return e.controller.ConnectionGeneration(); };
  steps.disconnect = [&e] { e.controller.Disconnect(); };
  return steps;
}

// One macro per session. Recording taps the same events the client is about to send, so what is
// captured is exactly what the host saw.
remote60::native_poc::InputMacro g_macro;
// keepalive-multi r1: the binding playback was started on -- its steps go to that one only (a switch stops it).
std::atomic<uint64_t> g_macro_binding{0};

uint64_t now_ms() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

// Both directions go through UTF-16 instead of the JVM's "modified UTF-8". NewStringUTF ABORTS
// the process on malformed input, and a window title cut mid-character by an older host is
// exactly that -- the app died on every window list from one PC (2026-09-25). It also cannot
// carry 4-byte characters (emoji) correctly. GetStringUTFChars has the mirror problem: it hands
// back surrogate pairs as 6-byte CESU-8 rather than UTF-8. Invalid bytes become U+FFFD.
std::string jstring_to_string(JNIEnv* env, jstring value) {
  if (!value) return {};
  const jsize len = env->GetStringLength(value);
  const jchar* chars = env->GetStringChars(value, nullptr);
  if (!chars) return {};
  std::string out = remote60::native_poc::utf16_to_utf8_lossy(
      reinterpret_cast<const char16_t*>(chars), static_cast<size_t>(len));
  env->ReleaseStringChars(value, chars);
  return out;
}

jstring to_jstring(JNIEnv* env, const std::string& value) {
  const std::u16string wide = remote60::native_poc::utf8_to_utf16_lossy(value);
  return env->NewString(reinterpret_cast<const jchar*>(wide.data()),
                        static_cast<jsize>(wide.size()));
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

std::string window_panel_snapshot_json(const remote60::native_poc::ClientSessionController& controller,
                                       const remote60::native_poc::WindowPanelSnapshot& snapshot) {
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
    oss << "\"thumbVersion\":" << controller.WindowThumbnailVersion(item.id) << ",";
    oss << "\"title\":\"";
    append_json_escaped(oss, item.title);
    oss << "\"}";
  }
  oss << "],";
  oss << "\"hostSupportsMonitors\":" << (snapshot.hostSupportsMonitors ? "true" : "false") << ",";
  oss << "\"hostSupportsMonitorSelect\":" << (snapshot.hostSupportsMonitorSelect ? "true" : "false") << ",";
  oss << "\"monitorListRevision\":" << snapshot.monitorListRevision << ",";
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

// ---------------------------------------------------------------- keepalive-multi r1: the kept PCs

namespace {
const char* reservation_kind(remote60::native_poc::SessionRegistry::Reservation::Kind k) {
  using R = remote60::native_poc::SessionRegistry::Reservation;
  return k == R::Reuse ? "reuse" : k == R::New ? "new" : "refused";
}
jlongArray to_jlongs(JNIEnv* env, const std::vector<jlong>& v) {
  jlongArray out = env->NewLongArray(static_cast<jsize>(v.size()));
  if (out && !v.empty()) env->SetLongArrayRegion(out, 0, static_cast<jsize>(v.size()), v.data());
  return out;
}
}  // namespace

// A tap on a PC (key = directory origin|account|hostId) or a manual connect (manual=true): [kind, handle, victim]
// with kind 0 reuse (the live entry), 1 new (reserved; its eviction, if any, happens at commit), 2 refused
// (handle = the entry already connecting for this key, or 0; why via nativeTableLastRefusal). epoch = the
// lifecycle epoch the tap was made in: a tap from before a sign-out / stop is refused.
extern "C" JNIEXPORT jlongArray JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeReserve(
    JNIEnv* env, jobject /* this */, jstring key, jboolean manual, jlong epoch) {
  g_table.Reap(now_ms());
  const auto r = g_table.Reserve(jstring_to_string(env, key), manual == JNI_TRUE, static_cast<uint64_t>(epoch),
                                 [](uint64_t h) { return std::make_shared<SessionEntry>(h); });
  std::fprintf(stderr, "[native-bridge] reserve %s handle=%llu victim=%llu %s\n", reservation_kind(r.kind),
               static_cast<unsigned long long>(r.handle), static_cast<unsigned long long>(r.victim), r.why.c_str());
  using R = remote60::native_poc::SessionRegistry::Reservation;
  return to_jlongs(env, {r.kind == R::Reuse ? 0 : r.kind == R::New ? 1 : 2, static_cast<jlong>(r.handle),
                         static_cast<jlong>(r.victim)});
}

// The new connection is up: the eviction it reserved (if the limit needed one) happens now. The closed handles.
extern "C" JNIEXPORT jlongArray JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeCommit(
    JNIEnv* env, jobject /* this */, jlong handle) {
  std::vector<jlong> out;
  for (uint64_t h : g_table.Commit(static_cast<uint64_t>(handle), now_ms())) out.push_back(static_cast<jlong>(h));
  return to_jlongs(env, out);
}

// The connect failed or went stale: this entry's own resources go; nothing else is evicted.
extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeFail(
    JNIEnv* env, jobject /* this */, jlong handle, jstring reason) {
  g_table.Fail(static_cast<uint64_t>(handle), jstring_to_string(env, reason), now_ms());
}

// On screen: the binding token every screen action carries from now on (0 = refused: another PC is still on screen
// or still confirming its park, or this one could not resume -- then it is ended). A macro playing for the PC that
// was on screen is stopped.
extern "C" JNIEXPORT jlong JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeActivate(
    JNIEnv* /* env */, jobject /* this */, jlong handle) {
  g_macro.StopPlayback();
  g_macro_binding.store(0);
  const uint64_t token = g_table.Activate(static_cast<uint64_t>(handle), now_ms());
  std::fprintf(stderr, "[native-bridge] activate handle=%lld binding=%llu\n", static_cast<long long>(handle),
               static_cast<unsigned long long>(token));
  return static_cast<jlong>(token);
}

// One PC ends (its disconnect, refused, a failed / unsupported pause, replaced, peer lost). The others stay.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeEnd(
    JNIEnv* env, jobject /* this */, jlong handle, jstring reason) {
  if (auto e = entry_of(handle); e && e->BindingToken() != 0 && g_macro_binding.load() == e->BindingToken()) {
    g_macro.StopPlayback();
    g_macro.StopRecording();
  }
  return g_table.End(static_cast<uint64_t>(handle), jstring_to_string(env, reason), now_ms()) ? JNI_TRUE : JNI_FALSE;
}

// Everything ends (sign-out, account / directory change, the app stopped, a network change, disconnect all): a new
// lifecycle epoch first, then every entry, torn down away from the UI thread. The new epoch.
extern "C" JNIEXPORT jlong JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeEndAll(
    JNIEnv* env, jobject /* this */, jstring reason) {
  g_macro.StopPlayback();
  g_macro.StopRecording();
  g_macro_binding.store(0);
  return static_cast<jlong>(g_table.EndAll(jstring_to_string(env, reason), now_ms()));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeEpoch(
    JNIEnv* /* env */, jobject /* this */) {
  return static_cast<jlong>(g_table.Epoch());
}

// The policy object: how many PCs are kept (the one on screen included) and each one's park limit.
extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeSetKeepalivePolicy(
    JNIEnv* /* env */, jobject /* this */, jint max_hosts, jlong park_ttl_ms) {
  remote60::native_poc::KeepalivePolicy p;
  if (max_hosts > 0) p.maxHosts = static_cast<uint32_t>(max_hosts);
  if (park_ttl_ms > 0) p.parkTtlMs = static_cast<uint64_t>(park_ttl_ms);
  g_table.SetPolicy(p);
}

// Every kept PC and the recent endings, as JSON for the list (no token, no address): sessions that ended on their own
// are taken off first. remainingMs counts from that PC's own BeginPark.
extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeEntriesJson(
    JNIEnv* env, jobject /* this */) {
  const uint64_t now = now_ms();
  g_table.Reap(now);
  std::ostringstream oss;
  oss << "{\"epoch\":" << g_table.Epoch() << ",\"maxHosts\":" << g_table.Policy().maxHosts
      << ",\"parkTtlMs\":" << g_table.Policy().parkTtlMs << ",\"entries\":[";
  bool first = true;
  for (const auto& re : g_table.Snapshot()) {
    auto e = g_table.Find(re.handle);
    if (!first) oss << ",";
    first = false;
    oss << "{\"handle\":" << re.handle << ",\"key\":\"";
    append_json_escaped(oss, re.key);
    oss << "\",\"phase\":\"" << remote60::native_poc::entry_phase_name(re.phase) << "\""
        << ",\"manual\":" << (re.manual ? "true" : "false")
        << ",\"parkPhase\":" << (e ? e->ParkPhaseCode() : -1)
        << ",\"alive\":" << (e && e->Alive() ? "true" : "false")
        << ",\"generation\":" << (e ? e->controller.ConnectionGeneration() : 0)
        << ",\"remainingMs\":"
        << (re.phase == remote60::native_poc::EntryPhase::Parked && re.parkDeadlineMs > now ? re.parkDeadlineMs - now : 0)
        << ",\"parkEpisode\":" << re.parkEpisode << ",\"reason\":\"";
    append_json_escaped(oss, re.endReason);
    oss << "\"}";
  }
  oss << "],\"ended\":[";
  first = true;
  for (const auto& x : g_table.Ended()) {
    if (!first) oss << ",";
    first = false;
    oss << "{\"handle\":" << x.handle << ",\"key\":\"";
    append_json_escaped(oss, x.key);
    oss << "\",\"reason\":\"";
    append_json_escaped(oss, x.reason);
    oss << "\",\"agoMs\":" << (now >= x.atMs ? now - x.atMs : 0) << "}";
  }
  oss << "],\"closing\":" << g_table.PendingCloseCount() << ",\"binding\":" << g_video_decoder_sink.Binding() << "}";
  return to_jstring(env, oss.str());
}

// r4 S1: the manual (IP) connect is an operation like the directory's -- `op` from nativeConnectOpNext, adopted
// through its entry's gate, so a stale directory operation's cleanup can never close it. The generation it
// made comes back (0 = none). keepalive-multi r1: `handle` is the entry nativeReserve made for it.
extern "C" JNIEXPORT jlong JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeConnect(
    JNIEnv* env, jobject /* this */, jlong handle, jstring host, jint video_port, jint control_port, jlong op) {
  auto e = entry_of(handle);
  if (!e) return 0;
  const std::string host_value = jstring_to_string(env, host);

  remote60::native_poc::ClientSessionConnectArgs args{};
  args.host = host_value;
  args.videoPort = video_port;
  args.controlPort = control_port;
  args.encodedFrameSink = &e->sink;
  return static_cast<jlong>(e->gate.Adopt(static_cast<int64_t>(op), session_adopt_steps(*e, args)));
}

// r4 S2-a: a new connect operation, numbered here -- never a number this process handed out before, so a
// new Activity cannot make an older one's operation current again. Also the invalidation (the app takes a
// number it does not use).
extern "C" JNIEXPORT jlong JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeConnectOpNext(
    JNIEnv* /* env */, jobject /* this */, jlong handle) {
  auto e = entry_of(handle);
  return e ? static_cast<jlong>(e->gate.Next()) : 0;
}

// r4 S1: a stale operation's late cleanup -- closes the session only if `op` adopted generation `gen` and it
// is still the one running, compared and closed in one step with every adoption. true = closed.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeCloseIfOwn(
    JNIEnv* /* env */, jobject /* this */, jlong handle, jlong op, jlong gen) {
  auto e = entry_of(handle);
  if (!e) return JNI_FALSE;
  return e->gate.CloseIfOwn(static_cast<int64_t>(op), static_cast<uint64_t>(gen),
                            [&e] { return e->controller.ConnectionGeneration(); },
                            [&e] { e->controller.Disconnect(); })
             ? JNI_TRUE
             : JNI_FALSE;
}

// Returns "ip:port" as the directory sees this device, or "" with the reason available from
// nativeDirectoryLastError. The socket stays open for the punch that follows.
extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeDirectoryObserve(
    JNIEnv* env, jobject /* this */, jlong handle, jstring directory_host, jint directory_udp_port,
    jstring observe_token, jlong op) {
  std::string observed;
  auto e = entry_of(handle);
  if (!e) return to_jstring(env, std::string());
  e->SetError(std::string());
  // r4 S2: one network step at a time, only for the current operation; its stop flag goes true the moment
  // it is not, and nothing of this call can clear it. The socket it observes with becomes this operation's.
  remote60::native_poc::ConnectOpGate::StepGuard step(e->gate, static_cast<int64_t>(op));
  if (!step.entered() || !e->gate.ClaimRendezvous(static_cast<int64_t>(op))) {
    e->SetError("stale_operation");
    return to_jstring(env, std::string());
  }
  e->observes.fetch_add(1);
  std::string error;
  const bool ok = e->rendezvous.Observe(jstring_to_string(env, directory_host),
                                        directory_udp_port,
                                        jstring_to_string(env, observe_token), &observed,
                                        &error, step.stop());
  e->SetError(error);
  return to_jstring(env, ok ? observed : std::string());
}

// Punches every address the directory offered and starts the session on the observed socket. A punch that
// times out is not fatal: the hello handshake still gets a chance on NATs that would have worked.
//
// The client cannot tell from the inside which of them its own network permits: a company Wi-Fi
// blocks the high port outbound, a residential ISP blocks the well-known one inbound, and both
// look identical from here -- nothing arrives. So all of them are punched at once and whichever
// answers is used. Trying them in turn would multiply the wait by however many blocked addresses
// happen to come first in the list.
//
// `candidates` is "ip:port|kind" per entry, which keeps the JNI surface to a string array rather
// than a class the Kotlin side would have to mirror.
// r3 R2-a: `op` is the app's connect operation; the generation of the connection this call made comes back
// (0 = none: failed, or the operation went stale -- before the punch, after it, or while Connect ran).
extern "C" JNIEXPORT jlong JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeDirectoryConnectAny(
    JNIEnv* env, jobject /* this */, jlong handle, jobjectArray candidates, jint punch_budget_ms,
    jstring punch_token, jlong op) {
  auto e = entry_of(handle);
  if (!e) return 0;
  e->SetError(std::string());
  std::string error;
  const int64_t owner = static_cast<int64_t>(op);
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
    e->SetError("no usable candidate");
    return 0;
  }

  remote60::native_poc::RendezvousCandidate chosen;
  remote60::native_poc::SocketHandle prepared = remote60::native_poc::kInvalidSocket;
  bool answered = false;
  {
    // r4 S2: the punch and the hand-over are one network step of the current operation, on the socket THIS
    // operation observed with (another operation's observe made it theirs: not punched, not handed over).
    remote60::native_poc::ConnectOpGate::StepGuard step(e->gate, owner);
    if (!step.entered() || !e->gate.OwnsRendezvous(owner)) {
      e->SetError("stale_operation");
      return 0;
    }
    e->punches.fetch_add(1);
    answered = e->rendezvous.PunchAny(
        parsed, punch_budget_ms > 0 ? static_cast<uint32_t>(punch_budget_ms) : 4000u, &chosen,
        &error, step.stop());
    // r3 R2-a: stale after the punch -- the socket is not handed to a session.
    if (step.stopped() || !e->gate.OwnsRendezvous(owner)) {
      e->rendezvous.Close();
      e->SetError("stale_operation");
      return 0;
    }
    prepared = e->rendezvous.Release();
  }
  // A punch that answered tells us which address to use. When none did, fall back to the first
  // candidate and let the hello handshake try anyway -- some NATs pass it even after the punch
  // was dropped, and giving up here would refuse connections that would have worked.
  const remote60::native_poc::RendezvousCandidate& target = answered ? chosen : parsed.front();
  const std::string chosen_text = target.ip + ":" + std::to_string(target.port) + "|" +
                       (target.kind.empty() ? "unknown" : target.kind) +
                       (answered ? "" : " (no answer, trying anyway)");
  {
    std::lock_guard<std::mutex> lk(e->infoMu);
    e->chosenCandidate = chosen_text;
  }

  if (prepared == remote60::native_poc::kInvalidSocket) {
    e->SetError(error.empty() ? std::string("no prepared socket") : error);
    return 0;
  }

  remote60::native_poc::ClientSessionConnectArgs args{};
  args.host = target.ip;
  args.videoPort = target.port;
  args.controlPort = target.port;
  args.requireTcpControl = false;
  args.controlOverUdp = true;
  args.peerAuthToken = jstring_to_string(env, punch_token);
  args.preparedUdpSocket = prepared;
  args.encodedFrameSink = &e->sink;
  remote60::native_poc::ConnectOpGate::Steps steps = session_adopt_steps(*e, args);
  steps.closePrepared = [prepared] {
    remote60::native_poc::SocketHandle s = prepared;
    remote60::native_poc::close_socket(&s);
  };
  const uint64_t made = e->gate.Adopt(owner, steps);
  if (made == 0 && !e->gate.IsCurrent(owner)) error = "stale_operation";
  e->SetError(error);
  return static_cast<jlong>(made);
}

// Which candidate answered, for the diagnostics log. Empty until a connect has been attempted.
extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeDirectoryChosenCandidate(
    JNIEnv* env, jobject /* this */, jlong handle) {
  auto e = entry_of(handle);
  if (!e) return to_jstring(env, std::string());
  std::lock_guard<std::mutex> lk(e->infoMu);
  return to_jstring(env, e->chosenCandidate);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeDirectoryLastError(
    JNIEnv* env, jobject /* this */, jlong handle) {
  auto e = entry_of(handle);
  return to_jstring(env, e ? e->Error() : std::string("no_entry"));
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
    JNIEnv* env, jobject /* this */, jlong handle) {
  auto e = entry_of(handle);
  if (!e) return to_jstring(env, "disconnected");
  return to_jstring(env, e->controller.Snapshot().status);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetLastError(
    JNIEnv* env, jobject /* this */, jlong handle) {
  auto e = entry_of(handle);
  if (!e) return to_jstring(env, std::string());
  return to_jstring(env, e->controller.Snapshot().lastError);
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

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativePrepareVideoSwitch(
    JNIEnv* /* env */, jobject /* this */, jlong binding, jlong selection_generation) {
  if (binding <= 0) return JNI_FALSE;
  return g_video_decoder_sink.PrepareForWindowSelectionAs(static_cast<uint64_t>(binding),
                                                          static_cast<uint64_t>(selection_generation))
             ? JNI_TRUE
             : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeAbortVideoSwitch(
    JNIEnv* /* env */, jobject /* this */, jlong binding) {
  if (binding <= 0 || !g_video_decoder_sink.AbortWindowSelectionAs(static_cast<uint64_t>(binding))) return;
  // The switch is abandoned: an IDR the decoder was still owed is not asked for (r4 M1-A).
  if (auto e = bound_entry(binding)) e->controller.ClearPendingDecoderKeyframe();
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetReadySelectionGeneration(
    JNIEnv* /* env */, jobject /* this */, jlong binding) {
  // a read of the decoder: only for the binding on screen (another PC's ready selection is not this one's)
  if (binding <= 0 || g_video_decoder_sink.Binding() != static_cast<uint64_t>(binding)) return 0;
  return static_cast<jlong>(g_video_decoder_sink.ReadySelectionGeneration());
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetLastOutputPresentationUs(
    JNIEnv* /* env */, jobject /* this */) {
  return static_cast<jlong>(g_video_decoder_sink.LastOutputPresentationUs());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeRequestWindowList(
    JNIEnv* /* env */, jobject /* this */, jlong binding) {
  auto e = bound_entry(binding);
  if (!e) return JNI_FALSE;
  return e->controller.RequestWindowList() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeSelectWindow(
    JNIEnv* /* env */, jobject /* this */, jlong binding, jlong window_id) {
  auto e = bound_entry(binding);
  if (!e) return JNI_FALSE;
  return e->controller.RequestWindowSelect(static_cast<uint64_t>(window_id)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeSelectDesktopMode(
    JNIEnv* /* env */, jobject /* this */, jlong binding) {
  auto e = bound_entry(binding);
  if (!e) return JNI_FALSE;
  return e->controller.RequestDesktopMode() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeSelectMonitor(
    JNIEnv* /* env */, jobject /* this */, jlong binding, jint monitor_id) {
  auto e = bound_entry(binding);
  if (!e) return JNI_FALSE;
  if (monitor_id < 0) return JNI_FALSE;
  return e->controller.RequestMonitorSelect(static_cast<uint32_t>(monitor_id))
             ? JNI_TRUE : JNI_FALSE;
}

// t-970r4zgo r3: the same pick, with the monitorListRevision of the list it was made from, so a
// list replaced in between is refused rather than its index read as another screen.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeSelectMonitorAt(
    JNIEnv* /* env */, jobject /* this */, jlong binding, jint monitor_id, jlong list_revision) {
  auto e = bound_entry(binding);
  if (!e) return JNI_FALSE;
  if (monitor_id < 0 || list_revision < 0) return JNI_FALSE;
  return e->controller.RequestMonitorSelect(static_cast<uint32_t>(monitor_id),
                                           static_cast<uint64_t>(list_revision))
             ? JNI_TRUE : JNI_FALSE;
}

// True while a UAC prompt or the lock screen is in front of the desktop, which is when nothing
// can be captured and the offer to unlock is worth making.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeIsHostScreenLocked(
    JNIEnv* /* env */, jobject /* this */, jlong binding) {
  auto e = bound_entry(binding);
  if (!e) return JNI_FALSE;
  return e->controller.HostSecureDesktopActive() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeRequestRuntimeConfig(
    JNIEnv* /* env */, jobject /* this */, jlong binding, jint bitrate_bps, jint fps) {
  auto e = bound_entry(binding);
  if (!e) return JNI_FALSE;
  if (fps > 0) {
    // the decoder's pacing target, for this binding only (refused if another PC is on screen by now)
    g_video_decoder_sink.SetTargetFps(
        static_cast<uint32_t>(std::clamp<jint>(fps, 1, 240)));
  }
  return e->controller.RequestRuntimeConfig(
             static_cast<uint32_t>(std::max<jint>(0, bitrate_bps)),
             static_cast<uint32_t>(std::max<jint>(0, fps)))
             ? JNI_TRUE
             : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeRequestDesktopCaptureBackend(
    JNIEnv* /* env */, jobject /* this */, jlong binding, jint backend) {
  auto e = bound_entry(binding);
  if (!e) return JNI_FALSE;
  return e->controller.RequestDesktopCaptureBackend(
             static_cast<uint16_t>(std::clamp<jint>(backend, 0, 0xffff)))
             ? JNI_TRUE
             : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeRequestStreamActive(
    JNIEnv* /* env */, jobject /* this */, jlong binding, jboolean active) {
  auto e = bound_entry(binding);
  if (!e) return JNI_FALSE;
  return e->controller.RequestStreamActive(active == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeQueueInputEvent(
    JNIEnv* /* env */, jobject /* this */, jlong binding, jint kind, jint x, jint y, jint wheel_delta,
    jint key_code, jint buttons) {
  auto e = bound_entry(binding);
  if (!e) return JNI_FALSE;
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
  return e->controller.QueueInputEvent(
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
    JNIEnv* /* env */, jobject /* this */, jlong binding, jint timing_jitter_ms, jint position_jitter_px,
    jint repeat_count, jint gap_min_ms, jint gap_max_ms) {
  if (!bound_entry(binding)) return JNI_FALSE;
  remote60::native_poc::MacroPlaybackOptions options;
  options.timingJitterMs = static_cast<uint32_t>(std::max<jint>(0, timing_jitter_ms));
  options.positionJitterPx = static_cast<uint32_t>(std::max<jint>(0, position_jitter_px));
  options.repeatCount = static_cast<uint32_t>(std::max<jint>(0, repeat_count));
  options.repeatGapMinMs = static_cast<uint32_t>(std::max<jint>(0, gap_min_ms));
  options.repeatGapMaxMs = static_cast<uint32_t>(std::max<jint>(0, gap_max_ms));
  const uint32_t seed = static_cast<uint32_t>(now_ms());
  if (!g_macro.StartPlayback(options, now_ms(), seed)) return JNI_FALSE;
  g_macro_binding.store(static_cast<uint64_t>(binding));
  return JNI_TRUE;
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
    JNIEnv* /* env */, jobject /* this */, jlong binding) {
  int sent = 0;
  // keepalive-multi r1: only to the binding playback started on, and only while it is the one on screen.
  auto e = binding > 0 && g_macro_binding.load() == static_cast<uint64_t>(binding) ? bound_entry(binding) : nullptr;
  if (!e) {
    if (g_macro.state() == remote60::native_poc::InputMacro::State::Playing) g_macro.StopPlayback();
    return 0;
  }
  remote60::native_poc::MacroStep step;
  const uint64_t now = now_ms();
  while (g_macro.PollDueStep(now, &step)) {
    e->controller.QueueInputEvent(step.kind, step.x, step.y, step.wheelDelta,
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
    JNIEnv* env, jobject /* this */, jlong binding, jstring text) {
  if (!text) return JNI_FALSE;
  auto e = bound_entry(binding);
  if (!e) return JNI_FALSE;
  const jsize length = env->GetStringLength(text);
  if (length <= 0) return JNI_FALSE;
  const jchar* chars = env->GetStringChars(text, nullptr);
  if (!chars) return JNI_FALSE;
  const bool ok = e->controller.QueueInputText(
      reinterpret_cast<const uint16_t*>(chars), static_cast<size_t>(length));
  env->ReleaseStringChars(text, chars);
  return ok ? JNI_TRUE : JNI_FALSE;
}

// --- clipboard text sync (K1) -----------------------------------------------------------------
//
// Java strings are already UTF-16, which is exactly what the wire carries, so the jchar buffer
// crosses with no transcoding. The app drives both directions because Android only lets a
// foreground app touch the clipboard; nothing down here reads or writes it.

/** The phone's clipboard changed: hand it over for the control thread to send. */
extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeQueueClipboardText(
    JNIEnv* env, jobject /* this */, jlong binding, jstring text) {
  if (!text) return JNI_FALSE;
  auto e = bound_entry(binding);
  if (!e) return JNI_FALSE;
  const jsize length = env->GetStringLength(text);
  const jchar* chars = env->GetStringChars(text, nullptr);
  if (!chars) return JNI_FALSE;
  const bool ok = e->controller.QueueClipboardText(
      reinterpret_cast<const uint16_t*>(chars), static_cast<size_t>(length));
  env->ReleaseStringChars(text, chars);
  return ok ? JNI_TRUE : JNI_FALSE;
}

/** Clipboard text the host sent, or null when none is waiting. Drains as it returns. */
extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeTakeIncomingClipboardText(
    JNIEnv* env, jobject /* this */, jlong binding) {
  std::u16string text;
  auto e = bound_entry(binding);
  if (!e || !e->controller.TakeIncomingClipboardText(&text)) return nullptr;
  return env->NewString(reinterpret_cast<const jchar*>(text.data()),
                        static_cast<jsize>(text.size()));
}

/**
 * True once per session: the session is asking the app to push the phone's current clipboard, so
 * the machine the user just connected from wins over whatever the host held from an earlier one.
 */
extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeTakeClipboardPushRequest(
    JNIEnv* /* env */, jobject /* this */, jlong binding) {
  auto e = bound_entry(binding);
  if (!e) return JNI_FALSE;
  return e->controller.TakeClipboardPushRequest() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeSetClipboardSyncEnabled(
    JNIEnv* /* env */, jobject /* this */, jboolean enabled) {
  // the user's switch: every kept connection, and each new one (session_adopt_steps)
  g_clipboard_sync_enabled.store(enabled == JNI_TRUE);
  for (const auto& re : g_table.Snapshot()) {
    if (auto e = g_table.Find(re.handle)) e->controller.SetClipboardSyncEnabled(enabled == JNI_TRUE);
  }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeIsClipboardSyncEnabled(
    JNIEnv* /* env */, jobject /* this */) {
  return g_clipboard_sync_enabled.load() ? JNI_TRUE : JNI_FALSE;
}

// ---------------------------------------------------------------- keepalive r1: park

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeBeginPark(
    JNIEnv* /* env */, jobject /* this */, jlong handle) {
  // A macro is the app's own source of input: stopped with the rest, so nothing it was about to send
  // is sent while parked (its queued steps are dropped by the park).
  g_macro.StopPlayback();
  g_macro.StopRecording();
  g_macro_binding.store(0);
  // keepalive-multi r1: the PC's frames stop reaching the decoder here (its binding goes), then its own park
  // starts -- inhibit, release, pause, Pong fence -- and its TTL with it. false = not parkable (the app ends it).
  return g_table.BeginPark(static_cast<uint64_t>(handle), now_ms()) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeEndPark(
    JNIEnv* /* env */, jobject /* this */, jlong handle) {
  if (auto e = entry_of(handle)) e->controller.EndPark();
}

/** ClientParkPhase: 0 none, 1 releasing, 2 pausing, 3 fencing, 4 parked, 5 failed, 6 expired. */
extern "C" JNIEXPORT jint JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeParkPhase(
    JNIEnv* /* env */, jobject /* this */, jlong handle) {
  auto e = entry_of(handle);
  if (!e) return 0;
  return static_cast<jint>(e->ParkPhaseCode());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeParkReleasedCount(
    JNIEnv* /* env */, jobject /* this */, jlong handle) {
  auto e = entry_of(handle);
  if (!e) return 0;
  return static_cast<jint>(e->controller.ParkReleasedCount());
}

/** r2 K5: whether this connection's host is known to stop sending while paused (window-list flags). */
extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeHostHonorsStreamPause(
    JNIEnv* /* env */, jobject /* this */, jlong handle) {
  auto e = entry_of(handle);
  if (!e) return JNI_FALSE;
  return e->controller.HostHonorsStreamPause() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeParkDroppedActionCount(
    JNIEnv* /* env */, jobject /* this */, jlong handle) {
  auto e = entry_of(handle);
  if (!e) return 0;
  return static_cast<jint>(e->controller.ParkDroppedActionCount());
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeConnectionGeneration(
    JNIEnv* /* env */, jobject /* this */, jlong handle) {
  auto e = entry_of(handle);
  if (!e) return 0;
  return static_cast<jlong>(e->controller.ConnectionGeneration());
}

/** "gen=G connects=C hellos=H observes=O punches=P", for the diagnostics log. */
extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeSessionCounters(
    JNIEnv* env, jobject /* this */, jlong handle) {
  auto e = entry_of(handle);
  if (!e) return to_jstring(env, "no_entry");
  std::ostringstream oss;
  oss << "handle=" << e->handle << " gen=" << e->controller.ConnectionGeneration()
      << " connects=" << e->controller.ConnectCount()
      << " hellos=" << e->controller.UdpHelloCount()
      << " observes=" << e->observes.load() << " punches=" << e->punches.load()
      << " staleRefused=" << e->gate.StaleRefused() << " staleClosed=" << e->gate.StaleClosed();
  return to_jstring(env, oss.str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeSeedClipboardBaseline(
    JNIEnv* env, jobject /* this */, jlong binding, jstring text) {
  if (!text) return;
  auto e = bound_entry(binding);
  if (!e) return;
  const jsize length = env->GetStringLength(text);
  const jchar* chars = env->GetStringChars(text, nullptr);
  if (!chars) return;
  e->controller.SeedClipboardBaseline(
      std::u16string(reinterpret_cast<const char16_t*>(chars), static_cast<size_t>(length)));
  env->ReleaseStringChars(text, chars);
}

/** Whether the connected host advertised clipboard support, so the UI can say so. */
extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeHostSupportsClipboard(
    JNIEnv* /* env */, jobject /* this */, jlong binding) {
  auto e = bound_entry(binding);
  if (!e) return JNI_FALSE;
  return e->controller.HostSupportsClipboard() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetWindowPanelJson(
    JNIEnv* env, jobject /* this */, jlong binding) {
  auto e = bound_entry(binding);
  if (!e) return to_jstring(env, "{}");
  return to_jstring(env, window_panel_snapshot_json(e->controller, e->controller.WindowPanelSnapshotCopy()));
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeResetVideoStream(
    JNIEnv* /* env */, jobject /* this */, jlong binding) {
  if (binding > 0) g_video_decoder_sink.OnVideoStreamResetAs(static_cast<uint64_t>(binding));
}

// Preview pixels for one target card. Layout: [width:int32][height:int32][rgba bytes...],
// little-endian, RGBA byte order ready for Bitmap.copyPixelsFromBuffer. Null if no preview
// has been fetched yet for this window id (0 = desktop).
extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetWindowThumbnail(
    JNIEnv* env, jobject /* this */, jlong binding, jlong window_id) {
  auto e = bound_entry(binding);
  if (!e) return nullptr;
  remote60::native_poc::ClientSessionController::WindowThumbnail thumb;
  if (!e->controller.CopyWindowThumbnail(static_cast<uint64_t>(window_id), &thumb)) {
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
    JNIEnv* /* env */, jobject /* this */, jlong binding) {
  auto e = bound_entry(binding);
  if (!e) return 0;
  return static_cast<jlong>(e->controller.SessionBytesReceived());
}
