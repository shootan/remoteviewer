// mobile-abr r3: the APK's decoder sink on a real MediaCodec -- TEST ONLY, never part of the APK.
//
// An Android command-line executable (pushed to an emulator's /data/local/tmp, run from adb shell) that
// links the product code the APK links -- ClientSessionController (native_video_client_session.cpp) and
// AndroidVideoDecoderSink (android_video_decoder.cpp), unchanged -- and gives the sink a real Surface: an
// AImageReader's window, handed over through the one NDK call the sink makes to get its window
// (ANativeWindow_fromSurface, defined below for this executable only). The real MediaCodec decodes into it;
// every image that reaches the reader is counted: what the decoder actually rendered.
//
// It connects to an isolated host on the PC (through the PC harness's relay: mobile_abr_session_e2e_test
// --scenario external), and on a fixed schedule from its first frame:
//   +25 s  the surface goes and the stream is paused (what the app does when its view goes away)
//   +28 s  the surface comes back and the stream resumes
//   +45 s  a window selection is attempted (the window-locked test host refuses it: prepare -> answer
//          -> abort, the sink's selection gate on a real codec)
//   +70 s  disconnect
// It prints one line a second: images rendered, the sink's own status line. The reports themselves are
// counted where the host receives them (the PC relay).
//
//   mediacodec_smoke <host> <port> [seconds=70] [plan=app|r3]   (the plans: see the steps in main)

#include <android/native_window.h>
#include <media/NdkImageReader.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <string>
#include <thread>
#include <vector>

#include <jni.h>

#include "android_video_decoder.hpp"
#include "native_video_client_session.hpp"

namespace {
ANativeWindow* gReaderWindow = nullptr;
std::atomic<uint64_t> gImages{0};

void OnImage(void*, AImageReader* reader) {
  AImage* image = nullptr;
  while (AImageReader_acquireNextImage(reader, &image) == AMEDIA_OK && image) {
    gImages.fetch_add(1, std::memory_order_relaxed);
    AImage_delete(image);
    image = nullptr;
  }
}
// r4 F2: this device's UDP counters (all sockets; the smoke's is the only busy one) -- whether datagrams
// leave the device while the PC relay sees none arrive.
std::string udp_counters() {
  FILE* f = std::fopen("/proc/net/snmp", "r");
  if (!f) return "udp=?";
  char header[512] = {}, values[512] = {}, line[512];
  while (std::fgets(line, sizeof(line), f)) {
    if (std::strncmp(line, "Udp:", 4) == 0) {
      if (header[0] == 0) std::strncpy(header, line, sizeof(header) - 1);
      else std::strncpy(values, line, sizeof(values) - 1);
    }
  }
  std::fclose(f);
  std::string v = values;
  while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
  return "udp[" + v + "]";
}
// r5 F2: this device's Udp OutDatagrams (/proc/net/snmp, the 4th Udp field) -- 0 when unreadable.
uint64_t udp_out_datagrams() {
  FILE* f = std::fopen("/proc/net/snmp", "r");
  if (!f) return 0;
  char line[512];
  int seen = 0;
  uint64_t out = 0;
  while (std::fgets(line, sizeof(line), f)) {
    if (std::strncmp(line, "Udp:", 4) != 0) continue;
    if (++seen == 2) {  // the values line
      unsigned long long in = 0, noPorts = 0, inErr = 0, o = 0;
      if (std::sscanf(line + 4, "%llu %llu %llu %llu", &in, &noPorts, &inErr, &o) == 4) out = o;
    }
  }
  std::fclose(f);
  return out;
}
uint64_t now_ms() {
  using namespace std::chrono;
  return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
}  // namespace

// The sink asks the platform for the window behind a Java Surface. There is no Java here: this executable
// answers with the reader's window. The reader owns it, so "release" is a no-op.
extern "C" ANativeWindow* ANativeWindow_fromSurface(JNIEnv*, jobject surface) {
  return surface ? gReaderWindow : nullptr;
}
extern "C" void ANativeWindow_release(ANativeWindow*) {}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: mediacodec_smoke <host> <port> [seconds]\n");
    return 2;
  }
  const std::string host = argv[1];
  const int port = std::atoi(argv[2]);
  const int seconds = argc > 3 ? std::atoi(argv[3]) : 70;
  const std::string plan = argc > 4 ? argv[4] : "app";
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  AImageReader* reader = nullptr;
  if (AImageReader_new(1920, 1088, AIMAGE_FORMAT_PRIVATE, 4, &reader) != AMEDIA_OK || !reader) {
    std::printf("SMOKE FAIL no AImageReader\n");
    return 1;
  }
  AImageReader_ImageListener listener{nullptr, OnImage};
  AImageReader_setImageListener(reader, &listener);
  AImageReader_getWindow(reader, &gReaderWindow);

  static int surfaceToken = 1;
  remote60::android_direct::AndroidVideoDecoderSink sink;
  sink.SetSurface(nullptr, reinterpret_cast<jobject>(&surfaceToken));

  remote60::native_poc::ClientSessionController session;
  remote60::native_poc::ClientSessionConnectArgs args;
  args.host = host;
  args.videoPort = port;
  args.controlPort = port;
  args.requireUdpHello = true;
  args.requireTcpControl = false;
  args.controlOverUdp = true;  // the directory path's shape: control tunnelled over the media socket
  args.encodedFrameSink = &sink;
  if (!session.Connect(args)) {
    std::printf("SMOKE FAIL connect\n");
    return 1;
  }
  const uint64_t t0 = now_ms();
  while (gImages.load() == 0 && now_ms() < t0 + 20000) {
    (void)sink.ReadySelectionGeneration();  // the app's UI polls the sink like this
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
  if (gImages.load() == 0) {
    std::printf("SMOKE FAIL no image rendered in 20 s (%s)\n", sink.DebugStatus().c_str());
    session.Disconnect();
    return 1;
  }
  const uint64_t first = now_ms();
  std::printf("SMOKE first image at +%llu ms\n", static_cast<unsigned long long>(first - t0));
  uint64_t lastImages = gImages.load(), nextLog = first + 1000;
  // One-shot steps (r5: each runs once, at its time). Two plans:
  //  app (default) -- the APK's own call order (MainActivity), r5 Codex F1 contract:
  //   +20 s leave the viewer: stream pause, then the surface released (releaseVideoSurface)
  //   +23 s back: surface bound, stream resumed
  //   +30 s a selection the host refuses (startSelectionTransition: stream active -> prepare -> a REAL
  //         request -- desktop, the window-locked test host answers "selection_locked_by_config"); when
  //         it is not ready 1.5 s later the app's failure path: stream pause -> abort -> window list
  //   +36 s back to viewing the same target: stream resumed
  //   +45 s fault A: a switch prepared and abandoned while the stream stays ACTIVE (the sink asks for a
  //         fresh IDR; it comes) -- must not demote
  //   +58 s fault B: the same while the PC relay drops every keyframe (+57..+66 s from its first report,
  //         --keydrop-*): no IDR can come; after the 2 s grace the 0 is real and the host may demote
  //  r3 -- the r3/r4 model kept for comparison: +25 surface off + pause, +28 back, +45 prepare with no
  //        request (asked=0) and stream active, +47 abort ONCE.
  struct Step {
    uint64_t atMs;
    const char* name;
    bool done;
  };
  std::vector<Step> steps;
  if (plan == "r3") {
    steps = {{25000, "r3-surface-off-pause", false}, {28000, "r3-surface-on-resume", false},
             {45000, "r3-prepare-no-request", false}, {47000, "r3-abort-once", false}};
  } else {
    steps = {{20000, "leave-viewer", false},        {23000, "back-to-viewer", false},
             {30000, "select-refused", false},      {31500, "select-failed-path", false},
             {36000, "resume-viewing", false},      {45000, "faultA-abort-active", false},
             {58000, "faultB-abort-active-no-idr", false}};
  }
  auto run_step = [&](Step& st, uint64_t t) {
    st.done = true;
    const std::string n = st.name;
    std::string detail;
    if (n == "r3-surface-off-pause") {
      sink.SetSurface(nullptr, nullptr);
      (void)session.RequestStreamActive(false);
    } else if (n == "r3-surface-on-resume") {
      sink.SetSurface(nullptr, reinterpret_cast<jobject>(&surfaceToken));
      (void)session.RequestStreamActive(true);
    } else if (n == "r3-prepare-no-request") {
      sink.PrepareForWindowSelection(4501);
    } else if (n == "r3-abort-once") {
      sink.AbortWindowSelection();
      session.ClearPendingDecoderKeyframe();
    } else if (n == "leave-viewer") {
      (void)session.RequestStreamActive(false);  // moveToTargets / onStop: the stream first
      sink.SetSurface(nullptr, nullptr);          // releaseVideoSurface
    } else if (n == "back-to-viewer") {
      sink.SetSurface(nullptr, reinterpret_cast<jobject>(&surfaceToken));
      (void)session.RequestStreamActive(true);
    } else if (n == "select-refused") {
      (void)session.RequestStreamActive(true);  // startSelectionTransition: requestStreamActive(true)
      sink.PrepareForWindowSelection(31);       // nativePrepareVideoSwitch
      const bool asked = session.RequestDesktopMode();  // nativeSelectDesktopMode: a real request
      detail = std::string(" asked=") + (asked ? "1" : "0");
    } else if (n == "select-failed-path") {
      const bool ready = sink.ReadySelectionGeneration() == 31;
      detail = std::string(" ready=") + (ready ? "1" : "0") + " status=" + session.WindowPanelSnapshotCopy().status;
      if (!ready) {  // the selection did not open: the app's failure / back path (moveToTargets)
        (void)session.RequestStreamActive(false);
        sink.AbortWindowSelection();               // nativeAbortVideoSwitch
        session.ClearPendingDecoderKeyframe();
        (void)session.RequestWindowList();         // TARGETS
      }
    } else if (n == "resume-viewing") {
      (void)session.RequestStreamActive(true);
    } else if (n == "faultA-abort-active" || n == "faultB-abort-active-no-idr") {
      sink.PrepareForWindowSelection(n == "faultA-abort-active" ? 45 : 58);
      sink.AbortWindowSelection();  // the stream stays active: the sink asks for a fresh IDR
    }
    std::printf("SMOKE event +%llu %s%s\n", static_cast<unsigned long long>(t), st.name, detail.c_str());
  };
  // r5 F2: the moment this device stops sending any datagram for 1.5 s while the session is connected,
  // every thread's stack (debuggerd -b, needs adb root on the emulator) -- once, from here, at once.
  uint64_t lastOut = udp_out_datagrams(), lastOutChangeMs = now_ms(), nextOutCheck = now_ms() + 200;
  bool f2Captured = false;
  while (now_ms() < first + static_cast<uint64_t>(seconds) * 1000ULL) {
    const uint64_t t = now_ms() - first;
    if (now_ms() >= nextOutCheck) {
      nextOutCheck = now_ms() + 200;
      const uint64_t o = udp_out_datagrams();
      if (o != lastOut) {
        lastOut = o;
        lastOutChangeMs = now_ms();
      } else if (!f2Captured && now_ms() - lastOutChangeMs >= 1500 &&
                 session.Snapshot().state == remote60::native_poc::ClientSessionState::Connected) {
        f2Captured = true;
        char cmd[256];
        std::snprintf(cmd, sizeof(cmd), "debuggerd -b %d > /data/local/tmp/smoke_f2_stack_t%llu.txt 2>&1", static_cast<int>(getpid()),
                      static_cast<unsigned long long>(t / 1000));
        std::printf("SMOKE F2 no datagram out for %llu ms at +%llu (out=%llu bytes=%llu) -> %s\n",
                    static_cast<unsigned long long>(now_ms() - lastOutChangeMs), static_cast<unsigned long long>(t),
                    static_cast<unsigned long long>(o), static_cast<unsigned long long>(session.SessionBytesReceived()), cmd);
        (void)std::system(cmd);
        std::printf("SMOKE F2 stack captured %s\n", udp_counters().c_str());
      }
    }
    for (auto& st : steps) {
      if (!st.done && t >= st.atMs) run_step(st, t);
    }
    (void)sink.ReadySelectionGeneration();
    if (now_ms() >= nextLog) {
      const uint64_t img = gImages.load();
      // r4 F2: what reached the session's socket (bytes) and the session's own state, next to the sink's.
      const auto snap = session.Snapshot();
      std::printf("SMOKE t=%llu images=%llu (+%llu) bytes=%llu state=%d status=%s err=%s %s | %s\n",
                  static_cast<unsigned long long>(t / 1000), static_cast<unsigned long long>(img),
                  static_cast<unsigned long long>(img - lastImages),
                  static_cast<unsigned long long>(session.SessionBytesReceived()), static_cast<int>(snap.state),
                  snap.status.c_str(), snap.lastError.c_str(), udp_counters().c_str(), sink.DebugStatus().c_str());
      lastImages = img;
      nextLog += 1000;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
  session.Disconnect();
  sink.SetSurface(nullptr, nullptr);
  AImageReader_delete(reader);
  std::printf("SMOKE done images=%llu\n", static_cast<unsigned long long>(gImages.load()));
  return 0;
}
