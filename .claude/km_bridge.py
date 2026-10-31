import re, sys
root = sys.argv[1]
P = root + '/apps/android_direct_client/app/src/main/cpp/native_bridge.cpp'
s = open(root + '/.claude/native_bridge_orig.cpp', 'rb').read().decode('utf-8').replace('\r\n', '\n')

PREFIX = 'Java_com_remote60_androiddirect_NativeSessionBridge_'
done = set()

def fn_span(name):
    m = re.search(r'extern "C" JNIEXPORT [^\n]*\n' + re.escape(PREFIX + name) + r'\(', s)
    if not m:
        raise SystemExit('no fn ' + name)
    start = m.start()
    end = s.index('\n}\n', start) + 3
    return start, end

def repl(name, text):
    global s
    a, b = fn_span(name)
    s = s[:a] + text.strip('\n') + '\n' + s[b:]
    done.add(name)

def sub(old, new, count=1):
    global s
    if s.count(old) != count:
        raise SystemExit('count %d != %d for %r' % (s.count(old), count, old[:80]))
    s = s.replace(old, new)

def J(ret, name, params, body):
    return ('extern "C" JNIEXPORT %s JNICALL\n%s%s(\n    %s) {\n%s\n}' % (ret, PREFIX, name, params, body.strip('\n')))

# ------------------------------------------------------------------ globals
a = s.index('remote60::native_poc::ClientSessionController g_session_controller;')
b = s.index('// One macro per session.')
GLOBALS = r'''// keepalive-multi r1 (codex_keepalive_multi_r1.md; docs/keepalive_multi_resource_table.md): one entry per PC
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
  bool Alive() { return controller.Snapshot().state == remote60::native_poc::ClientSessionState::Connected; }
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

'''
s = s[:a] + GLOBALS + s[b:]

sub('// One macro per session. Recording taps the same events the client is about to send, so what is\n// captured is exactly what the host saw.\nremote60::native_poc::InputMacro g_macro;\n',
    '// One macro per session. Recording taps the same events the client is about to send, so what is\n// captured is exactly what the host saw.\nremote60::native_poc::InputMacro g_macro;\n// keepalive-multi r1: the binding playback was started on -- its steps go to that one only (a switch stops it).\nstd::atomic<uint64_t> g_macro_binding{0};\n')
sub('#include "android_video_decoder.hpp"\n#include "connect_op_gate.hpp"\n#include "directory_rendezvous.hpp"\n#include "input_macro.hpp"\n#include "native_video_client_session.hpp"\n#include "utf8_bounded.hpp"\n',
    '#include "android_video_decoder.hpp"\n#include "connect_op_gate.hpp"\n#include "directory_rendezvous.hpp"\n#include "entry_sink_adapter.hpp"\n#include "input_macro.hpp"\n#include "native_video_client_session.hpp"\n#include "session_table.hpp"\n#include "utf8_bounded.hpp"\n')
sub('#include <functional>\n#include <sstream>\n', '#include <functional>\n#include <memory>\n#include <mutex>\n#include <sstream>\n')
sub('std::string window_panel_snapshot_json(const remote60::native_poc::WindowPanelSnapshot& snapshot) {',
    'std::string window_panel_snapshot_json(const remote60::native_poc::ClientSessionController& controller,\n                                       const remote60::native_poc::WindowPanelSnapshot& snapshot) {')
sub('g_session_controller.WindowThumbnailVersion(item.id)', 'controller.WindowThumbnailVersion(item.id)')

# ------------------------------------------------------------------ the table (new)
TABLE = r'''
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
'''
sub('}  // namespace\n\n// r4 S1: the manual (IP) connect', '}  // namespace\n' + TABLE + '\n// r4 S1: the manual (IP) connect')

# ------------------------------------------------------------------ entry-scoped
repl('nativeConnect', J('jlong', 'nativeConnect',
    'JNIEnv* env, jobject /* this */, jlong handle, jstring host, jint video_port, jint control_port, jlong op',
    '''  auto e = entry_of(handle);
  if (!e) return 0;
  const std::string host_value = jstring_to_string(env, host);

  remote60::native_poc::ClientSessionConnectArgs args{};
  args.host = host_value;
  args.videoPort = video_port;
  args.controlPort = control_port;
  args.encodedFrameSink = &e->sink;
  return static_cast<jlong>(e->gate.Adopt(static_cast<int64_t>(op), session_adopt_steps(*e, args)));'''))
sub('// r4 S1: the manual (IP) connect is an operation like the directory\'s -- `op` from nativeConnectOpNext, adopted\n// through the same gate, so a stale directory operation\'s cleanup can never close it. The generation it\n// made comes back (0 = none).',
    '// r4 S1: the manual (IP) connect is an operation like the directory\'s -- `op` from nativeConnectOpNext, adopted\n// through its entry\'s gate, so a stale directory operation\'s cleanup can never close it. The generation it\n// made comes back (0 = none). keepalive-multi r1: `handle` is the entry nativeReserve made for it.')

repl('nativeConnectOpNext', J('jlong', 'nativeConnectOpNext', 'JNIEnv* /* env */, jobject /* this */, jlong handle',
    '''  auto e = entry_of(handle);
  return e ? static_cast<jlong>(e->gate.Next()) : 0;'''))

repl('nativeCloseIfOwn', J('jboolean', 'nativeCloseIfOwn', 'JNIEnv* /* env */, jobject /* this */, jlong handle, jlong op, jlong gen',
    '''  auto e = entry_of(handle);
  if (!e) return JNI_FALSE;
  return e->gate.CloseIfOwn(static_cast<int64_t>(op), static_cast<uint64_t>(gen),
                            [&e] { return e->controller.ConnectionGeneration(); },
                            [&e] { e->controller.Disconnect(); })
             ? JNI_TRUE
             : JNI_FALSE;'''))

# nativeDisconnect is replaced by nativeEnd(handle, reason)
a, b = fn_span('nativeDisconnect')
s = s[:a] + s[b:].lstrip('\n')
done.add('nativeDisconnect')

repl('nativeDirectoryObserve', J('jstring', 'nativeDirectoryObserve',
    'JNIEnv* env, jobject /* this */, jlong handle, jstring directory_host, jint directory_udp_port,\n    jstring observe_token, jlong op',
    '''  std::string observed;
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
  return to_jstring(env, ok ? observed : std::string());'''))

# connect-any: rewrite body with entry
a, b = fn_span('nativeDirectoryConnectAny')
body = s[a:b]
body = body.replace('JNIEnv* env, jobject /* this */, jobjectArray candidates, jint punch_budget_ms,\n    jstring punch_token, jlong op) {\n  g_rendezvous_error.clear();\n',
                    'JNIEnv* env, jobject /* this */, jlong handle, jobjectArray candidates, jint punch_budget_ms,\n    jstring punch_token, jlong op) {\n  auto e = entry_of(handle);\n  if (!e) return 0;\n  e->SetError(std::string());\n  std::string error;\n')
for old, new in [
    ('    g_rendezvous_error = "no usable candidate";\n    return 0;', '    e->SetError("no usable candidate");\n    return 0;'),
    ('remote60::native_poc::ConnectOpGate::StepGuard step(g_connect_gate, owner);', 'remote60::native_poc::ConnectOpGate::StepGuard step(e->gate, owner);'),
    ('    if (!step.entered() || !g_connect_gate.OwnsRendezvous(owner)) {\n      g_rendezvous_error = "stale_operation";\n      return 0;\n    }',
     '    if (!step.entered() || !e->gate.OwnsRendezvous(owner)) {\n      e->SetError("stale_operation");\n      return 0;\n    }'),
    ('    ++g_punch_count;\n    answered = g_rendezvous.PunchAny(', '    e->punches.fetch_add(1);\n    answered = e->rendezvous.PunchAny('),
    ('        &g_rendezvous_error, step.stop());', '        &error, step.stop());'),
    ('    if (step.stopped() || !g_connect_gate.OwnsRendezvous(owner)) {\n      g_rendezvous.Close();\n      g_rendezvous_error = "stale_operation";\n      return 0;\n    }\n    prepared = g_rendezvous.Release();',
     '    if (step.stopped() || !e->gate.OwnsRendezvous(owner)) {\n      e->rendezvous.Close();\n      e->SetError("stale_operation");\n      return 0;\n    }\n    prepared = e->rendezvous.Release();'),
    ('  g_chosen_candidate = target.ip', '  const std::string chosen_text = target.ip'),
    ('                       (answered ? "" : " (no answer, trying anyway)");\n',
     '                       (answered ? "" : " (no answer, trying anyway)");\n  {\n    std::lock_guard<std::mutex> lk(e->infoMu);\n    e->chosenCandidate = chosen_text;\n  }\n'),
    ('    if (g_rendezvous_error.empty()) g_rendezvous_error = "no prepared socket";\n    return 0;',
     '    e->SetError(error.empty() ? std::string("no prepared socket") : error);\n    return 0;'),
    ('  args.encodedFrameSink = &g_video_decoder_sink;\n  remote60::native_poc::ConnectOpGate::Steps steps = session_adopt_steps(args);',
     '  args.encodedFrameSink = &e->sink;\n  remote60::native_poc::ConnectOpGate::Steps steps = session_adopt_steps(*e, args);'),
    ('  const uint64_t made = g_connect_gate.Adopt(owner, steps);\n  if (made == 0 && !g_connect_gate.IsCurrent(owner)) g_rendezvous_error = "stale_operation";',
     '  const uint64_t made = e->gate.Adopt(owner, steps);\n  if (made == 0 && !e->gate.IsCurrent(owner)) error = "stale_operation";\n  e->SetError(error);'),
]:
    if body.count(old) != 1:
        raise SystemExit('connectany: ' + old[:70])
    body = body.replace(old, new)
s = s[:a] + body + s[b:]
done.add('nativeDirectoryConnectAny')

repl('nativeDirectoryChosenCandidate', J('jstring', 'nativeDirectoryChosenCandidate', 'JNIEnv* env, jobject /* this */, jlong handle',
    '''  auto e = entry_of(handle);
  if (!e) return to_jstring(env, std::string());
  std::lock_guard<std::mutex> lk(e->infoMu);
  return to_jstring(env, e->chosenCandidate);'''))
repl('nativeDirectoryLastError', J('jstring', 'nativeDirectoryLastError', 'JNIEnv* env, jobject /* this */, jlong handle',
    '''  auto e = entry_of(handle);
  return to_jstring(env, e ? e->Error() : std::string("no_entry"));'''))

def entry_read(name, ret, expr, default, extra_params=''):
    params = 'JNIEnv* env, jobject /* this */, jlong handle' if ret == 'jstring' else 'JNIEnv* /* env */, jobject /* this */, jlong handle'
    repl(name, J(ret, name, params + extra_params, '  auto e = entry_of(handle);\n  if (!e) return %s;\n  return %s;' % (default, expr)))

entry_read('nativeGetStatus', 'jstring', 'to_jstring(env, e->controller.Snapshot().status)', 'to_jstring(env, "disconnected")')
entry_read('nativeGetLastError', 'jstring', 'to_jstring(env, e->controller.Snapshot().lastError)', 'to_jstring(env, std::string())')
entry_read('nativeParkPhase', 'jint', 'static_cast<jint>(e->ParkPhaseCode())', '0')
entry_read('nativeParkReleasedCount', 'jint', 'static_cast<jint>(e->controller.ParkReleasedCount())', '0')
entry_read('nativeParkDroppedActionCount', 'jint', 'static_cast<jint>(e->controller.ParkDroppedActionCount())', '0')
entry_read('nativeHostHonorsStreamPause', 'jboolean', 'e->controller.HostHonorsStreamPause() ? JNI_TRUE : JNI_FALSE', 'JNI_FALSE')
entry_read('nativeConnectionGeneration', 'jlong', 'static_cast<jlong>(e->controller.ConnectionGeneration())', '0')

repl('nativeSessionCounters', J('jstring', 'nativeSessionCounters', 'JNIEnv* env, jobject /* this */, jlong handle',
    '''  auto e = entry_of(handle);
  if (!e) return to_jstring(env, "no_entry");
  std::ostringstream oss;
  oss << "handle=" << e->handle << " gen=" << e->controller.ConnectionGeneration()
      << " connects=" << e->controller.ConnectCount()
      << " hellos=" << e->controller.UdpHelloCount()
      << " observes=" << e->observes.load() << " punches=" << e->punches.load()
      << " staleRefused=" << e->gate.StaleRefused() << " staleClosed=" << e->gate.StaleClosed();
  return to_jstring(env, oss.str());'''))

repl('nativeBeginPark', J('jboolean', 'nativeBeginPark', 'JNIEnv* /* env */, jobject /* this */, jlong handle',
    '''  // A macro is the app's own source of input: stopped with the rest, so nothing it was about to send
  // is sent while parked (its queued steps are dropped by the park).
  g_macro.StopPlayback();
  g_macro.StopRecording();
  g_macro_binding.store(0);
  // keepalive-multi r1: the PC's frames stop reaching the decoder here (its binding goes), then its own park
  // starts -- inhibit, release, pause, Pong fence -- and its TTL with it. false = not parkable (the app ends it).
  return g_table.BeginPark(static_cast<uint64_t>(handle), now_ms()) ? JNI_TRUE : JNI_FALSE;'''))
# resume is part of nativeActivate now
a, b = fn_span('nativeResumePark')
s = s[:a] + s[b:].lstrip('\n')
done.add('nativeResumePark')
repl('nativeEndPark', J('void', 'nativeEndPark', 'JNIEnv* /* env */, jobject /* this */, jlong handle',
    '''  if (auto e = entry_of(handle)) e->controller.EndPark();'''))

# ------------------------------------------------------------------ binding-scoped
def bound(name, ret, extra, body_expr, default, env_used=False):
    params = ('JNIEnv* env' if env_used else 'JNIEnv* /* env */') + ', jobject /* this */, jlong binding' + extra
    repl(name, J(ret, name, params, '  auto e = bound_entry(binding);\n  if (!e) return %s;\n%s' % (default, body_expr)))

bound('nativeRequestWindowList', 'jboolean', '', '  return e->controller.RequestWindowList() ? JNI_TRUE : JNI_FALSE;', 'JNI_FALSE')
bound('nativeSelectWindow', 'jboolean', ', jlong window_id',
      '  return e->controller.RequestWindowSelect(static_cast<uint64_t>(window_id)) ? JNI_TRUE : JNI_FALSE;', 'JNI_FALSE')
bound('nativeSelectDesktopMode', 'jboolean', '', '  return e->controller.RequestDesktopMode() ? JNI_TRUE : JNI_FALSE;', 'JNI_FALSE')
bound('nativeSelectMonitor', 'jboolean', ', jint monitor_id',
      '  if (monitor_id < 0) return JNI_FALSE;\n  return e->controller.RequestMonitorSelect(static_cast<uint32_t>(monitor_id))\n             ? JNI_TRUE : JNI_FALSE;', 'JNI_FALSE')
bound('nativeSelectMonitorAt', 'jboolean', ', jint monitor_id, jlong list_revision',
      '  if (monitor_id < 0 || list_revision < 0) return JNI_FALSE;\n  return e->controller.RequestMonitorSelect(static_cast<uint32_t>(monitor_id),\n                                           static_cast<uint64_t>(list_revision))\n             ? JNI_TRUE : JNI_FALSE;', 'JNI_FALSE')
bound('nativeIsHostScreenLocked', 'jboolean', '', '  return e->controller.HostSecureDesktopActive() ? JNI_TRUE : JNI_FALSE;', 'JNI_FALSE')
bound('nativeRequestRuntimeConfig', 'jboolean', ', jint bitrate_bps, jint fps',
      '''  if (fps > 0) {
    // the decoder's pacing target, for this binding only (refused if another PC is on screen by now)
    g_video_decoder_sink.SetTargetFps(
        static_cast<uint32_t>(std::clamp<jint>(fps, 1, 240)));
  }
  return e->controller.RequestRuntimeConfig(
             static_cast<uint32_t>(std::max<jint>(0, bitrate_bps)),
             static_cast<uint32_t>(std::max<jint>(0, fps)))
             ? JNI_TRUE
             : JNI_FALSE;''', 'JNI_FALSE')
bound('nativeRequestDesktopCaptureBackend', 'jboolean', ', jint backend',
      '''  return e->controller.RequestDesktopCaptureBackend(
             static_cast<uint16_t>(std::clamp<jint>(backend, 0, 0xffff)))
             ? JNI_TRUE
             : JNI_FALSE;''', 'JNI_FALSE')
bound('nativeRequestStreamActive', 'jboolean', ', jboolean active',
      '  return e->controller.RequestStreamActive(active == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;', 'JNI_FALSE')

a, b = fn_span('nativeQueueInputEvent')
body = s[a:b]
body = body.replace('JNIEnv* /* env */, jobject /* this */, jint kind,', 'JNIEnv* /* env */, jobject /* this */, jlong binding, jint kind,')
body = body.replace('  const uint16_t safe_kind', '  auto e = bound_entry(binding);\n  if (!e) return JNI_FALSE;\n  const uint16_t safe_kind')
body = body.replace('  return g_session_controller.QueueInputEvent(', '  return e->controller.QueueInputEvent(')
s = s[:a] + body + s[b:]
done.add('nativeQueueInputEvent')

a, b = fn_span('nativeMacroStartPlayback')
body = s[a:b]
body = body.replace('JNIEnv* /* env */, jobject /* this */, jint timing_jitter_ms,', 'JNIEnv* /* env */, jobject /* this */, jlong binding, jint timing_jitter_ms,')
body = body.replace('  remote60::native_poc::MacroPlaybackOptions options;', '  if (!bound_entry(binding)) return JNI_FALSE;\n  remote60::native_poc::MacroPlaybackOptions options;')
body = body.replace('  return g_macro.StartPlayback(options, now_ms(), seed) ? JNI_TRUE : JNI_FALSE;',
                    '  if (!g_macro.StartPlayback(options, now_ms(), seed)) return JNI_FALSE;\n  g_macro_binding.store(static_cast<uint64_t>(binding));\n  return JNI_TRUE;')
s = s[:a] + body + s[b:]
done.add('nativeMacroStartPlayback')

a, b = fn_span('nativeMacroPump')
body = s[a:b]
body = body.replace('JNIEnv* /* env */, jobject /* this */) {\n  int sent = 0;',
                    'JNIEnv* /* env */, jobject /* this */, jlong binding) {\n  int sent = 0;\n  // keepalive-multi r1: only to the binding playback started on, and only while it is the one on screen.\n  auto e = binding > 0 && g_macro_binding.load() == static_cast<uint64_t>(binding) ? bound_entry(binding) : nullptr;\n  if (!e) {\n    if (g_macro.state() == remote60::native_poc::InputMacro::State::Playing) g_macro.StopPlayback();\n    return 0;\n  }')
body = body.replace('    g_session_controller.QueueInputEvent(step.kind', '    e->controller.QueueInputEvent(step.kind')
s = s[:a] + body + s[b:]
done.add('nativeMacroPump')

a, b = fn_span('nativeQueueInputText')
body = s[a:b]
body = body.replace('JNIEnv* env, jobject /* this */, jstring text) {\n  if (!text) return JNI_FALSE;', 'JNIEnv* env, jobject /* this */, jlong binding, jstring text) {\n  if (!text) return JNI_FALSE;\n  auto e = bound_entry(binding);\n  if (!e) return JNI_FALSE;')
body = body.replace('g_session_controller.QueueInputText(', 'e->controller.QueueInputText(')
s = s[:a] + body + s[b:]
done.add('nativeQueueInputText')

a, b = fn_span('nativeQueueClipboardText')
body = s[a:b]
body = body.replace('JNIEnv* env, jobject /* this */, jstring text) {\n  if (!text) return JNI_FALSE;', 'JNIEnv* env, jobject /* this */, jlong binding, jstring text) {\n  if (!text) return JNI_FALSE;\n  auto e = bound_entry(binding);\n  if (!e) return JNI_FALSE;')
body = body.replace('g_session_controller.QueueClipboardText(', 'e->controller.QueueClipboardText(')
s = s[:a] + body + s[b:]
done.add('nativeQueueClipboardText')

a, b = fn_span('nativeTakeIncomingClipboardText')
body = s[a:b]
body = body.replace('JNIEnv* env, jobject /* this */) {\n  std::u16string text;\n  if (!g_session_controller.TakeIncomingClipboardText(&text)) return nullptr;',
                    'JNIEnv* env, jobject /* this */, jlong binding) {\n  std::u16string text;\n  auto e = bound_entry(binding);\n  if (!e || !e->controller.TakeIncomingClipboardText(&text)) return nullptr;')
s = s[:a] + body + s[b:]
done.add('nativeTakeIncomingClipboardText')

bound('nativeTakeClipboardPushRequest', 'jboolean', '', '  return e->controller.TakeClipboardPushRequest() ? JNI_TRUE : JNI_FALSE;', 'JNI_FALSE')
repl('nativeSetClipboardSyncEnabled', J('void', 'nativeSetClipboardSyncEnabled', 'JNIEnv* /* env */, jobject /* this */, jboolean enabled',
    '''  // the user's switch: every kept connection, and each new one (session_adopt_steps)
  g_clipboard_sync_enabled.store(enabled == JNI_TRUE);
  for (const auto& re : g_table.Snapshot()) {
    if (auto e = g_table.Find(re.handle)) e->controller.SetClipboardSyncEnabled(enabled == JNI_TRUE);
  }'''))
repl('nativeIsClipboardSyncEnabled', J('jboolean', 'nativeIsClipboardSyncEnabled', 'JNIEnv* /* env */, jobject /* this */',
    '  return g_clipboard_sync_enabled.load() ? JNI_TRUE : JNI_FALSE;'))

a, b = fn_span('nativeSeedClipboardBaseline')
body = s[a:b]
body = body.replace('JNIEnv* env, jobject /* this */, jstring text) {\n  if (!text) return;', 'JNIEnv* env, jobject /* this */, jlong binding, jstring text) {\n  if (!text) return;\n  auto e = bound_entry(binding);\n  if (!e) return;')
body = body.replace('  g_session_controller.SeedClipboardBaseline(', '  e->controller.SeedClipboardBaseline(')
s = s[:a] + body + s[b:]
done.add('nativeSeedClipboardBaseline')

bound('nativeHostSupportsClipboard', 'jboolean', '', '  return e->controller.HostSupportsClipboard() ? JNI_TRUE : JNI_FALSE;', 'JNI_FALSE')
bound('nativeGetWindowPanelJson', 'jstring', '',
      '  return to_jstring(env, window_panel_snapshot_json(e->controller, e->controller.WindowPanelSnapshotCopy()));',
      'to_jstring(env, "{}")', env_used=True)

a, b = fn_span('nativeGetWindowThumbnail')
body = s[a:b]
body = body.replace('JNIEnv* env, jobject /* this */, jlong window_id) {\n', 'JNIEnv* env, jobject /* this */, jlong binding, jlong window_id) {\n  auto e = bound_entry(binding);\n  if (!e) return nullptr;\n')
body = body.replace('if (!g_session_controller.CopyWindowThumbnail(', 'if (!e->controller.CopyWindowThumbnail(')
s = s[:a] + body + s[b:]
done.add('nativeGetWindowThumbnail')

bound('nativeGetSessionBytesReceived', 'jlong', '', '  return static_cast<jlong>(e->controller.SessionBytesReceived());', '0')

repl('nativeResetVideoStream', J('void', 'nativeResetVideoStream', 'JNIEnv* /* env */, jobject /* this */, jlong binding',
    '  if (binding > 0) g_video_decoder_sink.OnVideoStreamResetAs(static_cast<uint64_t>(binding));'))
repl('nativePrepareVideoSwitch', J('jboolean', 'nativePrepareVideoSwitch', 'JNIEnv* /* env */, jobject /* this */, jlong binding, jlong selection_generation',
    '''  if (binding <= 0) return JNI_FALSE;
  return g_video_decoder_sink.PrepareForWindowSelectionAs(static_cast<uint64_t>(binding),
                                                          static_cast<uint64_t>(selection_generation))
             ? JNI_TRUE
             : JNI_FALSE;'''))
repl('nativeAbortVideoSwitch', J('void', 'nativeAbortVideoSwitch', 'JNIEnv* /* env */, jobject /* this */, jlong binding',
    '''  if (binding <= 0 || !g_video_decoder_sink.AbortWindowSelectionAs(static_cast<uint64_t>(binding))) return;
  // The switch is abandoned: an IDR the decoder was still owed is not asked for (r4 M1-A).
  if (auto e = bound_entry(binding)) e->controller.ClearPendingDecoderKeyframe();'''))
repl('nativeGetReadySelectionGeneration', J('jlong', 'nativeGetReadySelectionGeneration', 'JNIEnv* /* env */, jobject /* this */, jlong binding',
    '''  // a read of the decoder: only for the binding on screen (another PC's ready selection is not this one's)
  if (binding <= 0 || g_video_decoder_sink.Binding() != static_cast<uint64_t>(binding)) return 0;
  return static_cast<jlong>(g_video_decoder_sink.ReadySelectionGeneration());'''))

open(P, 'wb').write(s.replace('\n', '\r\n').encode('utf-8'))
left = re.findall(r'g_session_controller|g_rendezvous|g_connect_gate|g_chosen_candidate|g_observe_count|g_punch_count', s)
print('done', len(done), 'leftover globals', len(left))
