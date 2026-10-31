cpp = 'C:/work/gnlink-wt/feat-apk-0233/apps/android_direct_client/app/src/main/cpp/native_bridge.cpp'
kt = 'C:/work/gnlink-wt/feat-apk-0233/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt'

s = open(cpp, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
assert s.count('#include "session_table.hpp"\n') == 1
s = s.replace('#include "session_table.hpp"\n', '#include "session_table.hpp"\n#include "standby_runner.hpp"\n')
s = s.rstrip('\n') + r'''

// ===================================================================== keepalive-r2 (2) e: standby leases (one per PC)
// The app's StandbyRunner. Nothing calls it while Preconnect.SEND_TO_REAL_HOSTS is off (the shipped value): these
// functions exist so the wiring can be tested; a lease is only ever started by nativeStandbyStart.
namespace {

// The install's device id + last generation, in one small file under the app's own files dir. Written whole to a
// temporary file and renamed over the old one (a crash leaves the old or the new, never a half). Unreadable or
// damaged = nothing usable: GenerationAllocator makes a new device id (the old entry lapses on its lease).
class FileGenerationStore : public remote60::native_poc::StandbyGenerationStore {
 public:
  explicit FileGenerationStore(std::string path) : path_(std::move(path)) {}
  bool Load(std::array<uint8_t, 16>* id, uint32_t* last) override {
    FILE* f = std::fopen(path_.c_str(), "rb");
    if (!f) return false;
    uint8_t buf[21] = {};
    const size_t n = std::fread(buf, 1, sizeof(buf), f);
    std::fclose(f);
    if (n != 20) return false;  // a short or longer file is damaged
    std::memcpy(id->data(), buf, 16);
    std::memcpy(last, buf + 16, 4);
    return true;
  }
  bool Save(const std::array<uint8_t, 16>& id, uint32_t last) override {
    const std::string tmp = path_ + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    uint8_t buf[20];
    std::memcpy(buf, id.data(), 16);
    std::memcpy(buf + 16, &last, 4);
    const bool wrote = std::fwrite(buf, 1, sizeof(buf), f) == sizeof(buf) && std::fflush(f) == 0;
    std::fclose(f);
    return wrote && std::rename(tmp.c_str(), path_.c_str()) == 0;
  }

 private:
  std::string path_;
};

bool urandom_fill(uint8_t* out, size_t n) {
  FILE* f = std::fopen("/dev/urandom", "rb");
  if (!f) return false;
  const bool ok = std::fread(out, 1, n, f) == n;
  std::fclose(f);
  return ok;
}

uint64_t standby_now_ms() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::mutex g_standby_mu;
std::unique_ptr<FileGenerationStore> g_standby_store;
std::unique_ptr<remote60::native_poc::GenerationAllocator> g_standby_gens;
std::unique_ptr<remote60::native_poc::StandbyRunner> g_standby;

remote60::native_poc::StandbyRunner* standby_runner() {
  std::lock_guard<std::mutex> lock(g_standby_mu);
  return g_standby.get();
}

}  // namespace

// Once per process, with the app's files dir. The Hello features a session made by activation asks for are the
// ordinary Hello's (the activation's HelloAck answers them).
extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeStandbyInit(JNIEnv* env, jobject /* this */, jstring files_dir) {
  std::lock_guard<std::mutex> lock(g_standby_mu);
  if (g_standby) return JNI_TRUE;
  g_standby_store = std::make_unique<FileGenerationStore>(jstring_to_string(env, files_dir) + "/standby_generation.bin");
  g_standby_gens = std::make_unique<remote60::native_poc::GenerationAllocator>(*g_standby_store, urandom_fill);
  const uint32_t features = remote60::native_poc::kUdpFeatureVideoFec | remote60::native_poc::kUdpFeatureVideoFecInterleaved;
  // The APK's capability is asynchronous (the directory call is Kotlin's): no source here -- the lease says it needs
  // one (status), the app observes from the lease's socket and hands it in (nativeStandbyObserve / nativeStandbyProvide).
  g_standby = std::make_unique<remote60::native_poc::StandbyRunner>(
      *g_standby_gens, features, [](const std::string&) { return remote60::native_poc::StandbyLease::CapabilityFn(); },
      standby_now_ms);
  return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeStandbyStart(JNIEnv* env, jobject /* this */, jstring key) {
  auto* r = standby_runner();
  return r && r->Start(jstring_to_string(env, key)) ? JNI_TRUE : JNI_FALSE;
}

// [exists, state (StandbyEntryClient::State: 0 Idle, 1 Admitting, 2 Parked, 3 Activating, 4 Active, 5 Unsupported,
// 6 Waiting), lost count, handed off, needs a capability]
extern "C" JNIEXPORT jlongArray JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeStandbyStatus(JNIEnv* env, jobject /* this */, jstring key) {
  auto* r = standby_runner();
  const auto st = r ? r->StatusOf(jstring_to_string(env, key)) : remote60::native_poc::StandbyRunner::Status{};
  const jlong v[5] = {st.exists ? 1 : 0, static_cast<jlong>(st.state), static_cast<jlong>(st.lost), st.handedOff ? 1 : 0,
                      st.needsCapability ? 1 : 0};
  jlongArray out = env->NewLongArray(5);
  if (out) env->SetLongArrayRegion(out, 0, 5, v);
  return out;
}

// The directory observation from that PC's lease socket (blocking up to ~2 s: call off the UI thread).
extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeStandbyObserve(JNIEnv* env, jobject /* this */, jstring key,
                                                                         jstring host, jint port, jstring token) {
  auto* r = standby_runner();
  return r && r->Observe(jstring_to_string(env, key), jstring_to_string(env, host), static_cast<uint16_t>(port),
                         jstring_to_string(env, token))
             ? JNI_TRUE
             : JNI_FALSE;
}

// The capability the app got for it: the one-time token and where to aim (the relay when reserved, else the host).
extern "C" JNIEXPORT jboolean JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeStandbyProvide(JNIEnv* env, jobject /* this */, jstring key,
                                                                         jstring token, jstring ip, jint port) {
  auto* r = standby_runner();
  if (!r) return JNI_FALSE;
  remote60::native_poc::StandbyCapability cap;
  cap.token = jstring_to_string(env, token);
  cap.ip = jstring_to_string(env, ip);
  cap.port = static_cast<uint16_t>(port);
  return r->Provide(jstring_to_string(env, key), cap) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeStandbyRelease(JNIEnv* env, jobject /* this */, jstring key) {
  if (auto* r = standby_runner()) r->Release(jstring_to_string(env, key));
}

extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeStandbyReleaseAll(JNIEnv* /* env */, jobject /* this */) {
  if (auto* r = standby_runner()) r->ReleaseAll();
}
'''
if crlf:
    s = s.replace('\n', '\r\n')
open(cpp, 'wb').write(s.encode('utf-8'))

k = open(kt, 'rb').read().decode('utf-8')
c2 = '\r\n' in k
k = k.replace('\r\n', '\n')
idx = k.rstrip().rfind('}')
k = k[:idx] + '''
    // keepalive-r2 (2) e: the standby leases (StandbyRunner). Nothing calls these while Preconnect.SEND_TO_REAL_HOSTS
    // is off. Status: [exists, state, lost, handedOff, needsCapability].
    external fun nativeStandbyInit(filesDir: String): Boolean
    external fun nativeStandbyStart(key: String): Boolean
    external fun nativeStandbyStatus(key: String): LongArray?
    external fun nativeStandbyObserve(key: String, host: String, port: Int, token: String): Boolean
    external fun nativeStandbyProvide(key: String, token: String, ip: String, port: Int): Boolean
    external fun nativeStandbyRelease(key: String)
    external fun nativeStandbyReleaseAll()
}
'''
if c2:
    k = k.replace('\n', '\r\n')
open(kt, 'wb').write(k.encode('utf-8'))
print('ok')
