// keepalive-multi r3: the clipboard across two kept PCs -- TEST ONLY, never packaged.
//
// Two fail-closed pinned TEST hosts (A, B) on the private window station (no capture: they serve control only;
// clipboard sync on), each its own staged copy / LOCALAPPDATA / port, and two of the product's client sessions
// (ClientSessionController, the APK's own native session) held in the product's SessionTable -- every phone-side
// clipboard action goes through SessionTable::FindByBinding with the binding token taken when it is made, exactly
// as native_bridge.cpp's bound_entry does for nativeTakeIncomingClipboardText / nativeQueueClipboardText. The phone's
// clipboard is a mock sink (what MainActivity.drainIncomingClipboard would hand to ClipboardManager.setPrimaryClip).
//
// Real: the controllers' clipboard poll / push / park / resume, the table's binding resolution and transitions, the
// wire, the hosts' clipboard handlers and their clipboard (the isolated station's). Substituted: JNI marshalling,
// the Kotlin drain / push loop (one call each here), Android's ClipboardManager (the sink).
//
// The two hosts share one clipboard (a Medium process's private stations are all the logon session's one unnamed
// station). Who received what is therefore read from each host's OWN log -- "[clipboard] update applied
// utf16Count=N" -- and from the sink's record, never from the station's content; every text has its own length.
//
//   km_multiclip_e2e_test --out DIR      (REMOTE60_ALLOW_HOST_E2E=1)
#include "e2e_isolation.hpp"
#include "e2e_station_lock.hpp"
#include "control_resume_e2e_support.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "native_video_client_session.hpp"
#include "session_table.hpp"
#include "time_utils.hpp"

using namespace remote60::native_poc;
using namespace remote60::native_poc::e2e;

namespace {

void say(const std::string& s) {
  const std::string line = s + '\n';
  std::cout.write(line.data(), static_cast<std::streamsize>(line.size()));
  std::cout.flush();
}

class NullSink : public ClientEncodedFrameSink {
 public:
  void OnEncodedH264Frame(UdpH264AssembledFrame&&) override {}
  void OnVideoStreamReset() override {}
};

// The table's entry: a product session and its binding token -- the same interface native_bridge.cpp's SessionEntry
// gives SessionTable (the decoder adapter is left out: no picture here).
struct Entry {
  ClientSessionController controller;
  std::atomic<uint64_t> token{0};
  int ParkPhaseCode() { return static_cast<int>(controller.ParkPhase()); }
  bool Alive() {
    const auto st = controller.Snapshot().state;
    return st == ClientSessionState::Connected || st == ClientSessionState::Connecting;
  }
  int Established() {
    const auto st = controller.Snapshot().state;
    return st == ClientSessionState::Connected ? 1 : st == ClientSessionState::Connecting ? 0 : -1;
  }
  uint64_t Generation() { return controller.ConnectionGeneration(); }
  bool BeginPark() { return controller.BeginPark(); }
  bool ResumeFromPark() { return controller.ResumeFromPark(); }
  void SetParkLimit(bool unlimited, uint64_t ms) { controller.SetParkLimit(unlimited, ms); }
  void SetBindingToken(uint64_t t) { token = t; }
  uint64_t BindingToken() { return token.load(); }
  void Close() { controller.Disconnect(); }
};

int64_t local_ms() {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  return ((static_cast<int64_t>(st.wHour) * 60 + st.wMinute) * 60 + st.wSecond) * 1000 + st.wMilliseconds;
}
int64_t line_ms(const std::string& line) {  // "MM-DD HH:MM:SS.mmm ..."
  if (line.size() < 18 || line[2] != '-' || line[8] != ':' || line[11] != ':' || line[14] != '.') return -1;
  return ((std::stoll(line.substr(6, 2)) * 60 + std::stoll(line.substr(9, 2))) * 60 + std::stoll(line.substr(12, 2))) * 1000 +
         std::stoll(line.substr(15, 3));
}
// "update applied ... utf16Count=N" lines of a host log: (local ms, N)
std::vector<std::pair<int64_t, int>> applied(const std::wstring& log) {
  std::vector<std::pair<int64_t, int>> out;
  std::ifstream f(log);
  std::string line;
  while (std::getline(f, line)) {
    const size_t at = line.find("[clipboard] update applied");
    if (at == std::string::npos) continue;
    const size_t n = line.find("utf16Count=", at);
    if (n == std::string::npos) continue;
    out.emplace_back(line_ms(line), std::atoi(line.c_str() + n + 11));
  }
  return out;
}
bool host_got(const std::wstring& log, int len, int64_t fromMs = -1, int64_t toMs = -1) {
  for (const auto& [t, n] : applied(log)) {
    if (n == len && (fromMs < 0 || t >= fromMs) && (toMs < 0 || t <= toMs)) return true;
  }
  return false;
}
std::string applied_str(const std::wstring& log) {
  std::string s;
  for (const auto& [t, n] : applied(log)) s += std::to_string(t % 100000) + ":" + std::to_string(n) + " ";
  return s.empty() ? "none" : s;
}

struct Host {
  std::wstring name, dir, log, title;
  uint16_t port = 0;
  DWORD fixturePid = 0;
  PROCESS_INFORMATION pi{};
  HANDLE logHandle = INVALID_HANDLE_VALUE;
  StagingDir staging;
};

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::wstring outDir;
  bool heldControl = false;  // --held-control: after the hold, A is NOT parked and the app drains -- HA must be there
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--out" && i + 1 < argc) outDir = argv[i + 1];
    if (std::wstring(argv[i]) == L"--held-control") heldControl = true;
  }
  if (!outDir.empty() && outDir.back() != L'\\') outDir += L"\\";
  if (!host_e2e_allowed()) {
    say("SKIP  km_multiclip_e2e_test (starts listening hosts)\nRESULT: SKIPPED");
    return kE2eSkippedExit;
  }
  std::thread([] {
    std::this_thread::sleep_for(std::chrono::seconds(240));
    const char line[] = "\nFAIL  watchdog: no finish within 240 s\nRESULT: FAIL\n";
    DWORD w = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line, sizeof(line) - 1, &w, nullptr);
    TerminateProcess(GetCurrentProcess(), 3);
  }).detach();
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

  // ---------------------------------------------------------------- the station (its clipboard is not the user's)
  StationLock stationLock;
  if (!stationLock.Acquire("km_multiclip_e2e_test")) return StationLock::Busy("km_multiclip_e2e_test");
  HWINSTA ws = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
  wchar_t wsName[256] = L"";
  DWORD wsLen = 0;
  if (ws) GetUserObjectInformationW(ws, UOI_NAME, wsName, sizeof(wsName), &wsLen);
  HWINSTA origWs = GetProcessWindowStation();
  HDESK privateDesk = nullptr;
  if (ws) {
    SetProcessWindowStation(ws);
    privateDesk = CreateDesktopW(L"Default", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    SetProcessWindowStation(origWs);
  }
  std::wstring clipDesktop = std::wstring(wsName) + L"\\Default";
  check("a private window station for the hosts (its clipboard is its own)", ws && privateDesk);
  const DWORD userClipSeq0 = GetClipboardSequenceNumber();

  const std::wstring myDir = directory_of(self_path());
  const std::wstring helper = myDir + L"remote60_mobile_abr_session_e2e_test.exe";  // --station-fixture / --clip-*
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
  auto onStation = [&](const std::wstring& cmdIn, bool wait, DWORD* pidOut = nullptr) -> int {
    std::wstring cmd = cmdIn;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.lpDesktop = clipDesktop.data();
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return -1;
    AssignProcessToJobObject(job, pi.hProcess);
    if (pidOut) *pidOut = pi.dwProcessId;
    int code = 0;
    if (wait) {
      WaitForSingleObject(pi.hProcess, 10000);
      DWORD c = 99;
      GetExitCodeProcess(pi.hProcess, &c);
      code = static_cast<int>(c);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
  };
  auto stationSet = [&](const std::string& t) {
    return onStation(L"\"" + helper + L"\" --clip-set " + std::wstring(t.begin(), t.end()), true) == 0;
  };

  // ---------------------------------------------------------------- two fail-closed pinned test hosts on it
  Host hosts[2];
  hosts[0].name = L"A";
  hosts[1].name = L"B";
  SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
  SetEnvironmentVariableW(L"REMOTE60_CLIPBOARD_SYNC", L"1");
  SetEnvironmentVariableW(L"REMOTE60_PINNED_TEST_NO_CAPTURE", L"1");
  bool hostsUp = true;
  for (Host& h : hosts) {
    if (!h.staging.Create(L"km_multiclip_" + h.name)) {
      check("staging " + std::string(h.name.begin(), h.name.end()), false, h.staging.why());
      hostsUp = false;
      continue;
    }
    h.dir = h.staging.path();
    h.log = h.dir + L"host.log";
    h.port = e2e_pick_free_udp_port();
    h.title = e2e_unique_window_title(L"remote60 km multiclip " + h.name);
    const bool staged =
        CopyFileW((myDir + L"GNLinkStreamPinnedSelectTest.exe").c_str(), (h.dir + L"GNLinkStream.exe").c_str(), FALSE) != 0;
    onStation(L"\"" + helper + L"\" --station-fixture \"" + h.title + L"\"", false, &h.fixturePid);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::wstring cmd = L"\"" + h.dir + L"GNLinkStream.exe\" --transport udp --codec h264 --bind-address 127.0.0.1 --bind-port " +
                       std::to_wstring(h.port) + L" --fps 30 --bitrate 3000000 --seconds 200 --input-injection-mode none" +
                       L" --capture-window-pid " + std::to_wstring(h.fixturePid) + L" --capture-window-title \"" + h.title + L"\"";
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    h.logHandle = CreateFileW(h.log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = h.logHandle;
    si.hStdError = h.logHandle;
    si.lpDesktop = clipDesktop.data();
    const std::wstring iso = h.dir + L"localappdata";
    CreateDirectoryW(iso.c_str(), nullptr);
    std::vector<wchar_t> env = e2e_isolated_environment(iso);
    std::string why;
    const bool isolated = e2e_command_is_isolated(cmd, h.dir, &why) && e2e_path_is_under(e2e_block_localappdata(env), h.dir);
    const bool up = staged && h.fixturePid && isolated &&
                    CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, env.data(), h.dir.c_str(), &si,
                                   &h.pi) != 0;
    if (up) {
      AssignProcessToJobObject(job, h.pi.hProcess);
      ResumeThread(h.pi.hThread);
    }
    check("host " + std::string(h.name.begin(), h.name.end()) + ": the pinned test host, isolated, on the station, up",
          up, why);
    hostsUp = hostsUp && up;
  }
  std::this_thread::sleep_for(std::chrono::seconds(3));

  // ---------------------------------------------------------------- the phone: the product's sessions in the table
  std::vector<std::string> sink;  // the phone's clipboard writes (setPrimaryClip), in order
  std::string localClip;          // what the phone's clipboard holds
  uint64_t bound = 0;             // the decoder's binding (the one PC on screen)
  SessionTable<Entry> table(SessionTable<Entry>::Binder{[&](uint64_t t) { bound = t; },
                                                       [&](uint64_t t) {
                                                         if (bound != t) return false;
                                                         bound = 0;
                                                         return true;
                                                       }},
                            [](std::function<void()> close) { close(); });
  NullSink frames;
  auto connect = [&](const std::string& key, uint16_t port) -> uint64_t {
    auto r = table.Reserve(key, false, table.Epoch(), [](uint64_t) { return std::make_shared<Entry>(); });
    auto e = table.Find(r.handle);
    if (r.kind != SessionRegistry::Reservation::New || !e) return 0;
    ClientSessionConnectArgs args;  // what native_bridge.cpp sets for a session tunnelled over UDP
    args.host = "127.0.0.1";
    args.videoPort = port;
    args.controlPort = port;
    args.requireUdpHello = true;
    args.requireTcpControl = false;
    args.controlOverUdp = true;
    args.encodedFrameSink = &frames;
    if (!e->controller.Connect(args)) return 0;
    const uint64_t gen = e->controller.ConnectionGeneration();
    using CR = SessionTable<Entry>::CommitResult;
    for (int t = 0; t < 150; ++t) {  // the app's tryCommit, on its ticks
      const auto c = table.Commit(r.handle, gen, 0);
      if (c.kind == CR::Committed) return r.handle;
      if (c.kind == CR::Failed) return 0;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
  };
  auto parkConfirmed = [&](uint64_t h) {
    auto e = table.Find(h);
    return e && wait_until([&] { return e->ParkPhaseCode() == static_cast<int>(ClientParkPhase::Parked); }, 3000);
  };
  // the app's drain (MainActivity.drainIncomingClipboard -> nativeTakeIncomingClipboardText(binding))
  auto drain = [&](uint64_t binding) -> std::string {
    auto e = table.FindByBinding(binding);
    std::u16string got;
    if (!e || !e->controller.TakeIncomingClipboardText(&got) || got.empty()) return {};
    const std::string s(got.begin(), got.end());
    sink.push_back(s);
    localClip = s;
    return s;
  };
  // the app's status ticks (250 ms) over `ms`; stops at the first text when `untilAny` (an expected arrival)
  auto drainFor = [&](uint64_t binding, int ms, bool untilAny = false) {
    std::string any;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
      const std::string s = drain(binding);
      if (!s.empty()) any += (any.empty() ? "" : ",") + s;
      if (untilAny && !any.empty()) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return any;
  };
  // the app's push (pushLocalClipboardToHost -> nativeQueueClipboardText(binding))
  auto copyOnPhone = [&](uint64_t binding, const std::string& text) {
    localClip = text;
    auto e = table.FindByBinding(binding);
    if (!e) return false;
    const std::u16string w(text.begin(), text.end());
    return e->controller.QueueClipboardText(reinterpret_cast<const uint16_t*>(w.data()), w.size());
  };
  // the app's resume baseline (resumeKept -> nativeSeedClipboardBaseline(binding, phone's text))
  auto seed = [&](uint64_t binding) {
    if (auto e = table.FindByBinding(binding)) e->controller.SeedClipboardBaseline(std::u16string(localClip.begin(), localClip.end()));
  };

  uint64_t a = 0, b = 0, tokA = 0, tokB = 0;
  if (hostsUp) {
    a = connect("t|A", hosts[0].port);
    tokA = a ? table.Activate(a, 1) : 0;
    check("A connected (established) and on screen", a && tokA, table.Find(a) ? table.Find(a)->controller.Snapshot().status : "");
    auto honours = [&](uint64_t h) {  // the pause capability arrives with the host's first window list
      auto e = table.Find(h);
      return e && wait_until([&] { return e->controller.HostHonorsStreamPause(); }, 8000);
    };
    check("A parks", tokA && honours(a) && table.BeginPark(a, 2) && parkConfirmed(a));
    b = connect("t|B", hosts[1].port);
    tokB = b ? table.Activate(b, 3) : 0;
    check("B connected (established) and on screen", b && tokB);
    auto eb = table.Find(b);
    check("B's host does clipboard sync", eb && wait_until([&] { return eb->controller.HostSupportsClipboard(); }, 8000));
    check("B parks", tokB && honours(b) && table.BeginPark(b, 4) && parkConfirmed(b));
    tokA = table.Activate(a, 5);  // A resumed: A on screen, B parked
    auto ea = table.Find(a);
    check("A on screen again (resumed), B parked", tokA != 0 && ea && ea->ParkPhaseCode() == 0);
    seed(tokA);
    check("A's host does clipboard sync", ea && wait_until([&] { return ea->controller.HostSupportsClipboard(); }, 8000));
    (void)(ea && ea->controller.TakeClipboardPushRequest());
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const bool ready = a && b && tokA;
  // The no-capture test hosts end themselves ~30 s after their client connected (no capture: their main loop stays in
  // Startup and the host's own watchdog grace runs out -- kmclip_r1). Every wait below is a few clipboard polls
  // (0.7 s each), and an expected arrival ends its wait.
  const auto t0 = std::chrono::steady_clock::now();
  auto elapsed = [&] {
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count()) + " ms";
  };
  bool ready2 = true;
  if (ready) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));  // A's baseline poll
    // ---------------------------------------------------------- positive control: A on screen receives a host copy
    const std::string Hc = "Hctl1";  // 5
    stationSet(Hc);
    const std::string gotC = drainFor(tokA, 3000, true);
    check("CONTROL: a host copy reaches the phone through A's binding while A is on screen", gotC == Hc, gotC);

    // ---------------------------------------------------------- 1: A's received text held -> A parked / B on screen
    const std::string HA = "HA12345", Pold = "Pold12";  // 7, 6
    stationSet(HA);
    // A's session takes HA in (two polls); the app does NOT drain it yet
    std::this_thread::sleep_for(std::chrono::milliseconds(1600));
    if (heldControl) {
      // the same hold, then the app's next tick with A still on screen: the text the main run parks on must be in A
      const std::string held = drainFor(tokA, 800, true);
      check("HELD CONTROL: after the same hold, A's session holds HA (drained at the next tick)", held == HA, held);
      ready2 = false;
    }
    if (ready2) {
    const bool pendingQueued = copyOnPhone(tokA, Pold);   // a phone copy still pending in A at the park
    const int64_t parkA = local_ms();
    const bool parkedA = table.BeginPark(a, 10) && parkConfirmed(a);
    const uint64_t oldTokA = tokA;
    tokB = table.Activate(b, 11);
    seed(tokB);
    const int64_t switched = local_ms();
    check("1: P_old pending in A, A parked, B on screen (resumed)", pendingQueued && parkedA && tokB != 0);
    const size_t sink1 = sink.size();
    const std::string viaOldA = drainFor(oldTokA, 800);
    const std::string viaB = drainFor(tokB, 2200);
    check("1: A's late reception through A's OLD binding: nothing (the binding resolves to no PC)", viaOldA.empty(), viaOldA);
    check("1: ...nor through B (B re-baselined at its resume): the held HA is applied NOWHERE", viaB.empty(), viaB);
    bool haInSink = false;
    for (size_t i = sink1; i < sink.size(); ++i) haInSink = haInSink || sink[i] == HA;
    check("1: the phone's clipboard sink never received HA", !haInSink && localClip != HA);
    check("1: host B was not sent HA or P_old", !host_got(hosts[1].log, 7) && !host_got(hosts[1].log, 6),
          applied_str(hosts[1].log));

    // ---------------------------------------------------------- 2: B's new copy goes to B only
    const std::string PB = "PB123456";  // 8
    const bool staleA = copyOnPhone(oldTokA, "XXXXXXXXXXXXXXXXXXXX");  // 20: an action of A taken before the switch
    const bool sentPB = copyOnPhone(tokB, PB);
    const bool atB = wait_until([&] { return host_got(hosts[1].log, 8); }, 4000);
    check("2: a copy with A's stale binding is refused (resolves to no PC)", !staleA && !host_got(hosts[0].log, 20) &&
                                                                               !host_got(hosts[1].log, 20));
    check("2: B's new copy reaches host B", sentPB && atB, applied_str(hosts[1].log));
    check("2: ...and not host A (parked)", !host_got(hosts[0].log, 8), applied_str(hosts[0].log));
    const std::string HB = "HB1234567";  // 9: a copy on the PC side while A is parked (B on screen)
    stationSet(HB);
    const std::string gotHB = drainFor(tokB, 3000, true);
    check("2: a host copy while B is on screen reaches the phone through B", gotHB == HB, gotHB);

    // ---------------------------------------------------------- 3: back to A
    const bool parkedB = table.BeginPark(b, 20) && parkConfirmed(b);
    tokA = table.Activate(a, 21);
    seed(tokA);
    const int64_t resumedA = local_ms();
    check("3: B parked, A on screen again", parkedB && tokA != 0 && tokA != oldTokA);
    const std::string replay = drainFor(tokA, 2200);
    check("3: A RESUMED: nothing of its park time (HA held at the park, HB) is replayed to the phone", replay.empty(), replay);
    check("3: A RESUMED: its old pending P_old is not pushed after the resume",
          !host_got(hosts[0].log, 6, resumedA), applied_str(hosts[0].log));
    check("3: (P_old went before the pause or not at all -- never during the park)",
          !host_got(hosts[0].log, 6, parkA + 2500, resumedA), applied_str(hosts[0].log));
    const std::string H3 = "H3-1234567";  // 10
    stationSet(H3);
    const std::string got3 = drainFor(tokA, 3000, true);
    check("3: a new host copy reaches the phone through A", got3 == H3, got3);
    const std::string P3 = "P3-12345678";  // 11
    const bool sentP3 = copyOnPhone(tokA, P3);
    check("3: a new phone copy reaches host A", sentP3 && wait_until([&] { return host_got(hosts[0].log, 11); }, 4000),
          applied_str(hosts[0].log));
    say("NOTE  the sequence took " + elapsed() + " (the hosts' own limit: ~30 s from their client)");
    check("3: ...and not host B (parked)", !host_got(hosts[1].log, 11), applied_str(hosts[1].log));
    say("NOTE  sink: " + [&] {
      std::string s;
      for (const auto& x : sink) s += x + " ";
      return s;
    }());
    say("NOTE  host A applied (ms:len): " + applied_str(hosts[0].log) + "| host B: " + applied_str(hosts[1].log) +
        "| park A " + std::to_string(parkA % 100000) + " switch " + std::to_string(switched % 100000) + " resume A " +
        std::to_string(resumedA % 100000));
    }
  }
  for (Host& h : hosts) {
    check("host " + std::string(h.name.begin(), h.name.end()) + " was still running at the end (judged while alive)",
          h.pi.hProcess && WaitForSingleObject(h.pi.hProcess, 0) == WAIT_TIMEOUT);
  }
  check("THE USER'S CLIPBOARD (WinSta0) DID NOT MOVE", GetClipboardSequenceNumber() == userClipSeq0,
        std::to_string(userClipSeq0) + " -> " + std::to_string(GetClipboardSequenceNumber()));

  table.EndAll("test_end", 0);
  for (Host& h : hosts) {
    if (h.pi.hProcess) {
      TerminateProcess(h.pi.hProcess, 0);
      WaitForSingleObject(h.pi.hProcess, 5000);
      CloseHandle(h.pi.hProcess);
      CloseHandle(h.pi.hThread);
    }
    if (h.logHandle != INVALID_HANDLE_VALUE) CloseHandle(h.logHandle);
    if (!outDir.empty()) {
      CreateDirectoryW(outDir.c_str(), nullptr);
      CopyFileW(h.log.c_str(), (outDir + L"host_" + h.name + L".log").c_str(), FALSE);
    }
  }
  CloseHandle(job);
  for (Host& h : hosts) {
    if (h.staging.created() && !h.staging.Remove()) say("WARN  " + h.staging.why());
  }
  say("\nkm_multiclip_e2e_test: " + std::to_string(gChecks - gFailures) + " passed, " + std::to_string(gFailures) +
      " failed\nRESULT: " + (gFailures == 0 ? "PASS" : "FAIL"));
  WSACleanup();
  return gFailures == 0 ? 0 : 1;
}
