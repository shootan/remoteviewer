// See host_control_session.hpp for the module summary. Serve() below is the former
// serve_control_session lambda of native_video_host_main.cpp, moved verbatim (host split refactor
// Phase 2-2 step 1); step 2 splits it into one Handle* per message type.

#include <winsock2.h>
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>

#include "host_unlock_relay.hpp"
#include "bandwidth_observe_wire.hpp"
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "host_bgra_scale.hpp"
#include "host_capture_device.hpp"
#include "host_control_session.hpp"
#include "host_pinned_select_seam.hpp"

#include "clipboard_monitor.hpp"
#include "clipboard_sync.hpp"
#include "paste_apply_wire.hpp"
#include "host_clip_image.hpp"
#include "host_thumbnail_budget.hpp"
#include "host_thumbnail_helper.hpp"
#include "host_input_inject.hpp"
#include "host_net_io.hpp"
#include "host_string_util.hpp"
#include "utf8_bounded.hpp"
#include "host_window_list_wire.hpp"
#include "host_file_copy.hpp"
#include "file_copy_wire.hpp"
#include "host_window_enum.hpp"
#include "host_monitor_selection.hpp"
#include "monitor_select_target.hpp"
#include "stream_test_hold.hpp"
#include "poc_protocol.hpp"
#include "prelogin_control_allowlist.hpp"
#include "prelogin_input_filter.hpp"
#include "lock_input_policy.hpp"
#include "prelogin_reboot_gate.hpp"
#include "prelogin_cred_host.hpp"
#include "prelogin_admin_pipe.hpp"
#include "prelogin_logon_desktop.hpp"
#include "prelogin_sas_wire.hpp"
#include "reboot_wire.hpp"
#include <mutex>
#include "peer_version.hpp"
#include "time_utils.hpp"

namespace remote60::native_poc {

namespace {
// Paste on demand (t-y4wj64jw): whether kCaptureFlagPasteOnDemandV1 is advertised. On unless
// REMOTE60_PASTE_ON_DEMAND starts with 0 / f / n. Read per pong, like mouse_xbuttons_enabled.
bool paste_on_demand_advertised() {
  char buf[8]{};
  const DWORD n = GetEnvironmentVariableA("REMOTE60_PASTE_ON_DEMAND", buf, sizeof(buf));
  if (n == 0 || n >= sizeof(buf)) return true;
  const char c = buf[0];
  return !(c == '0' || c == 'f' || c == 'F' || c == 'n' || c == 'N');
}
}  // namespace

namespace {

// Closes the outbound message when the dispatch arm returns, however it returns.
struct FlushControlMessageOnExit {
  ControlLink* link = nullptr;
  ~FlushControlMessageOnExit() {
    if (link) (void)link->EndMessage();
  }
};

}  // namespace

namespace {
// One remote restart in flight per host process, shared across both control dispatcher threads.
std::mutex gRebootGateMu;

// prelogin-ui r5 (G11): whether the service has pre-login access on, as last asked. Asked in the
// background every 30 s (never on the pong path, which must not wait on a pipe); false until known.
// r14 L2: on an owned thread (stoppable_worker.hpp), declared after the value it writes -- so at exit it is
// stopped (its 30 s wait woken, a query in flight cancelled) and joined before that value goes.
bool durable_prelogin_enabled() {
  static std::atomic<bool> enabled{false};
  static remote60::native_poc::StoppableWorker poll;
  static const bool started = [] {
    poll.Start([](remote60::native_poc::StoppableWorker& w) {
      const std::function<bool()> keep = [&w] { return !w.Stopping(); };
      do {
        enabled.store(remote60::native_poc::prelogin::QueryDurablePreloginEnabled(keep), std::memory_order_release);
      } while (w.Wait(30000));
    });
    return true;
  }();
  (void)started;
  return enabled.load(std::memory_order_acquire);
}

// prelogin-ui r3 (item 5): the pre-login worker's end of "Windows 로그인" (prelogin_cred_host.hpp).
// Its result line names the request and the stage, never the credential.
remote60::native_poc::prelogin::PreloginCredReceiver& prelogin_cred() {
  static remote60::native_poc::prelogin::PreloginCredReceiver receiver(
      remote60::native_poc::prelogin::ProductCredInject(), remote60::native_poc::prelogin::ProductCredTarget(),
      remote60::native_poc::prelogin::ProductCredEnvironment(), remote60::native_poc::prelogin::ProductCredHostId(),
      remote60::native_poc::prelogin::ProductCredTargetWaitMs());
  static const bool logged = [] {
    // r17 R16-1: nothing issued, opened or typed once this worker has left the logon desktop.
    receiver.stillServing = [] { return !remote60::native_poc::prelogin::PreloginLeftLatch().load(); };
    receiver.onResult = [](uint32_t requestId, uint16_t stage, uint32_t win32) {
      std::cout << "[native-video-host][prelogin-cred] requestId=" << requestId << " stage=" << stage
                << " win32=" << win32 << "\n";
    };
    return true;
  }();
  (void)logged;
  return receiver;
}

}  // namespace

ControlSessionServer::ControlSessionServer(const Args& args, std::atomic<bool>& stop,
                                           SessionState& clientSession, CaptureState& capture,
                                           ClientMetricsSnapshot& clientMetrics, EncoderState& encoder,
                                           InputRouterState& inputRouter, DesktopBackendState& backend,
                                           WindowSelectionTxn& windowSelectionTxn,
                                           MainLoopMailbox& mailbox, HostClipboardHub* clipboard)
    : args(args),
      stop(stop),
      clientSession(clientSession),
      capture(capture),
      clientMetrics(clientMetrics),
      encoder(encoder),
      inputRouter(inputRouter),
      backend(backend),
      windowSelectionTxn(windowSelectionTxn),
      mailbox(mailbox),
      clipboard(clipboard) {}

#ifdef REMOTE60_PINNED_SELECT_TEST_SEAM
// TEST BUILD ONLY (GNLinkStreamPinnedSelectTest, keepalive r1/r2): the host offers the test's own window
// and nothing else -- so a real client UI (the APK on an isolated AVD) can open its viewer through the
// product's own selection transaction on a host that never shows the user's desktop. FAIL-CLOSED (r2 K6):
//   - it does not start without --capture-window-pid AND --capture-window-title, nor when that process
//     cannot be opened, nor when no window of that process carries that title (pinned_select_seam_admit_start;
//     the open handle also keeps the pid from being reused for another process while this host runs);
//   - r3 R3: every capture start passes pinned_select_seam_capture_gate (host_pinned_select_seam.hpp) -- the
//     pinned window or the host ends; no screen, no fallback -- and a watcher ends the host the moment the
//     pinned window, its process or its title is gone;
//   - the window list is the pinned window or nothing -- never the product's enumeration;
//   - a selection, a thumbnail or a monitor is admitted only for the pinned window (no desktop, no other
//     HWND, no screen); the monitor list is empty;
//   - selection is not locked, so the pinned window goes through the product's selection transaction,
//     generation and key-frame order unchanged (no answer of its own, no extra IDR).
// The shipped GNLinkStream is built without any of this (build gate: no "pinned_test_seam" string).
namespace {
HANDLE g_pinnedProcess = nullptr;
// r3 R3: the pinned window, found once at start -- never re-enumerated, so no other HWND can take its place.
HWND g_pinnedHwnd = nullptr;
DWORD g_pinnedPid = 0;
std::wstring g_pinnedNeedle;  // the pinned title, lower-case
bool env_is_one(const char* name) {
  char v[8] = {};
  return GetEnvironmentVariableA(name, v, sizeof(v)) > 0 && v[0] == '1';
}
bool pinned_capture_probe() {
  static const bool on = env_is_one("REMOTE60_PINNED_TEST_CAPTURE_PROBE");
  return on;
}
// r2 K5 negative control: REMOTE60_PINNED_TEST_OLD_HOST=1 makes this test host behave as a host from
// before the thumbnail flag -- it does not advertise it, and it takes ControlStreamState without stopping
// (the stream stays on) while still answering every Ping. The phone must not park on it.
bool pinned_test_old_host() {
  static const bool on = [] {
    char v[8] = {};
    return GetEnvironmentVariableA("REMOTE60_PINNED_TEST_OLD_HOST", v, sizeof(v)) > 0 && v[0] == '1';
  }();
  return on;
}
bool pinned_by_args(const Args& args) {
  return args.captureWindowPid != 0 && !args.captureWindowTitle.empty() && g_pinnedProcess != nullptr;
}

struct PinnedFind {
  DWORD pid;
  std::wstring needle;
  HWND found;
};
BOOL CALLBACK pinned_enum(HWND hwnd, LPARAM lp) {
  auto* f = reinterpret_cast<PinnedFind*>(lp);
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid != f->pid) return TRUE;
  std::wstring title = remote60::native_poc::get_window_title(hwnd);
  for (auto& c : title) c = static_cast<wchar_t>(towlower(c));
  if (title.find(f->needle) == std::wstring::npos) return TRUE;
  f->found = hwnd;
  return FALSE;
}
uint64_t pinned_window_id(const Args& args) {
  if (!pinned_by_args(args)) return 0;  // fail-closed: no pinned window, nothing admitted
  // r3 R3: the window found at start, while it is still the pinned one -- not whatever matches now.
  return pinned_select_seam_target_ok(g_pinnedHwnd) ? remote60::native_poc::hwnd_to_id(g_pinnedHwnd) : 0;
}

bool pinned_window_entry(uint64_t id, remote60::native_poc::WindowListEntry* out) {
  HWND hwnd = remote60::native_poc::window_id_to_hwnd(id);
  if (id == 0 || !hwnd || !IsWindow(hwnd)) return false;
  out->id = id;
  out->hwnd = hwnd;
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  out->pid = pid;
  RECT rc{};
  GetClientRect(hwnd, &rc);
  out->width = rc.right - rc.left;
  out->height = rc.bottom - rc.top;
  out->minimized = IsIconic(hwnd) != FALSE;
  const std::wstring title = remote60::native_poc::get_window_title(hwnd);
  out->title.clear();
  for (wchar_t c : title) out->title.push_back(c < 0x80 ? static_cast<char>(c) : '?');
  return true;
}
}  // namespace

bool pinned_select_seam_probe_ungated() {
  static const bool on = pinned_capture_probe() && env_is_one("REMOTE60_PINNED_TEST_PROBE_UNGATED");
  return on;
}

bool pinned_select_seam_target_ok(HWND hwnd) {
  if (!hwnd || hwnd != g_pinnedHwnd || !g_pinnedProcess || !IsWindow(hwnd)) return false;
  if (WaitForSingleObject(g_pinnedProcess, 0) != WAIT_TIMEOUT) return false;  // the pinned process ended
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid != g_pinnedPid) return false;
  std::wstring title = remote60::native_poc::get_window_title(hwnd);
  for (auto& c : title) c = static_cast<wchar_t>(towlower(c));
  return !g_pinnedNeedle.empty() && title.find(g_pinnedNeedle) != std::wstring::npos;
}

HWND pinned_select_seam_pinned_hwnd() { return g_pinnedHwnd; }

bool pinned_select_seam_no_capture() {
  static const bool on = env_is_one("REMOTE60_PINNED_TEST_NO_CAPTURE");
  return on;
}

PinnedTestFaults pinned_select_seam_faults() {
  PinnedTestFaults f;
  char path[MAX_PATH] = {};
  if (GetEnvironmentVariableA("REMOTE60_PINNED_TEST_FAULT_FILE", path, sizeof(path)) == 0) return f;
  FILE* fp = nullptr;
  if (fopen_s(&fp, path, "rb") != 0 || !fp) return f;
  char line[128];
  while (std::fgets(line, sizeof(line), fp)) {
    unsigned v = 0;
    if (std::sscanf(line, "pong_delay_ms=%u", &v) == 1) f.pongDelayMs = (std::min)(v, 10000u);
    if (std::sscanf(line, "ack_delay_ms=%u", &v) == 1) f.ackDelayMs = (std::min)(v, 10000u);
    if (std::sscanf(line, "drop_keys=%u", &v) == 1) f.dropKeys = (std::min)(v, 20u);
  }
  std::fclose(fp);
  return f;
}

std::atomic<unsigned> g_pinnedKeyDrops{0};
void pinned_select_seam_arm_key_drops(unsigned n) { g_pinnedKeyDrops.store(n, std::memory_order_release); }
bool pinned_select_seam_take_key_drop() {
  unsigned v = g_pinnedKeyDrops.load(std::memory_order_acquire);
  while (v > 0 && !g_pinnedKeyDrops.compare_exchange_weak(v, v - 1, std::memory_order_acq_rel)) {
  }
  return v > 0;
}

void pinned_select_seam_refuse(const char* where) {
  std::fprintf(stderr, "[pinned-select-test] refused: %s -- the host ends (65)\n", where ? where : "?");
  std::cout.flush();
  std::fflush(stdout);
  std::fflush(stderr);
  TerminateProcess(GetCurrentProcess(), 65);
  for (;;) Sleep(INFINITE);
}

void pinned_select_seam_capture_gate(HWND window, const char* where) {
  const bool ungated = pinned_select_seam_probe_ungated();
  const bool admitted = ungated || (window != nullptr && pinned_select_seam_target_ok(window));
  if (pinned_capture_probe()) {
    std::fprintf(stderr, "[pinned-select-test] capture-probe where=%s target=%s hwnd=0x%llx pinned=%d admitted=%d%s\n",
                 where ? where : "?", window ? "window" : "screen",
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(window)),
                 window != nullptr && window == g_pinnedHwnd ? 1 : 0, admitted ? 1 : 0, ungated ? " UNGATED" : "");
    std::cout.flush();
    std::fflush(stdout);
    std::fflush(stderr);
    TerminateProcess(GetCurrentProcess(), admitted ? 66 : 65);  // before any capture object exists
    for (;;) Sleep(INFINITE);
  }
  if (!admitted) pinned_select_seam_refuse(where);
}

bool pinned_select_seam_admit_start(const Args& args, std::string* why) {
  if (args.captureWindowPid == 0 || args.captureWindowTitle.empty()) {
    if (why) *why = "--capture-window-pid and --capture-window-title are both required";
    return false;
  }
  g_pinnedProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, args.captureWindowPid);
  if (!g_pinnedProcess) {
    if (why) *why = "the pinned process cannot be opened";
    return false;
  }
  g_pinnedPid = args.captureWindowPid;
  for (unsigned char c : args.captureWindowTitle) g_pinnedNeedle.push_back(static_cast<wchar_t>(towlower(c)));
  if (pinned_select_seam_probe_ungated()) return true;  // negative control: the 2abb8be admission
  // r3 R3: the window itself, of that process, with that title -- a live pid alone admits nothing. A
  // fixture that is still coming up gets 5 s.
  PinnedFind f{g_pinnedPid, g_pinnedNeedle, nullptr};
  for (int t = 0; t < 50 && !f.found; ++t) {
    if (WaitForSingleObject(g_pinnedProcess, 0) != WAIT_TIMEOUT) break;
    EnumWindows(pinned_enum, reinterpret_cast<LPARAM>(&f));
    if (!f.found) Sleep(100);
  }
  if (!f.found) {
    if (why) *why = "no window of the pinned process carries the pinned title";
    return false;
  }
  g_pinnedHwnd = f.found;
  // Lifetime: the window, its process or its title gone -> the host ends, whatever it is doing.
  std::thread([] {
    for (;;) {
      Sleep(100);
      if (!pinned_select_seam_target_ok(g_pinnedHwnd)) pinned_select_seam_refuse("the pinned window is gone");
    }
  }).detach();
  wchar_t deskName[128] = L"";
  DWORD deskLen = 0;
  GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, deskName, sizeof(deskName), &deskLen);
  wchar_t staName[128] = L"";
  GetUserObjectInformationW(GetProcessWindowStation(), UOI_NAME, staName, sizeof(staName), &deskLen);
  std::fprintf(stderr, "[pinned-select-test] pinned hwnd=0x%llx pid=%lu station=%ls desktop=%ls\n",
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_pinnedHwnd)),
               static_cast<unsigned long>(g_pinnedPid), staName, deskName);
  return true;
}
#endif

void ControlSessionServer::Serve(ControlLink& link, bool linkDirectoryAuthenticated) {
  // Which session this conversation belongs to. Checked again at the bottom before touching any
  // shared stream state, so a session that ends after its successor has started cannot turn the
  // successor's stream off. (Ledger H-28.)
  const uint64_t servedEpoch = clientSession.epoch.load(std::memory_order_acquire);
  // r7 H4 + r9 R3: what directory auth allows on THIS link -- the link's own standing (TCP: none), and the
  // admission of exactly this epoch (SessionState::AdmitHello), never the shared flag a newer session's
  // Hello sets while this dispatcher still serves the old one.
  // r9 W2: a --prelogin worker injects only while the input desktop is the logon desktop it serves.
  // r10 T2: and not from a cached look either -- the input desktop is asked again for this event: it must be
  // the desktop the worker serves (the logon desktop; a test build's named one), or nothing is injected.
  auto prelogin_not_serving = [&]() {
    if (!args.preloginMode) return false;
    if (!capture.preloginDesktopServing.load(std::memory_order_acquire)) return true;
    const std::wstring now = remote60::native_poc::prelogin::InputDesktopName();
    // r17 R16-1: what this path SEES is the same permanent leave as the watch's (exit 14), not only this
    // input refused.
    return remote60::native_poc::prelogin::PreloginInputRefused(now, capture.preloginServingDesktop);
  };
  auto link_directory_authenticated = [&]() {
    return linkDirectoryAuthenticated && clientSession.DirectoryAuthenticatedFor(servedEpoch);
  };
#ifdef REMOTE60_STREAM_TEST_SEAM
  // TEST BUILD ONLY (r7 H6): the first pong of this conversation is answered late
  // (GNLINK_STREAM_TEST_FIRST_PONG_DELAY_MS, at most 5 s) so a test can type while the viewer does not
  // know yet what this host is. Nothing else changes.
  bool firstPongDelayed = false;
#endif
  // Keys and buttons held from an earlier client are not this one's; a resume keeps its marks.
  clientSession.injectedInput.BeginServe(servedEpoch);
  // A new session starts with the stream on, exactly like the first client of a fresh
  // process. The previous session's disconnect turned it off, and a client that never
  // sends stream-state (the Windows client) would otherwise stare at a black screen
  // forever after any reconnect. Clients that manage the state explicitly still can.
  if (!clientSession.streamControlActive.exchange(true, std::memory_order_acq_rel)) {
    std::cout << "[native-video-host][control] stream restored for new session\n";
  }
  auto send_window_list = [&](uint32_t seq) -> bool {
    ControlWindowListMessage rsp{};
    rsp.header.magic = remote60::native_poc::kMagic;
    rsp.header.type = static_cast<uint16_t>(MessageType::ControlWindowList);
    rsp.header.size = static_cast<uint16_t>(sizeof(rsp));
    rsp.seq = seq;
#ifdef REMOTE60_PINNED_SELECT_TEST_SEAM
    constexpr bool pinnedSeam = true;  // r2 K6: never the product's enumeration in this build
#else
    constexpr bool pinnedSeam = false;
#endif
    if (capture.windowSelectionLocked.load(std::memory_order_relaxed) && !pinnedSeam) {
      rsp.flags |= remote60::native_poc::kControlWindowListFlagSelectionLocked;
    }
    // Tells the client it may ask for previews; older hosts leave this clear and
    // older clients ignore the bit, so both directions stay compatible.
#ifdef REMOTE60_PINNED_SELECT_TEST_SEAM
    if (!pinned_test_old_host())
#endif
    rsp.flags |= remote60::native_poc::kControlWindowListFlagThumbnails;
    // Says the monitor messages exist here. A client that asked an older host would wait for a
    // reply that never comes, since unknown opcodes are drained silently.
    rsp.flags |= remote60::native_poc::kControlWindowListFlagMonitors;
    // And that a monitor can be selected through the window-select transaction (applied, then
    // acknowledged with the new stream generation). Clients without it keep ControlMonitorSelect.
    rsp.flags |= remote60::native_poc::kControlWindowListFlagMonitorSelectTransactionV1;
    rsp.selectedWindowId = capture.selectedWindowId.load(std::memory_order_relaxed);
    auto windows = pinnedSeam ? std::vector<remote60::native_poc::WindowListEntry>{} : enumerate_shareable_windows();
#ifdef REMOTE60_PINNED_SELECT_TEST_SEAM
    if (pinnedSeam) {
      remote60::native_poc::WindowListEntry pinned;
      if (pinned_window_entry(pinned_window_id(args), &pinned)) windows.push_back(pinned);
    }
#endif
    rsp.itemCount = std::min<uint32_t>(
        static_cast<uint32_t>(windows.size()), remote60::native_poc::kControlWindowListMaxEntries);
    for (uint32_t i = 0; i < rsp.itemCount; ++i) {
      remote60::native_poc::fill_window_entry(rsp.items[i], windows[i]);
    }
    std::cout << "[native-video-host][control] window-list seq=" << seq
              << " count=" << rsp.itemCount
              << " selectedId=" << rsp.selectedWindowId
              << "\n";
    return link.Write(&rsp, sizeof(rsp));
  };
  // The screens, by device name, in the order of the last monitor list THIS connection was sent:
  // a monitor-select target names an index into the list the client saw, so it is resolved here
  // and not against whatever is enumerated when the request arrives (a screen added or removed
  // in between would otherwise turn "monitor 1" into another physical screen).
  std::vector<std::wstring> monitorListSent;
  auto send_monitor_list = [&](uint32_t seq) -> bool {
    ControlMonitorListMessage rsp{};
    rsp.header.magic = remote60::native_poc::kMagic;
    rsp.header.type = static_cast<uint16_t>(MessageType::ControlMonitorList);
    rsp.header.size = static_cast<uint16_t>(sizeof(rsp));
    rsp.seq = seq;
    rsp.selectedMonitorId = capture.selectedMonitorId.load(std::memory_order_acquire);
#ifdef REMOTE60_PINNED_SELECT_TEST_SEAM
    const std::vector<remote60::native_poc::MonitorListEntry> monitors;  // r2 K6: no screen in this build
#else
    const auto monitors = enumerate_monitors();
#endif
    // t-970r4zgo: the selection named against THIS list, by the rule the capture restart opens
    // the screen with -- a picked screen that is gone is reported as the one shown instead
    // (the primary), not as a stale index that may now be out of range or another screen.
    std::wstring selectedDevice;
    {
      std::lock_guard<std::mutex> lk(capture.metaMu);
      selectedDevice = capture.selectedMonitorDevice;
    }
    const SelectedMonitorChoice choice = choose_selected_monitor(monitors, selectedDevice);
    if (choice.outcome == SelectedMonitorOutcome::Found || choice.outcome == SelectedMonitorOutcome::FellBack) {
      rsp.selectedMonitorId = static_cast<uint32_t>(choice.index);
    }
    rsp.itemCount = std::min<uint32_t>(static_cast<uint32_t>(monitors.size()),
                                       remote60::native_poc::kControlMonitorListMaxEntries);
    for (uint32_t i = 0; i < rsp.itemCount; ++i) {
      const auto& src = monitors[i];
      auto& dst = rsp.items[i];
      dst.id = i;
      dst.x = src.x;
      dst.y = src.y;
      dst.width = src.width;
      dst.height = src.height;
      if (src.primary) dst.flags |= remote60::native_poc::kControlMonitorFlagPrimary;
      remote60::native_poc::utf8_copy_bounded(dst.name, sizeof(dst.name), src.name);
    }
    monitorListSent.clear();
    for (uint32_t i = 0; i < rsp.itemCount; ++i) monitorListSent.push_back(monitors[i].device);
    std::cout << "[native-video-host][control] monitor-list seq=" << seq
              << " count=" << rsp.itemCount << " selectedId=" << rsp.selectedMonitorId << "\n";
    return link.Write(&rsp, sizeof(rsp));
  };
  // Deliberately a function-local static, which is to say per process rather than per session.
  //
  // A window that stopped answering has not started answering because a client reconnected, and
  // the 2026-09-15 host saw three sessions in a row walk into the same window. Per-session state
  // would have reset the budget each time and paid the deadline again on every one.
  //
  // Guarded, because Serve is not single-threaded. With controlPort > 0 the host runs two
  // dispatchers on the same server object -- the TCP controlThread (host_startup_control.cpp:127,
  // serving at :163) and the UDP udpControlThread (:397, serving at :431) -- and an operational
  // host runs both. An earlier version of this comment said there was one, which would have left
  // two threads walking the same map.
  //
  // Concurrency is no longer left to the caller's shape. Two dispatchers could each be in a
  // capture, so capture_thumbnail_isolated enforces one at a time itself and refuses the second
  // with thumb_busy -- which costs the window nothing, because the refusal is ours. Each
  // dispatcher serves one client at a time and holds nothing while a helper runs.
  static ThumbnailBudget thumbnailBudget;
  static std::mutex thumbnailBudgetMu;

  auto send_window_thumbnail =
      [&](const ControlWindowThumbnailRequestMessage& req) -> bool {
    const uint32_t maxW = std::clamp<uint32_t>(
        req.maxWidth == 0 ? 256u : req.maxWidth, 16u,
        remote60::native_poc::kWindowThumbnailMaxWidth);
    const uint32_t maxH = std::clamp<uint32_t>(
        req.maxHeight == 0 ? 160u : req.maxHeight, 16u,
        remote60::native_poc::kWindowThumbnailMaxHeight);

    std::vector<uint8_t> bgra;
    uint32_t tw = 0;
    uint32_t th = 0;
    bool ok = false;

    HWND hwnd = req.windowId == 0 ? nullptr : window_id_to_hwnd(req.windowId);
#ifdef REMOTE60_PINNED_SELECT_TEST_SEAM
    // r2 K6: the pinned window's preview or none -- not the desktop, not another window.
    const bool eligible = req.windowId != 0 && req.windowId == pinned_window_id(args);
#else
    const bool eligible = req.windowId == 0 || should_include_window(hwnd);
#endif

    if (eligible) {
      // The key identifies the window well enough that a recycled HWND does not inherit another
      // window's cooldown. The desktop (id 0) has no owner, and its capture does not go through
      // PrintWindow at all, so it is keyed on zeros and in practice never accumulates failures.
      ThumbnailTargetKey key;
      key.windowId = req.windowId;
      if (hwnd != nullptr) {
        DWORD ownerPid = 0;
        GetWindowThreadProcessId(hwnd, &ownerPid);
        key.ownerPid = static_cast<uint32_t>(ownerPid);
        HANDLE owner = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ownerPid);
        if (owner) {
          FILETIME created{}, exited{}, kernel{}, user{};
          if (GetProcessTimes(owner, &created, &exited, &kernel, &user)) {
            key.processCreatedUs = (static_cast<uint64_t>(created.dwHighDateTime) << 32) |
                                   created.dwLowDateTime;
          }
          CloseHandle(owner);
        }
      }

      const uint64_t nowUs = qpc_now_us();
      bool allowed = false;
      {
        std::lock_guard<std::mutex> lk(thumbnailBudgetMu);
        allowed = thumbnailBudget.Allow(key, nowUs);
      }
      if (!allowed) {
        // Skipped, not failed. Nothing is attempted, so nothing is charged, and the client is
        // told there is no preview -- which is what it would have been told anyway, without the
        // control thread spending a deadline to find out.
        std::cout << "[native-video-host][control] thumbnail skipped windowId=" << req.windowId
                  << " (cooldown)\n";
      } else {
        // The capture runs in a process this host can end. See host_thumbnail_helper.hpp for why
        // a thread would not do.
        // Deliberately outside the lock. The capture is up to a deadline long, and holding the
        // budget for it would make the other dispatcher wait on this window's helper.
        ThumbnailCaptureResult captured =
            capture_thumbnail_isolated(hwnd, maxW, maxH, kThumbnailDeadlineUs);
        {
          std::lock_guard<std::mutex> lk(thumbnailBudgetMu);
          thumbnailBudget.Record(key, captured.outcome, qpc_now_us());
        }
        if (captured.outcome == ThumbnailOutcome::Ok) {
          bgra = std::move(captured.bgra);
          tw = captured.width;
          th = captured.height;
          ok = true;
        } else {
          std::cout << "[native-video-host][control] thumbnail windowId=" << req.windowId
                    << " gave up after " << (captured.elapsedUs / 1000) << "ms"
                    << " (spawn " << (captured.spawnUs / 1000) << "ms, wait "
                    << (captured.waitUs / 1000) << "ms, teardown "
                    << (captured.teardownUs / 1000) << "ms"
                    << (captured.helperLingering ? ", helper LINGERING" : "") << "): "
                    << captured.detail << "\n";
        }
      }
    }

    if (bgra.size() > remote60::native_poc::kWindowThumbnailMaxPayloadBytes) {
      ok = false;
    }

    ControlWindowThumbnailHeader rsp{};
    rsp.header.magic = remote60::native_poc::kMagic;
    rsp.header.type = static_cast<uint16_t>(MessageType::ControlWindowThumbnail);
    rsp.header.size = static_cast<uint16_t>(sizeof(rsp));
    rsp.seq = req.seq;
    rsp.windowId = req.windowId;
    if (ok) {
      rsp.flags |= 0x1u;
      rsp.width = tw;
      rsp.height = th;
      rsp.stride = tw * 4u;
      rsp.payloadSize = static_cast<uint32_t>(bgra.size());
      rsp.version = qpc_now_us();
    }
    if (!link.Write(&rsp, sizeof(rsp))) return false;
    if (rsp.payloadSize == 0) return true;
    return link.Write(bgra.data(), bgra.size());
  };
  auto send_input_ack = [&](uint32_t seq) -> bool {
    ControlInputAckMessage ack{};
    ack.header.magic = remote60::native_poc::kMagic;
    ack.header.type = static_cast<uint16_t>(MessageType::ControlInputAck);
    ack.header.size = static_cast<uint16_t>(sizeof(ack));
    ack.seq = seq;
    ack.hostRecvQpcUs = qpc_now_us();
    ack.hostSendQpcUs = qpc_now_us();
    return link.Write(&ack, sizeof(ack));
  };

  // P0 telemetry (input serialization diagnosis, #351): host-side move recv/inject rate, one line
  // per second, to place against client moveSent and DXGI content per second.
  uint64_t p0hMoveRecv = 0;
  uint64_t p0hMoveInjected = 0;
  uint64_t p0hMoveInjectFail = 0;
  uint64_t p0hLastEmitUs = 0;
  // Sealed-unlock relay: process-wide (one worker + one pipe to the SYSTEM service). The cookie binds
  // this control connection to the challenge it requests, so a sealed reply from another session is
  // rejected by the service. (Codex #365/#366.)
  static remote60::native_poc::HostUnlockRelay gUnlockRelay;
#ifdef REMOTE60_STREAM_TEST_SEAM
  // TEST BUILD ONLY (lockscreen-cred e2e): the relay's pipe is the test service's (GNLinkInputServiceLifecycleTest,
  // per-run name), whose server this build accepts as the service -- the shipped host verifies the installed one.
  {
    static const bool pointed = [] {
      static wchar_t pipe[256] = {};
      const DWORD n = GetEnvironmentVariableW(L"GNLINK_STREAM_TEST_UNLOCK_PIPE", pipe, 256);
      if (n == 0 || n >= 256) return false;
      gUnlockRelay.SetVerificationEndpointForTest(pipe, L"GNLinkSisTestService", [](void* h, const wchar_t*) {
        remote60::native_poc::ServerIdentityFacts f;
        ULONG pid = 0;
        f.serverPidKnown = GetNamedPipeServerProcessId(static_cast<HANDLE>(h), &pid) != FALSE;
        f.serverPid = f.servicePid = pid;
        f.serviceRunning = f.serverImageKnown = f.serviceBinaryKnown = f.imageSameFile = f.finalConsistent =
            f.serverPidKnown;
        return f;
      });
      std::cout << "[native-video-host][lock-cred] test build: unlock relay pointed at the test service pipe\n";
      return true;
    }();
    (void)pointed;
  }
#endif
  bool lockCredAdvertised = false;  // lockscreen-cred r1: the last LockCredV1 said, for its transition line
  // lockscreen-cred r1 (A6, A7): plaintext refused because the own session is locked, and host-IME physical
  // keys dropped because the input desktop is not Default (K3) -- counted, said on the first and every 200th,
  // never with a key code or text.
  static std::atomic<uint64_t> lockRefusedTotal{0};
  static std::atomic<uint64_t> physicalSecureDropTotal{0};
  auto note_lock_refusal = [&](const char* what) {
    const uint64_t n = lockRefusedTotal.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n == 1 || n % 200 == 0) {
      std::cout << "[native-video-host][input][lock] plaintext " << what
                << " refused: the session is at its lock screen (use \"Windows 로그인\") total=" << n << "\n";
    }
  };
  auto note_physical_secure_drop = [&]() {
    const uint64_t n = physicalSecureDropTotal.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n == 1 || n % 200 == 0) {
      std::cout << "[native-video-host][input][physical] key not injected: the input desktop is not Default total=" << n
                << "\n";
    }
  };
  const uint64_t unlockCookie =
      (static_cast<uint64_t>(qpc_now_us()) << 20) ^ reinterpret_cast<uintptr_t>(&stop);
  // r5 G7: when this control session ends, however it ends, a "Windows 로그인" credential it sent that is
  // still waiting for the logon UI is dropped untyped.
  struct EndPreloginCredOnExit {
    bool prelogin;
    uint64_t cookie;
    ~EndPreloginCredOnExit() {
      if (prelogin) {
        prelogin_cred().EndSession(cookie);
        std::cout << "[native-video-host][prelogin-cred] control session ended\n";
      }
    }
  } endPreloginCredOnExit{args.preloginMode, unlockCookie};
  // Host-side IME: scan|E0 of physical keys we actually injected as down. Their matching up must be
  // released even if the policy gate has since closed (foreground/secure change), or a modifier
  // stays stuck on the host. Released for all on connection end. (Codex #370 BLOCKER C.)
  std::set<uint16_t> physicalDown;
  // C0 stage 1 observation log state, per control session. Host clock only.
  uint64_t bwLastRecvUs = 0;
  uint64_t bwLastLogUs = 0;
  uint64_t bwSuppressed = 0;
  uint64_t bwUnnegotiated = 0;
  uint64_t bwInvalid = 0;
  auto release_all_physical_host = [&]() {
    for (uint16_t key : physicalDown) {
      (void)inject_physical_scan_key(static_cast<uint16_t>(key & 0xff), false, (key & 0x100) != 0);
    }
    physicalDown.clear();
  };
  // The same for virtual keys and mouse buttons (RV-01): everything this session injected as
  // down and has not seen go up -- including an up whose injection failed, which is not
  // retransmitted by anyone -- is released through the ordinary injection path when this Serve()
  // ends. That is both the end of the session and a control resume, which leaves Serve() and
  // re-enters it: a key-up the user made while the channel was dead may never arrive.
  auto release_injected_input = [&]() {
    int32_t lastX = 0;
    int32_t lastY = 0;
    const std::vector<InjectedInputTracker::Release> held =
        clientSession.injectedInput.TakeHeldForRelease(&lastX, &lastY);
    if (held.empty()) return;
    const bool desktopMode = !inputRouter.targetCriteria.enabled() &&
                             (capture.selectedWindowId.load(std::memory_order_acquire) == 0);
    HMONITOR targetMonitor = nullptr;
    {
      std::lock_guard<std::mutex> lock(capture.inputTargetMu);
      targetMonitor = reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(capture.inputTarget.monitorHandle));
    }
    for (const auto& r : held) {
      ControlInputEventMessage up{};
      up.kind = r.kind;
      up.keyCode = r.keyCode;
      up.x = lastX;
      up.y = lastY;
      std::string target;
      InputFailStage stage = InputFailStage::None;
      DWORD error = 0;
      const InputInjectResult result =
          desktopMode && !targetMonitor
              ? InputInjectResult::NoTarget
              : inject_background_input_event(up, inputRouter.targetCriteria, capture.targetHwnd,
                                              desktopMode,
                                              inputRouter.domainW.load(std::memory_order_acquire),
                                              inputRouter.domainH.load(std::memory_order_acquire),
                                              &inputRouter.desktopState, &target, &stage, &error,
                                              targetMonitor);
      std::cout << "[native-video-host][input] released-at-serve-exit kind=" << r.kind
                << " key=" << r.keyCode << " result="
                << (result == InputInjectResult::Injected ? "injected" : "not-injected")
                << " stage=" << input_fail_stage_name(stage) << " err=" << error << target << "\n";
    }
  };
  std::cout << "[native-video-host][connection-version] localProcess=GNLinkStream"
            << " localVersion=" << local_product_version()
            << " localPid=" << GetCurrentProcessId()
            << " peerProcess=GNLinkViewer peerVersion=unknown-awaiting-exchange\n";
  while (!stop.load()) {
    MessageHeader header{};
    if (!link.Read(&header, sizeof(header))) break;
    // Marks the response boundary the UDP transport needs; a no-op over TCP.
    const FlushControlMessageOnExit flushResponse{&link};
    if (header.magic != remote60::native_poc::kMagic || header.size < sizeof(header)) break;
    const size_t bodySize = static_cast<size_t>(header.size - sizeof(header));
    const auto type = static_cast<MessageType>(header.type);

    // Prelogin: the pre-login worker exposes only the minimum control surface (prelogin-service §3).
    // A control type outside the allowlist (clipboard/file/paste/char-text/raw-key/window-enum/IME/...)
    // is a misbehaving, old, or hostile peer; close the link rather than risk a half-read variable
    // body. A correct viewer gates these on kCaptureFlagPreloginV1 and never sends them here.
    if (args.preloginMode && !remote60::native_poc::prelogin::IsControlAllowedInPrelogin(type)) {
      std::cout << "[native-video-host][control][prelogin] rejected control type="
                << static_cast<unsigned>(header.type) << "\n";
      break;
    }

    if (type == MessageType::ControlVersionRequest && header.size == sizeof(ControlVersionMessage)) {
      ControlVersionMessage request{};
      if (!link.Read(&request.seq, sizeof(request) - sizeof(MessageHeader))) break;
      std::cout << "[native-video-host][connection-version] localProcess=GNLinkStream"
                << " localVersion=" << local_product_version()
                << " localPid=" << GetCurrentProcessId()
                << " peerProcess=GNLinkViewer peerVersion=" << reported_peer_version(request.productVersion)
                << " peerVersionSource=peer-report seq=" << request.seq << "\n";
      const auto response = make_version_message(MessageType::ControlVersionResponse, request.seq);
      if (!link.Write(&response, sizeof(response))) break;
      continue;
    }

    if (type == MessageType::ControlPing && header.size == sizeof(ControlPingMessage)) {
      ControlPingMessage ping{};
      ping.header = header;
      if (!link.Read(&ping.seq, sizeof(ping) - sizeof(MessageHeader))) break;
#ifdef REMOTE60_PINNED_SELECT_TEST_SEAM
      if (!clientSession.streamControlActive.load(std::memory_order_acquire)) {
        const unsigned delayMs = pinned_select_seam_faults().pongDelayMs;
        if (delayMs > 0) {
          std::cout << "[pinned-select-test] fault: pong seq=" << ping.seq << " held " << delayMs << " ms (paused)\n";
          std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }
      }
#endif
#ifdef REMOTE60_STREAM_TEST_SEAM
      if (!firstPongDelayed) {
        firstPongDelayed = true;
        wchar_t v[16] = {};
        if (GetEnvironmentVariableW(L"GNLINK_STREAM_TEST_FIRST_PONG_DELAY_MS", v, 16) > 0) {
          const DWORD ms = static_cast<DWORD>(_wtoi(v));
          std::cout << "[native-video-host][test-seam] first pong delayed " << ms << " ms\n";
          Sleep(ms > 5000 ? 5000 : ms);
        }
      }
#endif
      ControlPongMessage pong{};
      pong.header.magic = remote60::native_poc::kMagic;
      pong.header.type = static_cast<uint16_t>(MessageType::ControlPong);
      pong.header.size = static_cast<uint16_t>(sizeof(pong));
      pong.seq = ping.seq;
      pong.clientSendQpcUs = ping.clientSendQpcUs;
      pong.hostRecvQpcUs = qpc_now_us();
      pong.hostSendQpcUs = qpc_now_us();
      // One snapshot, so the pid / hwnd / process / title the viewer receives all describe the
      // same instant. (Phase 4: SnapshotTarget.)
      const CaptureTargetSnapshot target = capture.SnapshotTarget();
      pong.captureTargetPid = target.pid;
      pong.captureTargetFlags = target.flags;
      // The probe is cached for 250ms, so asking it per ping is cheap. Telling the viewer that
      // a security prompt is up is the difference between an explained pause and an apparent
      // freeze, and it costs one bit in a word that is already on the wire.
      if (!interactive_desktop_is_default()) {
        pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagSecureDesktopActive;
      }
      // Advertise sealed-unlock v1 support so a viewer only sends the unlock messages here.
      pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagUnlockSealedV1;
      pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagHostImeV1;
      // v2: this host understands the make-only (Hangul/Hanja) pulse flag and the ImeState handshake.
      pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagHostImePulseStateV2;
      if (capture.frameHeartbeatEnabled.load())
        pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagFrameHeartbeat;
      pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagPeerVersion;
      if (args.preloginMode) {
        // Serving the pre-login secure desktop right now (prelogin-service r3 R1: the momentary signal).
        pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagPreloginV1;
        // The worker only runs because the feature is enabled, so it is also durable-enabled -- the bit
        // the reboot confirm popup keys its "no connect until login" warning on.
        pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagPreloginEnabledV1;
        pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagPreloginSasV1;  // r9 R5: Ctrl+Alt+Del here
      } else if (durable_prelogin_enabled()) {
        // r5 G11: a logged-in host says so too when the service has pre-login access on, so the restart
        // popup does not warn "no connect until someone logs in" for a PC that will be reachable.
        pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagPreloginEnabledV1;
      }
      // lockscreen-cred r1 (Codex A1): "Windows 로그인" at the lock screen -- only while THIS host's own
      // session is known (WTS) to be locked, active and the console, and the service said it can type into
      // the lock screen. A UAC prompt (secure desktop, session not locked) never sets it; nor does an
      // unknown state. The pre-login worker has its own role (PreloginV1) and never sets this.
      if (!args.preloginMode) {
        const remote60::native_poc::OwnSessionState own = remote60::native_poc::own_session_state();
        const bool lockScreen = own.lockKnown && own.locked && own.stateKnown && own.active && own.console;
        bool offered = false;
        if (lockScreen) {
          gUnlockRelay.ProbeLockCredAsync();
          offered = gUnlockRelay.LockCredSupportNow() == remote60::native_poc::HostUnlockRelay::LockCredSupport::Supported;
        }
        if (offered) pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagLockCredV1;
        if (offered != lockCredAdvertised) {
          lockCredAdvertised = offered;
          std::cout << "[native-video-host][lock-cred] advertised=" << (offered ? 1 : 0) << " lockKnown=" << own.lockKnown
                    << " locked=" << own.locked << " active=" << own.active << " console=" << own.console << "\n";
        }
      }
      // Remote reboot is offered by ANY host -- prelogin AND the logged-in session (the case the user
      // hits most). The controls are the directory-authenticated session, the confirm popup, and the
      // single-flight dedup, NOT the mode. A host that is not durable-enabled simply did not advertise
      // 0x8000 above, so the viewer shows the "no connect until login after restart" warning.
      // (prelogin-service r4 item 6.)
      pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagRemoteRebootV1;
      // Clipboard text sync (K1): advertised only when the hub is running, so a viewer never sends
      // clipboard messages this host cannot handle. An old host never sets this and is never sent one.
      if (clipboard && clipboard->enabled())
        pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagClipboardTextV1;
      // Paste on demand: this host answers 80 with the outcome of the clipboard write (81). The same
      // condition as text sync, because the write goes through the same hub. REMOTE60_PASTE_ON_DEMAND=0
      // withholds the bit (operational off switch; the e2e uses it to stand in for an older host).
      if (clipboard && clipboard->enabled() && paste_on_demand_advertised())
        pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagPasteOnDemandV1;
      // Clipboard image v1: the image receiver runs. A viewer also needs kUdpFeatureBulkChannel in
      // its HelloAck before it offers anything.
      if (clientSession.clipImage && clientSession.clipImage->Enabled())
        pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagClipboardImageV1;
      // File copy (t-zdmsd4gb): switched on and the helper may run as the interactive user. Each
      // handler asks the same question again (file_copy_allowed): the bit is not the only gate.
      if (clientSession.fileCopy && clientSession.fileCopy->Advertised())
        pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagFileCopyV1;
      // Paste on demand r7 (D2): its file offers say which copy they are (86/87), with paste on demand.
      if (clientSession.fileCopy && clientSession.fileCopy->Advertised() && clipboard && clipboard->enabled() &&
          paste_on_demand_advertised())
        pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagFileOfferCopyGenV1;
      // Mouse X buttons (back / forward): a viewer sends VK_XBUTTON1/2 edges and X bits in its
      // held mask only to a host that says so -- an older host made an unknown key a LEFT click.
      if (mouse_xbuttons_enabled())
        pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagMouseXButtonsV1;
      // Prelogin exposes the MINIMUM surface (Codex review 3.3): strip every user-data feature bit so a
      // new viewer never even polls clipboard/IME/image/file/paste/x-button here (the allowlist would
      // close the link on the inbound message; not advertising it keeps a correct viewer from sending
      // one at all). Prelogin keeps only stream/heartbeat/version/secure-desktop + the sealed credential
      // path + the prelogin/reboot bits set earlier.
      if (args.preloginMode) {
        pong.captureTargetFlags &= ~(remote60::native_poc::kCaptureFlagHostImeV1 |
                                     remote60::native_poc::kCaptureFlagHostImePulseStateV2 |
                                     remote60::native_poc::kCaptureFlagClipboardTextV1 |
                                     remote60::native_poc::kCaptureFlagPasteOnDemandV1 |
                                     remote60::native_poc::kCaptureFlagClipboardImageV1 |
                                     remote60::native_poc::kCaptureFlagFileCopyV1 |
                                     remote60::native_poc::kCaptureFlagFileOfferCopyGenV1 |  // 86/87: not allowed here
                                     remote60::native_poc::kCaptureFlagMouseXButtonsV1);
      }
      pong.captureRebindCount = target.rebindCount;
      pong.captureTargetHwnd = target.targetHwnd;
      remote60::native_poc::utf8_copy_bounded(pong.captureTargetProcess,
                                              sizeof(pong.captureTargetProcess), target.process);
      remote60::native_poc::utf8_copy_bounded(pong.captureTargetTitle,
                                              sizeof(pong.captureTargetTitle), target.title);
      if (!link.Write(&pong, sizeof(pong))) break;
      continue;
    }

    // --- Sealed unlock (host relays to/from the SYSTEM service; never sees key material) ---------
    if (type == MessageType::ControlUnlockChallengeRequest &&
        header.size == sizeof(ControlUnlockChallengeRequestMessage)) {
      ControlUnlockChallengeRequestMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      ControlUnlockChallengeMessage rsp{};
      if (args.preloginMode) {
        // prelogin-ui r3 (item 5): before login the worker answers it -- the "Windows 로그인" credential.
        rsp = prelogin_cred().Challenge(req, unlockCookie);
      } else if (!gUnlockRelay.ChallengeSync(req, unlockCookie, &rsp)) {
        rsp = ControlUnlockChallengeMessage{};
        rsp.requestId = req.requestId;
        rsp.status = static_cast<uint16_t>(UnlockStage::InternalError);
      }
      rsp.header.magic = remote60::native_poc::kMagic;
      rsp.header.type = static_cast<uint16_t>(MessageType::ControlUnlockChallenge);
      rsp.header.size = static_cast<uint16_t>(sizeof(rsp));
      if (!link.Write(&rsp, sizeof(rsp))) break;
      continue;
    }

    // lockscreen-cred r1: "Windows 로그인" at the lock screen -- a challenge for the lock-credential purpose,
    // answered by the SYSTEM service (never by this host); 41 carries the purpose in `reserved`. 42~45 then go
    // the same way as the unlock's. Not in the pre-login worker's allowlist (it closes the link there).
    if (type == MessageType::ControlLockCredChallengeRequest &&
        header.size == sizeof(ControlUnlockChallengeRequestMessage)) {
      ControlUnlockChallengeRequestMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      ControlUnlockChallengeMessage rsp{};
      if (!gUnlockRelay.LockCredChallengeSync(req, unlockCookie, &rsp)) {
        rsp = ControlUnlockChallengeMessage{};
        rsp.requestId = req.requestId;
        rsp.status = static_cast<uint16_t>(UnlockStage::InternalError);
        rsp.reserved = remote60::native_poc::kUnlockPurposeLockCred;
      }
      std::cout << "[native-video-host][lock-cred] challenge requestId=" << req.requestId << " status=" << rsp.status
                << "\n";
      rsp.header.magic = remote60::native_poc::kMagic;
      rsp.header.type = static_cast<uint16_t>(MessageType::ControlUnlockChallenge);
      rsp.header.size = static_cast<uint16_t>(sizeof(rsp));
      if (!link.Write(&rsp, sizeof(rsp))) break;
      continue;
    }

    if (type == MessageType::ControlUnlockSealedRequest &&
        header.size == sizeof(ControlUnlockSealedRequestMessage)) {
      ControlUnlockSealedRequestMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      const uint32_t sealedRequestId = req.requestId;  // preserve before we wipe the ciphertext copy
      if (args.preloginMode) {
        prelogin_cred().Sealed(req, unlockCookie);  // opened under the prelogin purpose, typed, zeroed
      } else {
        gUnlockRelay.SealedAsync(req, unlockCookie);
      }
      SecureZeroMemory(&req, sizeof(req));  // drop the ciphertext copy promptly
      ControlUnlockAcceptedMessage rsp{};
      rsp.header.magic = remote60::native_poc::kMagic;
      rsp.header.type = static_cast<uint16_t>(MessageType::ControlUnlockAccepted);
      rsp.header.size = static_cast<uint16_t>(sizeof(rsp));
      rsp.requestId = sealedRequestId;
      rsp.accepted = 1;
      if (!link.Write(&rsp, sizeof(rsp))) break;
      continue;
    }

    if (type == MessageType::ControlUnlockStatusRequest &&
        header.size == sizeof(ControlUnlockStatusRequestMessage)) {
      ControlUnlockStatusRequestMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      ControlUnlockStatusResultMessage rsp{};
      // lockscreen-cred r1: a typed lock-screen credential is still waiting for its unlock -- ask the service
      // where it is now (a bounded wait; other jobs are answered from the relay's cache as before).
      if (!args.preloginMode) (void)gUnlockRelay.RefreshLockCredStatus(req.requestId);
      const bool haveResult = args.preloginMode ? prelogin_cred().PollResult(req.requestId, &rsp)
                                                : gUnlockRelay.PollResult(req.requestId, &rsp);
      if (!haveResult) {
        rsp.requestId = req.requestId;
        rsp.stage = static_cast<uint16_t>(UnlockStage::ConnectStarted);  // still running
        rsp.terminal = 0;
      }
      rsp.header.magic = remote60::native_poc::kMagic;
      rsp.header.type = static_cast<uint16_t>(MessageType::ControlUnlockStatusResult);
      rsp.header.size = static_cast<uint16_t>(sizeof(rsp));
      if (!link.Write(&rsp, sizeof(rsp))) break;
      continue;
    }

    if (type == MessageType::ControlPreloginControl &&
        header.size == sizeof(remote60::native_poc::ControlPreloginControlMessage)) {
      remote60::native_poc::ControlPreloginControlMessage req{};
      req.header = header;
      if (!link.Read(&req.requestId, sizeof(req) - sizeof(MessageHeader))) break;
      if (req.requestId == 0) continue;  // never echoable
      // r9 R5: Ctrl+Alt+Del at the logon screen, by the service. Only a --prelogin worker, only on this
      // link's own directory-authenticated session; the service decides the rest (session, policy).
      using remote60::native_poc::PreloginSasResult;
      PreloginSasResult result = PreloginSasResult::Failed;
      uint32_t win32 = 0;
      bool cadNotRequired = false;
      if (req.op != remote60::native_poc::kPreloginControlOpSas) {
        result = PreloginSasResult::Failed;
        win32 = ERROR_NOT_SUPPORTED;
      } else if (!args.preloginMode) {
        result = PreloginSasResult::NotAtLogon;
      } else if (!link_directory_authenticated()) {
        result = PreloginSasResult::NotAuthorized;
      } else {
        result = static_cast<PreloginSasResult>(remote60::native_poc::prelogin::DelegateSasToService(&win32, &cadNotRequired));
      }
      std::cout << "[native-video-host][control][sas] requestId=" << req.requestId
                << " result=" << static_cast<unsigned>(result) << " win32=" << win32 << " cad-not-required="
                << (cadNotRequired ? 1 : 0) << "\n";
      const auto rsp = remote60::native_poc::make_prelogin_status(
          req.requestId, req.op, result, win32,
          cadNotRequired ? remote60::native_poc::kPreloginStatusFlagCadNotRequired : uint8_t{0});
      if (!link.Write(&rsp, sizeof(rsp))) break;
      continue;
    }

    if (type == MessageType::ControlRebootRequest &&
        header.size == sizeof(remote60::native_poc::ControlRebootRequestMessage)) {
      remote60::native_poc::ControlRebootRequestMessage req{};
      req.header = header;
      if (!link.Read(&req.requestId, sizeof(req) - sizeof(MessageHeader))) break;
      // A malformed request id (0) is never echoable and cannot be deduped -- drop it (Codex review S4;
      // Serve was not calling reboot_request_valid). A correct viewer never sends 0.
      if (!remote60::native_poc::reboot_request_valid(req)) {
        std::cout << "[native-video-host][control][reboot] dropped malformed request (id=0)\n";
        continue;
      }
      // R2/S3: authorize against THIS served session, not just the shared bool. The auth must hold AND
      // the session epoch must still be the one this Serve began with -- a superseded/stale link cannot
      // borrow a current auth. (The full per-link auth flag threaded from ingress, and closing the direct
      // TCP/LAN listener in prelogin, are the remaining S3 depth.)
      // r7 H4: and THIS link must be the authenticated one -- a separate TCP link borrows nothing.
      auto reboot_authorized = [&]() { return link_directory_authenticated(); };
      const bool authorized = reboot_authorized();
      uint32_t win32 = 0;
      remote60::native_poc::RebootResult result = remote60::native_poc::RebootResult::NotAuthorized;
      if (authorized) {
        // r5 G10: the host answers nothing on its own -- no early Accepted for a retransmit, no refusal of
        // its own. Every authorized request goes to the service, which keeps the one restart and the one
        // answer per request (this host process, this control session, this id); the host relays it. The
        // OS call is the service's (it holds the shutdown privilege); a failure is Failed -- never forced.
        std::lock_guard<std::mutex> lock(gRebootGateMu);  // one delegation at a time from this host
        if (reboot_authorized()) {  // revoked / superseded while waiting: not on stale authority
          const uint8_t delegated =
              remote60::native_poc::prelogin::DelegateRebootToService(req.requestId, unlockCookie, &win32);
          result = static_cast<remote60::native_poc::RebootResult>(delegated);
        }
      }
      std::cout << "[native-video-host][control][reboot] requestId=" << req.requestId
                << " auth=" << (authorized ? 1 : 0)
                << " result=" << static_cast<unsigned>(result) << " win32=" << win32 << "\n";
      const remote60::native_poc::ControlRebootReplyMessage rsp =
          remote60::native_poc::make_reboot_reply(req.requestId, result, win32);
      if (!link.Write(&rsp, sizeof(rsp))) break;
      continue;
    }

    if (type == MessageType::ControlPhysicalKey &&
        header.size == sizeof(ControlPhysicalKeyMessage)) {
      ControlPhysicalKeyMessage k{};
      k.header = header;
      if (!link.Read(&k.seq, sizeof(k) - sizeof(MessageHeader))) break;
      // Host-side IME: inject the raw scan code so the host IME composes live. Gate it like the mouse
      // path (Codex #370 BLOCKER 5): only when injection is enabled, the desktop is NOT secure (never
      // type into a UAC/lock screen), and either desktop mode is active or the selected window is the
      // foreground window -- otherwise a physical key would leak into whatever app is in front. The ack
      // is a transport receipt (the client's one-per-RTT loop needs it), not an injection-success claim.
      const bool physExt = (k.flags & 0x1u) != 0;
      const bool physMakeOnly = (k.flags & 0x4u) != 0;
      const uint16_t physKey = static_cast<uint16_t>(k.scanCode | (physExt ? 0x100 : 0));
      auto gate_open = [&]() {
        if (!inputRouter.injectionEnabled || !interactive_desktop_is_default()) return false;
        const bool desktopMode = !inputRouter.targetCriteria.enabled() &&
                                 (capture.selectedWindowId.load(std::memory_order_acquire) == 0);
        if (desktopMode) return true;
        // Window mode: inject only when the selected window actually holds focus, so a physical key
        // never leaks into whatever app is in front. Use the shared focus test (root/owner aware)
        // so the host-IME physical path agrees with the legacy key/text paths -- exact-HWND was too
        // narrow and silently dropped every key for a windowed Electron target (GMux). (Codex B.)
        const HWND tgt = reinterpret_cast<HWND>(
            static_cast<uintptr_t>(capture.targetHwnd.load(std::memory_order_acquire)));
        return target_has_focus(tgt);
      };
      // lockscreen-cred r1: a down this host refuses is said (K3 drop / A6 lock), never with its key. The lock
      // rule is checked on its own, not only through the desktop gate, so it holds if that gate ever changes.
      auto down_allowed = [&]() {
        if (!args.preloginMode && remote60::native_poc::lock_input::RefusePhysicalKey(
                                      remote60::native_poc::own_session_treat_as_locked(), true, k.vk)) {
          note_lock_refusal("physical key");
          return false;
        }
        if (inputRouter.injectionEnabled && !interactive_desktop_is_default()) {
          note_physical_secure_drop();
          return false;
        }
        return gate_open();
      };
      if (physMakeOnly) {
        // Dedicated Hangul/Hanja key: a single make, never tracked and never released, so the host
        // IME toggles the same way the physical key does. A stray up (should not arrive) is ignored
        // -- it is not in physicalDown, so the release-all path never fabricates a break for it.
        // (Codex Edge 1.)
        if (k.down != 0 && down_allowed()) {
          (void)inject_physical_scan_key(k.scanCode, true, physExt);
        }
        send_input_ack(k.seq);
        continue;
      }
      if (k.down != 0) {
        if (down_allowed() && inject_physical_scan_key(k.scanCode, true, physExt)) {
          physicalDown.insert(physKey);
        }
      } else {
        // Release only a key we actually injected as down (gate or no gate, so nothing sticks). An
        // unmatched up -- e.g. a down that failed the gate or SendInput -- is dropped, never injected.
        // (Codex 3rd review.)
        if (physicalDown.erase(physKey) > 0) {
          (void)inject_physical_scan_key(k.scanCode, false, physExt);
        }
      }
      send_input_ack(k.seq);
      continue;
    }

    if (type == MessageType::ControlImeStateRequest &&
        header.size == sizeof(ControlImeStateRequestMessage)) {
      ControlImeStateRequestMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      // Host-side IME v2: align/query the focused app's IME under a target fence and report the
      // authoritative state. Only when injection is enabled and the desktop is not secure (never
      // touch a lock/UAC screen's IME). Otherwise report unknown so the viewer proceeds as '?'.
      ControlImeStateResponseMessage rsp{};
      rsp.header.magic = kMagic;
      rsp.header.type = static_cast<uint16_t>(MessageType::ControlImeStateResponse);
      rsp.header.size = static_cast<uint16_t>(sizeof(rsp));
      rsp.seq = req.seq;
      rsp.targetGeneration = req.targetGeneration;
      rsp.hostQpcUs = static_cast<uint64_t>(qpc_now_us());
      if (inputRouter.injectionEnabled && interactive_desktop_is_default()) {
        const bool setEnglish = (req.action == 1);
        const HostImeAlignResult a = host_ime_align_query(setEnglish);
        rsp.status = a.status;
        rsp.open = a.open;
        rsp.conversionMode = a.conversionMode;
        std::cout << "[native-video-host][ime] state-req seq=" << req.seq
                  << " gen=" << req.targetGeneration << " action=" << req.action
                  << " -> status=" << a.status << " open=" << a.open
                  << " conv=0x" << std::hex << a.conversionMode << std::dec << "\n";
      } else {
        rsp.status = 3;  // no-target / not allowed here
        std::cout << "[native-video-host][ime] state-req seq=" << req.seq
                  << " rejected inject=" << (inputRouter.injectionEnabled ? 1 : 0)
                  << " secureDesktop=" << (interactive_desktop_is_default() ? 0 : 1) << "\n";
      }
      if (!link.Write(&rsp, sizeof(rsp))) break;
      continue;
    }

    if (type == MessageType::ControlInputEvent && header.size == sizeof(ControlInputEventMessage)) {
      ControlInputEventMessage input{};
      input.header = header;
      if (!link.Read(&input.seq, sizeof(input) - sizeof(MessageHeader))) break;
      const uint64_t inputReceivedUs = qpc_now_us();
#ifdef REMOTE60_PINNED_SELECT_TEST_SEAM
      // viewer-sharpness r1, TEST BUILD ONLY (GNLinkStreamPinnedSelectTest, injection off): the remote pixel each
      // pointer event arrived with, so a tap on the phone's picture can be checked against where it was aimed.
      if (input.kind >= 1 && input.kind <= 4) {
        std::cout << "[native-video-host][input-trace] seq=" << input.seq << " kind=" << input.kind
                  << " x=" << input.x << " y=" << input.y << " buttons=" << input.buttons << "\n";
      }
#endif
      std::string resolvedTarget;
      // Prelogin payload filter (r4 #2 / Codex 3.1): kind 5/6 are key_down/key_up. Before login only
      // navigation keys may be injected as plaintext; a character/chord key is dropped here (never
      // injected) so credentials cannot leak into a field as cleartext -- they come via the sealed path.
      // The event is still acknowledged below so the viewer's strict request/response loop is not stalled.
      const bool preloginBlockedKey =
          args.preloginMode && (input.kind == 5 || input.kind == 6) &&
          !remote60::native_poc::prelogin::IsPreloginNavKeyAllowed(input.keyCode);
      if (preloginBlockedKey) {
        // r5 G8: which key it was is not written down -- it may be a character of a password.
        std::cout << "[native-video-host][input][prelogin] dropped non-navigation key seq=" << input.seq
                  << " (characters use the sealed credential path)\n";
      }
      // lockscreen-cred r1 (Codex A6): no plaintext character to this host's own session while it is locked
      // (or its lock state is unknown off the Default desktop) -- whatever the viewer believed, however late
      // the key arrives. Navigation keys, key ups and the pointer pass; a UAC prompt on an unlocked session
      // is untouched. Acknowledged below like any other (the ack is a receipt, not an injection).
      const bool lockBlockedKey = !args.preloginMode && remote60::native_poc::lock_input::IsCharacterKey(input.kind, input.keyCode) &&
                                  remote60::native_poc::own_session_treat_as_locked();
      if (lockBlockedKey) note_lock_refusal("key");
      // An up for a key or button the host already released itself at a Serve() exit: injecting
      // it again would be a second up for one keystroke. Acknowledged like any other. (RV-01)
      const bool releaseAlreadyDone =
          inputRouter.injectionEnabled &&
          clientSession.injectedInput.SwallowRelease(input.kind, input.keyCode);
      if (releaseAlreadyDone) {
        std::cout << "[native-video-host][input] up already released by the host seq=" << input.seq
                  << " kind=" << input.kind << " key=" << input.keyCode << "\n";
      } else if (inputRouter.injectionEnabled && !preloginBlockedKey && !lockBlockedKey && !prelogin_not_serving()) {
        const bool desktopMode =
            !inputRouter.targetCriteria.enabled() &&
            (capture.selectedWindowId.load(std::memory_order_acquire) == 0);
        const uint32_t domainW = inputRouter.domainW.load(std::memory_order_acquire);
        const uint32_t domainH = inputRouter.domainH.load(std::memory_order_acquire);
        InputInjectResult injectResult = InputInjectResult::Failed;
        // Prefer the SYSTEM agent, which is the only way into elevated windows and the lock
        // screen -- but fall back when it is unavailable. The service is registered by the
        // installer, so a host running from a build tree (or before installation) has no
        // broker at all, and treating that as a hard failure left the session with no input
        // whatsoever instead of the ordinary desktop injection that still works fine.
        //
        // The conjuncts are evaluated separately so a failure can name itself. Clicks that go
        // nowhere on a consent prompt look identical to clicks that work: the fallback path
        // below reports Injected either way, because SendInput on the Default desktop succeeds
        // whether or not anything is there to receive it. Counting which branch ran, and why,
        // is the difference between "input is broken" and a specific cause.
        const bool secureDesktopActive = !interactive_desktop_is_default();
        bool routedToAgent = false;
        if (secureDesktopActive) {
          inputRouter.secureAttempts.fetch_add(1, std::memory_order_relaxed);
          if (!desktopMode) {
            inputRouter.secureSkipWindowMode.fetch_add(1, std::memory_order_relaxed);
          } else if (!link_directory_authenticated()) {
            // A plain-LAN session has no capability token, and the agent will not act without
            // one. Nothing about the click is wrong; it simply cannot be authorised.
            inputRouter.secureSkipUnauthenticated.fetch_add(1, std::memory_order_relaxed);
          } else {
            // The one dispatch path (P9): the rect is verified for this event and stamped into
            // it; a refusal (rect not knowable, target changed) is counted inside.
            const SecureDispatchOutcome d = secure_dispatch_event(capture, inputRouter, input, domainW, domainH);
            if (d.sent) {
              inputRouter.secureDelivered.fetch_add(1, std::memory_order_relaxed);
              routedToAgent = true;
            } else if (d.writeFailed) {
              inputRouter.secureBrokerFailed.fetch_add(1, std::memory_order_relaxed);
            }
          }
        }
        // Set once the outcome has already been tallied (the secure-desktop path), so the result
        // switch below does not also count it -- e.g. as an inject failure.
        bool injectAccounted = false;
        if (routedToAgent) {
          injectResult = InputInjectResult::Injected;
          resolvedTarget = " secure-system-agent";
        } else if (secureDesktopActive) {
          // The cached check says the desktop is secure and the broker path did not route (window
          // mode, unauthenticated, or broker failure -- all already tallied). Ordinary SendInput on
          // a secure desktop would just fail, so do not fall through to it, and do not double-count
          // the miss as an inject failure.
          injectAccounted = true;
        } else {
          InputFailStage directFailStage = InputFailStage::None;
          DWORD directFailError = 0;
          HMONITOR targetMonitor = nullptr;
          {
            std::lock_guard<std::mutex> lock(capture.inputTargetMu);
            targetMonitor = reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(capture.inputTarget.monitorHandle));
          }
          injectResult =
              desktopMode && !targetMonitor ? InputInjectResult::NoTarget :
              inject_background_input_event(input, inputRouter.targetCriteria, capture.targetHwnd,
                                            desktopMode, domainW, domainH,
                                            &inputRouter.desktopState, &resolvedTarget,
                                            &directFailStage, &directFailError, targetMonitor);
          if (injectResult == InputInjectResult::Failed) {
            switch (directFailStage) {
              case InputFailStage::SetCursorPos:
                inputRouter.failSetCursorPos.fetch_add(1, std::memory_order_relaxed);
                break;
              case InputFailStage::SendInputMouse:
                inputRouter.failSendInputMouse.fetch_add(1, std::memory_order_relaxed);
                break;
              case InputFailStage::SendInputKey:
                inputRouter.failSendInputKey.fetch_add(1, std::memory_order_relaxed);
                break;
              case InputFailStage::PostMessage:
                inputRouter.failPostMessage.fetch_add(1, std::memory_order_relaxed);
                break;
              default:
                break;
            }
            // Stage travels with the target description so the existing inject-fail line needs
            // no format change downstream tooling would have to relearn.
            resolvedTarget += std::string(" stage=") + input_fail_stage_name(directFailStage) +
                              " err=" + std::to_string(directFailError);
          }
          if (injectResult == InputInjectResult::Failed) {
            // The cached check said default but injection failed: the 250ms cache may be stale
            // because a UAC prompt or lock screen rose since the last refresh. Pay for ONE uncached
            // probe on this specific failing event (never per event -- that would be far too costly
            // on a 100+/s pointer stream) to find out which it is.
            if (!interactive_desktop_is_default_uncached()) {
              inputRouter.freshProbeSecure.fetch_add(1, std::memory_order_relaxed);
              // Actually secure now. Retry THIS event through the SYSTEM broker exactly once.
              if (desktopMode &&
                  link_directory_authenticated() &&
                  secure_dispatch_event(capture, inputRouter, input, domainW, domainH).sent) {
                injectResult = InputInjectResult::Injected;
                resolvedTarget = " secure-system-agent(reprobe)";
                inputRouter.freshProbeReroute.fetch_add(1, std::memory_order_relaxed);
                inputRouter.secureDelivered.fetch_add(1, std::memory_order_relaxed);
              }
              // else: genuinely secure but not broker-eligible (window mode / unauthenticated) or
              // the broker failed -- stays Failed, but inputRouter.freshProbeSecure distinguishes it from
              // a real default-desktop failure below.
            } else {
              // Uncached re-probe still says default: a genuine failure on the interactive desktop
              // (UIPI, no target, a transient block). Counted separately so it is not confused
              // with the stale-cache case.
              inputRouter.injectFailDefault.fetch_add(1, std::memory_order_relaxed);
              // Field case (14:51 freeze): direct default-desktop injection FAILED at
              // SetCursorPos while the target resolved to a CoreWindow. SetCursorPos failing only
              // tells us one of its required conditions was unmet -- the host thread MAY lack the
              // current-input-desktop association or the window-station access it needs; the new
              // stage/error plus the agent log pin which on the next repro. The SYSTEM agent
              // reattaches to the current input desktop (SetThreadDesktop) before
              // SetCursorPos+SendInput, so retry this one event through it. Only stages that are
              // an actual OS injection API failure are worth retrying -- a coordinate/mapping
              // error (MapPoint) would just repeat in the agent.
              const bool brokerRetryableStage =
                  directFailStage == InputFailStage::SetCursorPos ||
                  directFailStage == InputFailStage::SendInputMouse ||
                  directFailStage == InputFailStage::SendInputKey;
              if (brokerRetryableStage && desktopMode && link_directory_authenticated()) {
                inputRouter.defaultBrokerFallback.fetch_add(1, std::memory_order_relaxed);
                if (secure_dispatch_event(capture, inputRouter, input, domainW, domainH).sent) {
                  // Queued to the agent, not confirmed landed (the broker does not ACK). Mark
                  // Injected so the host stops re-reporting inject-fail, but the honest signal
                  // is inputRouter.defaultBrokerQueued + the service log, not this result.
                  injectResult = InputInjectResult::Injected;
                  resolvedTarget += " default-broker-fallback(queued)";
                  inputRouter.defaultBrokerQueued.fetch_add(1, std::memory_order_relaxed);
                } else {
                  inputRouter.defaultBrokerPipeFail.fetch_add(1, std::memory_order_relaxed);
                }
              }
            }
          }
        }
        if (injectAccounted) {
          // Already tallied on the secure path; nothing more to record.
        } else if (injectResult == InputInjectResult::Injected) {
          clientSession.injectedInput.NoteInjected(input.kind, input.keyCode, input.x, input.y);
          const uint64_t n = inputRouter.events.fetch_add(1) + 1;
          if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
            std::cout << "[native-video-host][input] injected seq=" << input.seq
                      << " kind=" << input.kind
                      << " x=" << input.x
                      << " y=" << input.y
                      << " buttons=" << input.buttons
                      << " key=" << input.keyCode
                      << " mode=" << (desktopMode ? "desktop" : "window")
                      << resolvedTarget
                      << "\n";
          }
        } else if (injectResult == InputInjectResult::IgnoredMove) {
          inputRouter.ignoredMove.fetch_add(1, std::memory_order_relaxed);
        } else if (injectResult == InputInjectResult::NoTarget) {
          const uint64_t n = inputRouter.noTarget.fetch_add(1, std::memory_order_relaxed) + 1;
          if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
            std::cout << "[native-video-host][input] no-target seq=" << input.seq
                      << " kind=" << input.kind
                      << " filterPid=" << args.inputTargetPid
                      << " filterProc=" << trim_ascii(args.inputTargetProcess)
                      << " filterTitle=" << trim_ascii(args.inputTargetTitle)
                      << resolvedTarget
                      << "\n";
          }
        } else if (injectResult == InputInjectResult::Unsupported) {
          const uint64_t n = inputRouter.unsupported.fetch_add(1, std::memory_order_relaxed) + 1;
          if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
            std::cout << "[native-video-host][input] unsupported seq=" << input.seq
                      << " kind=" << input.kind
                      << " key=" << input.keyCode
                      << "\n";
          }
        } else {
          const uint64_t n = inputRouter.injectFail.fetch_add(1, std::memory_order_relaxed) + 1;
          if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
            std::cout << "[native-video-host][input] inject-fail seq=" << input.seq
                      << " kind=" << input.kind
                      << " key=" << input.keyCode
                      << resolvedTarget
                      << "\n";
          }
        }
        // P0 (#351): host-side move rate — moves RECEIVED and how many injected/failed, placed
        // against client moveSent and DXGI content per second.
        if (input.kind == 1) {
          ++p0hMoveRecv;
          if (injectResult == InputInjectResult::Injected) {
            ++p0hMoveInjected;
          } else if (injectResult == InputInjectResult::Failed ||
                     injectResult == InputInjectResult::NoTarget ||
                     injectResult == InputInjectResult::Unsupported) {
            ++p0hMoveInjectFail;
          }
        }
      } else if (args.inputLogEvery > 0 && (input.seq % args.inputLogEvery) == 0) {
        std::cout << "[native-video-host][input] blocked seq=" << input.seq
                  << " key=" << input.keyCode
                  << " kind=" << input.kind
                  << "\n";
      }
      {
        // P0 (#351): emit host move rate about once a second (driven by input arrivals).
        const uint64_t p0NowUs = qpc_now_us();
        if (p0hLastEmitUs == 0) p0hLastEmitUs = p0NowUs;
        if (p0NowUs - p0hLastEmitUs >= 1000000ULL) {
          std::cout << "[native-video-host][input-p0] windowUs=" << (p0NowUs - p0hLastEmitUs)
                    << " moveRecv=" << p0hMoveRecv
                    << " moveInjected=" << p0hMoveInjected
                    << " moveInjectFail=" << p0hMoveInjectFail << "\n";
          p0hMoveRecv = 0;
          p0hMoveInjected = 0;
          p0hMoveInjectFail = 0;
          p0hLastEmitUs = p0NowUs;
        }
      }
      const uint64_t inputHandledUs = qpc_now_us();
      const bool inputAckSent = send_input_ack(input.seq);
      if (input.kind != 1 || inputHandledUs - inputReceivedUs >= 100000ULL) {
        std::cout << "[native-video-host][input-timing] seq=" << input.seq
                  << " eventKind=" << input.kind << " hostReceivedUs=" << inputReceivedUs
                  << " hostHandledUs=" << inputHandledUs << " hostAckDoneUs=" << qpc_now_us()
                  << " injectionEnabled=" << (inputRouter.injectionEnabled ? 1 : 0)
                  << " ackSent=" << (inputAckSent ? 1 : 0) << " osInjectionConfirmed=0\n";
      }
      if (!inputAckSent) break;
      continue;
    }

    if (type == MessageType::ControlInputText && header.size == sizeof(ControlInputTextMessage)) {
      ControlInputTextMessage text{};
      text.header = header;
      if (!link.Read(&text.seq, sizeof(text) - sizeof(MessageHeader))) break;
      const uint64_t textReceivedUs = qpc_now_us();
      std::string resolvedTarget;
      // lockscreen-cred r1 (Codex A6): no text to the own session while it is at its lock screen.
      const bool lockBlockedText = !args.preloginMode && remote60::native_poc::own_session_treat_as_locked();
      if (lockBlockedText) note_lock_refusal("text");
      if (inputRouter.injectionEnabled && !lockBlockedText) {
        const bool desktopMode =
            !inputRouter.targetCriteria.enabled() &&
            (capture.selectedWindowId.load(std::memory_order_acquire) == 0);
        InputInjectResult injectResult = InputInjectResult::Failed;
        if (desktopMode && link_directory_authenticated() && !interactive_desktop_is_default() &&
            secure_dispatch_text(capture, inputRouter, text,
                                 inputRouter.domainW.load(std::memory_order_acquire),
                                 inputRouter.domainH.load(std::memory_order_acquire)).sent) {
          injectResult = InputInjectResult::Injected;
          resolvedTarget = " secure-system-agent";
        } else {
          injectResult = apply_input_text_message(text, capture.targetHwnd, desktopMode,
                                                  &inputRouter.desktopState, &resolvedTarget);
        }
        if (injectResult == InputInjectResult::Injected) {
          const uint64_t n = inputRouter.events.fetch_add(1) + 1;
          if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
            std::cout << "[native-video-host][input-text] injected seq=" << text.seq
                      << " utf16Count=" << text.utf16Count
                      << " mode=" << (desktopMode ? "desktop" : "window")
                      << resolvedTarget
                      << "\n";
          }
        } else if (injectResult == InputInjectResult::NoTarget) {
          const uint64_t n = inputRouter.noTarget.fetch_add(1, std::memory_order_relaxed) + 1;
          if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
            std::cout << "[native-video-host][input-text] no-target seq=" << text.seq
                      << " utf16Count=" << text.utf16Count
                      << resolvedTarget
                      << "\n";
          }
        } else if (injectResult == InputInjectResult::Unsupported) {
          const uint64_t n = inputRouter.unsupported.fetch_add(1, std::memory_order_relaxed) + 1;
          if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
            std::cout << "[native-video-host][input-text] unsupported seq=" << text.seq
                      << " utf16Count=" << text.utf16Count
                      << "\n";
          }
        } else {
          const uint64_t n = inputRouter.injectFail.fetch_add(1, std::memory_order_relaxed) + 1;
          if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
            std::cout << "[native-video-host][input-text] inject-fail seq=" << text.seq
                      << " utf16Count=" << text.utf16Count
                      << resolvedTarget
                      << "\n";
          }
        }
      }
      const uint64_t textHandledUs = qpc_now_us();
      const bool textAckSent = send_input_ack(text.seq);
      std::cout << "[native-video-host][input-timing] seq=" << text.seq
                << " textEvent=1 hostReceivedUs=" << textReceivedUs
                << " hostHandledUs=" << textHandledUs << " hostAckDoneUs=" << qpc_now_us()
                << " injectionEnabled=" << (inputRouter.injectionEnabled ? 1 : 0)
                << " ackSent=" << (textAckSent ? 1 : 0) << " osInjectionConfirmed=0\n";
      if (!textAckSent) break;
      continue;
    }

    if (type == MessageType::ControlWindowListRequest &&
        header.size == sizeof(ControlWindowListRequestMessage)) {
      ControlWindowListRequestMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      if (!send_window_list(req.seq)) break;
      continue;
    }

    if (type == MessageType::ControlMonitorListRequest &&
        header.size == sizeof(ControlMonitorListRequestMessage)) {
      ControlMonitorListRequestMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      if (!send_monitor_list(req.seq)) break;
      continue;
    }

    if (type == MessageType::ControlMonitorSelect &&
        header.size == sizeof(ControlMonitorSelectMessage)) {
      ControlMonitorSelectMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      // Applied by the render loop, which owns the capture item; answered with the list so the
      // client sees the selection that actually took effect rather than the one it asked for.
#ifndef REMOTE60_PINNED_SELECT_TEST_SEAM
      mailbox.PostSelectMonitor({servedEpoch, req.monitorId});
#endif  // r2 K6: the pinned test host takes no screen
      if (!send_monitor_list(req.seq)) break;
      continue;
    }

    if (type == MessageType::ControlWindowThumbnailRequest &&
        header.size == sizeof(ControlWindowThumbnailRequestMessage)) {
      ControlWindowThumbnailRequestMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      if (!send_window_thumbnail(req)) break;
      continue;
    }

    if (type == MessageType::ControlWindowSelect &&
        header.size == sizeof(ControlWindowSelectMessage)) {
      ControlWindowSelectMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;

      ControlWindowSelectedMessage rsp{};
      rsp.header.magic = remote60::native_poc::kMagic;
      rsp.header.type = static_cast<uint16_t>(MessageType::ControlWindowSelected);
      rsp.header.size = static_cast<uint16_t>(sizeof(rsp));
      rsp.seq = req.seq;
      rsp.windowId = req.windowId;
      rsp.streamGeneration = capture.streamGenerationState.load(std::memory_order_acquire);
      rsp.hostSendQpcUs = qpc_now_us();

      // t-970r4zgo: a monitor-select target is classified before anything treats the id as an
      // HWND. Malformed ids in its namespace are refused here, and a monitor id is resolved to
      // the screen THIS connection was listed under that id -- not one listed or re-indexed since.
      const SelectRequestResolution resolved = resolve_select_request(req.windowId, monitorListSent);
      const std::wstring& monitorDevice = resolved.device;
      const char* refused = resolved.refused;

#ifdef REMOTE60_PINNED_SELECT_TEST_SEAM
      // r2 K6: only the pinned window; it then goes through the product's transaction below.
      const bool seamRefuses = req.windowId == 0 || req.windowId != pinned_window_id(args);
#else
      constexpr bool seamRefuses = false;
#endif
      if (seamRefuses) {
        rsp.flags |= 0x2u;
        remote60::native_poc::utf8_copy_bounded(rsp.reason, sizeof(rsp.reason), "pinned_test_only");
        std::cout << "[native-video-host][control] window-select seq=" << req.seq << " requestedId=" << req.windowId
                  << " applied=0 reason=pinned_test_seam_refused\n";
      } else if (capture.windowSelectionLocked.load(std::memory_order_acquire)) {
        rsp.flags |= 0x2u;
        remote60::native_poc::utf8_copy_bounded(rsp.reason, sizeof(rsp.reason),
                                                "selection_locked_by_config");
        if (req.windowId == 0) {
          remote60::native_poc::utf8_copy_bounded(rsp.title, sizeof(rsp.title), "desktop");
        }
      } else if (refused) {
        remote60::native_poc::utf8_copy_bounded(rsp.reason, sizeof(rsp.reason), refused);
        std::cout << "[native-video-host][control] window-select seq=" << req.seq << " requestedId=" << req.windowId
                  << " applied=0 reason=" << refused << "\n";
      } else {
        uint64_t myTxn = 0;
        {
          std::lock_guard<std::mutex> lk(windowSelectionTxn.mu);
          myTxn = ++windowSelectionTxn.nextTxnId;
          windowSelectionTxn.txnId = myTxn;
          windowSelectionTxn.ownerEpoch = servedEpoch;
          windowSelectionTxn.requestedMonitorDevice = monitorDevice;
          windowSelectionTxn.pending = true;
          windowSelectionTxn.completed = false;
          windowSelectionTxn.reqSeq = req.seq;
          windowSelectionTxn.requestedWindowId = req.windowId;
          windowSelectionTxn.responseFlags = 0;
          windowSelectionTxn.responseWindowId = req.windowId;
          windowSelectionTxn.responseStreamGeneration = 0;
          windowSelectionTxn.responseReason.clear();
          windowSelectionTxn.responseTitle.clear();
        }
        windowSelectionTxn.cv.notify_all();
        std::cout << "[native-video-host][control] window-select seq=" << req.seq << " requestedId=" << req.windowId
                  << " pending\n";
#ifdef REMOTE60_PINNED_SELECT_TEST_SEAM
        if (const unsigned drops = pinned_select_seam_faults().dropKeys; drops > 0) {
          pinned_select_seam_arm_key_drops(drops);
          std::cout << "[pinned-select-test] fault: the next " << drops << " key frame(s) are dropped\n";
        }
#endif

        std::unique_lock<std::mutex> lk(windowSelectionTxn.mu);
        // Also give up when the link dies. The client that asked for this selection may be the
        // one that just went away, and the next client cannot be served until this returns --
        // so waiting only for completion would hold the whole session handover behind a reply
        // nobody is left to read. Polled, because a rollover closes the channel rather than
        // touching this transaction.
        const auto mine = [&] { return windowSelectionTxn.completed && windowSelectionTxn.completedTxnId == myTxn; };
        while (!stop.load() && !mine() && link.Alive()) {
          windowSelectionTxn.cv.wait_for(lk, std::chrono::milliseconds(100));
        }
        if (mine()) {
          rsp.flags = windowSelectionTxn.responseFlags;
          rsp.windowId = windowSelectionTxn.responseWindowId;
          rsp.streamGeneration = windowSelectionTxn.responseStreamGeneration;
          remote60::native_poc::utf8_copy_bounded(rsp.reason, sizeof(rsp.reason),
                                                  windowSelectionTxn.responseReason);
          remote60::native_poc::fill_window_title(rsp.title, windowSelectionTxn.responseTitle);
        } else {
          // Not this request's result (stopped, the link died, or another request took over):
          // nothing is claimed, and the shared fields are left to whoever they belong to.
          // r3 M5: and a request of ours the main loop has not taken yet is withdrawn, so it is
          // never applied to whichever session comes next.
          if (windowSelectionTxn.pending && windowSelectionTxn.txnId == myTxn) {
            windowSelectionTxn.pending = false;
            std::cout << "[native-video-host][control] window-select seq=" << req.seq
                      << " withdrawn (asker gone before it was applied)\n";
          }
          rsp.flags = 0;
          rsp.windowId = req.windowId;
          rsp.streamGeneration = 0;
          remote60::native_poc::utf8_copy_bounded(rsp.reason, sizeof(rsp.reason), "not_completed");
        }
        rsp.hostSendQpcUs = qpc_now_us();
      }
#ifdef REMOTE60_PINNED_SELECT_TEST_SEAM
      if (const unsigned delayMs = pinned_select_seam_faults().ackDelayMs; delayMs > 0 && !seamRefuses) {
        std::cout << "[pinned-select-test] fault: window-select answer seq=" << req.seq << " held " << delayMs
                  << " ms (its key frame goes first)\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        rsp.hostSendQpcUs = qpc_now_us();
      }
#endif

      if (!link.Write(&rsp, sizeof(rsp))) break;
      continue;
    }

    // C0 stage 1: the viewer's bandwidth observation. LOGGED, and read by nothing else -- no ABR,
    // encoder or pacing input is touched here (the regression compares those logs with this on
    // and off). No reply: the viewer does not wait for one.
    if (type == MessageType::ControlClientBandwidth &&
        header.size == sizeof(ControlClientBandwidthMessage)) {
      ControlClientBandwidthMessage bw{};
      bw.header = header;
      if (!link.Read(&bw.seq, sizeof(bw) - sizeof(MessageHeader))) break;
      const uint64_t recvUs = qpc_now_us();  // freshness is judged on THIS clock only
      if (!clientSession.bandwidthObserveNegotiated.load(std::memory_order_acquire)) {
        if (bwUnnegotiated++ == 0) {
          std::cout << "[bwe-observe] ignored: this client did not ask for it in its Hello\n";
        }
        continue;
      }
      if (!client_bandwidth_message_valid(bw)) {
        if (bwInvalid++ < 5) {
          std::cout << "[bwe-observe] rejected seq=" << bw.seq << " (out of range)\n";
        }
        continue;
      }
      const uint64_t sinceLastUs = bwLastRecvUs ? recvUs - bwLastRecvUs : 0;
      bwLastRecvUs = recvUs;
      // Bounded: one line a second whatever the viewer sends.
      if (bwLastLogUs != 0 && recvUs - bwLastLogUs < 900000) {
        ++bwSuppressed;
        continue;
      }
      bwLastLogUs = recvUs;
      std::cout << "[bwe-observe] seq=" << bw.seq << " bwe=" << bw.bweBps
                << " state=" << to_string(static_cast<BweUsage>(bw.usage))
                << " rate=" << to_string(static_cast<BweRateState>(bw.rateState))
                << " goodputUnique=" << bw.goodputUniqueBps << " wireLoad=" << bw.wireLoadBps
                << " gradUs=" << bw.delayGradientUs << " thrUs=" << bw.thresholdUs
                << " lossPm=" << bw.lossPm
                << " appLimited=" << ((bw.flags & kBandwidthFlagAppLimited) ? 1 : 0)
                << " static=" << ((bw.flags & kBandwidthFlagStaticHold) ? 1 : 0)
                << " samples=" << bw.delaySamples << " gen=" << bw.streamGeneration
                << " curGen=" << capture.streamGenerationState.load(std::memory_order_acquire)
                << " sinceLastMs=" << (sinceLastUs / 1000) << " suppressed=" << bwSuppressed
                << "\n";
      continue;
    }

    if (type == MessageType::ControlClientMetrics &&
        header.size == sizeof(ControlClientMetricsMessage)) {
      ControlClientMetricsMessage metrics{};
      metrics.header = header;
      if (!link.Read(&metrics.seq, sizeof(metrics) - sizeof(MessageHeader))) break;
      // Published as one record: these describe a single instant on the viewer and the ABR/M9
      // decision reads them together. Storing them field by field let the main loop act on a mix
      // of two reports. (Phase 4: ClientMetricsSnapshot.)
      ViewerMetrics reported;
      reported.width = metrics.width;
      reported.height = metrics.height;
      reported.recvFpsX100 = metrics.recvFpsX100;
      reported.decodedFpsX100 = metrics.decodedFpsX100;
      reported.recvMbpsX1000 = metrics.recvMbpsX1000;
      reported.skippedFrames = metrics.skippedFrames;
      reported.avgLatencyUs = metrics.avgLatencyUs;
      reported.maxLatencyUs = metrics.maxLatencyUs;
      reported.avgDecodeTailUs = metrics.avgDecodeTailUs;
      reported.maxDecodeTailUs = metrics.maxDecodeTailUs;
      reported.congestionState = metrics.congestionState;
      reported.congestionTransitions = metrics.congestionTransitions;
      reported.congestionRecoveryCount = metrics.congestionRecoveryCount;
      reported.congestionRecoveryReq = metrics.congestionRecoveryReq;
      reported.congestionRecoveryMaxUs = metrics.congestionRecoveryMaxUs;
      reported.queueDepthMax = metrics.queueDepthMax;
      reported.queueDepthH4p = metrics.queueDepthH4p;
      reported.udpAssemblyDropPm = metrics.udpAssemblyDropPm;
      reported.updatedUs = qpc_now_us();
      clientMetrics.Publish(reported);
      // Logged as it arrives rather than folded into the per-second stat line: this is the
      // only view the host gets of what the remote display is actually doing, and a viewer
      // reporting stutter needs it visible without attaching to the device.
      if (metrics.presentSampleCount > 0) {
        std::cout << "[native-video-host][client-present]"
                  << " fps=" << (metrics.presentFpsX100 / 100.0)
                  << " targetUs=" << metrics.presentTargetIntervalUs
                  << " gapP50Us=" << metrics.presentGapP50Us
                  << " gapP95Us=" << metrics.presentGapP95Us
                  << " gapMaxUs=" << metrics.presentGapMaxUs
                  << " over1_5x=" << metrics.presentOver1_5xCount
                  << " over2x=" << metrics.presentOver2xCount
                  << " samples=" << metrics.presentSampleCount
                  << " sched=" << metrics.presentScheduledCount
                  << " immediate=" << metrics.presentImmediateCount
                  << " reanchor=" << metrics.presentReanchorCount
                  << " displayed=" << metrics.presentDisplayedCount
                  << "\n";
      }
      continue;
    }

    if (type == MessageType::ControlRequestKeyFrame &&
        header.size == sizeof(ControlRequestKeyFrameMessage)) {
      ControlRequestKeyFrameMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      const uint64_t nowUs = qpc_now_us();
      // Refill + minimum-interval check + consume, as one transaction under encoder.keyReqMu.
      // These three fields are plain (a double and two uint64s) and the main loop resets them on
      // reconnect, so doing this open-coded here was a data race on the throttle that decides how
      // often the stream is forced to an IDR. (Ledger H-04.)
      double keyReqTokensNow = 0.0;
      bool refusedByTest = false;
#ifdef REMOTE60_STREAM_TEST_SEAM
      // TEST BUILD ONLY: while the hold file "refuse_keys" exists, every request takes the
      // limiter's refusal branch below (the e2e of a refused IDR request, t-970r4zgo r4).
      refusedByTest = stream_test_hold_set("refuse_keys");
      if (refusedByTest) {
        std::cout << "[stream-test-seam] keyframe-request refused seq=" << req.seq << "\n";
      }
#endif
      if (!refusedByTest && encoder.TryTakeKeyRequestToken(nowUs, &keyReqTokensNow)) {
        mailbox.PostRequestKeyframe(kKeyframeReasonViewer, req.reason);
        const uint64_t reqCount = clientMetrics.keyFrameRequestCount.fetch_add(1) + 1;
        std::cout << "[native-video-host][control] keyframe-request seq=" << req.seq
                  << " reason=" << req.reason
                  << " total=" << reqCount
                  << "\n";
      } else {
        const uint64_t dropCount = clientMetrics.keyFrameRequestDropped.fetch_add(1) + 1;
        if ((dropCount % 60) == 1) {
          std::cout << "[native-video-host][control] keyframe-request-throttled seq=" << req.seq
                    << " reason=" << req.reason
                    << " dropped=" << dropCount
                    << " tokens=" << keyReqTokensNow
                    << "\n";
        }
      }
      continue;
    }

    if (type == MessageType::ControlRuntimeEncoderConfig &&
        header.size == sizeof(ControlRuntimeEncoderConfigMessage)) {
      ControlRuntimeEncoderConfigMessage tune{};
      tune.header = header;
      if (!link.Read(&tune.seq, sizeof(tune) - sizeof(MessageHeader))) break;
      const bool hasBitrate = ((tune.flags & 0x1u) != 0) && tune.bitrate >= 100000;
      const bool hasKeyint = ((tune.flags & 0x2u) != 0) && tune.keyint >= 1;
      const bool hasFps = ((tune.flags & 0x4u) != 0) && tune.fps >= 1;
      if (hasBitrate || hasKeyint || hasFps) {
        TuneEncoderRequest tuneReq;
        tuneReq.epoch = servedEpoch;
        tuneReq.seq = tune.seq;
        if (hasBitrate) tuneReq.bitrate = tune.bitrate;
        if (hasKeyint) tuneReq.keyint = tune.keyint;
        if (hasFps) tuneReq.fps = tune.fps;
        mailbox.PostTuneEncoder(tuneReq);
        std::cout << "[native-video-host][control] runtime-config seq=" << tune.seq
                  << " bitrate=" << (hasBitrate ? tune.bitrate : 0)
                  << " keyint=" << (hasKeyint ? tune.keyint : 0)
                  << " fps=" << (hasFps ? tune.fps : 0)
                  << " flags=" << tune.flags
                  << "\n";
      }
      continue;
    }

    if (type == MessageType::ControlDesktopBackendRequest &&
        header.size == sizeof(ControlDesktopBackendRequestMessage)) {
      ControlDesktopBackendRequestMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      if (req.backend == 1 || req.backend == 2 || req.backend == 3) {
        mailbox.PostBackendRequest({servedEpoch, req.seq, req.backend});
        std::cout << "[native-video-host][control] desktop-backend-request seq=" << req.seq
                  << " backend="
                  << (req.backend == 2 ? "wgc" : (req.backend == 3 ? "gdi" : "dxgi"))
                  << "\n";
      }
      continue;
    }

    if (type == MessageType::ControlStreamState &&
        header.size == sizeof(ControlStreamStateMessage)) {
      ControlStreamStateMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      const bool active = ((req.flags & 0x1u) != 0);
#ifdef REMOTE60_PINNED_SELECT_TEST_SEAM
      if (pinned_test_old_host()) {
        std::cout << "[native-video-host][control] stream-state seq=" << req.seq << " active=" << (active ? 1 : 0)
                  << " IGNORED (old-host test)\n";
        continue;
      }
#endif
      clientSession.streamControlActive.store(active, std::memory_order_release);
      std::cout << "[native-video-host][control] stream-state seq=" << req.seq
                << " active=" << (active ? 1 : 0)
                << "\n";
      continue;
    }

    if (type == MessageType::ControlCaptureModeRequest &&
        header.size == sizeof(ControlCaptureModeRequestMessage)) {
      ControlCaptureModeRequestMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      if (req.mode == 1 || req.mode == 2) {
        mailbox.PostCaptureMode({servedEpoch, req.seq, req.mode,
                                 std::min<uint32_t>(10000u, req.xPermille),
                                 std::min<uint32_t>(10000u, req.yPermille)});
        std::cout << "[native-video-host][control] capture-mode-request seq=" << req.seq
                  << " mode=" << req.mode
                  << " xPermille=" << req.xPermille
                  << " yPermille=" << req.yPermille
                  << "\n";
      }
      continue;
    }

    // Clipboard text sync (K1). Only reached on a host that advertised kCaptureFlagClipboardTextV1,
    // because that is the only thing that makes a viewer send these. The payload is variable length,
    // so the fixed part is read first and the utf16Count units after -- bounded, so a bad count
    // cannot allocate.
    if (type == MessageType::ControlClipboardUpdate &&
        header.size == sizeof(ControlClipboardUpdateMessage)) {
      ControlClipboardUpdateMessage upd{};
      upd.header = header;
      if (!link.Read(&upd.seq, sizeof(upd) - sizeof(MessageHeader))) break;
      const size_t payloadBytes = static_cast<size_t>(upd.utf16Count) * sizeof(uint16_t);
      if (upd.utf16Count > remote60::native_poc::kClipboardTextMaxUtf16) {
        // A legit viewer skips oversize itself; this is the defensive path. Drain and skip.
        if (payloadBytes > 0 && !link.Discard(payloadBytes)) break;
        std::cout << "[native-video-host][clipboard] update seq=" << upd.seq
                  << " skipped utf16Count=" << upd.utf16Count << " (over cap)\n";
        send_input_ack(upd.seq);
        continue;
      }
      std::vector<uint8_t> payload(payloadBytes);
      if (payloadBytes > 0 && !link.Read(payload.data(), payloadBytes)) break;
      std::u16string text;
      if (remote60::native_poc::clipboard_parse_payload(payload.data(), payload.size(),
                                                        upd.utf16Count, &text) &&
          clipboard) {
        clipboard->ApplyRemote(text, upd.contentHash);
      }
      std::cout << "[native-video-host][clipboard] update applied seq=" << upd.seq
                << " utf16Count=" << upd.utf16Count << "\n";
      send_input_ack(upd.seq);
      continue;
    }

    // Paste on demand (t-y4wj64jw): the viewer is holding a paste key until this answer says the text
    // is on this clipboard. So the answer is the write's outcome, decided here and only here -- never
    // a receipt. Only reached on a host that advertised kCaptureFlagPasteOnDemandV1.
    if (type == MessageType::ControlPasteTextApply &&
        header.size == sizeof(remote60::native_poc::ControlPasteTextApplyMessage)) {
      remote60::native_poc::ControlPasteTextApplyMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      const uint64_t startUs = qpc_now_us();
      remote60::native_poc::PasteApplyResult result = remote60::native_poc::paste_text_apply_precheck(req);
      remote60::native_poc::PasteApplyStage stage = remote60::native_poc::PasteApplyStage::None;
      uint32_t win32 = 0, clipSeq = 0, userGen = 0;
      const size_t payloadBytes = static_cast<size_t>(req.utf16Count) * sizeof(uint16_t);
      if (result != remote60::native_poc::PasteApplyResult::Applied) {
        if (payloadBytes > 0 && !link.Discard(payloadBytes)) break;
      } else {
        std::vector<uint8_t> payload(payloadBytes);
        if (!link.Read(payload.data(), payloadBytes)) break;
        std::u16string text;
        if (!remote60::native_poc::clipboard_parse_payload(payload.data(), payload.size(), req.utf16Count, &text) ||
            remote60::native_poc::clipboard_fnv1a(text) != req.contentHash) {
          result = remote60::native_poc::PasteApplyResult::BadRequest;
        } else if (!clipboard || !clipboard->enabled()) {
          result = remote60::native_poc::PasteApplyResult::Disabled;
        } else {
          // Bounded: the viewer's own deadline is 3 s for text, and this control thread serves
          // nothing else while it waits (a file Offer already waits up to 5 s in the same place).
          const HostClipboardHub::PasteOutcome o = clipboard->ApplyPaste(text, req.contentHash, 2000);
          result = o.ok ? remote60::native_poc::PasteApplyResult::Applied : remote60::native_poc::PasteApplyResult::Failed;
          stage = static_cast<remote60::native_poc::PasteApplyStage>(o.stage);
          win32 = o.win32;
          clipSeq = o.clipSeq;
          userGen = static_cast<uint32_t>(o.userCopyGen);
        }
      }
      const auto reply = remote60::native_poc::make_paste_applied(req.seq, req.pasteRequestId, result, stage, win32,
                                                                  clipSeq, userGen);
      // Contents are never logged; the request id, the size and the outcome are what a field log needs.
      std::cout << "[native-video-host][clipboard] paste-apply id=" << std::hex << req.pasteRequestId << std::dec
                << " utf16Count=" << req.utf16Count << " result=" << static_cast<int>(result)
                << " stage=" << static_cast<int>(stage) << " win32=" << win32 << " ms=" << (qpc_now_us() - startUs) / 1000
                << " hostUserCopyGen=" << userGen << "\n";
      if (!link.Write(&reply, sizeof(reply))) break;
      continue;
    }

    if (type == MessageType::ControlClipboardRequest &&
        header.size == sizeof(ControlClipboardRequestMessage)) {
      ControlClipboardRequestMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      HostClipboardHub::Snapshot snap;
      if (clipboard) snap = clipboard->Get();
      const bool hasData = snap.generation > req.knownGeneration && !snap.text.empty();
      std::vector<uint8_t> reply = remote60::native_poc::build_clipboard_data(
          req.seq, snap.generation, hasData, snap.text, snap.hash, qpc_now_us());
      // Paste on demand r4: the copy generation rides every reply (bit1 + reserved).
      if (clipboard) {
        remote60::native_poc::clipboard_data_set_copy_gen(&reply, static_cast<uint32_t>(clipboard->CopyGen()));
      }
      if (!link.Write(reply.data(), reply.size())) break;
      continue;
    }

    // Clipboard image v1 (direction A): fixed-size requests, one answer each. Only reached on a host
    // that advertised kCaptureFlagClipboardImageV1. The image itself never rides this channel.
    if (type == MessageType::ControlClipImageOffer && header.size == sizeof(ControlClipImageOfferMessage)) {
      ControlClipImageOfferMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      ControlClipImageOfferReplyMessage rsp{};
      rsp.header.type = static_cast<uint16_t>(MessageType::ControlClipImageOfferReply);
      rsp.header.size = sizeof(rsp);
      rsp.seq = req.seq;
      rsp.transferId = req.transferId;
      rsp.verdict = static_cast<uint8_t>(ClipImageVerdict::Disabled);
      if (clientSession.clipImage) rsp = clientSession.clipImage->HandleOffer(req, servedEpoch);
      if (!link.Write(&rsp, sizeof(rsp))) break;
      continue;
    }
    if (type == MessageType::ControlClipImageCancel && header.size == sizeof(ControlClipImageCancelMessage)) {
      ControlClipImageCancelMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      ControlClipImageStatusReplyMessage rsp{};
      rsp.header.type = static_cast<uint16_t>(MessageType::ControlClipImageStatusReply);
      rsp.header.size = sizeof(rsp);
      rsp.seq = req.seq;
      rsp.transferId = req.transferId;
      if (clientSession.clipImage) rsp = clientSession.clipImage->HandleCancel(req);
      rsp.header.type = static_cast<uint16_t>(MessageType::ControlClipImageCancelReply);
      if (!link.Write(&rsp, sizeof(rsp))) break;
      continue;
    }
    if (type == MessageType::ControlClipImageStatus && header.size == sizeof(ControlClipImageStatusMessage)) {
      ControlClipImageStatusMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      ControlClipImageStatusReplyMessage rsp{};
      rsp.header.type = static_cast<uint16_t>(MessageType::ControlClipImageStatusReply);
      rsp.header.size = sizeof(rsp);
      rsp.seq = req.seq;
      rsp.transferId = req.transferId;
      if (clientSession.clipImage) rsp = clientSession.clipImage->HandleStatus(req);
      if (!link.Write(&rsp, sizeof(rsp))) break;
      continue;
    }

    // File copy (t-zdmsd4gb, file_copy_wire.hpp): a fixed header, then a body of at most
    // kMaxControlPayload -- bounded before anything is allocated. One answer each.
    if (remote60::native_poc::file_copy::net::is_file_control(header.type) &&
        header.size == sizeof(remote60::native_poc::file_copy::net::FileControlHeader)) {
      remote60::native_poc::file_copy::net::FileControlHeader fh{};
      fh.header = header;
      if (!link.Read(&fh.seq, sizeof(fh) - sizeof(MessageHeader))) break;
      if (fh.payloadBytes > remote60::native_poc::file_copy::net::kMaxControlPayload) break;
      std::vector<uint8_t> body(fh.payloadBytes);
      if (fh.payloadBytes > 0 && !link.Read(body.data(), body.size())) break;
      uint16_t replyType = 0;
      std::vector<uint8_t> replyBody, out;
      if (!clientSession.fileCopy ||
          !clientSession.fileCopy->HandleControl(header.type, body, servedEpoch, &replyType, &replyBody) ||
          !remote60::native_poc::file_copy::net::frame_control(
              static_cast<remote60::native_poc::file_copy::net::FileMsg>(replyType), fh.seq, replyBody, &out)) {
        break;  // a malformed request: the link is out of step, as for any other
      }
      if (!link.Write(out.data(), out.size())) break;
      continue;
    }

    if (bodySize > 0 && !link.Discard(bodySize)) break;
  }
  release_all_physical_host();  // release any physical keys still held on the host at session end
  release_injected_input();     // ...and the virtual keys and mouse buttons (RV-01)
  // Only if we are still the current session. A TCP control thread and the UDP dispatcher can be
  // alive at the same time, and this unconditional store meant whichever finished last switched
  // the OTHER one's video off -- the viewer would sit on a frozen picture with a healthy link.
  // (Ledger H-28.)
  // ...and not when this session is being REPAIRED rather than ending (item 8, C3 r2). A
  // resume wakes this loop out of its read so the control channel can be re-keyed on the same
  // session; turning the stream off on the way out would blink the picture and, worse, take
  // away the one thing that tells the viewer the session is worth repairing at all. The
  // dispatcher re-enters Serve() immediately afterwards.
  if (clientSession.epoch.load(std::memory_order_acquire) == servedEpoch &&
      !clientSession.controlResumePending.load(std::memory_order_acquire)) {
    clientSession.streamControlActive.store(false, std::memory_order_release);
  }
}

}  // namespace remote60::native_poc
