// t-970r4zgo, process level: a picked screen is unplugged while no viewer is watching, and the
// viewer that comes back must get a picture of the screen that is left -- not a window list and a
// host retrying the missing screen every 5 s forever (the 2026-10 tablet report).
//
// The host is the TEST BUILD GNLinkStreamMigrationTest.exe: the product's sources, with one more
// seam here -- enumerate_monitors() reads GNLINK_STREAM_TEST_MONITORS_FILE ("extra" = the real
// screens plus a second one under its own device name, captured through the primary's handle;
// "none" = nothing enumerated; anything else = the real screens). The PC's displays are not
// touched. What the build cannot show: a real hot-unplug's WGC item close / DXGI ACCESS_LOST while
// streaming (that path reaches the same restart function this drives through the idle reattach).
//
// Loopback, staged in the repository's test scratch, isolated from the user's files, input
// injection OFF. Without REMOTE60_ALLOW_HOST_E2E=1 it skips (exit 77). Tags: display-capture, gpu.

#include "e2e_isolation.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "native_video_client_session.hpp"

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

class CountingSink : public ClientEncodedFrameSink {
 public:
  void OnEncodedH264Frame(UdpH264AssembledFrame&& frame) override {
    (void)frame;
    frames_.fetch_add(1, std::memory_order_relaxed);
  }
  void OnVideoStreamReset() override {}
  bool ConsumeDecoderKeyframeRequest() override { return false; }
  uint64_t frames() const { return frames_.load(std::memory_order_relaxed); }

 private:
  std::atomic<uint64_t> frames_{0};
};

template <typename Fn>
bool wait_until(Fn&& fn, int timeoutMs) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline) {
    if (fn()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
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

struct SelfHost {
  std::wstring dir;
  HANDLE job = nullptr;
  PROCESS_INFORMATION pi{};
  HANDLE log = INVALID_HANDLE_VALUE;
  bool launched = false;
  remote60::native_poc::e2e::StagingDir staging;

  std::wstring logPath() const { return dir + L"host.log"; }
  std::wstring monitorsPath() const { return dir + L"monitors.txt"; }

  bool Start(std::string* why) {
    using namespace remote60::native_poc::e2e;
    if (!staging.Create(L"monfb")) {
      *why = staging.why();
      return false;
    }
    dir = staging.path();
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring myDir(self);
    myDir = myDir.substr(0, myDir.find_last_of(L"\\/") + 1);
    if (!CopyFileW((myDir + L"GNLinkStreamMigrationTest.exe").c_str(), (dir + L"GNLinkStreamMigrationTest.exe").c_str(),
                   FALSE) ||
        !CopyFileW(self, (dir + L"GNLinkCapture.exe").c_str(), FALSE)) {
      *why = "could not stage the test-build host";
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
    std::wstring cmd = L"\"" + dir + L"GNLinkStreamMigrationTest.exe\" --transport udp --codec h264" +
                       L" --bind-address 127.0.0.1 --bind-port " + std::to_wstring(gPort) +
                       L" --seconds 300 --input-injection-mode none";
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

ClientSessionConnectArgs connect_args(CountingSink* sink) {
  ClientSessionConnectArgs args;
  args.host = "127.0.0.1";
  args.videoPort = gPort;
  args.controlPort = gPort;
  args.requireUdpHello = true;
  args.requireTcpControl = false;
  args.controlOverUdp = true;
  args.controlIntervalMs = 200;
  args.encodedFrameSink = sink;
  return args;
}

std::string panel_said(const WindowPanelSnapshot& p) {
  return "count=" + std::to_string(p.monitors.size()) + " selectedId=" + std::to_string(p.selectedMonitorId);
}

// A viewer arriving: connects, waits for the host's monitor list, asks for the stream.
bool arrive(ClientSessionController& c, CountingSink* sink, const char* label) {
  const bool ok = c.Connect(connect_args(sink)) &&
                  wait_until([&] { return c.Snapshot().state == ClientSessionState::Connected; }, 8000) &&
                  wait_until([&] { return c.WindowPanelSnapshotCopy().hostSupportsMonitors; }, 10000);
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

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--thumbnail") {  // staged as GNLinkCapture.exe; answers nothing
      Sleep(120000);
      return 0;
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
  check("an isolated test-build host is started (loopback, no input injection, two screens enumerated)",
        self.Start(&why), why);
  if (!self.launched) {
    std::printf("\nRESULT: %d FAILED\n", gFailures);
    return 1;
  }
  const std::wstring log = self.logPath();
  const size_t real = real_screen_count();
  const uint32_t picked = static_cast<uint32_t>(real);  // the seam's screen is listed last
  std::printf("this PC has %zu screen(s); the test build adds one more, listed as id=%u\n", real, picked);

  {
    std::puts("\n--- 1. the real screens plus one; the viewer picks the added one and watches it ---");
    CountingSink sink;
    ClientSessionController c;
    if (arrive(c, &sink, "viewer 1 connects and the host advertises monitors")) {
      const bool listed = fresh_monitor_list(c, log, real + 1);
      check("the host lists the real screens plus the added one", listed, panel_said(c.WindowPanelSnapshotCopy()));
      check("monitor-select of the added screen is accepted", c.RequestMonitorSelect(picked));
      const std::string appliedLine = "monitor-select applied id=" + std::to_string(picked);
      const bool applied = wait_until([&] { return read_all(log).find(appliedLine) != std::string::npos; }, 10000);
      check("the host applied that monitor-select", applied);
      fresh_monitor_list(c, log, real + 1);
      check("the monitor list now names the added screen as selected",
            c.WindowPanelSnapshotCopy().selectedMonitorId == picked,
            panel_said(c.WindowPanelSnapshotCopy()));
      c.RequestStreamActive(true);
      check("video flows from the second screen", wait_until([&] { return sink.frames() >= 1; }, 20000),
            "frames=" + std::to_string(sink.frames()));
    }
    leave_and_detach(c, log, "viewer 1 leaves; the host detaches the idle capture");
  }

  {
    std::puts("\n--- 2. transient: nothing enumerated (a reconfiguration in progress) -- no fallback, the retry stays ---");
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
            c.WindowPanelSnapshotCopy().selectedMonitorId == picked && count_of(read_all(log).substr(mark), "monitor-fallback") == 0,
            panel_said(c.WindowPanelSnapshotCopy()));
    }
    leave_and_detach(c, log, "viewer 2 leaves; the host detaches the idle capture");
  }

  {
    std::puts("\n--- 3. the picked screen is unplugged while nobody watches; a viewer comes back ---");
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
      // and is not re-logged; what matters is that it stays KNOWN. Phase 4 shows it set again
      // from unknown.
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
      check("monitor-select of the added screen again (for phase 4)", c.RequestMonitorSelect(picked));
      const std::string appliedLine = "monitor-select applied id=" + std::to_string(picked);
      check("applied", wait_until([&] { return count_of(read_all(log), appliedLine, mark) >= 1; }, 10000));
    }
    leave_and_detach(c, log, "viewer 3 leaves; the host detaches the idle capture");
  }

  {
    std::puts("\n--- 4. an unplug seen in two steps: nothing enumerated, then the picked screen missing ---");
    // The rect goes unknown while absence is unconfirmed (the existing retry), and is set again
    // for the fallback screen once the enumeration lists screens without the picked one.
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
      const std::string rectLine = rectSet == std::string::npos ? std::string() : afterFb.substr(rectSet, afterFb.find('\n', rectSet) - rectSet);
      check("the secure-input rect is SET AGAIN for the fallback screen (reason=capture-restart, not 0x0)",
            rectLine.find("reason=capture-restart") != std::string::npos && rectLine.find("/0x0") == std::string::npos,
            rectLine);
      check("no 'monitor query failed' and no reattach failure after the fallback",
            afterFb.find("monitor query failed") == std::string::npos &&
                count_of(afterFb, "capture reattach failed") == 0);
      fresh_monitor_list(c, log, real);
      check("monitor-list: selectedId=0", c.WindowPanelSnapshotCopy().selectedMonitorId == 0,
            panel_said(c.WindowPanelSnapshotCopy()));
    }
    c.Disconnect();
  }

  {
    std::puts("\n--- 5. the monitor-list reply on its own: the screen goes while the viewer is connected but not streaming ---");
    // No capture restart can run here (the stream is off and the capture detached), so the reply
    // is the only thing that can name the screen that will be shown -- not the stale index.
    write_mode(self.monitorsPath(), "extra");
    CountingSink sink;
    ClientSessionController c;
    if (arrive(c, &sink, "viewer 5 connects")) {
      fresh_monitor_list(c, log, real + 1);
      const std::string appliedLine = "monitor-select applied id=" + std::to_string(picked);
      const size_t mark0 = read_all(log).size();
      check("monitor-select of the added screen", c.RequestMonitorSelect(picked));
      check("applied", wait_until([&] { return count_of(read_all(log), appliedLine, mark0) >= 1; }, 10000));
      fresh_monitor_list(c, log, real + 1);
      check("the list names the added screen", c.WindowPanelSnapshotCopy().selectedMonitorId == picked,
            panel_said(c.WindowPanelSnapshotCopy()));
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
      c.RequestStreamActive(true);
      check("streaming again: the restart falls back to the same screen",
            wait_until([&] { return count_of(read_all(log), "monitor-fallback", mark) >= 1 && sink.frames() >= 1; }, 20000),
            "frames=" + std::to_string(sink.frames()));
    }
    c.Disconnect();
  }

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
