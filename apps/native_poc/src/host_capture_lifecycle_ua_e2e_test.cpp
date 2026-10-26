// capture-lifecycle r1 (UA-1 / UA-2, Codex §1 and §4), end to end: an isolated TEST BUILD of the host
// (GNLinkStreamMigrationTest, staged as GNLinkStream.exe) capturing this PC's desktop on loopback, and
//
//   UA-1 (WGC)  the shared session the Android app drives (ClientSessionController) asks for WGC, then the
//               input desktop goes secure and comes back, twice, on the SAME connection: the host rebuilds its
//               WGC capture at each confirmed change and a new IDR of a capture made after the change is
//               decoded (the product decoder) -- no reconnect.
//   UA-2        that session ends; the PC viewer connects (no backend request -- the PC viewer never sends
//               one): the host starts the new session from its own default, DXGI, not the phone's WGC
//               (10-22..10-23 field: it was inherited for days).
//   UA-1 (DXGI) on the PC viewer's connection (the product receive chain: VideoReceiver, FrameGate, decoder,
//               FrameBuffer) the input desktop goes secure and back, twice: DXGI falls back to WGC, the WGC is
//               rebuilt when the desktop comes back, DXGI is climbed back to; a new IDR after each change.
//
// What differs from the product, said once, because it bounds the claim:
//   * The secure desktop is NOT real. GNLINK_STREAM_TEST_SECURE_DESKTOP_EVENT makes the host's input-desktop
//     name read "Winlogon" (desktop_name_seam.hpp) and, for DXGI, ask the fallback a real secure desktop's
//     ACCESS_LOST would cause (host_stage_backend.cpp seam). The pixels stay this desktop's: what is proven is
//     the LIFECYCLE (edge seen, capture rebuilt, new IDR of a later capture decoded, same connection) -- not
//     that WGC shows a real UAC prompt. That is the user's field check (a real UAC, approved by the user).
//   * The host is the test build, input injection off, its files isolated; the clipboard stays out.
//
//   --rebuild-fails (r2/r3, Codex N1 (b) / N1-a): the rebuild after the edge to the secure desktop fails AFTER
//               its detach, as a real one does (GNLINK_STREAM_TEST_FAIL_RESTART_FILE: while that file exists every
//               capture restart fails right after DetachCaptureSession). (1) one failure, the file goes: the
//               SAME desktop generation's 2nd attempt succeeds and the picture comes back. (2) the file stays:
//               3 attempts, then nothing -- not the rebuild, not the host's generic retry (4th attempt 0) -- and
//               no picture; the next edge (file gone) rebuilds and the picture comes back. Instead of the
//               phases above.
//
//   remote60_host_capture_lifecycle_ua_e2e_test   (REMOTE60_ALLOW_HOST_E2E=1; exit 77 = skipped)
//   REMOTE60_E2E_OLD_BIN=<exe>: run another host build (a negative control).
// Tags: display-capture (this desktop), gpu (DXGI/WGC, encoder), network (loopback).

#include "e2e_isolation.hpp"
#include "control_resume_e2e_support.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <mfapi.h>

#include "mf_h264_codec.hpp"
#include "native_video_client_session.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_client_tcp_control.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"
#include "udp_control_channel.hpp"
#include "viewer_args.hpp"
#include "viewer_constants.hpp"
#include "viewer_control_client.hpp"
#include "viewer_decoder_state.hpp"
#include "viewer_frame_gate_state.hpp"
#include "viewer_state.hpp"
#include "viewer_udp_session.hpp"
#include "viewer_video_receiver.hpp"

using namespace remote60::native_poc;
using namespace remote60::native_poc::e2e;
using remote60::native_poc::viewer::ViewerState;

namespace remote60::native_poc {
bool macro_window_visible() { return false; }
void macro_window_toggle(HINSTANCE, HWND, const MacroWindowHooks&) {}
}  // namespace remote60::native_poc

namespace {

std::string read_file(const std::wstring& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
size_t count_of(const std::string& hay, const std::string& needle) {
  size_t n = 0;
  for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) ++n;
  return n;
}

// A decoded picture, from whichever client: when it was decoded/published, the capture time the host
// stamped on it (the same QPC clock: loopback, one machine), and whether it was a key frame.
struct Seen {
  uint64_t us = 0;
  uint64_t captureUs = 0;
  bool key = false;
  bool synthetic = false;  // r2: a re-served cached frame (kEncodedFrameFlagSynthetic) -- its stamp is "now"
};
struct SeenLog {
  std::mutex mu;
  std::vector<Seen> v;
  void Add(const Seen& s) {
    std::lock_guard<std::mutex> lk(mu);
    v.push_back(s);
  }
  // A key frame of a REAL capture (not a re-served cached frame, whose stamp is the time it was re-served --
  // capture-lifecycle r2, Codex: a synthetic stamp is not a new source) made after `afterUs`, decoded by now.
  bool KeyCapturedAfter(uint64_t afterUs, Seen* out = nullptr) {
    std::lock_guard<std::mutex> lk(mu);
    for (const Seen& s : v) {
      if (s.key && !s.synthetic && s.captureUs > afterUs) {
        if (out) *out = s;
        return true;
      }
    }
    return false;
  }
  // What came at/after a capture time, for a failure's detail (up to 4: capture, key, synthetic).
  std::string Describe(uint64_t fromCaptureUs) {
    std::lock_guard<std::mutex> lk(mu);
    std::string out;
    int n = 0;
    for (const Seen& s : v) {
      if (s.captureUs + 1 < fromCaptureUs) continue;
      out += " [cap=" + std::to_string(s.captureUs) + " key=" + (s.key ? "1" : "0") + " syn=" + (s.synthetic ? "1" : "0") + "]";
      if (++n == 4) break;
    }
    return out.empty() ? std::string(" [nothing seen at/after]") : out;
  }
  // A real (not re-served) decoded picture captured at/after `fromCaptureUs`.
  bool RealCapturedAtOrAfter(uint64_t fromCaptureUs, Seen* out = nullptr) {
    std::lock_guard<std::mutex> lk(mu);
    for (const Seen& s : v) {
      if (!s.synthetic && s.captureUs >= fromCaptureUs) {
        if (out) *out = s;
        return true;
      }
    }
    return false;
  }
  // What was decoded at/after a time (decode clock), for a failure's detail (up to 4).
  std::string DescribeDecodedSince(uint64_t fromUs) {
    std::lock_guard<std::mutex> lk(mu);
    std::string out;
    int n = 0;
    for (const Seen& s : v) {
      if (s.us < fromUs) continue;
      out += " [at=" + std::to_string(s.us) + " cap=" + std::to_string(s.captureUs) + " key=" + (s.key ? "1" : "0") +
             " syn=" + (s.synthetic ? "1" : "0") + "]";
      if (++n == 4) break;
    }
    return out;
  }
  size_t CountSince(uint64_t fromUs) {
    std::lock_guard<std::mutex> lk(mu);
    return static_cast<size_t>(std::count_if(v.begin(), v.end(), [&](const Seen& s) { return s.us >= fromUs; }));
  }
};

// The shared session's frames, decoded by the product decoder as they arrive (what the app does).
struct DecodingSink : ClientEncodedFrameSink {
  SeenLog* log = nullptr;
  std::unique_ptr<H264Decoder> dec;
  uint32_t w = 0, h = 0;
  int64_t stamp = 0;
  uint64_t decoded = 0, failed = 0;
  void OnEncodedH264Frame(UdpH264AssembledFrame&& f) override {
    const bool key = (f.header.flags & kEncodedFrameFlagKeyFrame) != 0;
    if (!dec || (key && (f.header.width != w || f.header.height != h))) {
      if (!key) return;  // wait for a key frame to start
      dec = std::make_unique<H264Decoder>();
      w = f.header.width;
      h = f.header.height;
      if (!dec->initialize(w, h, 30)) {
        dec.reset();
        ++failed;
        return;
      }
    }
    std::vector<DecodedFrameNv12> out;
    bool overflow = false;
    stamp += 333333;
    if (dec->decode_access_unit(f.payload, key, stamp, &out, &overflow)) {
      for (const auto& d : out) {
        if (d.bytes.empty()) continue;
        ++decoded;
        log->Add(Seen{qpc_now_us(), f.header.captureQpcUs, key, (f.header.flags & kEncodedFrameFlagSynthetic) != 0});
      }
    } else {
      ++failed;
    }
  }
  void OnVideoStreamReset() override {}
};

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  bool rebuildFails = false;
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--rebuild-fails") rebuildFails = true;
  }
  std::printf("host_capture_lifecycle_ua_e2e_test (capture-lifecycle r1: UA-1 / UA-2)\n");
  if (!host_e2e_allowed()) {
    std::printf("SKIP  (starts a listening host that captures this desktop)\n");
    std::printf("      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED\n");
    return kE2eSkippedExit;
  }
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
  if (FAILED(MFStartup(MF_VERSION))) {
    std::printf("FAIL  MFStartup\n");
    return 1;
  }
  const uint16_t hostPort = e2e_pick_free_udp_port();
  StagingDir staging;
  if (!staging.Create(L"capture_lifecycle_ua")) {
    std::printf("FAIL  %s\n", staging.why().c_str());
    return 1;
  }
  const std::wstring dir = staging.path();
  std::wstring hostSrc = directory_of(self_path()) + L"GNLinkStreamMigrationTest.exe";
  {
    wchar_t oldBin[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"REMOTE60_E2E_OLD_BIN", oldBin, MAX_PATH) != 0 && oldBin[0] != L'\0') hostSrc = oldBin;
  }
  const bool staged = CopyFileW(hostSrc.c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE) != 0;
  check("the host (test build) could be staged", staged);

  // The secure desktop, as the test build is shown it.
  const std::wstring secureEventName = L"GNLinkTestSecureDesktop-" + std::to_wstring(GetCurrentProcessId());
  HANDLE secure = CreateEventW(nullptr, TRUE, FALSE, secureEventName.c_str());

  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
  const std::wstring hostLogPath = dir + L"host.log";
  // r3: while this file exists, every capture restart of the test host fails after its detach (--rebuild-fails).
  const std::wstring failRestartPath = dir + L"fail-restart.flag";
  const auto set_fail_restart = [&](bool on) {
    if (on) {
      HANDLE f = CreateFileW(failRestartPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    } else {
      DeleteFileW(failRestartPath.c_str());
    }
  };
  HANDLE hostLog = INVALID_HANDLE_VALUE;
  PROCESS_INFORMATION hostPi{};
  bool launched = false;
  if (staged) {
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");  // h264, as the product child gets it
    SetEnvironmentVariableW(L"REMOTE60_CLIPBOARD_SYNC", L"0");  // the user's clipboard stays out
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_SECURE_DESKTOP_EVENT", secureEventName.c_str());
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_FAIL_RESTART_FILE", rebuildFails ? failRestartPath.c_str() : nullptr);
    std::wstring cmd = L"\"" + dir + L"GNLinkStream.exe\" --transport udp --codec h264" +
                       L" --bind-address 127.0.0.1 --bind-port " + std::to_wstring(hostPort) +
                       L" --fps 30 --seconds 150 --input-injection-mode none";
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    hostLog = CreateFileW(hostLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (hostLog != INVALID_HANDLE_VALUE) {
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = hostLog;
      si.hStdError = hostLog;
    }
    const std::wstring isoAppData = dir + L"localappdata";
    CreateDirectoryW(isoAppData.c_str(), nullptr);
    std::vector<wchar_t> isoEnv = e2e_isolated_environment(isoAppData);
    std::string isoWhy;
    const bool isoOk = e2e_command_is_isolated(cmd, dir, &isoWhy) && e2e_path_is_under(e2e_block_localappdata(isoEnv), dir);
    check("the launched host is isolated from the user's files, input injection off", isoOk, isoWhy);
    launched = isoOk && CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, hostLog != INVALID_HANDLE_VALUE,
                                       CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, isoEnv.data(),
                                       dir.c_str(), &si, &hostPi) != 0;
    if (launched) {
      AssignProcessToJobObject(job, hostPi.hProcess);
      ResumeThread(hostPi.hThread);
    }
  }
  check("the host started (desktop capture, startup backend = its default)", launched);
  const auto host_log = [&] { return read_file(hostLogPath); };
  const auto host_says = [&](const std::string& needle, int budgetMs) {
    return wait_until([&] { return host_log().find(needle) != std::string::npos; }, budgetMs);
  };
  const auto host_count = [&](const std::string& needle) { return count_of(host_log(), needle); };
  // capture-lifecycle r2 (Codex N1 evidence): the desktop generation the host's LAST "<edge>" line moved to, and
  // the capture time of the first frame the host adopted from an attachment made under it ("adopted first
  // frame ... desktopGen=G captureUs=X", PublishFrame) -- 0 if none yet.
  const auto last_edge_gen = [&](const std::string& edge) -> uint64_t {
    const std::string log = host_log();
    const size_t at = log.rfind(edge);
    if (at == std::string::npos) return 0;
    const size_t g = log.find("desktopGen=", at);
    return g == std::string::npos ? 0 : std::strtoull(log.c_str() + g + 11, nullptr, 10);
  };
  const auto adopted_capture_us = [&](uint64_t gen) -> uint64_t {
    const std::string log = host_log();
    const std::string needle = "desktopGen=" + std::to_string(gen) + " captureUs=";
    size_t at = 0;
    while ((at = log.find("adopted first frame", at)) != std::string::npos) {
      const size_t eol = log.find('\n', at);
      const std::string line = log.substr(at, eol == std::string::npos ? std::string::npos : eol - at);
      const size_t n = line.find(needle);
      if (n != std::string::npos) return std::strtoull(line.c_str() + n + needle.size(), nullptr, 10);
      at += 1;
    }
    return 0;
  };
  const auto host_alive = [&] { return launched && WaitForSingleObject(hostPi.hProcess, 0) == WAIT_TIMEOUT; };

  // One secure-desktop round trip on a connection: shown secure, then back, with what each edge must bring.
  // *Needle: a host log line each edge must add; *What: how the check names it.
  const auto round_trip = [&](const char* who, int i, SeenLog& seen, const std::string& onSecure, const std::string& onSecureWhat,
                              const std::string& onBack, const std::string& onBackWhat) {
    const std::string tag = std::string(who) + " #" + std::to_string(i);
    const size_t toSecureBefore = host_count("input desktop Default -> Winlogon");
    const size_t toDefaultBefore = host_count("input desktop Winlogon -> Default");
    const size_t secureNeedleBefore = onSecure.empty() ? 0 : host_count(onSecure);
    const size_t backNeedleBefore = host_count(onBack);
    const uint64_t t1 = qpc_now_us();
    SetEvent(secure);
    check(tag + ": the host saw the input desktop go secure (a confirmed change, not one look)",
          wait_until([&] { return host_count("input desktop Default -> Winlogon") > toSecureBefore; }, 3000));
    if (!onSecure.empty()) {
      check(tag + ": ...and " + onSecureWhat, wait_until([&] { return host_count(onSecure) > secureNeedleBefore; }, 4000),
            onSecure);
    }
    Seen k{};
    const bool key1 = wait_until([&] { return seen.KeyCapturedAfter(t1, &k); }, 5000);
    check(tag + ": A NEW IDR OF A CAPTURE MADE AFTER THE CHANGE IS DECODED (secure side; a real capture, not re-served)",
          key1, key1 ? std::to_string((k.us - t1) / 1000) + " ms after the edge" : std::string("none in 5 s"));
    {
      // r2: ...of the attachment made under the new desktop generation -- the key's capture is no earlier than
      // that attachment's first adopted frame (so it is not a frame of the desktop that was, re-labelled).
      const uint64_t gen = last_edge_gen("input desktop Default -> Winlogon");
      uint64_t adoptedUs = 0;
      wait_until([&] { return (adoptedUs = adopted_capture_us(gen)) != 0; }, 3000);
      // The host sent that very frame as the new epoch's first key ("capture-switch first-frame ...
      // captureQpcUs=<adopted> auEpoch=E curEpoch=E"), and the viewer decoded a real picture captured at/after
      // it. (The decoded picture's capture time is the decoder OUTPUT's; the viewer's "key" mark is its INPUT
      // packet's -- an MF decoder may return a picture one call late, so the two are not joined on one Seen.)
      const std::string firstKey = "captureQpcUs=" + std::to_string(adoptedUs) + " size=";
      const bool hostKey = adoptedUs != 0 && host_says(firstKey, 3000);
      Seen kk{};
      const bool joined = adoptedUs != 0 && wait_until([&] { return seen.RealCapturedAtOrAfter(adoptedUs, &kk); }, 3000);
      check(tag + ": ...THE NEW ATTACHMENT'S FIRST FRAME WENT OUT AS THE NEW EPOCH'S KEY, AND WAS DECODED", hostKey && joined,
            "desktopGen=" + std::to_string(gen) + " adoptedCaptureUs=" + std::to_string(adoptedUs) +
                " hostFirstKey=" + (hostKey ? "1" : "0") +
                (joined ? " decodedCaptureUs=" + std::to_string(kk.captureUs) : seen.Describe(adoptedUs)));
    }
    Sleep(1500);
    const uint64_t t2 = qpc_now_us();
    ResetEvent(secure);
    check(tag + ": the host saw the input desktop come back to Default",
          wait_until([&] { return host_count("input desktop Winlogon -> Default") > toDefaultBefore; }, 3000));
    check(tag + ": ...and " + onBackWhat, wait_until([&] { return host_count(onBack) > backNeedleBefore; }, 8000), onBack);
    Seen k2{};
    const bool key2 = wait_until([&] { return seen.KeyCapturedAfter(t2, &k2); }, 5000);
    check(tag + ": A NEW IDR OF A CAPTURE MADE AFTER THE RETURN IS DECODED (a real capture, not re-served)", key2,
          key2 ? std::to_string((k2.us - t2) / 1000) + " ms after the edge" : std::string("none in 5 s"));
    {
      const uint64_t gen = last_edge_gen("input desktop Winlogon -> Default");
      uint64_t adoptedUs = 0;
      wait_until([&] { return (adoptedUs = adopted_capture_us(gen)) != 0; }, 3000);
      // The host sent that very frame as the new epoch's first key ("capture-switch first-frame ...
      // captureQpcUs=<adopted> auEpoch=E curEpoch=E"), and the viewer decoded a real picture captured at/after
      // it. (The decoded picture's capture time is the decoder OUTPUT's; the viewer's "key" mark is its INPUT
      // packet's -- an MF decoder may return a picture one call late, so the two are not joined on one Seen.)
      const std::string firstKey = "captureQpcUs=" + std::to_string(adoptedUs) + " size=";
      const bool hostKey = adoptedUs != 0 && host_says(firstKey, 3000);
      Seen kk{};
      const bool joined = adoptedUs != 0 && wait_until([&] { return seen.RealCapturedAtOrAfter(adoptedUs, &kk); }, 3000);
      check(tag + ": ...THE NEW ATTACHMENT'S FIRST FRAME WENT OUT AS THE NEW EPOCH'S KEY, AND WAS DECODED", hostKey && joined,
            "desktopGen=" + std::to_string(gen) + " adoptedCaptureUs=" + std::to_string(adoptedUs) +
                " hostFirstKey=" + (hostKey ? "1" : "0") +
                (joined ? " decodedCaptureUs=" + std::to_string(kk.captureUs) : seen.Describe(adoptedUs)));
    }
    const uint64_t t3 = qpc_now_us();
    Sleep(2000);
    check(tag + ": ...and the picture keeps coming (frames in the 2 s after)", seen.CountSince(t3) >= 2,
          std::to_string(seen.CountSince(t3)) + " frames");
  };

  // ------------------------------------------------------------------ r2/r3: an edge whose rebuild fails
  if (launched && rebuildFails) {
    std::cout << "\n--- r3 (N1-a): the rebuild after the edge fails after its detach ---\n";
    SeenLog seen;
    DecodingSink rs;
    rs.log = &seen;
    ClientSessionController phone;
    ClientSessionConnectArgs a;
    a.host = "127.0.0.1";
    a.videoPort = hostPort;
    a.controlPort = hostPort;
    a.requireUdpHello = true;
    a.requireTcpControl = false;
    a.controlOverUdp = true;
    a.controlIntervalMs = 200;
    a.udpHandshakeTimeoutMs = 20000;
    a.encodedFrameSink = &rs;
    const bool up = phone.Connect(a) &&
                    wait_until([&] { return phone.Snapshot().state == ClientSessionState::Connected; }, 25000);
    check("the shared session connects", up, phone.Snapshot().status);
    check("...and decodes the desktop", wait_until([&] { return rs.decoded > 0; }, 15000));
    wait_until([&] { return phone.HostAnswersDesktopBackendStatus(); }, 5000);
    check("it asks for WGC (a capture that keeps giving frames of this live desktop), and the host runs it",
          phone.RequestDesktopCaptureBackend(2) && host_says("desktop-backend-applied seq=", 8000));
    Sleep(2000);
    const auto injected = [&] { return host_count("capture restart failed (injected)"); };

    // (1) one failure after the detach, then the restart works again: the same generation's 2nd attempt.
    set_fail_restart(true);
    SetEvent(secure);
    check("(1) the host saw the input desktop go secure", host_says("input desktop Default -> Winlogon", 3000));
    const uint64_t gen1 = last_edge_gen("input desktop Default -> Winlogon");
    const std::string r1 = "rebuild desktopGen=" + std::to_string(gen1);
    check("(1) THE MEDIA FENCE CAME AT THE EDGE, before any rebuild",
          host_says("edge fence desktopGen=" + std::to_string(gen1) + " atUs=", 2000) &&
              host_log().find("edge fence desktopGen=" + std::to_string(gen1)) < host_log().find(r1 + " attempt=1"));
    check("(1) the 1st attempt failed AFTER its detach (the injected restart failure)",
          host_says(r1 + " attempt=1 ok=0", 3000) && injected() >= 1, std::to_string(injected()) + " injected");
    set_fail_restart(false);
    check("(1) THE SAME GENERATION'S 2nd ATTEMPT SUCCEEDED (a failed detach is not 'rebuilt')",
          host_says(r1 + " attempt=2 ok=1", 4000));
    const uint64_t t1 = qpc_now_us();
    check("(1) ...and the picture comes back on the same connection",
          wait_until([&] { return seen.RealCapturedAtOrAfter(t1); }, 5000));
    ResetEvent(secure);
    check("(1) back to Default: rebuilt", host_says("input desktop Winlogon -> Default", 3000) &&
                                            host_says("rebuild desktopGen=" + std::to_string(gen1 + 1) + " attempt=1 ok=1", 4000));
    Sleep(2000);

    // (2) the restart keeps failing: 3 attempts, then nothing more -- the generic retry does not take over.
    set_fail_restart(true);
    const size_t injectedBefore = injected();
    const size_t recoveredBefore = host_count("capture retry recovered");
    const size_t secureEdgesBefore = host_count("input desktop Default -> Winlogon");
    SetEvent(secure);
    check("(2) the host saw the input desktop go secure (a NEW edge)",
          wait_until([&] { return host_count("input desktop Default -> Winlogon") > secureEdgesBefore; }, 3000));
    const uint64_t gen2 = last_edge_gen("input desktop Default -> Winlogon");
    const std::string r2 = "rebuild desktopGen=" + std::to_string(gen2);
    uint64_t fenceUs = 0;
    if (host_says("edge fence desktopGen=" + std::to_string(gen2) + " atUs=", 2000)) {
      const std::string log = host_log();
      const size_t at = log.find("edge fence desktopGen=" + std::to_string(gen2) + " atUs=");
      fenceUs = std::strtoull(log.c_str() + log.find("atUs=", at) + 5, nullptr, 10);
    }
    const bool gaveUp = host_says("rebuild gave up desktopGen=" + std::to_string(gen2), 8000);
    const size_t third = host_count(r2 + " attempt=3 ok=0");
    const size_t injectedNow = injected() - injectedBefore;
    check("(2) 3 attempts failed after their detach, then the rebuild gave up", gaveUp && third == 1 && injectedNow >= 3,
          "gaveUp=" + std::to_string(gaveUp ? 1 : 0) + " attempt3=" + std::to_string(third) + " injected=" +
              std::to_string(injectedNow));
    Sleep(10000);  // past the generic retry's backoff (250 ms << 5 = 8 s)
    check("(2) NOTHING MORE FOR 10 s: no 4th attempt, the generic retry did not take over (3 restarts in all)",
          host_count(r2 + " attempt=4") == 0 && injected() - injectedBefore == 3 &&
              host_count("capture retry recovered") == recoveredBefore,
          std::to_string(injected() - injectedBefore) + " restarts tried");
    const size_t after = fenceUs != 0 ? seen.CountSince(fenceUs + 500000ULL) : 1;
    check("(2) NOTHING OF THE OLD ATTACHMENT REACHES THE VIEWER after the edge (0 pictures from fence+500 ms)",
          fenceUs != 0 && after == 0,
          std::to_string(after) + " pictures; fenceUs=" + std::to_string(fenceUs) + seen.DescribeDecodedSince(fenceUs + 500000ULL));
    check("(2) the session is still up (no reconnect)", phone.Snapshot().state == ClientSessionState::Connected);
    set_fail_restart(false);
    const uint64_t tBack = qpc_now_us();
    const size_t backEdgesBefore = host_count("input desktop Winlogon -> Default");
    ResetEvent(secure);
    check("(2) back to Default (file gone): THAT edge's rebuild succeeded",
          wait_until([&] { return host_count("input desktop Winlogon -> Default") > backEdgesBefore; }, 3000) &&
              host_says("rebuild desktopGen=" + std::to_string(gen2 + 1) + " attempt=1 ok=1", 4000));
    check("(2) THE PICTURE COMES BACK on the same connection",
          wait_until([&] { return seen.RealCapturedAtOrAfter(tBack); }, 5000));
    phone.Disconnect();
  }

  // ------------------------------------------------------------------ UA-1 (WGC): the shared session
  SeenLog phoneSeen;
  DecodingSink sink;
  sink.log = &phoneSeen;
  if (launched && !rebuildFails) {
    std::cout << "\n--- UA-1 (WGC): the shared session the Android app drives asks for WGC ---\n";
    ClientSessionController phone;
    ClientSessionConnectArgs a;
    a.host = "127.0.0.1";
    a.videoPort = hostPort;
    a.controlPort = hostPort;
    a.requireUdpHello = true;
    a.requireTcpControl = false;
    a.controlOverUdp = true;
    a.controlIntervalMs = 200;
    a.udpHandshakeTimeoutMs = 20000;
    a.encodedFrameSink = &sink;
    const bool up = phone.Connect(a) &&
                    wait_until([&] { return phone.Snapshot().state == ClientSessionState::Connected; }, 25000);
    check("the shared session connects", up, phone.Snapshot().status);
    const bool decodes = wait_until([&] { return sink.decoded > 0; }, 15000);
    check("...and decodes the desktop (the product decoder)", decodes,
          std::to_string(sink.decoded) + " decoded, " + std::to_string(sink.failed) + " failed");
    // UA-2 status (90): the host said it answers (its pong), so the request asks for the answer.
    check("UA-2 status: the host says it answers backend requests (pong 0x100000)",
          wait_until([&] { return phone.HostAnswersDesktopBackendStatus(); }, 5000));
    const bool asked = phone.RequestDesktopCaptureBackend(2);
    check("it asks for WGC, and the host runs WGC for it",
          asked && host_says("desktop-backend-applied seq=", 8000) &&
              host_log().find("requested=wgc active=wgc") != std::string::npos);
    DesktopBackendStatusSnapshot st{};
    const bool answered = wait_until([&] {
      st = phone.DesktopCaptureBackendStatus();
      return st.valid;
    }, 5000);
    const bool firstNotYet = answered && (st.flags & kDesktopBackendStatusFlagNotYetApplied) != 0;
    check("UA-2 status: the request is answered (within the host's 250 ms, applied or said NotYetApplied)", answered,
          firstNotYet ? std::string("NotYetApplied") : std::string("applied"));
    // As the app does: a NotYetApplied answer is followed by questions (backend 0) until it settles.
    const bool settled = answered && wait_until([&] {
      st = phone.DesktopCaptureBackendStatus();
      if (st.valid && (st.flags & kDesktopBackendStatusFlagNotYetApplied) == 0 && st.active == 2) return true;
      phone.QueryDesktopCaptureBackend();
      Sleep(200);
      return false;
    }, 5000);
    check("UA-2 status: THE ANSWER SAYS what this session asked and what runs -- WGC, for this session only",
          settled && st.requested == 2 && st.effective == 2 && st.active == 2 &&
              (st.flags & kDesktopBackendStatusFlagSessionScoped) != 0,
          "requested=" + std::to_string(st.requested) + " effective=" + std::to_string(st.effective) +
              " active=" + std::to_string(st.active) + " flags=" + std::to_string(st.flags));
    Sleep(1500);
    for (int i = 1; i <= 2; ++i) {
      round_trip("UA-1 WGC", i, phoneSeen, "attempt=1 ok=1 active=wgc", "THE HOST REBUILT ITS WGC CAPTURE for the secure desktop",
                 "attempt=1 ok=1 active=wgc", "THE HOST REBUILT ITS WGC CAPTURE for Default again");
    }
    check("UA-1 WGC: SAME CONNECTION throughout -- the session never left Connected; one session (epoch) at the host",
          phone.Snapshot().state == ClientSessionState::Connected && host_count("backend-owner epoch=") == 1,
          std::to_string(host_count("backend-owner epoch=")) + " session epoch(s)");
    // A question (backend 0) after the round trips: answered at once, changing nothing.
    const uint32_t seqBefore = phone.DesktopCaptureBackendStatus().seq;
    const bool queried = phone.QueryDesktopCaptureBackend();
    DesktopBackendStatusSnapshot q{};
    const bool qAnswered = queried && wait_until([&] {
      q = phone.DesktopCaptureBackendStatus();
      return q.valid && q.seq != seqBefore;
    }, 5000);
    check("UA-2 status: a question is answered (still WGC for this session), and nothing was applied for it",
          qAnswered && q.active == 2 && q.requested == 2 && host_count("desktop-backend-applied seq=") == 1,
          "active=" + std::to_string(q.active) + " applied=" + std::to_string(host_count("desktop-backend-applied seq=")));
    phone.Disconnect();
    std::cout << "    the shared session ended\n";
    Sleep(1500);
  }

  // ------------------------------------------------------------------ UA-2 + UA-1 (DXGI): the PC viewer
  static ViewerState ctx;
  static viewer::Args args;
  static viewer::DecoderState dec;
  static viewer::FrameGateState gate;
  std::optional<viewer::VideoReceiver> receiver;
  std::optional<viewer::ControlClient> control;
  std::thread recvThread, controlThread, presentThread;
  std::atomic<bool> stopPresent{false};
  SeenLog pcSeen;
  bool pcUp = false;
  if (launched && !rebuildFails && host_alive()) {
    std::cout << "\n--- UA-2: the PC viewer connects next (it asks for no backend) ---\n";
    using namespace remote60::native_poc::viewer;
    args.codec = "h264";
    args.transport = "udp";
    args.fpsHint = 30;
    args.udpMtu = 1200;
    args.inputLogEvery = 0;
    dec.useH264 = true;
    dec.transport = VideoTransport::Udp;
    gate.catchupReenterMinIntervalUs = kCatchupReenterMinIntervalUsDefault;
    gate.staleCaptureDropUs = kStaleCaptureDropUs;
    gate.staleReferenceRecoveryMinIntervalUs = kStaleRecoveryMinIntervalUsDefault;
    gate.congestionRecoverMinUs = kCongestionRecoverMinUsDefault;
    gate.congestionRecoveryTimeoutUs = kCongestionRecoveryTimeoutUsDefault;
    gate.decodeQueueLagDropUs = kDecodeQueueLagDropUs;
    gate.catchupLagDropUs = kCatchupLagDropUs;
    gate.denseArrivalMaxGapUs = kDenseArrivalMaxGapUsDefault;
    gate.lagTriggerStreakMin = kLagTriggerStreakMinDefault;
    gate.recoveryRetryIntervalUs = kKeyRecoveryRetryUsDefault;
    gate.recoveryRetryMaxIntervalUs = kKeyRecoveryRetryMaxUsDefault;
    gate.recoveryRetryDeferMaxUs = kKeyRecoveryDeferMaxUsDefault;
    gate.waitForKeyFrame = true;
    ctx.picker.visible.store(false);
    ctx.control.keyframeRequests.Reset();
    ctx.session.running.store(true);
    ctx.session.controlRequired = true;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(hostPort);
    connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    const int recvBuf = 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf), sizeof(recvBuf));
    UdpHelloOptions hello = viewer_udp_hello_options(std::string(), /*requestNack=*/true);
    hello.budgetMs = 20000;
    uint32_t ackFeatures = 0;
    std::string helloError;
    const bool helloOk = udp_hello_handshake(s, hello, nullptr, &helloError, &ackFeatures);
    check("the PC viewer's Hello is answered (a new session)", helloOk, helloError);
    (void)viewer_arm_udp_recv_timeout(s, 25);
    ctx.session.sock = s;
    viewer_apply_udp_hello_ack(ackFeatures, true, ctx.session);
    ctx.control.udpControl.Configure(
        [s](const void* data, size_t len) { return send(s, static_cast<const char*>(data), static_cast<int>(len), 0) > 0; },
        kUdpControlStreamClientToHost, kUdpControlStreamHostToClient, args.udpMtu);
    ctx.control.overUdp.store(true, std::memory_order_release);
    ctx.control.clipboard.enabled.store(false);
    if (helloOk) {
      VideoReceiver::NackOptions nack;
      nack.enabled = ctx.session.hostSupportsNack;
      nack.holdUs = 120000;
      receiver.emplace(ctx, args, dec, gate, qpc_now_us(), 0, 0, nack);
      recvThread = std::thread([&] { receiver->Run(); });
      presentThread = std::thread([&] {
        uint64_t seenVersion = 0;
        while (!stopPresent.load()) {
          {
            std::lock_guard<std::mutex> lk(ctx.frameBuf.frame.mu);
            auto& f = ctx.frameBuf.frame;
            if (f.version != seenVersion) {
              seenVersion = f.version;
              ctx.frameBuf.lastPresentedVersion.store(f.version, std::memory_order_relaxed);
              if (!f.synthetic) ctx.frameBuf.lastPresentedCaptureUs.store(f.captureUs, std::memory_order_relaxed);
              pcSeen.Add(Seen{qpc_now_us(), f.captureUs, f.key, f.synthetic});
            }
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
      });
      control.emplace(ctx, args, /*startInPicker=*/false);
      ctx.control.scheduler.Reset(kClientControlIntervalMsDefault, qpc_now_us());
      ctx.control.connected.store(true, std::memory_order_relaxed);
      controlThread = std::thread([&] { control->Run(); });
      pcUp = true;
    }
    check("UA-2: THE HOST STARTED THE NEW SESSION FROM ITS OWN DEFAULT (the phone's WGC is not inherited)",
          host_says("desktop-backend-reset new-session", 8000) &&
              host_log().find("requested=wgc -> host-default=dxgi") != std::string::npos);
    check("UA-2: ...and actually runs DXGI for the PC viewer",
          wait_until([&] {
            const std::string l = host_log();
            const size_t reset = l.find("desktop-backend-reset new-session");
            return reset != std::string::npos && (l.find("to=dxgi", reset) != std::string::npos ||
                                                  l.find("capture reattached backend=dxgi", reset) != std::string::npos);
          }, 15000));
    const bool pcFrames = wait_until([&] { return pcSeen.CountSince(0) > 0; }, 15000);
    check("UA-2: ...and the PC viewer shows the desktop (decoded frames)", pcFrames, std::to_string(pcSeen.CountSince(0)) + " frames");
    Sleep(1500);
    if (pcUp) {
      std::cout << "\n--- UA-1 (DXGI): the PC viewer's connection crosses a secure desktop ---\n";
      for (int i = 1; i <= 2; ++i) {
        round_trip("UA-1 DXGI", i, pcSeen, "fallback_reason=dxgi_access_lost_secure_desktop", "DXGI fell back to WGC on the secure desktop",
                   "desktop-backend-restored from=wgc to=dxgi", "THE HOST CLIMBED BACK TO DXGI once Default was back");
        Sleep(1000);
      }
      check("UA-1 DXGI: SAME CONNECTION throughout -- two sessions (epochs) at the host in all: the phone's, this one",
            host_count("backend-owner epoch=") == 2,
            std::to_string(host_count("backend-owner epoch=")) + " session epoch(s)");
    }
  }

  // ------------------------------------------------------------------ teardown
  ctx.session.running.store(false);
  if (controlThread.joinable()) controlThread.join();
  if (recvThread.joinable()) recvThread.join();
  stopPresent.store(true);
  if (presentThread.joinable()) presentThread.join();
  if (ctx.session.sock != INVALID_SOCKET) closesocket(ctx.session.sock);
  check("the host lived through it all", host_alive());
  {
    // The host's own account, for the evidence.
    std::istringstream lines(host_log());
    std::string line;
    while (std::getline(lines, line)) {
      for (const char* key : {"[desktop]", "desktop-backend", "fallback_reason", "full-frame", "test-seam", "backend-owner epoch=",
                              "capture reattached", "desktop_backend="}) {
        if (line.find(key) != std::string::npos) {
          std::cout << "    host| " << line << "\n";
          break;
        }
      }
    }
  }
  if (launched) {
    TerminateJobObject(job, 0);  // this test's own child, in its own job
    WaitForSingleObject(hostPi.hProcess, 5000);
    CloseHandle(hostPi.hProcess);
    CloseHandle(hostPi.hThread);
  }
  if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);
  {
    // r2: the whole host log, kept for the evidence where the caller says (REMOTE60_E2E_HOST_LOG_COPY=<file>).
    wchar_t keep[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"REMOTE60_E2E_HOST_LOG_COPY", keep, MAX_PATH) > 0) CopyFileW(hostLogPath.c_str(), keep, FALSE);
  }
  if (job) CloseHandle(job);
  if (secure) CloseHandle(secure);
  check("the scratch directory is cleaned up", staging.Remove(), staging.why());
  MFShutdown();
  WSACleanup();
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures == 0 ? "ALL PASS" : "FAILED", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
