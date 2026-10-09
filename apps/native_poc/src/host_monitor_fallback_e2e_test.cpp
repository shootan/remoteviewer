// t-970r4zgo, process level. Two field defects on one path, a real host and the shared client
// session the APK runs:
//  (1) a picked screen is unplugged while no viewer is watching, and the viewer that comes back
//      must get a picture of the screen that is left -- not a window list and a host retrying the
//      missing screen every 5 s forever;
//  (2) picking a screen must be ANSWERED once it is applied, with the stream generation the APK's
//      selection gate waits for -- the APK never got one, so every screen pick timed out.
//
// The host is the TEST BUILD GNLinkStreamMigrationTest.exe: the product's sources, with one more
// seam here -- enumerate_monitors() reads GNLINK_STREAM_TEST_MONITORS_FILE ("extra" = the real
// screens plus a second one under its own device name, captured through the primary's handle;
// "none" = nothing enumerated; anything else = the real screens). The PC's displays are not
// touched. The viewer side is ClientSessionController with a sink that runs the APK decoder's own
// gate (selection_ack_gate.hpp) -- no MediaCodec, so "ready" here is the first admitted IDR of the
// answered generation, not a decoded picture on a phone.
// What the build cannot show: a real hot-unplug's WGC item close / DXGI ACCESS_LOST while
// streaming (that path reaches the same restart function this drives through the idle reattach),
// and the added screen shares the primary's pixels and rect.
//
// Mixed versions (each its own run):
//   --old-host <GNLinkStream.exe>     this client against a host that predates (2): no answerable
//                                     screen pick is attempted, the session and desktop mode work
//   --old-client <udp_control_e2e>    a client that predates (2) against this host: its legacy
//                                     monitor-select exchange still works (the APK gate it feeds is
//                                     NOT fixed by this host -- that needs the new APK)
//
// Loopback, staged in the repository's test scratch, isolated from the user's files, input
// injection OFF. Without REMOTE60_ALLOW_HOST_E2E=1 it skips (exit 77). Tags: display-capture, gpu.

#include "e2e_isolation.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "monitor_select_target.hpp"
#include "native_video_client_session.hpp"
#include "selection_ack_gate.hpp"
#include "udp_impair_proxy.hpp"

using namespace remote60::native_poc;

namespace {

int gFailures = 0;
int gChecks = 0;

void check(const char* name, bool cond, const std::string& detail = {}) {
  ++gChecks;
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
  if (!cond) ++gFailures;
}
void check(const std::string& name, bool cond, const std::string& detail = {}) { check(name.c_str(), cond, detail); }

// The APK decoder sink without its codec: the same SelectionAckGate decides what is admitted, the
// selection is "ready" at the first admitted IDR of the answered generation.
class CountingSink : public ClientEncodedFrameSink {
 public:
  void OnEncodedH264Frame(UdpH264AssembledFrame&& frame) override {
    std::lock_guard<std::mutex> lk(mu_);
    if (!gate_.Admit(frame.header.streamGeneration)) {
      ++dropped_;
      if ((frame.header.flags & 0x1u) != 0 && heldAckGen_ != 0 && frame.header.streamGeneration == heldAckGen_) {
        droppedHeldKey_ = true;  // the answered generation's IDR, here before its (held) answer
      }
      return;
    }
    ++frames_;
    lastGen_ = frame.header.streamGeneration;
    if ((frame.header.flags & 0x1u) != 0 && gate_.pending() != 0 && ready_ != gate_.pending()) {
      ready_ = gate_.pending();
      readyGen_ = frame.header.streamGeneration;
    }
  }
  void OnVideoStreamReset() override {}
  bool ConsumeDecoderKeyframeRequest() override {
    std::lock_guard<std::mutex> lk(mu_);
    if (consumeOnce_) {  // a decoder-discard request, on demand (fills the session's limiter)
      consumeOnce_ = false;
      consumedOnce_ = true;
      return true;
    }
    const bool r = rekey_;
    rekey_ = false;
    return r;
  }
  uint64_t CurrentSelectionTag() override {
    std::lock_guard<std::mutex> lk(mu_);
    return gate_.pending();
  }
  void OnWindowSelectionControlResultFor(const ControlWindowSelectedMessage& msg, uint64_t tag) override {
    if (onAnswer) onAnswer(msg, tag);
    std::unique_lock<std::mutex> lk(mu_);
    if (holdAck_) {
      // The answer is held back while video keeps arriving: its generation's IDR gets here first.
      heldAckGen_ = msg.streamGeneration;
      ackHeld_ = true;
      cv_.notify_all();
      cv_.wait_for(lk, std::chrono::seconds(15), [&] { return !holdAck_; });
      ackHeld_ = false;
    }
    const auto a = gate_.OnAck((msg.flags & 0x1u) != 0, msg.streamGeneration, tag);
    if (a.result == SelectionAckGate::AckResult::Ignored) {
      ++ignored_;
    } else if (a.result == SelectionAckGate::AckResult::Applied) {
      ++applied_;
      ackGen_ = msg.streamGeneration;
      if (a.requestKeyframe) {
        rekey_ = true;
        ++rekeys_;
      }
    } else {
      ++failed_;
      failReason_.assign(msg.reason, strnlen(msg.reason, sizeof(msg.reason)));
    }
  }
  void Prepare(uint64_t selection) {
    std::lock_guard<std::mutex> lk(mu_);
    gate_.Prepare(selection);
  }
  // Test hooks: see the answer as it arrives (before the gate), hold it, demand one IDR.
  std::function<void(const ControlWindowSelectedMessage&, uint64_t)> onAnswer;
  void ArmHoldAck() {
    std::lock_guard<std::mutex> lk(mu_);
    holdAck_ = true;
    heldAckGen_ = 0;
    droppedHeldKey_ = false;
  }
  void ReleaseAck() {
    std::lock_guard<std::mutex> lk(mu_);
    holdAck_ = false;
    cv_.notify_all();
  }
  void ArmConsumeOnce() {
    std::lock_guard<std::mutex> lk(mu_);
    consumeOnce_ = true;
    consumedOnce_ = false;
  }
  // What the APK does on select_failed (nativeAbortVideoSwitch): the gate is disarmed.
  void Abort() {
    std::lock_guard<std::mutex> lk(mu_);
    gate_.Reset();
  }
#define GETTER(type, name, field) \
  type name() const {             \
    std::lock_guard<std::mutex> lk(mu_); \
    return field;                 \
  }
  GETTER(uint64_t, frames, frames_)
  GETTER(uint64_t, lastGen, lastGen_)
  GETTER(uint64_t, dropped, dropped_)
  GETTER(uint64_t, applied, applied_)
  GETTER(uint64_t, failed, failed_)
  GETTER(uint64_t, ignored, ignored_)
  GETTER(uint64_t, rekeys, rekeys_)
  GETTER(uint64_t, ready, ready_)
  GETTER(uint64_t, readyGen, readyGen_)
  GETTER(uint64_t, ackGen, ackGen_)
  GETTER(uint64_t, expected, gate_.expected())
  GETTER(std::string, failReason, failReason_)
  GETTER(bool, ackHeld, ackHeld_)
  GETTER(uint64_t, heldAckGen, heldAckGen_)
  GETTER(bool, droppedHeldKey, droppedHeldKey_)
  GETTER(bool, consumedOnce, consumedOnce_)
#undef GETTER

 private:
  mutable std::mutex mu_;
  SelectionAckGate gate_;
  uint64_t frames_ = 0, dropped_ = 0, applied_ = 0, failed_ = 0, ignored_ = 0, rekeys_ = 0;
  uint64_t ready_ = 0, readyGen_ = 0, ackGen_ = 0, lastGen_ = 0;
  bool rekey_ = false;
  std::string failReason_;
  std::condition_variable cv_;
  bool holdAck_ = false, ackHeld_ = false, droppedHeldKey_ = false, consumeOnce_ = false, consumedOnce_ = false;
  uint64_t heldAckGen_ = 0;
};

template <typename Fn>
bool wait_until(Fn&& fn, int timeoutMs) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline) {
    if (fn()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return fn();
}

std::string read_all(const std::wstring& path) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return {};
  std::string out;
  char buf[65536];
  DWORD got = 0;
  while (ReadFile(f, buf, sizeof(buf), &got, nullptr) && got > 0) out.append(buf, got);
  CloseHandle(f);
  return out;
}

size_t count_of(const std::string& text, const std::string& needle, size_t from = 0) {
  size_t n = 0;
  for (size_t p = text.find(needle, from); p != std::string::npos; p = text.find(needle, p + needle.size())) ++n;
  return n;
}

bool write_mode(const std::wstring& path, const char* mode) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  DWORD put = 0;
  const bool ok = WriteFile(f, mode, static_cast<DWORD>(std::strlen(mode)), &put, nullptr) != 0;
  CloseHandle(f);
  return ok;
}

int gPort = 0;

// The screens this PC really has (the seam adds one to them, or takes all away).
size_t real_screen_count() {
  size_t n = 0;
  EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT, LPARAM p) -> BOOL {
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(m, &mi) && mi.rcMonitor.right > mi.rcMonitor.left && mi.rcMonitor.bottom > mi.rcMonitor.top)
      ++*reinterpret_cast<size_t*>(p);
    return TRUE;
  }, reinterpret_cast<LPARAM>(&n));
  return n;
}

std::wstring my_dir() {
  wchar_t self[MAX_PATH]{};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  std::wstring d(self);
  return d.substr(0, d.find_last_of(L"\\/") + 1);
}

struct SelfHost {
  std::wstring dir;
  HANDLE job = nullptr;
  PROCESS_INFORMATION pi{};
  HANDLE log = INVALID_HANDLE_VALUE;
  bool launched = false;
  remote60::native_poc::e2e::StagingDir staging;

  std::wstring logPath() const { return dir + L"host.log"; }
  std::wstring monitorsPath() const { return dir + L"monitors.txt"; }
  std::wstring failRestartPath() const { return dir + L"fail_restart"; }
  std::wstring holdPath(const wchar_t* point) const { return dir + point; }  // stream_test_hold.hpp

  // hostExe empty: the test build next to this test (with the monitor seam).
  bool Start(const std::wstring& hostExe, std::string* why) {
    using namespace remote60::native_poc::e2e;
    if (!staging.Create(L"monfb")) {
      *why = staging.why();
      return false;
    }
    dir = staging.path();
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    const std::wstring source = hostExe.empty() ? my_dir() + L"GNLinkStreamMigrationTest.exe" : hostExe;
    const std::wstring staged = dir + (hostExe.empty() ? L"GNLinkStreamMigrationTest.exe" : L"GNLinkStream.exe");
    if (!CopyFileW(source.c_str(), staged.c_str(), FALSE) || !CopyFileW(self, (dir + L"GNLinkCapture.exe").c_str(), FALSE)) {
      *why = "could not stage the host";
      return false;
    }
    if (!write_mode(monitorsPath(), "extra")) {
      *why = "could not write the monitors file";
      return false;
    }
    job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_FRAME_GATING_DISABLE", L"1");
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_MONITORS_FILE", monitorsPath().c_str());
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_FAIL_RESTART_FILE", failRestartPath().c_str());
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_HOLD_DIR", dir.substr(0, dir.size() - 1).c_str());
    std::wstring cmd = L"\"" + staged + L"\" --transport udp --codec h264" + L" --bind-address 127.0.0.1 --bind-port " +
                       std::to_wstring(gPort) + L" --seconds 400 --input-injection-mode none";
    if (cmd.find(L" --input-injection-mode none") == std::wstring::npos) {
      *why = "refusing: the command line does not turn input injection off";
      return false;
    }
    const std::wstring isoAppData = dir + L"localappdata";
    CreateDirectoryW(isoAppData.c_str(), nullptr);
    std::vector<wchar_t> env = e2e_isolated_environment(isoAppData);
    std::string isoWhy;
    if (!e2e_command_is_isolated(cmd, dir, &isoWhy) || !e2e_path_is_under(e2e_block_localappdata(env), dir)) {
      *why = "refusing: not isolated from the user's files -- " + isoWhy;
      return false;
    }
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    log = CreateFileW(logPath().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                      FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (log != INVALID_HANDLE_VALUE) {
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = log;
      si.hStdError = log;
    }
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    launched = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, log != INVALID_HANDLE_VALUE,
                              CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, env.data(),
                              dir.c_str(), &si, &pi) != 0;
    if (!launched) {
      *why = "CreateProcessW failed " + std::to_string(GetLastError());
      return false;
    }
    AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    Sleep(1500);
    return true;
  }

  bool Stop() {
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    log = INVALID_HANDLE_VALUE;
    if (job) CloseHandle(job);
    job = nullptr;
    bool exited = true;
    if (pi.hProcess) {
      exited = WaitForSingleObject(pi.hProcess, 20000) == WAIT_OBJECT_0;
      CloseHandle(pi.hProcess);
    }
    if (pi.hThread) CloseHandle(pi.hThread);
    pi = PROCESS_INFORMATION{};
    if (!exited) {
      staging.set_keep(true, "the host did not exit within 20 s; its staging directory is kept");
      return false;
    }
    return staging.Remove();
  }
};

ClientSessionConnectArgs connect_args(CountingSink* sink, int port = 0) {
  ClientSessionConnectArgs args;
  args.host = "127.0.0.1";
  args.videoPort = port ? port : gPort;
  args.controlPort = port ? port : gPort;
  args.requireUdpHello = true;
  args.requireTcpControl = false;
  args.controlOverUdp = true;
  args.controlIntervalMs = 200;
  args.encodedFrameSink = sink;
  return args;
}

std::string panel_said(const WindowPanelSnapshot& p) {
  return "count=" + std::to_string(p.monitors.size()) + " selectedId=" + std::to_string(p.selectedMonitorId) +
         " status=" + p.status;
}

// A viewer arriving: connects and waits for the host's window list (which says what it supports).
bool arrive(ClientSessionController& c, CountingSink* sink, const char* label, int port = 0) {
  const bool ok = c.Connect(connect_args(sink, port)) &&
                  wait_until([&] { return c.Snapshot().state == ClientSessionState::Connected; }, 8000) &&
                  wait_until([&] { return c.Snapshot().latestWindowListCount > 0; }, 10000) &&
                  wait_until([&] { return c.WindowPanelSnapshotCopy().hostSupportsMonitors; }, 6000);
  check(label, ok, c.Snapshot().status + " / " + c.Snapshot().lastError);
  return ok;
}

// The viewer leaves, and the host detaches its capture once it has been idle (5 s).
bool leave_and_detach(ClientSessionController& c, const std::wstring& log, const char* label) {
  const size_t before = count_of(read_all(log), "capture detached (idle)");
  c.Disconnect();
  const bool ok = wait_until([&] { return count_of(read_all(log), "capture detached (idle)") > before; }, 25000);
  check(label, ok);
  return ok;
}

// The selectedId of the host's last monitor-list reply in its log, or -1.
long last_logged_selected_id(const std::string& text) {
  const size_t p = text.rfind("monitor-list seq=");
  if (p == std::string::npos) return -1;
  const size_t s = text.find("selectedId=", p);
  if (s == std::string::npos) return -1;
  return std::strtol(text.c_str() + s + 11, nullptr, 10);
}

// A NEW reply, not the panel's previous one: the host logs every reply it sends, so this waits
// for one more such line and for the panel to hold what that reply said.
bool fresh_monitor_list(ClientSessionController& c, const std::wstring& log, size_t wantCount) {
  const size_t before = count_of(read_all(log), "monitor-list seq=");
  if (!c.RequestMonitorList()) return false;
  return wait_until([&] {
    const std::string text = read_all(log);
    const auto p = c.WindowPanelSnapshotCopy();
    return count_of(text, "monitor-list seq=") > before && p.monitors.size() == wantCount &&
           static_cast<long>(p.selectedMonitorId) == last_logged_selected_id(text);
  }, 6000);
}

// The last rect the host handed the secure-input agent, as logged ("(x,y)/WxH"), or "".
std::string last_rect(const std::string& text) {
  const std::string key = "secure-input target rect=";
  const size_t p = text.rfind(key);
  if (p == std::string::npos) return {};
  const size_t e = text.find(' ', p + key.size());
  return text.substr(p + key.size(), e == std::string::npos ? std::string::npos : e - p - key.size());
}

// Keyframe requests the host received: taken ("keyframe-request seq=") or refused by its own
// limiter ("keyframe-request-throttled seq=") -- either way the client's request reached it.
size_t key_requests_received(const std::string& text) {
  return count_of(text, "[control] keyframe-request seq=") + count_of(text, "[control] keyframe-request-throttled seq=");
}

uint64_t gSelection = 0;

struct PickResult {
  bool requested = false;
  bool answered = false;
  bool applied = false;
  bool ready = false;
  uint64_t selection = 0;
  uint64_t ackGen = 0;
  std::string detail;
};

// The APK's order: the stream is on, the gate is armed for a new local selection, then the pick.
PickResult pick_screen(ClientSessionController& c, CountingSink& sink, uint32_t monitorId, bool expectReady = true) {
  PickResult r;
  const uint64_t applied0 = sink.applied(), failed0 = sink.failed();
  r.selection = ++gSelection;
  sink.Prepare(r.selection);
  r.requested = c.RequestMonitorSelect(monitorId);
  if (!r.requested) {
    r.detail = "refused locally: " + c.WindowPanelSnapshotCopy().status;
    return r;
  }
  r.answered = wait_until([&] { return sink.applied() > applied0 || sink.failed() > failed0; }, 15000);
  r.applied = sink.applied() > applied0;
  r.ackGen = sink.ackGen();
  if (r.applied && expectReady) r.ready = wait_until([&] { return sink.ready() == r.selection; }, 15000);
  r.detail = std::string("answered=") + (r.answered ? "1" : "0") + " applied=" + (r.applied ? "1" : "0") +
             " ackGen=" + std::to_string(r.ackGen) + " ready=" + (r.ready ? "1" : "0") +
             " readyGen=" + std::to_string(sink.readyGen()) + " fail=" + sink.failReason() +
             " ignored=" + std::to_string(sink.ignored()) + " rekeys=" + std::to_string(sink.rekeys());
  return r;
}

int finish(SelfHost& self, const std::wstring& log) {
  const std::string full = read_all(log);
  std::printf("\n(host log %zu bytes: %zu fallback, %zu reattach failed, %zu reattached)\n", full.size(),
              count_of(full, "monitor-fallback"), count_of(full, "capture reattach failed"),
              count_of(full, "capture reattached"));
  if (gFailures) {
    self.staging.set_keep(true, "a check failed; the host log is kept");
    std::printf("      kept: %ls\n", log.c_str());
  }
  self.Stop();
  if (gFailures == 0) {
    std::printf("\nRESULT: ALL PASS  (%d checks, 0 failed)\n", gChecks);
    return 0;
  }
  std::printf("\nRESULT: %d FAILED  (%d checks)\n", gFailures, gChecks);
  return 1;
}

// --old-host: this client against a host built before (2).
int run_old_host(SelfHost& self) {
  const std::wstring log = self.logPath();
  std::puts("\n--- mixed: this client, a host that predates the answerable screen pick ---");
  CountingSink sink;
  ClientSessionController c;
  if (arrive(c, &sink, "the client connects to the old host, which lists monitors (0x4)")) {
    const auto p = c.WindowPanelSnapshotCopy();
    check("the old host does not advertise the transaction (0x8)", !p.hostSupportsMonitorSelect);
    const auto t0 = std::chrono::steady_clock::now();
    const auto r = pick_screen(c, sink, 0);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    check("a screen pick is refused AT ONCE with 'host update required' -- no 6 s timeout",
          !r.requested && ms < 1000 &&
              c.WindowPanelSnapshotCopy().status.find("monitor_select_unsupported: host_update_required") != std::string::npos,
          r.detail + " ms=" + std::to_string(ms));
    check("no target id of the new namespace went to the old host",
          read_all(log).find("requestedId=8070450532247928") == std::string::npos);
    check("the session is still connected", c.Snapshot().state == ClientSessionState::Connected);
    const bool desktop =
        c.RequestDesktopMode() &&
        wait_until([&] { return c.WindowPanelSnapshotCopy().status.find("window_selected") != std::string::npos; }, 8000);
    check("desktop mode still round-trips on the old host", desktop, c.WindowPanelSnapshotCopy().status);
  }
  c.Disconnect();
  return finish(self, log);
}

// --old-client: a client built before (2) against this host (it brings its own legacy select).
int run_old_client(SelfHost& self, const std::wstring& oldClient) {
  const std::wstring log = self.logPath();
  std::puts("\n--- mixed: a client that predates the answerable screen pick, this host ---");
  const std::wstring outPath = self.dir + L"old_client.log";
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE out = CreateFileW(outPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  std::wstring cmd = L"\"" + oldClient + L"\" 127.0.0.1 " + std::to_wstring(gPort);
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = out;
  si.hStdError = out;
  PROCESS_INFORMATION pi{};
  const bool started = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                      self.dir.c_str(), &si, &pi) != 0;
  DWORD code = 999;
  if (started) {
    if (WaitForSingleObject(pi.hProcess, 180000) != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 998);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
  CloseHandle(out);
  const std::string text = read_all(outPath);
  check("the old client ran against this host and exited 0", started && code == 0, "exit=" + std::to_string(code));
  check("its legacy monitor select was accepted and the session survived it",
        text.find("PASS  monitor select request accepted") != std::string::npos &&
            text.find("PASS  session survives monitor select") != std::string::npos);
  check("the host answered it the legacy way (monitor-select applied, then the list)",
        read_all(log).find("monitor-select applied id=0") != std::string::npos);
  std::printf("      (an old APK on this host: the same legacy exchange, so its selection gate is still never\n"
              "       answered -- the screen pick needs the new APK; not claimed fixed)\n");
  return finish(self, log);
}

}  // namespace

int main(int argc, char** argv) {
  std::wstring oldHost, oldClient;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--thumbnail") {  // staged as GNLinkCapture.exe; answers nothing
      Sleep(120000);
      return 0;
    }
    if ((a == "--old-host" || a == "--old-client") && i + 1 < argc) {
      const std::string v = argv[++i];
      (a == "--old-host" ? oldHost : oldClient) = std::wstring(v.begin(), v.end());
    }
  }
  wchar_t allow[8]{};
  if (GetEnvironmentVariableW(L"REMOTE60_ALLOW_HOST_E2E", allow, 8) == 0 || allow[0] != L'1') {
    std::puts("SKIP  host_monitor_fallback_e2e_test starts its own listening test-build host.");
    std::puts("      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED");
    return remote60::native_poc::e2e::kE2eSkippedExit;
  }
  gPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
  if (gPort == 0) {
    std::puts("FAIL  no free UDP port\n\nRESULT: 1 FAILED");
    return 1;
  }
  SelfHost self;
  std::string why;
  check("an isolated host is started (loopback, no input injection)", self.Start(oldHost, &why), why);
  if (!self.launched) {
    std::printf("\nRESULT: %d FAILED\n", gFailures);
    return 1;
  }
  if (!oldHost.empty()) return run_old_host(self);
  if (!oldClient.empty()) return run_old_client(self, oldClient);

  const std::wstring log = self.logPath();
  const size_t real = real_screen_count();
  const uint32_t picked = static_cast<uint32_t>(real);  // the seam's screen is listed last
  std::printf("this PC has %zu screen(s); the test build adds one more, listed as id=%u\n", real, picked);

  {
    std::puts("\n--- 1. the screen pick is answered once applied: A -> B -> A, each with its own generation ---");
    CountingSink sink;
    ClientSessionController c;
    if (arrive(c, &sink, "viewer 1 connects; the host advertises monitors and the transaction")) {
      check("0x8 seen on this connection", c.WindowPanelSnapshotCopy().hostSupportsMonitorSelect);
      check("the host lists the real screens plus the added one", fresh_monitor_list(c, log, real + 1),
            panel_said(c.WindowPanelSnapshotCopy()));
      c.RequestStreamActive(true);
      check("video flows before any pick", wait_until([&] { return sink.frames() >= 1; }, 20000));
      const auto b = pick_screen(c, sink, picked);
      check("pick the added screen (B): answered ok, then the first IDR of THAT generation -> ready",
            b.applied && b.ready && sink.readyGen() == b.ackGen && b.ackGen != 0, b.detail);
      check("the host logged it applied as a monitor select, answered with the target id it was asked",
            read_all(log).find("requestedId=" + std::to_string(encode_monitor_select_target(picked)) + " applied=1") !=
                std::string::npos);
      check("the panel: selectedMonitorId = B, status window_selected",
            c.WindowPanelSnapshotCopy().selectedMonitorId == picked &&
                c.WindowPanelSnapshotCopy().status.rfind("window_selected", 0) == 0,
            panel_said(c.WindowPanelSnapshotCopy()));
      fresh_monitor_list(c, log, real + 1);
      check("the host's own list agrees (selectedId = B)", c.WindowPanelSnapshotCopy().selectedMonitorId == picked,
            panel_said(c.WindowPanelSnapshotCopy()));
      const auto a = pick_screen(c, sink, 0);
      check("back to the primary (A): answered with a NEWER generation, ready on it",
            a.applied && a.ready && a.ackGen > b.ackGen && sink.readyGen() == a.ackGen, a.detail);
      const auto b2 = pick_screen(c, sink, picked);
      check("and B again", b2.applied && b2.ready && b2.ackGen > a.ackGen, b2.detail);
      const uint64_t admittedBefore = sink.frames();
      check("frames keep flowing on B's generation", wait_until([&] { return sink.frames() > admittedBefore; }, 10000));

      std::puts("\n--- 2. a pick that cannot be applied is answered as such, and nothing moves ---");
      const uint64_t genBefore = sink.expected();
      const auto none = pick_screen(c, sink, 5);
      check("screen 5 (never listed): a failure answer (monitor_not_listed), gate not opened, no ready",
            none.answered && !none.applied && sink.failReason() == "monitor_not_listed" && sink.ready() != none.selection,
            none.detail);
      fresh_monitor_list(c, log, real + 1);
      check("the host's selection did not move (still B)", c.WindowPanelSnapshotCopy().selectedMonitorId == picked,
            panel_said(c.WindowPanelSnapshotCopy()));
      std::puts("      (the added screen goes between the list and the pick)");
      write_mode(self.monitorsPath(), "real");
      const auto gone = pick_screen(c, sink, picked);
      check("picking it now: a failure answer (monitor_gone) -- the id is not re-read as another screen",
            gone.answered && !gone.applied && sink.failReason() == "monitor_gone", gone.detail);
      check("the host logged the refusal, not an applied select",
            read_all(log).find("reason=monitor_gone") != std::string::npos);
      write_mode(self.monitorsPath(), "extra");
      fresh_monitor_list(c, log, real + 1);
      check("rolled back: still B, the rect still known",
            c.WindowPanelSnapshotCopy().selectedMonitorId == picked &&
                last_rect(read_all(log)).find("/0x0") == std::string::npos,
            panel_said(c.WindowPanelSnapshotCopy()) + " rect=" + last_rect(read_all(log)));
      (void)genBefore;

      std::puts("      (every capture restart fails while the pick is applied -- the test build's seam)");
      const uint64_t genB = sink.ackGen();
      write_mode(self.failRestartPath(), "1");
      const auto broken = pick_screen(c, sink, 0);
      check("picking A while the restart fails: a failure answer (capture_restart_failed), no ready",
            broken.answered && !broken.applied && sink.failReason() == "capture_restart_failed" &&
                sink.ready() != broken.selection,
            broken.detail);
      sink.Abort();  // the APK's select_failed path
      DeleteFileW(self.failRestartPath().c_str());
      fresh_monitor_list(c, log, real + 1);
      check("rolled back: the host still has B selected", c.WindowPanelSnapshotCopy().selectedMonitorId == picked,
            panel_said(c.WindowPanelSnapshotCopy()));
      const uint64_t f0 = sink.frames();
      check("once restarts work again the capture comes back on B's own generation (" + std::to_string(genB) +
                "), not the failed pick's",
            wait_until([&] { return sink.frames() > f0 + 2; }, 20000) && sink.lastGen() == genB,
            "lastGen=" + std::to_string(sink.lastGen()));
      check("the rect is known again", last_rect(read_all(log)).find("/0x0") == std::string::npos,
            "rect=" + last_rect(read_all(log)));

      std::puts("\n--- 3. a late answer to an earlier pick does not open a later pick's gate ---");
      const uint64_t ignored0 = sink.ignored();
      const uint64_t applied0 = sink.applied();
      const uint64_t selA = ++gSelection;
      sink.Prepare(selA);
      const std::string marker0 = "requestedId=" + std::to_string(encode_monitor_select_target(0)) + " applied=1";
      const size_t mark = read_all(log).size();
      check("pick A queued under selection " + std::to_string(selA), c.RequestMonitorSelect(0));
      const uint64_t selB = ++gSelection;
      sink.Prepare(selB);  // the user picks again before A is answered
      const bool aApplied = wait_until([&] { return count_of(read_all(log), marker0, mark) >= 1; }, 15000);
      check("the host applied A", aApplied);
      check("A's answer reached the sink and was IGNORED (it is not selection " + std::to_string(selB) + "'s)",
            wait_until([&] { return sink.ignored() > ignored0; }, 8000) && sink.applied() == applied0,
            "ignored=" + std::to_string(sink.ignored()) + " applied=" + std::to_string(sink.applied()));
      check("no ready for B from A's frames", sink.ready() != selB);
      check("pick B queued under selection " + std::to_string(selB), c.RequestMonitorSelect(picked));
      check("B's own answer opens B's gate, ready on B's generation",
            wait_until([&] { return sink.applied() > applied0 && sink.ready() == selB; }, 15000),
            "ackGen=" + std::to_string(sink.ackGen()) + " readyGen=" + std::to_string(sink.readyGen()));
    }
    leave_and_detach(c, log, "viewer 1 leaves; the host detaches the idle capture");
  }

  {
    std::puts("\n--- 4. transient: nothing enumerated (a reconfiguration in progress) -- no fallback, the retry stays ---");
    write_mode(self.monitorsPath(), "none");
    const size_t mark = read_all(log).size();
    CountingSink sink;
    ClientSessionController c;
    if (arrive(c, &sink, "viewer 2 connects")) {
      c.RequestStreamActive(true);
      const bool retried = wait_until([&] { return count_of(read_all(log), "capture reattach failed", mark) >= 2; }, 15000);
      const std::string tail = read_all(log).substr(mark);
      check("the reattach retries (the existing backoff) while absence is not established", retried,
            "retries=" + std::to_string(count_of(tail, "capture reattach failed")));
      check("and does NOT fall back", count_of(tail, "monitor-fallback") == 0);
      fresh_monitor_list(c, log, 0);
      check("the monitor list keeps the picked id while nothing is listed",
            c.WindowPanelSnapshotCopy().selectedMonitorId == picked, panel_said(c.WindowPanelSnapshotCopy()));
      std::puts("      (the second screen comes back)");
      write_mode(self.monitorsPath(), "extra");
      const bool reattached = wait_until([&] { return count_of(read_all(log), "capture reattached", mark) >= 1; }, 15000);
      check("the screen comes back: the retry reattaches to the SAME screen", reattached);
      fresh_monitor_list(c, log, real + 1);
      check("and the list still names the added screen, no fallback taken",
            c.WindowPanelSnapshotCopy().selectedMonitorId == picked &&
                count_of(read_all(log).substr(mark), "monitor-fallback") == 0,
            panel_said(c.WindowPanelSnapshotCopy()));
    }
    leave_and_detach(c, log, "viewer 2 leaves; the host detaches the idle capture");
  }

  {
    std::puts("\n--- 5. the picked screen is unplugged while nobody watches; a viewer comes back ---");
    write_mode(self.monitorsPath(), "real");
    const size_t mark = read_all(log).size();
    CountingSink sink;
    ClientSessionController c;
    if (arrive(c, &sink, "viewer 3 connects")) {
      fresh_monitor_list(c, log, real);
      check("the monitor list says the real screens, selectedId=0 (the one that will be shown)",
            c.WindowPanelSnapshotCopy().monitors.size() == real && c.WindowPanelSnapshotCopy().selectedMonitorId == 0,
            panel_said(c.WindowPanelSnapshotCopy()));
      c.RequestStreamActive(true);
      const bool frames = wait_until([&] { return sink.frames() >= 1; }, 20000);
      check("the viewer gets a PICTURE (the primary)", frames, "frames=" + std::to_string(sink.frames()));
      std::this_thread::sleep_for(std::chrono::seconds(8));  // longer than the retry ceiling (5 s)
      const std::string tail = read_all(log).substr(mark);
      check("the host logged the fallback once", count_of(tail, "monitor-fallback selected device gone; opening id=0") == 1,
            "fallbacks=" + std::to_string(count_of(tail, "monitor-fallback")));
      check("no endless retry: not one 'capture reattach failed' after the unplug",
            count_of(tail, "capture reattach failed") == 0,
            "retries=" + std::to_string(count_of(tail, "capture reattach failed")));
      const size_t fb = tail.find("monitor-fallback");
      const std::string afterFb = fb == std::string::npos ? std::string() : tail.substr(fb);
      // The added screen shares the primary's handle (the seam), so the rect does not change here
      // and is not re-logged; what matters is that it stays KNOWN. Phase 6 shows it set again.
      const std::string rect = last_rect(read_all(log));
      check("the secure-input target rect stays known for the fallback screen",
            !rect.empty() && rect.find("/0x0") == std::string::npos, "rect=" + rect);
      check("and 'monitor query failed' does not follow the fallback",
            !afterFb.empty() && afterFb.find("monitor query failed") == std::string::npos);
      fresh_monitor_list(c, log, real);
      check("monitor-list after the fallback: selectedId=0", c.WindowPanelSnapshotCopy().selectedMonitorId == 0,
            panel_said(c.WindowPanelSnapshotCopy()));
      std::puts("      (the old screen is plugged back in)");
      write_mode(self.monitorsPath(), "extra");
      fresh_monitor_list(c, log, real + 1);
      check("not reverted: the added screen listed again, still selectedId=0",
            c.WindowPanelSnapshotCopy().monitors.size() == real + 1 && c.WindowPanelSnapshotCopy().selectedMonitorId == 0,
            panel_said(c.WindowPanelSnapshotCopy()));
      const auto again = pick_screen(c, sink, picked);
      check("the user picks it again (answered, ready) -- for phase 6", again.applied && again.ready, again.detail);
    }
    leave_and_detach(c, log, "viewer 3 leaves; the host detaches the idle capture");
  }

  {
    std::puts("\n--- 6. an unplug seen in two steps: nothing enumerated, then the picked screen missing ---");
    write_mode(self.monitorsPath(), "none");
    const size_t mark = read_all(log).size();
    CountingSink sink;
    ClientSessionController c;
    if (arrive(c, &sink, "viewer 4 connects")) {
      c.RequestStreamActive(true);
      const bool unknown = wait_until([&] {
        const std::string t = read_all(log).substr(mark);
        return count_of(t, "capture reattach failed") >= 1 && last_rect(t).find("/0x0") != std::string::npos;
      }, 15000);
      check("while nothing is enumerated: retrying, and the secure-input rect is unknown (0x0)", unknown,
            "rect=" + last_rect(read_all(log).substr(mark)));
      write_mode(self.monitorsPath(), "real");
      const bool fell = wait_until([&] { return count_of(read_all(log), "monitor-fallback", mark) >= 1; }, 15000);
      check("then the screens are listed without the picked one: the next retry falls back", fell);
      const bool frames = wait_until([&] { return sink.frames() >= 1; }, 20000);
      check("the viewer gets a picture", frames, "frames=" + std::to_string(sink.frames()));
      std::this_thread::sleep_for(std::chrono::seconds(8));
      const std::string tail = read_all(log).substr(mark);
      const std::string afterFb = tail.substr(std::min(tail.size(), tail.find("monitor-fallback")));
      const size_t rectSet = afterFb.find("secure-input target rect=(");
      const std::string rectLine =
          rectSet == std::string::npos ? std::string() : afterFb.substr(rectSet, afterFb.find('\n', rectSet) - rectSet);
      check("the secure-input rect is SET AGAIN for the fallback screen (reason=capture-restart, not 0x0)",
            rectLine.find("reason=capture-restart") != std::string::npos && rectLine.find("/0x0") == std::string::npos,
            rectLine);
      check("no 'monitor query failed' and no reattach failure after the fallback",
            afterFb.find("monitor query failed") == std::string::npos && count_of(afterFb, "capture reattach failed") == 0);
      fresh_monitor_list(c, log, real);
      check("monitor-list: selectedId=0", c.WindowPanelSnapshotCopy().selectedMonitorId == 0,
            panel_said(c.WindowPanelSnapshotCopy()));
    }
    c.Disconnect();
  }

  {
    std::puts("\n--- 7. the fallback under an ANSWERED selection: the gate, the list and the picture stay consistent ---");
    // The viewer stays connected with its gate on the picked screen's generation; the stream is
    // turned off, the screen goes, the stream comes back. No capture restart can run while the
    // stream is off, so the list reply alone must already name the screen that will be shown.
    write_mode(self.monitorsPath(), "extra");
    CountingSink sink;
    ClientSessionController c;
    if (arrive(c, &sink, "viewer 5 connects")) {
      fresh_monitor_list(c, log, real + 1);
      c.RequestStreamActive(true);
      const auto b = pick_screen(c, sink, picked);
      check("the added screen picked: answered, ready", b.applied && b.ready, b.detail);
      const size_t detachedBefore = count_of(read_all(log), "capture detached (idle)");
      c.RequestStreamActive(false);
      check("the stream is turned off and the capture detaches while the viewer stays",
            wait_until([&] { return count_of(read_all(log), "capture detached (idle)") > detachedBefore; }, 25000));
      const size_t mark = read_all(log).size();
      write_mode(self.monitorsPath(), "real");
      fresh_monitor_list(c, log, real);
      const std::string tail = read_all(log).substr(mark);
      check("no capture restart ran (no fallback logged yet)", count_of(tail, "monitor-fallback") == 0);
      check("the reply already names the screen that will be shown: selectedId=0, not the stale id",
            c.WindowPanelSnapshotCopy().selectedMonitorId == 0, panel_said(c.WindowPanelSnapshotCopy()));
      const uint64_t admitted0 = sink.frames();
      c.RequestStreamActive(true);
      check("streaming again: the restart falls back",
            wait_until([&] { return count_of(read_all(log), "monitor-fallback", mark) >= 1; }, 20000));
      check("and the gate still admits the picture: the fallback keeps the answered generation (" +
                std::to_string(sink.expected()) + ")",
            wait_until([&] { return sink.frames() > admitted0; }, 20000),
            "admitted=" + std::to_string(sink.frames() - admitted0) + " dropped=" + std::to_string(sink.dropped()));
    }
    c.Disconnect();
  }

  {
    std::puts("\n--- 8. (r3 M2) a late answer to an earlier pick does not move the pick now in progress ---");
    // A is held at the host after it was taken; meanwhile the user picks B (B is queued). A's
    // answer -- success, then in a second round failure -- must not touch the panel token the APK
    // moves its selection on (SelectionStatusPolicy), and B then completes.
    write_mode(self.monitorsPath(), "extra");
    CountingSink sink;
    ClientSessionController c;
    if (arrive(c, &sink, "viewer 6 connects")) {
      fresh_monitor_list(c, log, real + 1);
      c.RequestStreamActive(true);
      check("video flows", wait_until([&] { return sink.frames() >= 1; }, 20000));
      for (int round = 0; round < 2; ++round) {
        const bool failA = round == 1;
        std::printf("      (round %d: A's late answer is a %s)\n", round + 1, failA ? "FAILURE" : "success");
        std::string statusAtAnswer, statusBefore;
        uint32_t seqAtAnswer = 0, seqBefore = 0;
        std::atomic<int> answers{0};
        sink.onAnswer = [&](const ControlWindowSelectedMessage&, uint64_t) {
          if (answers.fetch_add(1) == 0) {
            // The session has already decided whether to show it; this is what the APK would read.
            const auto p = c.WindowPanelSnapshotCopy();
            statusAtAnswer = p.status;
            seqAtAnswer = p.lastSelectSeq;
          }
        };
        const size_t holds0 = count_of(read_all(log), "holding at apply");
        write_mode(self.holdPath(L"apply"), "1");
        const uint64_t selA = ++gSelection;
        sink.Prepare(selA);
        check("round " + std::to_string(round + 1) + ": pick A queued", c.RequestMonitorSelect(round == 0 ? 0 : picked));
        check("A is taken and held at the host",
              wait_until([&] { return count_of(read_all(log), "holding at apply") > holds0; }, 10000));
        const uint64_t selB = ++gSelection;
        sink.Prepare(selB);
        check("the user picks B meanwhile (queued behind A)", c.RequestMonitorSelect(round == 0 ? picked : 0));
        statusBefore = c.WindowPanelSnapshotCopy().status;
        seqBefore = c.WindowPanelSnapshotCopy().lastSelectSeq;
        if (failA) write_mode(self.failRestartPath(), "1");
        write_mode(self.holdPath(L"take"), "1");  // B waits at the host until A is checked
        DeleteFileW(self.holdPath(L"apply").c_str());
        const uint64_t ignored0 = sink.ignored();
        check("A's answer arrives and the gate ignores it",
              wait_until([&] { return answers.load() >= 1 && sink.ignored() > ignored0; }, 15000),
              "answers=" + std::to_string(answers.load()));
        check("... and the panel token is still B's 'requested' (" + statusBefore + "), not A's result",
              statusAtAnswer == statusBefore && statusBefore == "monitor_select_requested" && seqAtAnswer == seqBefore,
              "atAnswer=" + statusAtAnswer + " seq " + std::to_string(seqBefore) + "->" + std::to_string(seqAtAnswer));
        if (failA) {
          DeleteFileW(self.failRestartPath().c_str());
          // A's failed restart leaves the host's restart retry in its backoff (0.5 s); B is let go
          // once the retry has reopened the capture, as a user's next pick would be.
          std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        }
        DeleteFileW(self.holdPath(L"take").c_str());
        check("B then completes: answered, ready on its generation",
              wait_until([&] { return sink.ready() == selB; }, 15000) &&
                  c.WindowPanelSnapshotCopy().status.rfind("window_selected", 0) == 0,
              c.WindowPanelSnapshotCopy().status + " ready=" + std::to_string(sink.ready()));
        sink.onAnswer = nullptr;
      }
    }
    c.Disconnect();
  }

  {
    std::puts("\n--- 9. (r3 M3) the picked screen goes DURING the restart: the previous target comes back ---");
    write_mode(self.monitorsPath(), "extra3");
    const uint32_t screenC = static_cast<uint32_t>(real);      // GNLINKTEST2
    const uint32_t screenB = static_cast<uint32_t>(real + 1);  // GNLINKTEST3
    CountingSink sink;
    ClientSessionController c;
    if (arrive(c, &sink, "viewer 7 connects")) {
      check("three listed screens (real + 2)", fresh_monitor_list(c, log, real + 2), panel_said(c.WindowPanelSnapshotCopy()));
      c.RequestStreamActive(true);
      for (int round = 0; round < 2; ++round) {
        const bool cGoneToo = round == 1;
        write_mode(self.monitorsPath(), "extra3");
        fresh_monitor_list(c, log, real + 2);
        const auto onC = pick_screen(c, sink, screenC);
        check(std::string("round ") + std::to_string(round + 1) + ": on C (GNLINKTEST2), answered and ready",
              onC.applied && onC.ready, onC.detail);
        const uint64_t genC = onC.ackGen;
        const size_t holds0 = count_of(read_all(log), "holding at restart");
        const size_t rolled0 = count_of(read_all(log), "monitor-select rolled back");
        write_mode(self.holdPath(L"restart"), "1");
        const uint64_t selB = ++gSelection;
        const uint64_t failed0 = sink.failed();
        sink.Prepare(selB);
        check("pick B (GNLINKTEST3) queued", c.RequestMonitorSelect(screenB));
        check("B passed the presence check and is held just before the restart",
              wait_until([&] { return count_of(read_all(log), "holding at restart") > holds0; }, 10000));
        write_mode(self.monitorsPath(), cGoneToo ? "real" : "extra");  // B goes (and C too, in round 2)
        DeleteFileW(self.holdPath(L"restart").c_str());
        check("the answer is a failure (monitor_gone), no ready for B",
              wait_until([&] { return sink.failed() > failed0; }, 15000) && sink.failReason() == "monitor_gone" &&
                  sink.ready() != selB,
              "fail=" + sink.failReason());
        check("the host rolled the pick back",
              wait_until([&] { return count_of(read_all(log), "monitor-select rolled back") > rolled0; }, 5000));
        sink.Abort();  // the APK's select_failed path
        fresh_monitor_list(c, log, cGoneToo ? real : real + 1);
        const uint32_t expectId = cGoneToo ? 0u : screenC;
        check(cGoneToo ? "C is gone too: the confirmed fallback, the primary (selectedId 0)"
                       : "C still attached: the host is back on C (selectedId = C), not the primary",
              c.WindowPanelSnapshotCopy().selectedMonitorId == expectId, panel_said(c.WindowPanelSnapshotCopy()));
        const uint64_t f0 = sink.frames();
        check("the picture continues on C's generation (" + std::to_string(genC) + "), not a new one",
              wait_until([&] { return sink.frames() > f0 + 2; }, 20000) && sink.lastGen() == genC,
              "lastGen=" + std::to_string(sink.lastGen()));
        check("the secure-input rect is known", last_rect(read_all(log)).find("/0x0") == std::string::npos,
              "rect=" + last_rect(read_all(log)));
      }
    }
    c.Disconnect();
  }

  {
    std::puts("\n--- 10. (r3 M4) the APK's own refresh brings a changed screen list, and an old list's index is refused ---");
    write_mode(self.monitorsPath(), "extra");
    CountingSink sink;
    ClientSessionController c;
    if (arrive(c, &sink, "viewer 8 connects")) {
      wait_until([&] { return c.WindowPanelSnapshotCopy().monitors.size() == real + 1; }, 6000);
      c.RequestStreamActive(true);
      const auto b = pick_screen(c, sink, picked);
      check("on the added screen", b.applied && b.ready, b.detail);
      const size_t detached0 = count_of(read_all(log), "capture detached (idle)");
      c.RequestStreamActive(false);
      wait_until([&] { return count_of(read_all(log), "capture detached (idle)") > detached0; }, 25000);
      write_mode(self.monitorsPath(), "real");
      const uint64_t rev0 = c.WindowPanelSnapshotCopy().monitorListRevision;
      check("RequestWindowList (what the APK's refresh calls) also refreshes the screens: real count, selectedId 0",
            c.RequestWindowList() && wait_until([&] {
              const auto p = c.WindowPanelSnapshotCopy();
              return p.monitors.size() == real && p.selectedMonitorId == 0;
            }, 25000),  // behind the window previews the refresh also asks for (this test's helper never answers: 1 s each)
            panel_said(c.WindowPanelSnapshotCopy()));
      check("... as a new list revision", c.WindowPanelSnapshotCopy().monitorListRevision != rev0);
      const uint64_t revReal = c.WindowPanelSnapshotCopy().monitorListRevision;
      write_mode(self.monitorsPath(), "extra");
      // No capture restart ran while it was gone (the stream is off), so nothing fell back: back in
      // the list, the picked screen is again the one that will be shown.
      check("and back: the added screen listed again by the same refresh, and it is again the one shown",
            c.RequestWindowList() && wait_until([&] {
              const auto p = c.WindowPanelSnapshotCopy();
              return p.monitors.size() == real + 1 && p.selectedMonitorId == picked;
            }, 25000),
            panel_said(c.WindowPanelSnapshotCopy()));
      check("a pick made from the replaced list (revision " + std::to_string(revReal) + ") is refused at once",
            !c.RequestMonitorSelect(picked, revReal) &&
                c.WindowPanelSnapshotCopy().status == "monitor_select_failed: monitor_list_changed",
            c.WindowPanelSnapshotCopy().status);
      // A pick queued against the current list, with a refresh queued just before it whose list
      // differs: the list goes out first, and the pick is then not sent.
      write_mode(self.monitorsPath(), "real");
      const size_t selects0 = count_of(read_all(log), "requestedId=" + std::to_string(encode_monitor_select_target(picked)));
      const uint64_t selX = ++gSelection;
      sink.Prepare(selX);
      const bool refreshQueued = c.RequestWindowList();
      const bool pickQueued = c.RequestMonitorSelect(picked);
      check("refresh and pick both queued", refreshQueued && pickQueued);
      check("the list changed under the queued pick: it is not sent, and says so (window_select_failed: monitor_list_changed)",
            wait_until([&] {
              return c.WindowPanelSnapshotCopy().status == "window_select_failed: monitor_list_changed";
            }, 25000) &&
                count_of(read_all(log), "requestedId=" + std::to_string(encode_monitor_select_target(picked))) == selects0,
            c.WindowPanelSnapshotCopy().status);
    }
    c.Disconnect();
  }

  {
    std::puts("\n--- 11. (r3 M5) a pick whose session is gone is not applied to the next session ---");
    write_mode(self.monitorsPath(), "extra");
    for (int round = 0; round < 2; ++round) {
      const bool afterTake = round == 1;
      const wchar_t* point = afterTake ? L"apply" : L"take";
      std::printf("      (round %d: held %s)\n", round + 1, afterTake ? "after the main loop took it" : "before it is taken");
      CountingSink sinkA;
      ClientSessionController a;
      uint32_t before = 0;
      if (arrive(a, &sinkA, "viewer A connects")) {
        fresh_monitor_list(a, log, real + 1);
        a.RequestStreamActive(true);
        const auto onPrimary = pick_screen(a, sinkA, 0);
        check("A starts on the primary", onPrimary.applied && onPrimary.ready, onPrimary.detail);
        before = a.WindowPanelSnapshotCopy().selectedMonitorId;
        const size_t holds0 = count_of(read_all(log), "holding at apply");
        write_mode(self.holdPath(point), "1");
        sinkA.Prepare(++gSelection);
        check("A picks the added screen", a.RequestMonitorSelect(picked));
        if (afterTake) {
          check("taken and held", wait_until([&] { return count_of(read_all(log), "holding at apply") > holds0; }, 10000));
        } else {
          std::this_thread::sleep_for(std::chrono::milliseconds(800));  // reached the host, left pending
        }
      }
      const size_t applied0 =
          count_of(read_all(log), "requestedId=" + std::to_string(encode_monitor_select_target(picked)) + " applied=1");
      a.Disconnect();
      CountingSink sinkB;
      ClientSessionController b;
      const bool bUp = arrive(b, &sinkB, "viewer B (a new session) connects");
      DeleteFileW(self.holdPath(point).c_str());
      std::this_thread::sleep_for(std::chrono::seconds(2));
      const std::string text = read_all(log);
      check("A's pick was NOT applied to B's session",
            count_of(text, "requestedId=" + std::to_string(encode_monitor_select_target(picked)) + " applied=1") == applied0);
      check(afterTake ? "the host said why (stale_session)" : "A's asker withdrew it (withdrawn)",
            text.find(afterTake ? "reason=stale_session" : "withdrawn (asker gone") != std::string::npos);
      if (bUp) {
        fresh_monitor_list(b, log, real + 1);
        check("B sees the screen A had, unchanged", b.WindowPanelSnapshotCopy().selectedMonitorId == before,
              panel_said(b.WindowPanelSnapshotCopy()));
        b.RequestStreamActive(true);
        const auto own = pick_screen(b, sinkB, picked);
        check("B's own pick completes", own.applied && own.ready, own.detail);
      }
      b.Disconnect();
    }
  }

  {
    std::puts("\n--- 12. (r3 M1) the answered generation's lone IDR came first and was dropped: a fresh IDR is asked for "
              "with no further video, even past the limiter ---");
    // Through a proxy that can cut the video while the tunnelled control keeps flowing. The answer
    // is held at the sink until the IDR of its generation has been dropped; one decoder-discard
    // request is made just before, so the answer's own request meets the limiter's minimum
    // interval; the video is then cut and the answer released. The host must still receive the
    // IDR request -- nothing else will produce one.
    write_mode(self.monitorsPath(), "extra");
    sockaddr_in hostAddr{};
    hostAddr.sin_family = AF_INET;
    hostAddr.sin_port = htons(static_cast<u_short>(gPort));
    InetPtonW(AF_INET, L"127.0.0.1", &hostAddr.sin_addr);
    remote60::native_poc::test::Proxy proxy;
    std::atomic<bool> cutVideo{false};
    proxy.dropIf = [&cutVideo](const uint8_t* b, int n, bool up) {
      if (up || n < 6 || !cutVideo.load()) return false;
      uint16_t kind = 0;
      std::memcpy(&kind, b + 4, sizeof(kind));
      return kind == static_cast<uint16_t>(UdpPacketKind::VideoChunk);
    };
    const bool proxyUp = proxy.Start(hostAddr, remote60::native_poc::test::Impair{});
    check("the proxy is up", proxyUp);
    CountingSink sink;
    ClientSessionController c;
    if (proxyUp && arrive(c, &sink, "viewer 9 connects through the proxy", ntohs(proxy.frontAddr.sin_port))) {
      wait_until([&] { return c.WindowPanelSnapshotCopy().monitors.size() == real + 1; }, 6000);
      c.RequestStreamActive(true);
      const auto a = pick_screen(c, sink, 0);
      check("on the primary", a.applied && a.ready, a.detail);
      sink.ArmHoldAck();
      const uint64_t selB = ++gSelection;
      sink.Prepare(selB);
      check("pick the added screen", c.RequestMonitorSelect(picked));
      check("its answer is held, and its generation's IDR arrives first and is dropped",
            wait_until([&] { return sink.ackHeld() && sink.droppedHeldKey(); }, 15000),
            "held=" + std::to_string(sink.ackHeld()) + " gen=" + std::to_string(sink.heldAckGen()));
      // Counted as the host RECEIVES them ("keyframe-request seq="): the host has a limiter of its own
      // and may then drop a second request this close to the first -- its first IDR is the one sent
      // after the answer.
      const size_t keyReq0 = key_requests_received(read_all(log));
      // The limiter must be idle for the fill to be TAKEN (not itself held back -- a held fill and a
      // held answer request would rightly coalesce into one): its interval is 120 ms, refill 300 ms.
      std::this_thread::sleep_for(std::chrono::milliseconds(700));
      sink.ArmConsumeOnce();
      const bool filled = wait_until([&] { return sink.consumedOnce(); }, 5000);
      const auto tFill = std::chrono::steady_clock::now();
      std::fprintf(stderr, "[e2e] phase 12: fill consumed=%d, cutting video\n", filled ? 1 : 0);
      cutVideo.store(true);
      std::this_thread::sleep_for(std::chrono::milliseconds(40));  // in-flight datagrams drain
      std::fprintf(stderr, "[e2e] phase 12: releasing the held answer\n");
      sink.ReleaseAck();
      const auto msAfterFill =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - tFill).count();
      check("a decoder-discard request was made just before the answer (limiter's 120 ms minimum interval)",
            filled && msAfterFill < 120, "answer released " + std::to_string(msAfterFill) + " ms after it");
      const uint64_t frames0 = sink.frames();
      const bool asked =
          wait_until([&] { return key_requests_received(read_all(log)) >= keyReq0 + 2; }, 5000);
      check("with the video cut, the host receives BOTH requests -- the answer's own, held past the client limiter",
            asked && sink.frames() == frames0,
            "requests=" + std::to_string(key_requests_received(read_all(log)) - keyReq0) +
                " framesWhileCut=" + std::to_string(sink.frames() - frames0));
      cutVideo.store(false);
      sink.ArmConsumeOnce();  // the IDR asked for was cut too; the decoder asks again, as the APK's would
      check("video back: the fresh IDR of B's generation makes B ready",
            wait_until([&] { return sink.ready() == selB; }, 15000) && sink.readyGen() == sink.heldAckGen(),
            "ready=" + std::to_string(sink.ready()) + " readyGen=" + std::to_string(sink.readyGen()));
    }
    c.Disconnect();
    if (proxyUp) proxy.Stop();
  }

  return finish(self, log);
}
