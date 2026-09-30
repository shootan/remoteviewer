// File copy, both directions, end to end in one process plus the real helpers and real paste
// consumers. (t-zdmsd4gb r1 steps 1-2)
//
// The chain, with nothing in it written by the test but the source files and the transport:
//
//   FileCopyClient (the product's viewer side: identify as the user, offer, 700 ms paste query,
//   pin, confirmed descriptor, the shared bulk sender) --control: LoopbackLink, answered by
//   HostFileCopyService::HandleControl exactly as the host's control session dispatches it--
//   --bulk: two real UDP sockets on 127.0.0.1-- HostFileCopyService (publish to the helper, pulls,
//   per-chunk SHA-256 check before a byte reaches the helper) --pipe-- GNLinkClipHelper.exe (the
//   real helper, started as this user on a private window station: the Medium test launch --
//   what it does not prove is file_copy_helper_host.hpp's) --OLE clipboard of that station--
//   the paste consumer (remote60_file_copy_helper_e2e_test --consumer: the destination folder's
//   IDropTarget driven as Explorer's Paste drives it; the shell's own copy engine).
//
// R->P runs the same chain the other way: HostFileCopyService is handed a copy of files the way the
// host's clipboard monitor hands it over (OnHostClipboard: path strings -- the one boundary the test
// injects, because the host's real clipboard is the user's), its helper identifies and pins them as
// the user, its paced sender answers the viewer's pulls; FileCopyClient asks every 700 ms, publishes
// the list through ITS OWN helper (a second private window station: "this PC"), and each helper Read
// becomes pulls checked chunk by chunk.
//
// The bytes that land are compared with the source files here. Scratch: StagingDir (inside the
// repository). The user's clipboard and files are never touched: the helper and the consumer live on
// a private window station, and every source file is made by this test.
//
// Tags: session-privilege (window station), filesystem (scratch), network (loopback), COM.

#include "e2e_isolation.hpp"

#include <objbase.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <functional>
#include <mutex>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "bulk_arbiter.hpp"
#include "file_copy_client.hpp"
#include "file_copy_helper_host.hpp"
#include "file_copy_wire.hpp"
#include "host_file_copy.hpp"
#include "e2e_station_lock.hpp"

using namespace remote60::native_poc;
namespace fc = remote60::native_poc::file_copy;
namespace fn = remote60::native_poc::file_copy::net;

namespace {

int gChecks = 0, gFailures = 0;
void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
}

std::string narrow(const std::wstring& s) {
  if (s.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
  std::string o(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), o.data(), n, nullptr, nullptr);
  return o;
}

std::wstring self_dir() {
  wchar_t p[MAX_PATH]{};
  GetModuleFileNameW(nullptr, p, MAX_PATH);
  std::wstring s = p;
  return s.substr(0, s.find_last_of(L'\\') + 1);
}

std::vector<uint8_t> make_content(uint64_t size, uint32_t seed) {
  std::vector<uint8_t> b(static_cast<size_t>(size));
  uint32_t x = seed | 1u;
  for (auto& v : b) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    v = static_cast<uint8_t>(x);
  }
  return b;
}

bool write_file(const std::wstring& path, const std::vector<uint8_t>& b) {
  std::ofstream o(path, std::ios::binary);
  if (!b.empty()) o.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
  return o.good();
}

bool read_file(const std::wstring& path, std::vector<uint8_t>* b) {
  std::ifstream i(path, std::ios::binary);
  if (!i) return false;
  b->assign(std::istreambuf_iterator<char>(i), std::istreambuf_iterator<char>());
  return true;
}

bool wait_until(const std::function<bool()>& done, int ms) {
  for (int i = 0; i < ms / 10; ++i) {
    if (done()) return true;
    Sleep(10);
  }
  return done();
}

struct Station {
  HWINSTA ws = nullptr;
  HDESK dk = nullptr;
  std::wstring desktop;
  std::wstring name;
  // `stationName` null: the unnamed station -- its name comes from the logon session, so a second
  // unnamed Create() in this process is the SAME station (one clipboard). A second, separate
  // clipboard needs a name.
  bool Create(const wchar_t* stationName = nullptr) {
    ws = CreateWindowStationW(stationName, 0, WINSTA_ALL_ACCESS, nullptr);
    if (!ws) return false;
    wchar_t name[256] = L"";
    DWORD len = 0;
    GetUserObjectInformationW(ws, UOI_NAME, name, sizeof(name), &len);
    HWINSTA orig = GetProcessWindowStation();
    SetProcessWindowStation(ws);
    dk = CreateDesktopW(L"Default", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    SetProcessWindowStation(orig);
    this->name = name;
    desktop = std::wstring(name) + L"\\Default";
    return dk != nullptr;
  }
};

struct Child {
  PROCESS_INFORMATION pi{};
  bool Start(const std::wstring& cmdline, const std::wstring& desktop) {
    std::vector<wchar_t> cmd(cmdline.begin(), cmdline.end());
    cmd.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    std::wstring d = desktop;
    si.lpDesktop = d.data();
    return CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != FALSE;
  }
  DWORD Wait(DWORD ms) {
    if (!pi.hProcess) return 0xFFFFFFFF;
    DWORD code = 0xFFFFFFFF;
    if (WaitForSingleObject(pi.hProcess, ms) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    return code;
  }
  ~Child() {
    if (pi.hProcess && WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT) {
      TerminateProcess(pi.hProcess, 125);
      WaitForSingleObject(pi.hProcess, 5000);
    }
    if (pi.hThread) CloseHandle(pi.hThread);
    if (pi.hProcess) CloseHandle(pi.hProcess);
  }
};

// The control link: requests dispatched to HandleControl exactly as host_control_session.cpp does.
class LoopbackLink : public ControlLink {
 public:
  LoopbackLink(HostFileCopyService* svc, uint64_t epoch) : svc_(svc), epoch_(epoch) {}
  bool Write(const void* d, size_t n) override {
    auto* b = static_cast<const uint8_t*>(d);
    tx_.insert(tx_.end(), b, b + n);
    return true;
  }
  bool EndMessage() override {
    fn::FileControlHeader h{};
    if (tx_.size() < sizeof(h)) return false;
    std::memcpy(&h, tx_.data(), sizeof(h));
    if (h.header.magic != kMagic || h.header.size != sizeof(h) || tx_.size() != sizeof(h) + h.payloadBytes) return false;
    std::vector<uint8_t> body(tx_.begin() + sizeof(h), tx_.end());
    tx_.clear();
    uint16_t rt = 0;
    std::vector<uint8_t> rb, out;
    if (!svc_->HandleControl(h.header.type, body, epoch_, &rt, &rb)) return false;
    {
      std::lock_guard<std::mutex> lock(rewriteMu_);
      if (rewrite_) rewrite_(rt, &rb);  // r3: what a broken or hostile host would answer instead
    }
    if (!fn::frame_control(static_cast<fn::FileMsg>(rt), h.seq, rb, &out)) return false;
    rx_.insert(rx_.end(), out.begin(), out.end());
    return true;
  }
  bool Read(void* out, size_t n) override {
    if (rx_.size() < n) return false;
    std::memcpy(out, rx_.data(), n);
    rx_.erase(rx_.begin(), rx_.begin() + static_cast<std::ptrdiff_t>(n));
    return true;
  }
  bool Alive() const override { return true; }
  void SetRewrite(std::function<void(uint16_t, std::vector<uint8_t>*)> f) {
    std::lock_guard<std::mutex> lock(rewriteMu_);
    rewrite_ = std::move(f);
  }

 private:
  std::mutex rewriteMu_;
  std::function<void(uint16_t, std::vector<uint8_t>*)> rewrite_;
  HostFileCopyService* svc_;
  uint64_t epoch_;
  std::vector<uint8_t> tx_, rx_;
};

}  // namespace

int wmain() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  remote60::native_poc::e2e::StationLock stationLock;  // TEST ONLY: queue behind any other clipboard e2e (e2e_station_lock.hpp)
  if (!stationLock.Acquire("file_copy_net_e2e_test")) return remote60::native_poc::e2e::StationLock::Busy("file_copy_net_e2e_test");
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const DWORD userClipBefore = GetClipboardSequenceNumber();
  e2e::StagingDir staging;
  if (!staging.Create(L"file_copy_net")) {
    std::printf("FAIL  %s\n", staging.why().c_str());
    return 1;
  }
  const std::wstring root = staging.path();
  const std::wstring srcDir = root + L"src";
  CreateDirectoryW(srcDir.c_str(), nullptr);
  const std::wstring bin = self_dir();
  const std::wstring helperExe = bin + L"GNLinkClipHelper.exe";
  const std::wstring consumerExe = bin + L"remote60_file_copy_helper_e2e_test.exe";
  check("the helper and the paste consumer are beside this test",
        GetFileAttributesW(helperExe.c_str()) != INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(consumerExe.c_str()) != INVALID_FILE_ATTRIBUTES);

  Station station;
  check("a private window station for the helper and the consumer (their clipboard is not the user's)", station.Create(),
        narrow(station.desktop));
  // R->P: "this PC" has to be a private station too. A Medium process cannot name a window station,
  // and the unnamed one is per logon session -- so it is the SAME station, one clipboard for both
  // helpers (unlike two real PCs). The R->P part therefore starts only after the P->R offer is
  // withdrawn and off that clipboard (below), and this is said, not hidden.
  Station& local = station;

  // ------------------------------------------------------------------ the transport
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);
  SOCKET hostSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  SOCKET viewSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in hostAddr{}, viewAddr{};
  for (auto* a : {&hostAddr, &viewAddr}) {
    a->sin_family = AF_INET;
    a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  }
  bind(hostSock, reinterpret_cast<sockaddr*>(&hostAddr), sizeof(hostAddr));
  bind(viewSock, reinterpret_cast<sockaddr*>(&viewAddr), sizeof(viewAddr));
  int len = sizeof(hostAddr);
  getsockname(hostSock, reinterpret_cast<sockaddr*>(&hostAddr), &len);
  len = sizeof(viewAddr);
  getsockname(viewSock, reinterpret_cast<sockaddr*>(&viewAddr), &len);
  DWORD to = 50;
  int buf = 8 << 20;
  for (SOCKET s : {hostSock, viewSock}) {
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof(to));
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buf), sizeof(buf));
  }
  // Scenario C flips one byte in the payload of one file-chunk datagram on its way to the host.
  std::atomic<int> tamperNext{0};  // > 0: corrupt the N-th data fragment from now
  const auto viewer_send = [&](const void* d, size_t n) {
    std::vector<uint8_t> copy(static_cast<const uint8_t*>(d), static_cast<const uint8_t*>(d) + n);
    if (tamperNext.load() > 0 && n > sizeof(UdpControlChunkHeader) + 64 && bulk_datagram_is_file(d, n)) {
      UdpControlChunkHeader h{};
      std::memcpy(&h, copy.data(), sizeof(h));
      if (h.kind == static_cast<uint16_t>(UdpPacketKind::ControlData) && h.fragIndex > 0 && --tamperNext == 0) {
        copy[copy.size() - 7] ^= 0x5A;  // inside the chunk's bytes, far from any header
      }
    }
    return sendto(viewSock, reinterpret_cast<const char*>(copy.data()), static_cast<int>(copy.size()), 0,
                  reinterpret_cast<const sockaddr*>(&hostAddr), sizeof(hostAddr)) > 0;
  };
  // Scenario G flips one byte of one file-chunk datagram on its way to the viewer.
  std::atomic<int> tamperHostNext{0};
  const auto host_send = [&](const void* d, size_t n) {
    std::vector<uint8_t> copy(static_cast<const uint8_t*>(d), static_cast<const uint8_t*>(d) + n);
    if (tamperHostNext.load() > 0 && n > sizeof(UdpControlChunkHeader) + 64 && bulk_datagram_is_file(d, n)) {
      UdpControlChunkHeader h{};
      std::memcpy(&h, copy.data(), sizeof(h));
      if (h.kind == static_cast<uint16_t>(UdpPacketKind::ControlData) && h.fragIndex > 0 && --tamperHostNext == 0) {
        copy[copy.size() - 7] ^= 0x5A;
      }
    }
    return sendto(hostSock, reinterpret_cast<const char*>(copy.data()), static_cast<int>(copy.size()), 0,
                  reinterpret_cast<const sockaddr*>(&viewAddr), sizeof(viewAddr)) > 0;
  };

  BulkArbiter hostArbiter, viewArbiter;
  HostFileCopyService host;
  HostFileCopyService::Config cfg;
  cfg.enabled = true;
  const std::wstring helperLog = root + L"helper.log";
  // r6 Z1: the test can hold the launcher once the helper is up and has said hello -- the moment a
  // session switch would find "a helper of the old session, started, not yet the channel's".
  std::mutex lpm;
  std::condition_variable lpcv;
  bool launchPark = false, launchParked = false, launchRelease = false;
  std::atomic<DWORD> lastHelperPid{0};  // r7 Z3
  cfg.launcher = [&](fc::HelperLink* link, std::string* why) {
    std::wstring sid;
    if (!fc::current_process_user_sid(&sid)) {
      *why = "no user SID";
      return false;
    }
    const bool up = link->CreateServerPipe(sid, why) &&
                    link->Launch(helperExe, nullptr, station.desktop.c_str(), L"--idle-ms 8000 --log \"" + helperLog + L"\"", why) &&
                    link->AwaitHello(10000, why);
    if (up) lastHelperPid.store(link->helper_pid());
    {
      std::lock_guard<std::mutex> l(lpm);
      std::printf("      launcher: up=%d pid=%lu park=%d\n", up ? 1 : 0, static_cast<unsigned long>(up ? link->helper_pid() : 0), launchPark ? 1 : 0);
    }
    if (up) {
      std::unique_lock<std::mutex> l(lpm);
      if (launchPark) {
        launchPark = false;
        launchParked = true;
        lpcv.notify_all();
        lpcv.wait(l, [&] { return launchRelease; });
      }
    }
    return up;
  };
  host.Start(host_send, 1200, cfg, &hostArbiter, [](const std::string& l) { std::printf("      host: %s\n", l.c_str()); });
  // Before any viewer asks: this copy was on the remote clipboard before the session began.
  const std::wstring preDir = srcDir + L"\\pre";
  CreateDirectoryW(preDir.c_str(), nullptr);
  write_file(preDir + L"\\before.txt", make_content(333, 51));
  uint64_t hostSeq = 500;
  host.OnHostClipboard(hostSeq, {preDir + L"\\before.txt"});
  FileCopyClient viewer;
  const std::wstring viewerHelperLog = root + L"viewer_helper.log";
  std::atomic<DWORD> viewerHelperPid{0};  // r8 V1
  viewer.SetHelperLauncher([&](fc::HelperLink* link, std::string* why) {
    const bool up = fc::launch_file_copy_helper_as_self(helperExe, local.desktop.c_str(),
                                                        L"--idle-ms 8000 --log \"" + viewerHelperLog + L"\"", link, why);
    if (up) viewerHelperPid.store(link->helper_pid());
    return up;
  });
  viewer.Start(viewer_send, [] { return uint64_t{0}; }, [] { return false; }, 1200, BulkRateConfig{}, &viewArbiter,
               [](const std::string& l) { std::printf("      viewer: %s\n", l.c_str()); });
  viewer.SetBulkNegotiated(true);
  viewer.SetHostSupports(host.Advertised());
  check("the host advertises file copy when switched on (Pong 0x800)", host.Advertised());

  std::atomic<bool> stop{false};
  std::thread hostRx([&] {
    std::vector<char> b(2048);
    while (!stop.load()) {
      const int n = recv(hostSock, b.data(), static_cast<int>(b.size()), 0);
      if (n > 0) host.OnDatagram(b.data(), static_cast<size_t>(n));
    }
  });
  std::thread viewRx([&] {
    std::vector<char> b(2048);
    while (!stop.load()) {
      const int n = recv(viewSock, b.data(), static_cast<int>(b.size()), 0);
      if (n > 0) viewer.OnDatagram(b.data(), static_cast<size_t>(n));
    }
  });
  LoopbackLink link(&host, 1);
  std::atomic<bool> linkBroken{false};
  std::atomic<bool> pumpPaused{false};  // r2 ②: hold the viewer's control turns to stage a race
  std::thread pump([&] {
    while (!stop.load()) {
      if (!pumpPaused.load() && viewer.Pump(link) < 0) linkBroken.store(true);
      Sleep(10);
    }
  });
  // A request straight to the host's handler, as another / an older requester would send it.
  const auto call = [&](fn::FileMsg type, const std::vector<uint8_t>& body, uint64_t epoch, std::vector<uint8_t>* reply) {
    uint16_t rt = 0;
    return host.HandleControl(static_cast<uint16_t>(type), body, epoch, &rt, reply);
  };
  const auto nothing_lands = [&](const std::wstring& dest, const std::wstring& name) {
    std::vector<uint8_t> got;
    return !read_file(dest + L"\\" + name, &got);
  };

  uint64_t revision = 100;
  std::vector<DWORD> ourPids = {GetCurrentProcessId()};  // this test, its helpers, its consumers
  const auto paste_on = [&](const Station& where, const std::wstring& dest, uint64_t expectBytes, int waitSec,
                            Child* consumer, const wchar_t* mode = L"drop") {
    CreateDirectoryW(dest.c_str(), nullptr);
    const std::wstring res = dest + L".result.txt";
    const std::wstring cmd = L"\"" + consumerExe + L"\" --consumer --mode " + std::wstring(mode) + L" --dest \"" + dest + L"\" --result \"" + res +
                             L"\" --expect-bytes " + std::to_wstring(expectBytes) + L" --wait-sec " + std::to_wstring(waitSec);
    const bool started = consumer->Start(cmd, where.desktop);
    ourPids.push_back(consumer->pi.dwProcessId);
    std::printf("      consumer pid=%lu\n", static_cast<unsigned long>(consumer->pi.dwProcessId));
    return started;
  };
  // P->R pastes happen on the remote station, R->P pastes on this PC's.
  const auto paste = [&](const std::wstring& dest, uint64_t expectBytes, int waitSec, Child* consumer) {
    return paste_on(station, dest, expectBytes, waitSec, consumer);
  };
  const auto same_files = [&](const std::wstring& dest, const std::map<std::wstring, std::vector<uint8_t>>& expect, std::string* detail) {
    bool ok = true;
    for (const auto& [name, content] : expect) {
      std::vector<uint8_t> got;
      const bool read = read_file(dest + L"\\" + name, &got);
      ok = ok && read && got == content;
      *detail += narrow(name) + (read ? (got == content ? "=same " : "=DIFFERENT(" + std::to_string(got.size()) + ") ") : "=missing ");
    }
    return ok;
  };

  // ------------------------------------------------------------------ A. three files, pasted
  std::printf("\n--- A. P->R: three files copied here, pasted there ---\n");
  std::map<std::wstring, std::vector<uint8_t>> setA = {
      {L"alpha.bin", make_content(300 * 1024 + 13, 11)},   // two helper reads, several chunks
      {L"사진 b.txt", make_content(5, 12)},
      {L"gamma.dat", make_content(2 * 1024 * 1024 + 1, 13)},
  };
  std::vector<std::wstring> pathsA;
  uint64_t totalA = 0;
  for (const auto& [name, content] : setA) {
    pathsA.push_back(srcDir + L"\\" + name);
    write_file(pathsA.back(), content);
    totalA += content.size();
  }
  viewer.SubmitLocalFiles(pathsA, ++revision);
  const bool accepted = wait_until([&] { return viewer.GetCounters().offersAccepted == 1; }, 20000);
  check("the offer reached the helper's clipboard (published)", accepted,
        "hostOffers=" + std::to_string(host.GetCounters().offersAccepted) + " launches=" + std::to_string(host.GetCounters().helperLaunches));
  {
    Child consumer;
    const std::wstring dest = root + L"destA";
    check("the paste consumer starts on the station", paste(dest, totalA, 60, &consumer));
    const DWORD code = consumer.Wait(90000);
    check("the consumer finished (EndOperation) with exit 0", code == 0, "exit=" + std::to_string(code));
    std::string detail;
    check("EVERY FILE LANDED BYTE-FOR-BYTE AS THE SOURCE", same_files(dest, setA, &detail), detail);
    const bool viewerSawEnd = wait_until([&] { return viewer.GetCounters().pastesEnded == 1 && !viewer.PasteActive(); }, 5000);
    const auto hc = host.GetCounters();
    const auto vc = viewer.GetCounters();
    check("the host saw the paste end completed", hc.pastesEnded == 1 && hc.lastEndReason == static_cast<uint8_t>(fn::PasteEndReason::Completed),
          "ended=" + std::to_string(hc.pastesEnded) + " failed=" + std::to_string(hc.pastesFailed) + " reason=" + std::to_string(hc.lastEndReason));
    check("...and the viewer learned it (700 ms query), unpinned, and gave the bulk back", viewerSawEnd &&
              viewArbiter.use() == BulkUse::Idle && hostArbiter.use() == BulkUse::Idle);
    // A consumer may read a range again (legal): every delivered byte was checked, and at least the
    // whole set was delivered -- the landed bytes are compared above (r2 ⑧).
    check("every delivered byte passed its chunk check, none rejected",
          hc.chunksRejected == 0 && hc.chunksVerified > 0 && hc.bytesDelivered >= totalA,
          "verified=" + std::to_string(hc.chunksVerified) + " delivered=" + std::to_string(hc.bytesDelivered) + "/" + std::to_string(totalA));
    std::printf("      files whole-file verified: %llu, chunk verified: %llu\n", static_cast<unsigned long long>(hc.filesWholeVerified),
                static_cast<unsigned long long>(hc.filesChunkVerified));
    check("the viewer served every chunk it was asked for", vc.pullsRefused == 0 && vc.bytesServed >= totalA,
          "served=" + std::to_string(vc.bytesServed));
  }

  // ------------------------------------------------------------------ C. a corrupted chunk
  std::printf("\n--- C. one byte flipped on the wire: that Read fails, nothing unchecked is delivered ---\n");
  {
    std::map<std::wstring, std::vector<uint8_t>> setC = {{L"tamper.bin", make_content(700 * 1024, 21)}};
    const std::wstring path = srcDir + L"\\tamper.bin";
    write_file(path, setC.begin()->second);
    const uint64_t acceptedBefore = viewer.GetCounters().offersAccepted;
    viewer.SubmitLocalFiles({path}, ++revision);
    wait_until([&] { return viewer.GetCounters().offersAccepted > acceptedBefore; }, 20000);
    const auto before = host.GetCounters();
    tamperNext.store(40);
    Child consumer;
    const std::wstring dest = root + L"destC";
    paste(dest, setC.begin()->second.size(), 30, &consumer);
    consumer.Wait(60000);
    wait_until([&] { return host.GetCounters().pastesFailed > before.pastesFailed; }, 15000);
    const auto hc = host.GetCounters();
    std::vector<uint8_t> got;
    const bool landed = read_file(dest + L"\\tamper.bin", &got);
    check("the corrupted chunk was rejected by its SHA-256", hc.chunksRejected > before.chunksRejected,
          std::to_string(hc.chunksRejected - before.chunksRejected) + " rejected");
    check("...its Read failed and the paste ended failed (verification)",
          hc.readsFailed > before.readsFailed && hc.pastesFailed > before.pastesFailed,
          "reason=" + std::to_string(hc.lastEndReason));
    check("...and no complete copy with different content exists at the destination", !landed || got != setC.begin()->second
              ? (!landed || got.size() != setC.begin()->second.size())
              : false,
          landed ? "partial " + std::to_string(got.size()) + " bytes (the shell's own cleanup decides; not guaranteed)" : "absent");
    wait_until([&] { return !viewer.PasteActive(); }, 5000);
  }

  // ------------------------------------------------------------------ D. a new copy mid-paste
  std::printf("\n--- D. a new copy while a paste runs: the new offer is the future, the running paste finishes ---\n");
  {
    std::map<std::wstring, std::vector<uint8_t>> setD = {{L"big.bin", make_content(16u * 1024u * 1024u, 41)}};
    const std::wstring bigPath = srcDir + L"\\big.bin";
    write_file(bigPath, setD.begin()->second);
    const uint64_t acceptedBefore = viewer.GetCounters().offersAccepted;
    viewer.SubmitLocalFiles({bigPath}, ++revision);
    wait_until([&] { return viewer.GetCounters().offersAccepted > acceptedBefore; }, 20000);
    const auto before = host.GetCounters();
    Child consumer;
    const std::wstring dest = root + L"destD";
    check("the paste of the big file starts", paste(dest, setD.begin()->second.size(), 120, &consumer));
    const bool moving = wait_until([&] { return host.GetCounters().bytesDelivered > before.bytesDelivered + 1024 * 1024; }, 30000);
    check("...bytes are moving (over 1 MiB delivered)", moving);
    // The user copies something else on the viewer's PC while the paste runs.
    const std::wstring nextPath = srcDir + L"\\next.txt";
    std::map<std::wstring, std::vector<uint8_t>> setNext = {{L"next.txt", make_content(4096, 42)}};
    write_file(nextPath, setNext.begin()->second);
    const uint64_t acceptedMid = viewer.GetCounters().offersAccepted;
    viewer.SubmitLocalFiles({nextPath}, ++revision);
    const bool newOffer = wait_until([&] { return viewer.GetCounters().offersAccepted > acceptedMid; }, 20000);
    check("the new copy became the remote clipboard's offer (published) during the paste", newOffer);
    check("...while the running paste is still running", viewer.PasteActive());
    const DWORD code = consumer.Wait(180000);
    check("the running paste finished (exit 0)", code == 0, "exit=" + std::to_string(code));
    std::string detail;
    check("THE BIG FILE LANDED WHOLE -- the new copy did not cut it", same_files(dest, setD, &detail), detail);
    check("...and the host counts it completed, not failed",
          host.GetCounters().pastesEnded == before.pastesEnded + 1 && host.GetCounters().pastesFailed == before.pastesFailed);
    wait_until([&] { return !viewer.PasteActive(); }, 5000);
    // And the new offer is what a paste now gets.
    Child next;
    const std::wstring destNext = root + L"destNext";
    paste(destNext, 4096, 30, &next);
    const DWORD codeNext = next.Wait(60000);
    std::string detailNext;
    check("a paste after it gets the NEW copy", codeNext == 0 && same_files(destNext, setNext, &detailNext), detailNext);
    wait_until([&] { return !viewer.PasteActive(); }, 5000);
  }

  // ------------------------------------------------------------------ F. the image holds the bulk
  std::printf("\n--- F. an image holds this session's bulk: the paste is refused before any byte ---\n");
  {
    const std::wstring path = srcDir + L"\\busy.txt";
    write_file(path, make_content(1000, 31));
    const uint64_t acceptedBefore = viewer.GetCounters().offersAccepted;
    viewer.SubmitLocalFiles({path}, ++revision);
    wait_until([&] { return viewer.GetCounters().offersAccepted > acceptedBefore; }, 20000);
    check("an image transfer takes the viewer's bulk", viewArbiter.TryAcquire(BulkUse::Image, 777));
    const auto before = viewer.GetCounters();
    Child consumer;
    const std::wstring dest = root + L"destF";
    paste(dest, 1000, 15, &consumer);
    consumer.Wait(30000);
    wait_until([&] { return viewer.GetCounters().pastesBusy > before.pastesBusy; }, 5000);
    std::vector<uint8_t> got;
    check("the paste was refused as Busy (never swapped in)", viewer.GetCounters().pastesBusy > before.pastesBusy);
    check("...nothing landed", !read_file(dest + L"\\busy.txt", &got) || got.empty());
    check("...and the image still holds the bulk", viewArbiter.use() == BulkUse::Image && viewArbiter.owner() == 777);
    viewArbiter.Release(777);
  }

  // ================================================================== r2 counterexamples (P->R)
  std::printf("\n--- K (r2 1). a TEXT / IMAGE copy on the viewer's PC while a paste runs: the paste finishes ---\n");
  {
    // A copy of anything but files is ClearLocalOffer (viewer_window_proc.cpp: text and image alike).
    std::map<std::wstring, std::vector<uint8_t>> setK = {{L"bigK.bin", make_content(16u * 1024u * 1024u, 101)}};
    const std::wstring bigPath = srcDir + L"\\bigK.bin";
    write_file(bigPath, setK.begin()->second);
    const uint64_t acceptedBefore = viewer.GetCounters().offersAccepted;
    viewer.SubmitLocalFiles({bigPath}, ++revision);
    wait_until([&] { return viewer.GetCounters().offersAccepted > acceptedBefore; }, 20000);
    const auto before = host.GetCounters();
    Child consumer;
    const std::wstring dest = root + L"destK";
    paste(dest, setK.begin()->second.size(), 120, &consumer);
    check("...bytes are moving", wait_until([&] { return host.GetCounters().bytesDelivered > before.bytesDelivered + 1024 * 1024; }, 30000));
    viewer.ClearLocalOffer();  // the user copies text (or an image) on the viewer's PC
    Sleep(1500);               // the withdrawal (End, 73) goes out and reaches the helper
    check("...the running paste is still running after the withdrawal", viewer.PasteActive());
    const DWORD code = consumer.Wait(180000);
    std::string detail;
    check("THE PASTE FINISHED WHOLE -- withdrawing the offer did not abort it", code == 0 && same_files(dest, setK, &detail), detail);
    check("...and the host counts it completed", wait_until([&] {
            return host.GetCounters().pastesEnded == before.pastesEnded + 1 && host.GetCounters().pastesFailed == before.pastesFailed;
          }, 5000));
    wait_until([&] { return !viewer.PasteActive(); }, 5000);
    Child next;
    const std::wstring destNext = root + L"destKNext";
    paste(destNext, 1, 8, &next);
    next.Wait(30000);
    check("...but the NEXT paste gets nothing (the withdrawn offer is off the remote clipboard)", nothing_lands(destNext, L"bigK.bin"));
  }

  std::printf("\n--- L (r2 2). a stale Prepare / End / Status with another id leaves the real paste alone ---\n");
  {
    const std::wstring path = srcDir + L"\\raceL.bin";
    std::map<std::wstring, std::vector<uint8_t>> setL = {{L"raceL.bin", make_content(3u * 1024u * 1024u, 111)}};
    write_file(path, setL.begin()->second);
    const uint64_t acceptedBefore = viewer.GetCounters().offersAccepted;
    viewer.SubmitLocalFiles({path}, ++revision);
    wait_until([&] { return viewer.GetCounters().offersAccepted > acceptedBefore; }, 20000);
    const auto before = host.GetCounters();
    pumpPaused.store(true);  // the viewer does not prepare yet: the paste stays "begun" on the host
    Child consumer;
    const std::wstring dest = root + L"destL";
    paste(dest, setL.begin()->second.size(), 60, &consumer);
    check("the paste began on the host", wait_until([&] { return host.GetCounters().pastesBegun > before.pastesBegun; }, 20000));
    // The begun paste's own ids: while it is begun the host names them in any PasteQuery answer.
    std::vector<uint8_t> raw;
    fn::PasteQueryReply begun;
    check("the host names the begun paste (its offer and paste op)",
          call(fn::FileMsg::PasteQuery, fn::body(fn::PasteQuery{0}), 1, &raw) && fn::parse(raw, &begun) &&
              begun.state == fn::PasteState::Begun && begun.pasteOp != 0);
    fn::Prepare stale;
    stale.direction = fn::Direction::PtoR;
    stale.offerId = 0x5A1E;
    stale.pasteOp = 0x5A1E;
    fn::PrepareReply pr;
    raw.clear();
    check("a stale Prepare of another offer is refused (UnknownId)",
          call(fn::FileMsg::Prepare, fn::body(stale), 1, &raw) && fn::parse(raw, &pr) && pr.verdict == fn::Verdict::UnknownId);
    pumpPaused.store(false);  // now the real Prepare
    check("...and the real one still succeeds: the paste is prepared",
          wait_until([&] { return host.GetCounters().pastesPrepared > before.pastesPrepared; }, 10000));
    // While it runs: an End and a Status with the RIGHT paste op and another offer.
    fn::End wrong{0xBAD0FFE4, begun.pasteOp, fn::PasteEndReason::Cancelled};
    fn::EndReply er;
    raw.clear();
    check("an End with the right paste op but another offer ends nothing",
          call(fn::FileMsg::End, fn::body(wrong), 1, &raw) && fn::parse(raw, &er) && er.state == fn::PasteState::None);
    fn::StatusReply sr;
    raw.clear();
    check("...a Status like it describes nothing",
          call(fn::FileMsg::Status, fn::body(fn::StatusQuery{0xBAD0FFE4, begun.pasteOp}), 1, &raw) && fn::parse(raw, &sr) &&
              sr.state == fn::PasteState::None);
    raw.clear();
    check("...while the right key still sees it running",
          call(fn::FileMsg::Status, fn::body(fn::StatusQuery{begun.offerId, begun.pasteOp}), 1, &raw) && fn::parse(raw, &sr) &&
              sr.state == fn::PasteState::Active);
    const DWORD code = consumer.Wait(90000);
    std::string detail;
    check("THE PASTE FINISHED WHOLE", code == 0 && same_files(dest, setL, &detail), detail);
    // (the refused stale Prepare counts as a failed prepare; the real paste is the completion)
    check("...and completed on the host (not cancelled by the foreign End)", wait_until([&] {
            const auto hc = host.GetCounters();
            return hc.pastesEnded == before.pastesEnded + 1 && hc.lastEndReason == static_cast<uint8_t>(fn::PasteEndReason::Completed);
          }, 5000));
    wait_until([&] { return !viewer.PasteActive(); }, 5000);
  }

  std::printf("\n--- N (r2 4). the viewer's switch goes OFF mid-paste: no file byte after it, the paste ends Disabled ---\n");
  {
    std::map<std::wstring, std::vector<uint8_t>> setN = {{L"bigN.bin", make_content(16u * 1024u * 1024u, 121)}};
    const std::wstring bigPath = srcDir + L"\\bigN.bin";
    write_file(bigPath, setN.begin()->second);
    const uint64_t acceptedBefore = viewer.GetCounters().offersAccepted;
    viewer.SubmitLocalFiles({bigPath}, ++revision);
    wait_until([&] { return viewer.GetCounters().offersAccepted > acceptedBefore; }, 20000);
    const auto before = host.GetCounters();
    Child consumer;
    const std::wstring dest = root + L"destN";
    paste(dest, setN.begin()->second.size(), 60, &consumer);
    check("...bytes are moving", wait_until([&] { return host.GetCounters().bytesDelivered > before.bytesDelivered + 1024 * 1024; }, 30000));
    viewer.SetAllowed(false);
    const uint64_t servedAt = viewer.GetCounters().bytesServed;
    Sleep(2000);
    check("NOT ONE FILE BYTE IS SERVED AFTER THE SWITCH WENT OFF", viewer.GetCounters().bytesServed == servedAt,
          std::to_string(servedAt) + " -> " + std::to_string(viewer.GetCounters().bytesServed));
    check("...the pins are released and the bulk is free on the viewer", !viewer.PasteActive() && viewArbiter.use() == BulkUse::Idle);
    check("...the host was told: the paste ended Disabled", wait_until([&] {
            const auto hc = host.GetCounters();
            return hc.pastesFailed > before.pastesFailed && hc.lastEndReason == static_cast<uint8_t>(fn::PasteEndReason::Disabled);
          }, 5000), "reason=" + std::to_string(host.GetCounters().lastEndReason));
    consumer.Wait(60000);
    std::vector<uint8_t> got;
    check("...and no complete copy landed", !read_file(dest + L"\\bigN.bin", &got) || got != setN.begin()->second);
    viewer.SetAllowed(true);
    wait_until([&] { return hostArbiter.use() == BulkUse::Idle; }, 5000);
  }

  std::printf("\n--- N2 (r2 4). the HOST's switch goes OFF mid-paste: nothing more reaches its helper ---\n");
  {
    std::map<std::wstring, std::vector<uint8_t>> setN2 = {{L"bigN2.bin", make_content(16u * 1024u * 1024u, 131)}};
    const std::wstring bigPath = srcDir + L"\\bigN2.bin";
    write_file(bigPath, setN2.begin()->second);
    const uint64_t acceptedBefore = viewer.GetCounters().offersAccepted;
    viewer.SubmitLocalFiles({bigPath}, ++revision);
    wait_until([&] { return viewer.GetCounters().offersAccepted > acceptedBefore; }, 20000);
    const auto before = host.GetCounters();
    Child consumer;
    const std::wstring dest = root + L"destN2";
    paste(dest, setN2.begin()->second.size(), 60, &consumer);
    check("...bytes are moving", wait_until([&] { return host.GetCounters().bytesDelivered > before.bytesDelivered + 1024 * 1024; }, 30000));
    host.SetEnabled(false);
    const uint64_t deliveredAt = host.GetCounters().bytesDelivered;
    Sleep(2000);
    const auto hc = host.GetCounters();
    check("NOT ONE BYTE REACHES THE HOST'S HELPER AFTER ITS SWITCH WENT OFF", hc.bytesDelivered == deliveredAt,
          std::to_string(deliveredAt) + " -> " + std::to_string(hc.bytesDelivered));
    check("...the paste ended Disabled and the host no longer advertises file copy",
          hc.pastesFailed > before.pastesFailed && hc.lastEndReason == static_cast<uint8_t>(fn::PasteEndReason::Disabled) && !host.Advertised());
    consumer.Wait(60000);
    host.SetEnabled(true);
    check("...the viewer learns it and lets go", wait_until([&] { return !viewer.PasteActive() && viewArbiter.use() == BulkUse::Idle; }, 10000));
  }

  std::printf("\n--- O (r2 5). a copy of 101 files: refused whole, and the older offer is withdrawn ---\n");
  {
    const std::wstring path = srcDir + L"\\oldO.txt";
    write_file(path, make_content(2000, 141));
    const uint64_t acceptedBefore = viewer.GetCounters().offersAccepted;
    viewer.SubmitLocalFiles({path}, ++revision);
    check("an ordinary offer is live on the remote PC", wait_until([&] { return viewer.GetCounters().offersAccepted > acceptedBefore; }, 20000));
    const std::wstring manyDir = srcDir + L"\\many";
    CreateDirectoryW(manyDir.c_str(), nullptr);
    std::vector<std::wstring> many;
    many.push_back(manyDir);  // a folder first: left out if filtering came before the limit
    many.push_back(manyDir);
    for (int i = 0; i < 99; ++i) {
      many.push_back(manyDir + L"\\f" + std::to_wstring(i) + L".txt");
      write_file(many.back(), make_content(10, 150 + i));
    }
    const auto vBefore = viewer.GetCounters();
    viewer.SubmitLocalFiles(many, ++revision);  // 101 names, 99 of them eligible files
    Sleep(1500);
    const auto vc = viewer.GetCounters();
    check("THE COPY OF 101 IS REFUSED WHOLE (TooMany), NOT OFFERED AS ITS 99 ELIGIBLE FILES",
          vc.offersSent == vBefore.offersSent && vc.lastVerdict == static_cast<uint8_t>(fn::Verdict::TooMany),
          "sent " + std::to_string(vBefore.offersSent) + " -> " + std::to_string(vc.offersSent) + " verdict=" + std::to_string(vc.lastVerdict));
    Child next;
    const std::wstring destO = root + L"destO";
    paste(destO, 1, 8, &next);
    next.Wait(30000);
    check("...and the OLDER offer is off the remote clipboard (a paste gets nothing)", nothing_lands(destO, L"oldO.txt"));
  }


  // D5: a scripted image holding the viewer's bulk. preempt() is what ClipImageClient does (asks it
  // to stop); here it "confirms its end" by releasing the bulk after `releaseAfterMs` (never, if 0).
  // after(mayResume) is recorded. Installed with the pump held, so the pump never races the setter.
  struct FakeImage {
    std::atomic<int> preempts{0}, afters{0}, lastMayResume{-1};
    std::atomic<uint32_t> releaseAfterMs{0};
    uint64_t owner = 0;
    std::thread releaser;
    ~FakeImage() {
      if (releaser.joinable()) releaser.join();
    }
  };
  FakeImage fakeImage;
  const auto install_fake_image = [&] {
    pumpPaused.store(true);
    Sleep(50);
    viewer.SetImagePreemption(
        [&] {
          ++fakeImage.preempts;
          const uint32_t ms = fakeImage.releaseAfterMs.load();
          if (ms != 0) {
            if (fakeImage.releaser.joinable()) fakeImage.releaser.join();
            fakeImage.releaser = std::thread([&, ms] {
              Sleep(ms);
              viewArbiter.Release(fakeImage.owner);  // the host confirmed the image's end
            });
          }
          return true;
        },
        [&](bool mayResume) {
          ++fakeImage.afters;
          fakeImage.lastMayResume.store(mayResume ? 1 : 0);
        });
    pumpPaused.store(false);
  };

  // ================================================================== step 3 counterexamples
  // A Seek / repeated-read consumer: every piece it read is the source's bytes at that offset, the
  // second stream reads the whole file, and the host counts it "chunk verified", not whole-range.
  const auto seek_ok = [&](const std::wstring& dest, const std::vector<uint8_t>& src, std::string* detail) {
    bool ok = true;
    int pieces = 0;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dest + L"\\seek_*.bin").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
      do {
        const uint64_t off = _wcstoui64(fd.cFileName + 5, nullptr, 10);
        std::vector<uint8_t> got;
        read_file(dest + L"\\" + fd.cFileName, &got);
        const bool same = off + got.size() <= src.size() && !got.empty() &&
                          std::equal(got.begin(), got.end(), src.begin() + static_cast<std::ptrdiff_t>(off));
        ok = ok && same;
        ++pieces;
        *detail += "@" + std::to_string(off) + "+" + std::to_string(got.size()) + (same ? "=same " : "=DIFFERENT ");
      } while (FindNextFileW(h, &fd));
      FindClose(h);
    }
    std::vector<uint8_t> full;
    const bool fullOk = read_file(dest + L"\\full.bin", &full) && full == src;
    *detail += fullOk ? "full=same" : "full=DIFFERENT(" + std::to_string(full.size()) + ")";
    return ok && pieces == 4 && fullOk;
  };

  std::printf("\n--- S1 (step 3). P->R: a consumer that Seeks and reads the same file twice ---\n");
  {
    const auto content = make_content(1024 * 1024 + 333, 201);
    const std::wstring path = srcDir + L"\\seekP.bin";
    write_file(path, content);
    const uint64_t acceptedBefore = viewer.GetCounters().offersAccepted;
    viewer.SubmitLocalFiles({path}, ++revision);
    wait_until([&] { return viewer.GetCounters().offersAccepted > acceptedBefore; }, 20000);
    const auto before = host.GetCounters();
    Child consumer;
    const std::wstring dest = root + L"destS1";
    paste_on(station, dest, 0, 30, &consumer, L"seek");
    const DWORD code = consumer.Wait(60000);
    std::string detail;
    const bool piecesOk = code == 0 && seek_ok(dest, content, &detail);
    check("every piece read after a Seek (forward, back, from the end) and the second full read are the source's bytes",
          piecesOk, "exit=" + std::to_string(code) + " " + detail);
    check("...the paste completed and is counted 'chunk verified', not whole-range", wait_until([&] {
            const auto hc = host.GetCounters();
            return hc.pastesEnded > before.pastesEnded && hc.filesChunkVerified > before.filesChunkVerified &&
                   hc.chunksRejected == before.chunksRejected;
          }, 5000), "chunkVerified " + std::to_string(before.filesChunkVerified) + " -> " +
                        std::to_string(host.GetCounters().filesChunkVerified));
    wait_until([&] { return !viewer.PasteActive(); }, 5000);
  }

  std::printf("\n--- S2 (step 3). P->R: the source file is REPLACED between the copy and the paste ---\n");
  {
    const std::wstring path = srcDir + L"\\replaceP.bin";
    write_file(path, make_content(300000, 211));
    const uint64_t acceptedBefore = viewer.GetCounters().offersAccepted;
    viewer.SubmitLocalFiles({path}, ++revision);
    wait_until([&] { return viewer.GetCounters().offersAccepted > acceptedBefore; }, 20000);
    DeleteFileW(path.c_str());
    write_file(path, make_content(300000, 212));  // same name and size, another file (FileId)
    const auto vBefore = viewer.GetCounters();
    Child consumer;
    const std::wstring dest = root + L"destS2";
    paste(dest, 300000, 10, &consumer);
    consumer.Wait(30000);
    check("the paste is refused before any byte (the pin sees another FileId)",
          wait_until([&] { return viewer.GetCounters().pastesFailed > vBefore.pastesFailed; }, 5000) &&
              viewer.GetCounters().bytesServed == vBefore.bytesServed);
    check("...nothing landed", nothing_lands(dest, L"replaceP.bin"));
    check("...and the viewer's bulk is free", viewArbiter.use() == BulkUse::Idle && !viewer.PasteActive());
  }

  std::printf("\n--- S3 (step 3). P->R: the paste consumer DIES mid-paste: both ends let go, bounded ---\n");
  {
    const auto content = make_content(16u * 1024u * 1024u, 221);
    const std::wstring path = srcDir + L"\\deathP.bin";
    write_file(path, content);
    const uint64_t acceptedBefore = viewer.GetCounters().offersAccepted;
    viewer.SubmitLocalFiles({path}, ++revision);
    wait_until([&] { return viewer.GetCounters().offersAccepted > acceptedBefore; }, 20000);
    const auto before = host.GetCounters();
    Child consumer;
    const std::wstring dest = root + L"destS3";
    paste(dest, content.size(), 120, &consumer);
    check("...bytes are moving", wait_until([&] { return host.GetCounters().bytesDelivered > before.bytesDelivered + 1024 * 1024; }, 30000));
    TerminateProcess(consumer.pi.hProcess, 99);  // the consumer is killed
    WaitForSingleObject(consumer.pi.hProcess, 5000);
    const auto t0 = GetTickCount64();
    const bool ended = wait_until([&] {
      const auto hc = host.GetCounters();
      return hc.pastesFailed > before.pastesFailed && !viewer.PasteActive() && viewArbiter.use() == BulkUse::Idle &&
             hostArbiter.use() == BulkUse::Idle;
    }, 40000);
    check("both ends end the paste and free the bulk within the helper's idle bound (8 s here) + slack", ended,
          std::to_string((GetTickCount64() - t0) / 1000) + " s, reason=" + std::to_string(host.GetCounters().lastEndReason));
  }

  std::printf("\n--- Q1 (step 3, D5). P->R paste while an IMAGE holds the bulk: the image is stopped, the paste waits for its confirmed end ---\n");
  install_fake_image();
  {
    const auto content = make_content(2u * 1024u * 1024u + 5, 261);
    const std::wstring path = srcDir + L"\\d5P.bin";
    write_file(path, content);
    const uint64_t acceptedBefore = viewer.GetCounters().offersAccepted;
    viewer.SubmitLocalFiles({path}, ++revision);
    wait_until([&] { return viewer.GetCounters().offersAccepted > acceptedBefore; }, 20000);
    fakeImage.owner = 0xD5D5;
    check("an image holds the viewer's bulk", viewArbiter.TryAcquire(BulkUse::Image, fakeImage.owner));
    fakeImage.releaseAfterMs.store(1500);
    const int preemptsBefore = fakeImage.preempts.load(), aftersBefore = fakeImage.afters.load();
    Child consumer;
    const std::wstring dest = root + L"destQ1";
    paste(dest, content.size(), 60, &consumer);
    const DWORD code = consumer.Wait(90000);
    std::map<std::wstring, std::vector<uint8_t>> setQ = {{L"d5P.bin", content}};
    std::string detail;
    check("the image was asked to stop (once)", fakeImage.preempts.load() == preemptsBefore + 1);
    check("THE PASTE WAITED FOR THE IMAGE'S END, THEN COMPLETED WHOLE", code == 0 && same_files(dest, setQ, &detail), detail);
    check("...and the image was told the paste is over, and may go again", wait_until([&] {
            return fakeImage.afters.load() == aftersBefore + 1 && fakeImage.lastMayResume.load() == 1;
          }, 5000));
    wait_until([&] { return !viewer.PasteActive(); }, 5000);
  }

  std::printf("\n--- Q2 (step 3, D5). the image never lets go: the paste is refused within the budget, before any byte ---\n");
  {
    const std::wstring path = srcDir + L"\\d5P2.bin";
    write_file(path, make_content(300000, 271));
    const uint64_t acceptedBefore = viewer.GetCounters().offersAccepted;
    viewer.SubmitLocalFiles({path}, ++revision);
    wait_until([&] { return viewer.GetCounters().offersAccepted > acceptedBefore; }, 20000);
    fakeImage.owner = 0xD5D6;
    check("an image holds the viewer's bulk (and will not let go)", viewArbiter.TryAcquire(BulkUse::Image, fakeImage.owner));
    fakeImage.releaseAfterMs.store(0);
    const auto vBefore = viewer.GetCounters();
    const int aftersBefore = fakeImage.afters.load();
    const auto t0 = GetTickCount64();
    Child consumer;
    const std::wstring dest = root + L"destQ2";
    paste(dest, 300000, 15, &consumer);
    const bool refused = wait_until([&] { return viewer.GetCounters().pastesBusy > vBefore.pastesBusy; }, 15000);
    const auto secs = (GetTickCount64() - t0) / 1000.0;
    check("the paste is refused as busy after the switch budget (7 s), inside the consumer's 10 s",
          refused && secs >= 6.0 && secs <= 10.5, std::to_string(secs) + " s");
    consumer.Wait(30000);
    check("...nothing landed, and no file byte was served", nothing_lands(dest, L"d5P2.bin") &&
                                                         viewer.GetCounters().bytesServed == vBefore.bytesServed);
    check("...the image still holds its bulk, and was told the paste is over", viewArbiter.owner() == fakeImage.owner &&
                                                                              fakeImage.afters.load() == aftersBefore + 1);
    viewArbiter.Release(fakeImage.owner);
  }

  std::printf("\n--- A2a (r3). P->R: the consumer COMPLETES, then a late Cancel: shown as completed, not cancelled ---\n");
  {
    const auto content = make_content(8u * 1024u * 1024u + 3, 291);
    const std::wstring path = srcDir + L"\\lateCancel.bin";
    write_file(path, content);
    const uint64_t acceptedBefore = viewer.GetCounters().offersAccepted;
    viewer.SubmitLocalFiles({path}, ++revision);
    wait_until([&] { return viewer.GetCounters().offersAccepted > acceptedBefore; }, 20000);
    const auto hBefore = host.GetCounters();
    Child consumer;
    const std::wstring dest = root + L"destA2a";
    paste(dest, content.size(), 60, &consumer);
    const bool running = wait_until([&] { return viewer.PasteActive(); }, 20000);
    pumpPaused.store(true);  // the viewer does not learn the end by its 700 ms question yet
    const DWORD code = consumer.Wait(90000);
    const bool hostDone = wait_until([&] { return host.GetCounters().pastesEnded > hBefore.pastesEnded; }, 10000);
    check("staged: the consumer completed while the viewer still counts the paste as running",
          running && hostDone && code == 0 && viewer.PasteActive(), "exit=" + std::to_string(code));
    viewer.CancelPaste();  // the user presses Cancel a moment too late
    pumpPaused.store(false);
    const bool ended = wait_until([&] { return !viewer.PasteActive() && !viewer.GetProgress().cancelling; }, 5000);
    const auto p = viewer.GetProgress();
    check("THE LATE CANCEL SHOWS THE REAL END: completed, not cancelled",
          ended && p.lastState == static_cast<uint8_t>(fn::PasteState::Ended) &&
              p.lastReason == static_cast<uint8_t>(fn::PasteEndReason::Completed),
          "state=" + std::to_string(p.lastState) + " reason=" + std::to_string(p.lastReason));
    check("...and the host kept it completed", host.GetCounters().lastEndReason == static_cast<uint8_t>(fn::PasteEndReason::Completed));
  }

  std::printf("\n--- A2b (r3). P->R cancel: another id's EndReply and a held-back answer confirm nothing ---\n");
  {
    const auto content = make_content(48u * 1024u * 1024u, 293);
    const std::wstring path = srcDir + L"\\cancelId.bin";
    write_file(path, content);
    const uint64_t acceptedBefore = viewer.GetCounters().offersAccepted;
    viewer.SubmitLocalFiles({path}, ++revision);
    wait_until([&] { return viewer.GetCounters().offersAccepted > acceptedBefore; }, 20000);
    Child consumer;
    const std::wstring dest = root + L"destA2b";
    paste(dest, content.size(), 60, &consumer);
    const bool running = wait_until([&] { return viewer.PasteActive(); }, 20000);
    link.SetRewrite([](uint16_t type, std::vector<uint8_t>* rb) {
      if (type == static_cast<uint16_t>(fn::FileMsg::EndReply)) {
        fn::EndReply r;
        if (fn::parse(*rb, &r)) {
          r.pasteOp += 1;  // an answer about another paste
          *rb = fn::body(r);
        }
      } else if (type == static_cast<uint16_t>(fn::FileMsg::PasteQueryReply)) {
        fn::PasteQueryReply r;
        if (fn::parse(*rb, &r) && (r.state == fn::PasteState::Ended || r.state == fn::PasteState::Failed)) {
          r.state = fn::PasteState::Active;  // the host's own answer held back for now
          r.reason = fn::PasteEndReason::None;
          *rb = fn::body(r);
        }
      }
    });
    viewer.CancelPaste();
    Sleep(2500);
    const bool stillCancelling = viewer.GetProgress().cancelling && viewer.PasteActive();
    link.SetRewrite(nullptr);
    check("another id's EndReply confirms nothing: still 'cancelling', still running", running && stillCancelling);
    const bool ended = wait_until([&] { return !viewer.PasteActive() && !viewer.GetProgress().cancelling; }, 5000);
    const auto p = viewer.GetProgress();
    check("...then the host's own answer ends it: cancelled", ended && p.lastReason == static_cast<uint8_t>(fn::PasteEndReason::Cancelled),
          "reason=" + std::to_string(p.lastReason));
    consumer.Wait(30000);
    DeleteFileW((dest + L"\\cancelId.bin").c_str());
  }

  // ================================================================== R->P (step 2)
  // One clipboard for both helpers here (see `local`): the viewer's last P->R offer comes off it first.
  {
    viewer.ClearLocalOffer();
    const bool withdrawn = wait_until([&] { return !viewer.PasteActive(); }, 3000);
    Sleep(1500);  // the End (73) on the next turn, the host's ClearRemoteFiles, the helper's clear
    check("the P->R offer was withdrawn before R->P (the test's shared station)", withdrawn);
  }
  std::printf("\n--- B0. a copy made on the remote PC BEFORE the session: never published here ---\n");
  {
    Sleep(2500);  // several 700 ms questions
    const auto vc = viewer.GetCounters();
    check("the viewer asked the host every 700 ms", vc.offerQueries >= 2, "queries=" + std::to_string(vc.offerQueries));
    check("...and published nothing (connecting never replaces this PC's clipboard)",
          vc.remotePublished == 0 && vc.remoteOffersSeen == 0);
  }

  std::printf("\n--- B. R->P: three files copied there, pasted here ---\n");
  std::map<std::wstring, std::vector<uint8_t>> setB = {
      {L"remote one.bin", make_content(900 * 1024 + 7, 61)},
      {L"원격.txt", make_content(17, 62)},
      {L"empty.dat", {}},
      {L"remote big.bin", make_content(3 * 1024 * 1024 + 5, 63)},
  };
  const std::wstring remoteDir = srcDir + L"\\remote";
  CreateDirectoryW(remoteDir.c_str(), nullptr);
  uint64_t totalB = 0;
  std::vector<std::wstring> pathsB;
  for (const auto& [name, content] : setB) {
    pathsB.push_back(remoteDir + L"\\" + name);
    write_file(pathsB.back(), content);
    totalB += content.size();
  }
  {
    const auto before = viewer.GetCounters();
    host.OnHostClipboard(++hostSeq, pathsB);
    const bool published = wait_until([&] { return viewer.GetCounters().remotePublished > before.remotePublished; }, 20000);
    check("the remote copy was identified by the host's helper and published on this PC's clipboard", published,
          "hostOffers=" + std::to_string(host.GetCounters().hostOffers) + " seen=" +
              std::to_string(viewer.GetCounters().remoteOffersSeen));
    Child consumer;
    const std::wstring dest = root + L"destB";
    check("the paste consumer starts on this PC's station", paste_on(local, dest, totalB, 60, &consumer));
    const DWORD code = consumer.Wait(90000);
    check("the consumer finished (EndOperation) with exit 0", code == 0, "exit=" + std::to_string(code));
    std::string detail;
    check("EVERY FILE LANDED BYTE-FOR-BYTE AS THE SOURCE (R->P, an empty file included)", same_files(dest, setB, &detail),
          detail);
    const bool told = wait_until([&] { return host.GetCounters().sendEnded == 1 && hostArbiter.use() == BulkUse::Idle; }, 5000);
    const auto hc = host.GetCounters();
    const auto vc = viewer.GetCounters();
    check("the viewer ended it and told the host (73): completed, the host's bulk is free", told,
          "sendEnded=" + std::to_string(hc.sendEnded) + " sendFailed=" + std::to_string(hc.sendFailed) +
              " reason=" + std::to_string(hc.lastSendEndReason));
    check("...and the viewer's bulk is free", wait_until([&] { return viewArbiter.use() == BulkUse::Idle; }, 2000));
    check("every received byte passed its chunk check, none rejected",
          vc.chunksRejected == 0 && vc.bytesReceived == totalB && vc.recvEnded == 1,
          "received=" + std::to_string(vc.bytesReceived) + "/" + std::to_string(totalB) +
              " rejected=" + std::to_string(vc.chunksRejected));
    std::printf("      files whole-file verified: %llu, chunk verified: %llu\n",
                static_cast<unsigned long long>(vc.filesWholeVerified), static_cast<unsigned long long>(vc.filesChunkVerified));
    check("the host served every byte from its helper's pinned handles", hc.bytesServed >= totalB && hc.localReadFailures == 0,
          "served=" + std::to_string(hc.bytesServed));
  }

  std::printf("\n--- G. R->P: one byte flipped on the wire: that Read fails, nothing unchecked is delivered ---\n");
  {
    const std::wstring path = remoteDir + L"\\tamperR.bin";
    const auto content = make_content(700 * 1024, 71);
    write_file(path, content);
    const auto before = viewer.GetCounters();
    const auto hBefore = host.GetCounters();
    host.OnHostClipboard(++hostSeq, {path});
    wait_until([&] { return viewer.GetCounters().remotePublished > before.remotePublished; }, 20000);
    tamperHostNext.store(40);
    Child consumer;
    const std::wstring dest = root + L"destG";
    paste_on(local, dest, content.size(), 30, &consumer);
    consumer.Wait(60000);
    wait_until([&] { return viewer.GetCounters().recvFailed > before.recvFailed; }, 15000);
    const auto vc = viewer.GetCounters();
    std::vector<uint8_t> got;
    const bool landed = read_file(dest + L"\\tamperR.bin", &got);
    check("the corrupted chunk was rejected by its SHA-256 (on the viewer)", vc.chunksRejected > before.chunksRejected,
          std::to_string(vc.chunksRejected - before.chunksRejected) + " rejected");
    // (Explorer may retry the paste afterwards as a new paste, which then succeeds -- the one the
    // flipped byte hit must have ended Verification on both sides.)
    check("...the paste ended failed (verification) here", vc.recvVerificationEnds > before.recvVerificationEnds,
          "verificationEnds=" + std::to_string(vc.recvVerificationEnds - before.recvVerificationEnds));
    check("...and the host was told the same", wait_until([&] {
            return host.GetCounters().sendVerificationEnds > hBefore.sendVerificationEnds;
          }, 5000));
    check("...and no complete copy with different content exists at the destination",
          !landed || got.size() != content.size() || got == content,
          landed ? "partial " + std::to_string(got.size()) + " bytes" : "absent");
  }

  std::printf("\n--- H. R->P while this session's bulk is taken (the host's): refused before any byte ---\n");
  {
    const std::wstring path = remoteDir + L"\\busyR.txt";
    write_file(path, make_content(1000, 81));
    const auto before = viewer.GetCounters();
    host.OnHostClipboard(++hostSeq, {path});
    wait_until([&] { return viewer.GetCounters().remotePublished > before.remotePublished; }, 20000);
    check("an image transfer takes the host's bulk", hostArbiter.TryAcquire(BulkUse::Image, 888));
    const auto hBefore = host.GetCounters();
    Child consumer;
    const std::wstring dest = root + L"destH";
    paste_on(local, dest, 1000, 15, &consumer);
    consumer.Wait(30000);
    wait_until([&] { return host.GetCounters().sendRefused > hBefore.sendRefused; }, 5000);
    std::vector<uint8_t> got;
    check("the host refused it as Busy (never swapped in)", host.GetCounters().sendRefused > hBefore.sendRefused &&
              host.GetCounters().lastSendVerdict == static_cast<uint8_t>(fn::Verdict::Busy));
    check("...nothing landed", !read_file(dest + L"\\busyR.txt", &got) || got.empty());
    check("...the image still holds the host's bulk", hostArbiter.use() == BulkUse::Image && hostArbiter.owner() == 888);
    check("...and the viewer's bulk is free again", wait_until([&] { return viewArbiter.use() == BulkUse::Idle; }, 3000));
    hostArbiter.Release(888);
  }

  std::printf("\n--- J. R->P: a new copy on the remote PC while a paste runs: the running paste finishes ---\n");
  {
    std::map<std::wstring, std::vector<uint8_t>> setJ = {{L"bigR.bin", make_content(16u * 1024u * 1024u, 91)}};
    const std::wstring bigPath = remoteDir + L"\\bigR.bin";
    write_file(bigPath, setJ.begin()->second);
    const auto before = viewer.GetCounters();
    host.OnHostClipboard(++hostSeq, {bigPath});
    wait_until([&] { return viewer.GetCounters().remotePublished > before.remotePublished; }, 20000);
    Child consumer;
    const std::wstring dest = root + L"destJ";
    check("the R->P paste of the big file starts", paste_on(local, dest, setJ.begin()->second.size(), 120, &consumer));
    const bool moving = wait_until([&] { return viewer.GetCounters().bytesReceived > before.bytesReceived + 1024 * 1024; }, 30000);
    check("...bytes are moving (over 1 MiB received)", moving);
    const std::wstring nextPath = remoteDir + L"\\nextR.txt";
    std::map<std::wstring, std::vector<uint8_t>> setNext = {{L"nextR.txt", make_content(4096, 92)}};
    write_file(nextPath, setNext.begin()->second);
    const auto mid = viewer.GetCounters();
    host.OnHostClipboard(++hostSeq, {nextPath});
    check("the new remote copy was published here during the paste",
          wait_until([&] { return viewer.GetCounters().remotePublished > mid.remotePublished; }, 20000));
    check("...while the running paste is still running", viewer.ReceiveActive());
    const DWORD code = consumer.Wait(180000);
    check("the running paste finished (exit 0)", code == 0, "exit=" + std::to_string(code));
    std::string detail;
    check("THE BIG FILE LANDED WHOLE -- the new copy did not cut it", same_files(dest, setJ, &detail), detail);
    check("...and both ends count it completed", wait_until([&] {
            return viewer.GetCounters().recvEnded == before.recvEnded + 1 && host.GetCounters().sendEnded >= 2;
          }, 5000));
    Child next;
    const std::wstring destNext = root + L"destJNext";
    paste_on(local, destNext, 4096, 30, &next);
    const DWORD codeNext = next.Wait(60000);
    std::string detailNext;
    check("a paste after it gets the NEW copy", codeNext == 0 && same_files(destNext, setNext, &detailNext), detailNext);
  }

  std::printf("\n--- S4 (step 3). R->P: a consumer that Seeks and reads the same file twice ---\n");
  {
    const auto content = make_content(1024 * 1024 + 777, 231);
    const std::wstring path = remoteDir + L"\\seekR.bin";
    write_file(path, content);
    const auto before = viewer.GetCounters();
    host.OnHostClipboard(++hostSeq, {path});
    wait_until([&] { return viewer.GetCounters().remotePublished > before.remotePublished; }, 20000);
    Child consumer;
    const std::wstring dest = root + L"destS4";
    paste_on(local, dest, 0, 30, &consumer, L"seek");
    const DWORD code = consumer.Wait(60000);
    std::string detail;
    const bool piecesOk = code == 0 && seek_ok(dest, content, &detail);
    check("R->P: every piece after a Seek and the second full read are the source's bytes",
          piecesOk, "exit=" + std::to_string(code) + " " + detail);
    check("...completed and counted 'chunk verified' on the viewer", wait_until([&] {
            const auto vc = viewer.GetCounters();
            return vc.recvEnded > before.recvEnded && vc.filesChunkVerified > before.filesChunkVerified;
          }, 5000));
  }

  std::printf("\n--- S5 (step 3). R->P: the source file is REPLACED between the copy and the paste ---\n");
  {
    const std::wstring path = remoteDir + L"\\replaceR.bin";
    write_file(path, make_content(300000, 241));
    const auto before = viewer.GetCounters();
    host.OnHostClipboard(++hostSeq, {path});
    wait_until([&] { return viewer.GetCounters().remotePublished > before.remotePublished; }, 20000);
    DeleteFileW(path.c_str());
    write_file(path, make_content(300000, 242));
    const auto hBefore = host.GetCounters();
    Child consumer;
    const std::wstring dest = root + L"destS5";
    paste_on(local, dest, 300000, 10, &consumer);
    consumer.Wait(30000);
    check("the host's pin refuses it (another FileId) before any byte",
          wait_until([&] { return host.GetCounters().sendRefused > hBefore.sendRefused; }, 5000) &&
              host.GetCounters().bytesServed == hBefore.bytesServed);
    check("...nothing landed and both ends' bulk is free",
          nothing_lands(dest, L"replaceR.bin") && hostArbiter.use() == BulkUse::Idle && viewArbiter.use() == BulkUse::Idle);
  }

  std::printf("\n--- S6 (step 3). R->P: the paste consumer DIES mid-paste: both ends let go, bounded ---\n");
  {
    const auto content = make_content(16u * 1024u * 1024u, 251);
    const std::wstring path = remoteDir + L"\\deathR.bin";
    write_file(path, content);
    const auto before = viewer.GetCounters();
    host.OnHostClipboard(++hostSeq, {path});
    wait_until([&] { return viewer.GetCounters().remotePublished > before.remotePublished; }, 20000);
    const auto hBefore = host.GetCounters();
    Child consumer;
    const std::wstring dest = root + L"destS6";
    paste_on(local, dest, content.size(), 120, &consumer);
    check("...bytes are moving", wait_until([&] { return viewer.GetCounters().bytesReceived > before.bytesReceived + 1024 * 1024; }, 30000));
    TerminateProcess(consumer.pi.hProcess, 99);
    WaitForSingleObject(consumer.pi.hProcess, 5000);
    const auto t0 = GetTickCount64();
    const bool ended = wait_until([&] {
      return host.GetCounters().sendFailed > hBefore.sendFailed && !viewer.ReceiveActive() &&
             viewArbiter.use() == BulkUse::Idle && hostArbiter.use() == BulkUse::Idle;
    }, 40000);
    check("both ends end it and free the bulk within the helper's idle bound + slack", ended,
          std::to_string((GetTickCount64() - t0) / 1000) + " s, reason=" + std::to_string(host.GetCounters().lastSendEndReason));
  }

  std::printf("\n--- Q3 (step 3, D5). R->P paste while an image holds the bulk; the remote clipboard changes meanwhile ---\n");
  {
    const auto content = make_content(2u * 1024u * 1024u + 9, 281);
    const std::wstring path = remoteDir + L"\\d5R.bin";
    write_file(path, content);
    const auto before = viewer.GetCounters();
    host.OnHostClipboard(++hostSeq, {path});
    wait_until([&] { return viewer.GetCounters().remotePublished > before.remotePublished; }, 20000);
    fakeImage.owner = 0xD5D7;
    check("an image holds the viewer's bulk", viewArbiter.TryAcquire(BulkUse::Image, fakeImage.owner));
    fakeImage.releaseAfterMs.store(2500);
    const int aftersBefore = fakeImage.afters.load(), preemptsBefore = fakeImage.preempts.load();
    Child consumer;
    const std::wstring dest = root + L"destQ3";
    paste_on(local, dest, content.size(), 60, &consumer);
    // While the paste waits for the image: the remote clipboard gets another copy (not files). Only
    // once the paste has really begun and is waiting (the image was asked to stop) -- a fixed sleep
    // let a slow consumer start AFTER the change, when the copy was already withdrawn (verifier's
    // 18a38fa run under load: published 231.5 s, cleared 232.9 s, no StartOperation).
    check("staged: the paste began and waits for the image (it was asked to stop)",
          wait_until([&] { return fakeImage.preempts.load() > preemptsBefore; }, 30000));
    host.OnHostClipboard(++hostSeq, {});
    const DWORD code = consumer.Wait(90000);
    std::map<std::wstring, std::vector<uint8_t>> setQ = {{L"d5R.bin", content}};
    std::string detail;
    check("R->P: the paste waited for the image's end, then completed whole", code == 0 && same_files(dest, setQ, &detail), detail);
    check("...and because the remote clipboard changed meanwhile, the image may NOT go again", wait_until([&] {
            return fakeImage.afters.load() == aftersBefore + 1 && fakeImage.lastMayResume.load() == 0;
          }, 5000), "mayResume=" + std::to_string(fakeImage.lastMayResume.load()));
  }

  std::printf("\n--- Q4 (r3 A-1). R->P: the image never lets go: refused within the budget, and the image is RELEASED ---\n");
  {
    const std::wstring path = remoteDir + L"\\d5R4.bin";
    write_file(path, make_content(300000, 283));
    const auto before = viewer.GetCounters();
    host.OnHostClipboard(++hostSeq, {path});
    wait_until([&] { return viewer.GetCounters().remotePublished > before.remotePublished; }, 20000);
    fakeImage.owner = 0xD5D8;
    check("an image holds the viewer's bulk (and will not let go)", viewArbiter.TryAcquire(BulkUse::Image, fakeImage.owner));
    fakeImage.releaseAfterMs.store(0);
    const int preemptsBefore = fakeImage.preempts.load(), aftersBefore = fakeImage.afters.load();
    const auto t0 = GetTickCount64();
    Child consumer;
    const std::wstring dest = root + L"destQ4";
    paste_on(local, dest, 300000, 15, &consumer);
    const bool refused = wait_until([&] { return viewer.GetCounters().recvBusy > before.recvBusy; }, 15000);
    const auto secs = (GetTickCount64() - t0) / 1000.0;
    check("R->P: the image was asked to stop, and the paste is refused as busy after the budget (7 s)",
          fakeImage.preempts.load() == preemptsBefore + 1 && refused && secs >= 6.0 && secs <= 10.5, std::to_string(secs) + " s");
    consumer.Wait(30000);
    check("...nothing landed", nothing_lands(dest, L"d5R4.bin"));
    check("THE STOPPED IMAGE IS RELEASED (told the paste is over) -- later images are not held for ever",
          wait_until([&] { return fakeImage.afters.load() == aftersBefore + 1; }, 3000),
          "afters=" + std::to_string(fakeImage.afters.load() - aftersBefore));
    viewArbiter.Release(fakeImage.owner);
  }

  std::printf("\n--- P3 (r3 A-3). R->P: a PrepareReply whose sizes are not the offered ones (huge, overflowing): refused, the host lets go ---\n");
  {
    const std::wstring p1 = remoteDir + L"\\sz1.bin", p2 = remoteDir + L"\\sz2.bin";
    write_file(p1, make_content(4000, 301));
    write_file(p2, make_content(5000, 302));
    const auto before = viewer.GetCounters();
    host.OnHostClipboard(++hostSeq, {p1, p2});
    wait_until([&] { return viewer.GetCounters().remotePublished > before.remotePublished; }, 20000);
    link.SetRewrite([](uint16_t type, std::vector<uint8_t>* rb) {
      if (type != static_cast<uint16_t>(fn::FileMsg::PrepareReply)) return;
      fn::PrepareReply r;
      if (!fn::parse(*rb, &r) || r.items.size() != 2) return;
      r.items[0].size = ~uint64_t{0};  // a small offer, a huge prepare -- and the sum wraps
      r.items[1].size = 2;
      *rb = fn::body(r);
    });
    const auto hb = host.GetCounters();
    Child consumer;
    const std::wstring dest = root + L"destP3";
    paste_on(local, dest, 9000, 15, &consumer);
    const bool refused = wait_until([&] { return viewer.GetCounters().recvRefused > before.recvRefused; }, 15000);
    link.SetRewrite(nullptr);
    consumer.Wait(30000);
    check("THE SIZES THAT ARE NOT THE OFFER'S ARE REFUSED, before any byte",
          refused && nothing_lands(dest, L"sz1.bin") && nothing_lands(dest, L"sz2.bin") &&
              viewer.GetCounters().bytesReceived == before.bytesReceived);
    check("...the host's pinned send ended and both bulks are free", wait_until([&] {
            return host.GetCounters().sendFailed > hb.sendFailed && hostArbiter.use() == BulkUse::Idle &&
                   viewArbiter.use() == BulkUse::Idle;
          }, 5000), "sendFailed=" + std::to_string(host.GetCounters().sendFailed - hb.sendFailed));
  }

  std::printf("\n--- N3 (r2 4). the viewer's switch goes OFF mid R->P paste: the host sends nothing more ---\n");
  {
    std::map<std::wstring, std::vector<uint8_t>> setN3 = {{L"bigN3.bin", make_content(16u * 1024u * 1024u, 161)}};
    const std::wstring bigPath = remoteDir + L"\\bigN3.bin";
    write_file(bigPath, setN3.begin()->second);
    const auto before = viewer.GetCounters();
    host.OnHostClipboard(++hostSeq, {bigPath});
    wait_until([&] { return viewer.GetCounters().remotePublished > before.remotePublished; }, 20000);
    const auto hBefore = host.GetCounters();
    Child consumer;
    const std::wstring dest = root + L"destN3";
    paste_on(local, dest, setN3.begin()->second.size(), 60, &consumer);
    check("...bytes are moving", wait_until([&] { return viewer.GetCounters().bytesReceived > before.bytesReceived + 1024 * 1024; }, 30000));
    viewer.SetAllowed(false);
    const bool told = wait_until([&] {
      const auto hc = host.GetCounters();
      return hc.sendFailed > hBefore.sendFailed && hc.lastSendEndReason == static_cast<uint8_t>(fn::PasteEndReason::Disabled);
    }, 5000);
    check("the host was told: the send ended Disabled", told, "reason=" + std::to_string(host.GetCounters().lastSendEndReason));
    const uint64_t servedAt = host.GetCounters().bytesServed;
    Sleep(2000);
    check("NOT ONE FILE BYTE IS SENT BY THE HOST AFTER IT", host.GetCounters().bytesServed == servedAt,
          std::to_string(servedAt) + " -> " + std::to_string(host.GetCounters().bytesServed));
    check("...both ends' bulk is free", hostArbiter.use() == BulkUse::Idle && viewArbiter.use() == BulkUse::Idle);
    consumer.Wait(60000);
    viewer.SetAllowed(true);
  }

  std::printf("\n--- O2 (r2 5). a copy of 101 files on the remote PC: not offered at all ---\n");
  {
    std::vector<std::wstring> many;
    // Real, eligible files after two folders: cut first and filtered after (the old way), this would be
    // offered as its 99 files; judged as a copy of 101 it is not offered at all.
    const std::wstring manyR = remoteDir + L"\\manyR";
    CreateDirectoryW(manyR.c_str(), nullptr);
    many.push_back(manyR);
    many.push_back(manyR);
    for (int i = 0; i < 99; ++i) {
      many.push_back(manyR + L"\\r" + std::to_wstring(i) + L".txt");
      write_file(many.back(), make_content(10, 250 + i));
    }
    // First an ordinary remote copy, published here -- the one the refused copy must replace.
    const std::wstring oldPath = remoteDir + L"\\oldO2.txt";
    write_file(oldPath, make_content(700, 191));
    const auto pub = viewer.GetCounters();
    host.OnHostClipboard(++hostSeq, {oldPath});
    check("an ordinary remote copy is published here first",
          wait_until([&] { return viewer.GetCounters().remotePublished > pub.remotePublished; }, 20000));
    const auto hBefore = host.GetCounters();
    const auto vBefore = viewer.GetCounters();
    host.OnHostClipboard(++hostSeq, many);
    Sleep(2000);
    check("the host offers none of it (no identification even started)", host.GetCounters().hostOffers == hBefore.hostOffers);
    check("...and the older remote offer comes off this PC's clipboard (the newer copy replaced it)",
          wait_until([&] { return viewer.GetCounters().remoteCleared > vBefore.remoteCleared; }, 5000));
  }

  std::printf("\n--- I. the remote clipboard stops naming files: ours comes off this PC's clipboard ---\n");
  {
    const std::wstring path = remoteDir + L"\\lastI.txt";
    write_file(path, make_content(500, 181));
    const auto pub = viewer.GetCounters();
    host.OnHostClipboard(++hostSeq, {path});
    wait_until([&] { return viewer.GetCounters().remotePublished > pub.remotePublished; }, 20000);
    const auto before = viewer.GetCounters();
    host.OnHostClipboard(++hostSeq, {});
    check("the viewer withdrew what it had published", wait_until([&] { return viewer.GetCounters().remoteCleared > before.remoteCleared; }, 5000));
  }


  std::printf("\n--- V1 (r8 2). the viewer: an old helper's late 'gone' does not undo what its successor published ---\n");
  {
    // The viewer's helper H1 published the remote copies above. H1 dies; its "gone" is held at the
    // viewer's door (probe point 2) while a new remote copy makes the viewer start H2 and publish
    // through it; then the old "gone" is let in. What H2 published must stay published -- which
    // shows when the remote clipboard clears: the viewer withdraws only what it knows it published.
    std::mutex vpm;
    std::condition_variable vpcv;
    int vpoint = 0;
    bool vparked = false, vrelease = false;
    uint64_t vinst = 0;
    viewer.SetHelperProbeForTest([&](uint64_t instance, int point) {
      std::unique_lock<std::mutex> l(vpm);
      if (point != vpoint) return;
      vpoint = 0;
      vinst = instance;
      vparked = true;
      vpcv.notify_all();
      vpcv.wait(l, [&] { return vrelease; });
    });
    const std::wstring v1a = remoteDir + L"\\viewV1a.txt", v1b = remoteDir + L"\\viewV1b.txt";
    write_file(v1a, make_content(512, 201));
    write_file(v1b, make_content(768, 202));
    const auto before = viewer.GetCounters();
    host.OnHostClipboard(++hostSeq, {v1a});
    check("V1: the viewer published the remote copy through its helper H1",
          wait_until([&] { return viewer.GetCounters().remotePublished > before.remotePublished; }, 20000));
    const DWORD pid1 = viewerHelperPid.load();
    {
      std::lock_guard<std::mutex> l(vpm);
      vpoint = 2;  // H1's "gone"
      vparked = false;
      vrelease = false;
    }
    HANDLE hp = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid1);
    check("V1: H1's process could be opened", hp != nullptr, "pid=" + std::to_string(pid1));
    if (hp) {
      TerminateProcess(hp, 9);
      WaitForSingleObject(hp, 5000);
      CloseHandle(hp);
    }
    bool goneParked = false;
    {
      std::unique_lock<std::mutex> l(vpm);
      goneParked = vpcv.wait_for(l, std::chrono::seconds(10), [&] { return vparked; });
    }
    check("V1: H1's 'gone' was held at the viewer's door", goneParked, "instance=" + std::to_string(vinst));
    const auto mid = viewer.GetCounters();
    host.OnHostClipboard(++hostSeq, {v1b});  // a new remote copy: the viewer starts H2 and publishes through it
    check("V1: the viewer published the new copy through a new helper H2", wait_until([&] {
            const auto c = viewer.GetCounters();
            return c.remotePublished > mid.remotePublished && c.helperLaunches > mid.helperLaunches;
          }, 20000));
    {
      std::lock_guard<std::mutex> l(vpm);
      vrelease = true;  // the old "gone" goes in now
      vpcv.notify_all();
    }
    check("V1: the old 'gone' was not taken (a helper since replaced)",
          wait_until([&] { return viewer.GetCounters().staleHelperGones > mid.staleHelperGones; }, 5000));
    const auto pub = viewer.GetCounters();
    host.OnHostClipboard(++hostSeq, {});  // the remote clipboard clears
    check("V1: what H2 published is still known as published: the viewer withdraws it",
          wait_until([&] { return viewer.GetCounters().remoteCleared > pub.remoteCleared; }, 5000),
          "cleared +" + std::to_string(viewer.GetCounters().remoteCleared - pub.remoteCleared));
    viewer.SetHelperProbeForTest(nullptr);
  }
  // ------------------------------------------------------------------ teardown
  stop.store(true);
  pump.join();
  std::printf("\n--- E (r2 4). a request of a NEW control session: nothing of the old one is carried over ---\n");
  {
    const std::wstring path = srcDir + L"\\epochE.txt";
    write_file(path, make_content(100, 171));
    // The last P->R offer (O's refusal withdrew one; publish a fresh one straight to the handler).
    fn::Offer o;
    o.offerId = 0xE0E0;
    o.revision = 1;
    fn::OfferItem it;
    it.index = 0;
    it.name = u"epochE.txt";
    it.size = 100;
    o.items.push_back(it);
    std::vector<uint8_t> raw;
    fn::OfferReply orr;
    check("an offer of session 1 is published", call(fn::FileMsg::Offer, fn::body(o), 1, &raw) && fn::parse(raw, &orr) &&
                                                   orr.verdict == fn::Verdict::Accept);
    fn::PasteQuery q{0xE0E0};
    fn::PasteQueryReply qr;
    raw.clear();
    check("session 2 asking about it: Withdrawn (the old session's offer is gone)",
          call(fn::FileMsg::PasteQuery, fn::body(q), 2, &raw) && fn::parse(raw, &qr) && qr.state == fn::PasteState::Withdrawn,
          "state=" + std::to_string(static_cast<int>(qr.state)));
    // r2 review: a LATE request of the older session (another Serve() still alive) must not end
    // session 2's state the way a newer one ends session 1's.
    fn::Offer o2 = o;
    o2.offerId = 0xE0E1;
    raw.clear();
    check("an offer of session 2 is published", call(fn::FileMsg::Offer, fn::body(o2), 2, &raw) && fn::parse(raw, &orr) &&
                                                   orr.verdict == fn::Verdict::Accept);
    fn::PasteQuery late{0xE0E0};
    raw.clear();
    check("a late request of session 1 is DROPPED (the handler refuses it)", !call(fn::FileMsg::PasteQuery, fn::body(late), 1, &raw));
    fn::PasteQuery q2{0xE0E1};
    raw.clear();
    check("...and session 2's offer is still there (not Withdrawn)",
          call(fn::FileMsg::PasteQuery, fn::body(q2), 2, &raw) && fn::parse(raw, &qr) && qr.state == fn::PasteState::None,
          "state=" + std::to_string(static_cast<int>(qr.state)));
  }
  std::printf("\n--- E2 (r4). two control threads cross at the epoch check: the newer session keeps its state, the older request applies nothing ---\n");
  {
    // One control thread (A) is parked inside HandleControl by the test-only probe at a chosen point;
    // another (B, a newer session) is sent while A is parked; then A is let go. Point 1 = the epoch
    // is decided, before the switch: B must WAIT for A (one lock), so A's request lands in A's own
    // session and B's teardown comes after it -- never A's record after B's. Point 2 = recorded,
    // before the handler: A's handler must find the state no longer its session's and apply nothing.
    std::mutex pm;
    std::condition_variable pcv;
    const int kPinResultPoint = 700 + static_cast<int>(fc::PipeMsg::PinResult);  // the probe's "a PinResult arrived"
    int parkPoint = 0, parkPoint2 = 0;
    uint64_t parkEpoch = 0, parkEpoch2 = 0;
    bool parked = false, release = false, parked2 = false, release2 = false;
    host.SetEpochProbeForTest([&](uint64_t e, int point) {
      std::unique_lock<std::mutex> l(pm);
      if (point == parkPoint && e == parkEpoch) {
        parkPoint = 0;  // one thread per slot
        parked = true;
        pcv.notify_all();
        pcv.wait(l, [&] { return release; });
      } else if (point == parkPoint2 && e == parkEpoch2) {
        parkPoint2 = 0;
        parked2 = true;
        pcv.notify_all();
        pcv.wait(l, [&] { return release2; });
      }
    });
    const auto offer_of = [](uint64_t id) {
      fn::Offer o;
      o.offerId = id;
      o.revision = 1;
      fn::OfferItem it;
      it.index = 0;
      it.name = u"epochE.txt";
      it.size = 100;
      o.items.push_back(it);
      return o;
    };
    const auto state_of = [&](uint64_t epoch, uint64_t offerId, bool* answered) {
      fn::PasteQuery q{offerId};
      fn::PasteQueryReply qr;
      std::vector<uint8_t> raw;
      *answered = call(fn::FileMsg::PasteQuery, fn::body(q), epoch, &raw) && fn::parse(raw, &qr);
      return *answered ? qr.state : fn::PasteState::None;
    };
    struct Crossed {
      bool aAnswered = false, bAnswered = false, aParked = false, bWaitedForA = false;
      fn::OfferReply a, b;
    };
    const auto cross = [&](int point, uint64_t oldEpoch, uint64_t oldId, uint64_t newEpoch, uint64_t newId, bool bBlocked) {
      Crossed c;
      {
        std::lock_guard<std::mutex> l(pm);
        parkPoint = point;
        parkEpoch = oldEpoch;
        parked = false;
        release = false;
      }
      std::vector<uint8_t> ra, rb;
      std::atomic<bool> bDone{false};
      std::thread a([&] { c.aAnswered = call(fn::FileMsg::Offer, fn::body(offer_of(oldId)), oldEpoch, &ra) && fn::parse(ra, &c.a); });
      {
        std::unique_lock<std::mutex> l(pm);
        c.aParked = pcv.wait_for(l, std::chrono::seconds(5), [&] { return parked; });
      }
      std::thread b([&] {
        c.bAnswered = call(fn::FileMsg::Offer, fn::body(offer_of(newId)), newEpoch, &rb) && fn::parse(rb, &c.b);
        bDone.store(true);
      });
      // B, sent while A is parked: at point 1 it cannot finish before A is let go (same lock) -- A is
      // held 400 ms and B must still be inside; at point 2 it runs through -- B is given up to 5 s
      // (a helper launch under load) and must be done before A is let go.
      if (bBlocked) Sleep(400);
      else wait_until([&] { return bDone.load(); }, 5000);
      c.bWaitedForA = !bDone.load();
      {
        std::lock_guard<std::mutex> l(pm);
        release = true;
        pcv.notify_all();
      }
      a.join();
      b.join();
      return c;
    };
    bool answered = false;
    // X1: A of the current session (2) parked at point 1, B of session 3 sent meanwhile.
    {
      const auto before = host.GetCounters();
      const Crossed c = cross(1, 2, 0xE1A0, 3, 0xE1B0, true);
      check("X1: A (session 2) was parked at the decision", c.aParked);
      check("X1: B (session 3) could not finish while A held the decision", c.bWaitedForA);
      // A was session 2's when decided, so it is handled as session 2's -- and B's teardown may
      // overtake its helper round trip: accepted (then torn down with session 2), refused by a helper
      // that was going, or dropped. Never an accept that reaches session 3 (checked below).
      check("X1: A's answer, if any, is its own session's: Accept or HelperUnavailable",
            !c.aAnswered || c.a.verdict == fn::Verdict::Accept || c.a.verdict == fn::Verdict::HelperUnavailable,
            "answered=" + std::to_string(c.aAnswered) + " verdict=" + std::to_string(static_cast<int>(c.a.verdict)));
      check("X1: B's offer was accepted in session 3", c.bAnswered && c.b.verdict == fn::Verdict::Accept);
      const fn::PasteState sb = state_of(3, 0xE1B0, &answered);
      check("X1: session 3's offer is still there afterwards (not torn down by A's record)",
            answered && sb == fn::PasteState::None, "state=" + std::to_string(static_cast<int>(sb)));
      const fn::PasteState sa = state_of(3, 0xE1A0, &answered);
      check("X1: A's offer is gone with session 2", answered && sa == fn::PasteState::Withdrawn,
            "state=" + std::to_string(static_cast<int>(sa)));
      (void)state_of(2, 0xE1A0, &answered);
      check("X1: a late request of session 2 is dropped", !answered);
      // Session 3's helper is the one B started, and it still serves: another offer of session 3 goes
      // through it without a new launch.
      const auto launched = host.GetCounters().helperLaunches;
      std::vector<uint8_t> raw;
      fn::OfferReply orr;
      check("X1: session 3's helper still serves (another offer of session 3 is accepted)",
            call(fn::FileMsg::Offer, fn::body(offer_of(0xE1C0)), 3, &raw) && fn::parse(raw, &orr) && orr.verdict == fn::Verdict::Accept);
      check("X1: ...on the helper B started (one launch for session 3, none since)",
            launched == before.helperLaunches + 1 && host.GetCounters().helperLaunches == launched,
            "launches +" + std::to_string(host.GetCounters().helperLaunches - before.helperLaunches));
    }
    // X2: A of the current session (3) parked at point 2 (recorded, before its handler), B of session 4
    // runs through meanwhile: A's handler must apply nothing to session 4's state.
    {
      const auto before = host.GetCounters();
      const Crossed c = cross(2, 3, 0xE2A0, 4, 0xE2B0, false);
      check("X2: A (session 3) was parked before its handler", c.aParked);
      check("X2: B (session 4) ran through while A was parked", !c.bWaitedForA);
      check("X2: A's answer, if any, is not an Accept", !c.aAnswered || c.a.verdict != fn::Verdict::Accept);
      check("X2: B's offer was accepted in session 4", c.bAnswered && c.b.verdict == fn::Verdict::Accept);
      check("X2: A's request was DROPPED (no answer) -- its session ended while it was on the way",
            !c.aAnswered, "answered=" + std::to_string(c.aAnswered) + " verdict=" + std::to_string(static_cast<int>(c.a.verdict)));
      const fn::PasteState sb = state_of(4, 0xE2B0, &answered);
      check("X2: session 4's offer is still there", answered && sb == fn::PasteState::None,
            "state=" + std::to_string(static_cast<int>(sb)));
      const fn::PasteState sa = state_of(4, 0xE2A0, &answered);
      check("X2: A's offer never got in (Withdrawn = unknown to session 4)", answered && sa == fn::PasteState::Withdrawn,
            "state=" + std::to_string(static_cast<int>(sa)));
      const auto after = host.GetCounters();
      check("X2: only B's offer was counted (A applied nothing, not even a count)", after.offers == before.offers + 1,
            "offers +" + std::to_string(after.offers - before.offers));
    }
    // X3: the session-end hook names the new epoch: from then on the ended session's requests are
    // dropped at the first check, and the new session's first request finds the state its own.
    {
      std::vector<uint8_t> raw;
      fn::OfferReply orr;
      check("X3: an offer of session 4 is published", call(fn::FileMsg::Offer, fn::body(offer_of(0xE3A0)), 4, &raw) &&
                                                          fn::parse(raw, &orr) && orr.verdict == fn::Verdict::Accept);
      host.OnSessionEnd(5);
      (void)state_of(4, 0xE3A0, &answered);
      check("X3: after OnSessionEnd(5) a request of session 4 is dropped", !answered);
      const fn::PasteState sa = state_of(5, 0xE3A0, &answered);
      check("X3: session 5 does not see session 4's offer", answered && sa == fn::PasteState::Withdrawn,
            "state=" + std::to_string(static_cast<int>(sa)));
      raw.clear();
      check("X3: an offer of session 5 is published", call(fn::FileMsg::Offer, fn::body(offer_of(0xE3B0)), 5, &raw) &&
                                                          fn::parse(raw, &orr) && orr.verdict == fn::Verdict::Accept);
      const fn::PasteState sb = state_of(5, 0xE3B0, &answered);
      check("X3: ...and it stays (the hook already switched the epoch: no second teardown)",
            answered && sb == fn::PasteState::None, "state=" + std::to_string(static_cast<int>(sb)));
    }
    // ---------------------------------------------------------------- r5: the epoch owns the shared parts
    // Generalised parking: A runs `act` and parks at (point, epoch); B runs meanwhile; A is let go.
    struct Parked {
      bool aParked = false, bDone = false;
    };
    const auto park = [&](int point, uint64_t epoch, const std::function<void()>& act, const std::function<void()>& meanwhile,
                          int bWaitMs) {
      Parked p;
      {
        std::lock_guard<std::mutex> l(pm);
        parkPoint = point;
        parkEpoch = epoch;
        parked = false;
        release = false;
      }
      std::thread a(act);
      {
        std::unique_lock<std::mutex> l(pm);
        p.aParked = pcv.wait_for(l, std::chrono::seconds(10), [&] { return parked; });
      }
      std::atomic<bool> bDone{false};
      std::thread b([&] {
        meanwhile();
        bDone.store(true);
      });
      wait_until([&] { return bDone.load(); }, bWaitMs);
      p.bDone = bDone.load();
      {
        std::lock_guard<std::mutex> l(pm);
        release = true;
        pcv.notify_all();
      }
      a.join();
      b.join();
      return p;
    };
    const auto offer_call = [&](uint64_t epoch, uint64_t id, bool* answered, fn::OfferReply* reply) {
      std::vector<uint8_t> raw;
      *answered = call(fn::FileMsg::Offer, fn::body(offer_of(id)), epoch, &raw) && fn::parse(raw, reply);
    };
    // The host helper's log (appended by every helper started for this test): what the LAST
    // publish on the remote clipboard was, and whether anything cleared it afterwards.
    const auto helper_log_tail = [&](size_t from) {
      std::vector<uint8_t> b;
      read_file(helperLog, &b);
      if (b.size() < from) from = 0;  // a fresh helper truncated it: all of it is new
      return std::string(b.begin() + static_cast<std::ptrdiff_t>(from), b.end());
    };
    const auto helper_log_size = [&] {
      std::vector<uint8_t> b;
      read_file(helperLog, &b);
      return b.size();
    };
    const auto last_publish_is = [&](const std::string& tail, uint64_t offerId, std::string* detail) {
      const std::string want = "publish offer=" + std::to_string(offerId) + " items=";
      const size_t at = tail.rfind("publish offer=");
      const bool last = at != std::string::npos && tail.compare(at, want.size(), want) == 0;
      // (Two helpers append to one file: the old one's own exit lines may land after B's publish. A
      // "clipboard cleared" after B's publish would be B's object going -- the helper only clears
      // what is still its own, so the old one's exit must not log it then.)
      const bool clearedAfter = at != std::string::npos && (tail.find("clipboard cleared", at) != std::string::npos ||
                                                            tail.find("clear offer=", at) != std::string::npos);
      *detail = at == std::string::npos ? "no publish in the tail" : tail.substr(at, (std::min)(tail.size() - at, size_t{160}));
      return last && !clearedAfter;
    };
    // A remote-files object is on the private station's clipboard right now (the consumer, "empty"
    // mode, only enumerates the formats: descriptor + contents = the helper's object).
    const auto station_has_files_object = [&](const wchar_t* tag, std::string* detail) {
      Child probe;
      const std::wstring dest = root + L"probe_" + tag;
      if (!paste_on(station, dest, 0, 5, &probe, L"empty")) {
        *detail = "the consumer did not start";
        return false;
      }
      probe.Wait(15000);
      std::vector<uint8_t> b;
      read_file(dest + L".result.txt", &b);
      const std::string res(b.begin(), b.end());
      *detail = res;
      return res.find("formats desc=1 contents=1") != std::string::npos;
    };

    // Y1 (r5 1): a session-end hook that is LATE -- it names the epoch already served (the new
    // session's first request made the switch), or an older one: it must change nothing.
    std::printf("      --- Y1: a late session-end hook does not tear the new session down ---\n");
    {
      const auto before = host.GetCounters();
      const fn::PasteState s0 = state_of(5, 0xE3B0, &answered);
      check("Y1: session 5's offer stands before the hooks", answered && s0 == fn::PasteState::None);
      host.OnSessionEnd(5);  // "session 4 ended, 5 is next" -- but 5's request already switched
      const fn::PasteState s1 = state_of(5, 0xE3B0, &answered);
      check("Y1: after OnSessionEnd(5) (already served) session 5's offer is still there", answered && s1 == fn::PasteState::None,
            "state=" + std::to_string(static_cast<int>(s1)));
      host.OnSessionEnd(4);  // older still
      const fn::PasteState s2 = state_of(5, 0xE3B0, &answered);
      check("Y1: after OnSessionEnd(4) (older) session 5's offer is still there", answered && s2 == fn::PasteState::None,
            "state=" + std::to_string(static_cast<int>(s2)));
      check("Y1: no helper was restarted by the late hooks", host.GetCounters().helperLaunches == before.helperLaunches,
            "launches +" + std::to_string(host.GetCounters().helperLaunches - before.helperLaunches));
      std::string detail;
      check("Y1: the remote clipboard still carries session 5's object", station_has_files_object(L"y1", &detail), detail);
      host.OnSessionEnd(6);  // a real one: 6 is newer
      const fn::PasteState s3 = state_of(6, 0xE3B0, &answered);
      check("Y1: OnSessionEnd(6) (newer) does switch: session 5's offer is gone", answered && s3 == fn::PasteState::Withdrawn,
            "state=" + std::to_string(static_cast<int>(s3)));
    }

    // Y2 (r5 2): an Offer of the OLD session crosses the switch inside its helper round trip. Point 3 =
    // its helper is up, Publish not sent yet; point 4 = Publish sent, answer not awaited yet. Either way
    // the new session's publication (B) must be what stays on the clipboard, and A applies nothing.
    std::printf("      --- Y2: an old session's Offer crossing the switch publishes / clears nothing of the new one ---\n");
    for (int point = 3; point <= 4; ++point) {
      const uint64_t oldEpoch = point == 3 ? 6 : 7, newEpoch = oldEpoch + 1;
      const uint64_t oldId = point == 3 ? 0xF2A0 : 0xF2C0, newId = point == 3 ? 0xF2B0 : 0xF2D0;
      const std::string tag = "Y2." + std::to_string(point) + ": ";
      const auto before = host.GetCounters();
      const size_t logAt = helper_log_size();
      bool aAnswered = false, bAnswered = false;
      fn::OfferReply a, b;
      uint64_t launchesAfterB = 0;
      const Parked p = park(point, oldEpoch, [&] { offer_call(oldEpoch, oldId, &aAnswered, &a); },
                            [&] {
                              offer_call(newEpoch, newId, &bAnswered, &b);
                              launchesAfterB = host.GetCounters().helperLaunches;
                            },
                            15000);
      check(tag + "A (old session) was parked inside its helper round trip", p.aParked);
      check(tag + "B (new session) ran through while A was parked", p.bDone);
      check(tag + "B's offer was accepted in the new session", bAnswered && b.verdict == fn::Verdict::Accept,
            "verdict=" + std::to_string(static_cast<int>(b.verdict)));
      check(tag + "A was DROPPED (no answer): its session ended on the way", !aAnswered,
            "answered=" + std::to_string(aAnswered) + " verdict=" + std::to_string(static_cast<int>(a.verdict)));
      const fn::PasteState sb = state_of(newEpoch, newId, &answered);
      check(tag + "the new session's offer is still there", answered && sb == fn::PasteState::None,
            "state=" + std::to_string(static_cast<int>(sb)));
      const fn::PasteState sa = state_of(newEpoch, oldId, &answered);
      check(tag + "A's offer is unknown to the new session", answered && sa == fn::PasteState::Withdrawn,
            "state=" + std::to_string(static_cast<int>(sa)));
      const auto after = host.GetCounters();
      check(tag + "A started no helper for the new session (no launch after B's)", after.helperLaunches == launchesAfterB,
            "launches +" + std::to_string(after.helperLaunches - before.helperLaunches) + " afterB=" + std::to_string(launchesAfterB - before.helperLaunches));
      check(tag + "A's send / start was refused by the channel's owner check", after.staleHelperSends > before.staleHelperSends,
            "stale +" + std::to_string(after.staleHelperSends - before.staleHelperSends));
      // (A was counted as received -- it was its session's request when it came in; it is never ACCEPTED.)
      check(tag + "only B's offer was accepted", after.offersAccepted == before.offersAccepted + 1,
            "accepted +" + std::to_string(after.offersAccepted - before.offersAccepted));
      std::string detail;
      check(tag + "the helper's last publication is B's and nothing cleared it afterwards",
            last_publish_is(helper_log_tail(logAt), newId, &detail), detail);
      check(tag + "the remote clipboard carries a remote-files object now", station_has_files_object(point == 3 ? L"y2a" : L"y2b", &detail),
            detail);
    }

    // Y3 (r5 3): the shared R->P sender. Point 5 = an old session's Prepare is pinned and accepted,
    // the sender not begun yet; point 6 = an old session's End has ended the paste, the sender not
    // closed yet. In both, the new session begins its own send meanwhile: the old request must
    // neither begin on top of it nor close it.
    std::printf("      --- Y3: an old session's late begin / close does not touch the new session's sender ---\n");
    // The host's clipboard offer as a session sees it: the session's first request (its baseline),
    // then a copy made on the remote PC, identified by the helper, asked for until it has an id.
    const std::wstring y3Path = remoteDir + L"\\epochY3.txt";
    write_file(y3Path, make_content(2048, 173));
    const auto host_offer_in = [&](uint64_t epoch, const std::wstring& path = std::wstring()) {
      fn::OfferQueryReply qr;
      std::vector<uint8_t> raw;
      (void)(call(fn::FileMsg::OfferQuery, fn::body(fn::OfferQuery{0}), epoch, &raw) && fn::parse(raw, &qr));  // the baseline
      host.OnHostClipboard(++hostSeq, {path.empty() ? y3Path : path});
      uint64_t id = 0;
      wait_until([&] {
        std::vector<uint8_t> r2;
        fn::OfferQueryReply q2;
        if (call(fn::FileMsg::OfferQuery, fn::body(fn::OfferQuery{0}), epoch, &r2) && fn::parse(r2, &q2)) id = q2.offerId;
        return id != 0;
      }, 15000);
      return id;
    };
    const auto prepare_rtop = [&](uint64_t epoch, uint64_t offerId, uint64_t pasteOp, bool* answered, fn::PrepareReply* reply) {
      fn::Prepare pr;
      pr.direction = fn::Direction::RtoP;
      pr.offerId = offerId;
      pr.pasteOp = pasteOp;
      std::vector<uint8_t> raw;
      *answered = call(fn::FileMsg::Prepare, fn::body(pr), epoch, &raw) && fn::parse(raw, reply);
    };
    const auto end_paste = [&](uint64_t epoch, uint64_t offerId, uint64_t pasteOp, bool* answered, fn::EndReply* reply) {
      fn::End e{offerId, pasteOp, fn::PasteEndReason::Cancelled};
      std::vector<uint8_t> raw;
      *answered = call(fn::FileMsg::End, fn::body(e), epoch, &raw) && fn::parse(raw, reply);
    };
    {
      // Y3.5: A = Prepare R->P of session 8 parked at point 5; B = session 9 prepares its own send.
      const uint64_t h8 = host_offer_in(8);
      check("Y3.5: session 8 sees the remote copy as an offer", h8 != 0);
      const auto before = host.GetCounters();
      bool aAnswered = false, bAnswered = false;
      fn::PrepareReply a, b;
      uint64_t h9 = 0;
      HostFileCopyService::Counters atB{};
      // Both sessions use paste op 1 (r6): every session's ops start at 1, so the old request's
      // clean-up must tell the new session's reservation from its own by the whole key.
      const Parked p = park(5, 8, [&] { prepare_rtop(8, h8, 1, &aAnswered, &a); },
                            [&] {
                              h9 = host_offer_in(9);
                              prepare_rtop(9, h9, 1, &bAnswered, &b);
                              atB = host.GetCounters();
                            },
                            30000);
      check("Y3.5: A (session 8) was parked with its send pinned and accepted, not begun", p.aParked);
      check("Y3.5: B (session 9) ran through while A was parked", p.bDone);
      check("Y3.5: session 9 sees the remote copy as its own offer", h9 != 0 && h9 != h8, "h8=" + std::to_string(h8) + " h9=" + std::to_string(h9));
      check("Y3.5: B's send was prepared in session 9", bAnswered && b.verdict == fn::Verdict::Accept,
            "answered=" + std::to_string(bAnswered) + " verdict=" + std::to_string(static_cast<int>(b.verdict)));
      check("Y3.5: ...and the sender was begun for B while A was parked", atB.sendOpen && atB.sendPasteOp == 1 && atB.sendEpochTag == b.epochTag,
            "open=" + std::to_string(atB.sendOpen) + " op=" + std::to_string(atB.sendPasteOp) + " tagIsB=" + std::to_string(atB.sendEpochTag == b.epochTag));
      check("Y3.5: A was DROPPED (no answer)", !aAnswered,
            "answered=" + std::to_string(aAnswered) + " verdict=" + std::to_string(static_cast<int>(a.verdict)));
      const auto after = host.GetCounters();
      check("Y3.5: the sender is still B's after A was let go (A did not begin on top of it)",
            after.sendOpen && after.sendPasteOp == 1 && after.sendEpochTag == b.epochTag && a.epochTag != b.epochTag,
            "open=" + std::to_string(after.sendOpen) + " op=" + std::to_string(after.sendPasteOp) + " tagIsB=" + std::to_string(after.sendEpochTag == b.epochTag));
      check("Y3.5: only B's send was counted as prepared", after.sendPrepared == before.sendPrepared + 1,
            "prepared +" + std::to_string(after.sendPrepared - before.sendPrepared));
      // The old request's clean-up (same op 1) must not have released the NEW session's reservation:
      // the host's bulk is still held by paste op 1 (B's), and B's paste is still there to be ended.
      check("Y3.5: the host's bulk is still B's (the stale clean-up did not release the same op)",
            hostArbiter.use() == BulkUse::File && hostArbiter.owner() == 1,
            "use=" + std::to_string(static_cast<int>(hostArbiter.use())) + " owner=" + std::to_string(hostArbiter.owner()));
      bool eAnswered = false;
      fn::EndReply er;
      end_paste(9, h9, 1, &eAnswered, &er);
      check("Y3.5: session 9 ends its send (its paste was still there, not wiped by the stale clean-up)",
            eAnswered && er.state == fn::PasteState::Failed, "state=" + std::to_string(static_cast<int>(er.state)));
      check("Y3.5: ...and the bulk is free", hostArbiter.use() == BulkUse::Idle);
      check("Y3.5: ...and the sender is closed", wait_until([&] { return !host.GetCounters().sendOpen; }, 3000));
    }
    {
      // Y3.6: session 9 sends; its End is parked at point 6 (paste ended, sender not closed); session
      // 10 begins its own send meanwhile; the late close must leave session 10's sender alone.
      fn::OfferQueryReply qr;
      std::vector<uint8_t> raw;
      uint64_t h9 = 0;
      if (call(fn::FileMsg::OfferQuery, fn::body(fn::OfferQuery{0}), 9, &raw) && fn::parse(raw, &qr)) h9 = qr.offerId;
      check("Y3.6: session 9 still has its offer", h9 != 0);
      bool pAnswered = false;
      fn::PrepareReply pr;
      prepare_rtop(9, h9, 0x5A02, &pAnswered, &pr);
      check("Y3.6: session 9's send is prepared and begun", pAnswered && pr.verdict == fn::Verdict::Accept && host.GetCounters().sendOpen &&
                                                               host.GetCounters().sendPasteOp == 0x5A02);
      bool aAnswered = false, bAnswered = false;
      fn::EndReply a;
      fn::PrepareReply b;
      uint64_t h10 = 0;
      HostFileCopyService::Counters atB{};
      const Parked p = park(6, 9, [&] { end_paste(9, h9, 0x5A02, &aAnswered, &a); },
                            [&] {
                              h10 = host_offer_in(10);
                              prepare_rtop(10, h10, 0x5B02, &bAnswered, &b);
                              atB = host.GetCounters();
                            },
                            30000);
      check("Y3.6: A (session 9's End) was parked with the paste ended, the sender not closed", p.aParked);
      check("Y3.6: B (session 10) ran through while A was parked", p.bDone);
      check("Y3.6: session 10 sees the remote copy as its own offer", h10 != 0 && h10 != h9);
      check("Y3.6: B's send was prepared in session 10", bAnswered && b.verdict == fn::Verdict::Accept,
            "answered=" + std::to_string(bAnswered) + " verdict=" + std::to_string(static_cast<int>(b.verdict)));
      check("Y3.6: ...and the sender was begun for B (the stale one closed first)", atB.sendOpen && atB.sendPasteOp == 0x5B02,
            "open=" + std::to_string(atB.sendOpen) + " op=" + std::to_string(atB.sendPasteOp));
      const auto after = host.GetCounters();
      check("Y3.6: the sender is still B's after A's late close (A did not close it)", after.sendOpen && after.sendPasteOp == 0x5B02,
            "open=" + std::to_string(after.sendOpen) + " op=" + std::to_string(after.sendPasteOp));
      check("Y3.6: A's own answer, if any, reports its paste ended", !aAnswered || a.state == fn::PasteState::Failed,
            "answered=" + std::to_string(aAnswered) + " state=" + std::to_string(static_cast<int>(a.state)));
      bool eAnswered = false;
      fn::EndReply er;
      end_paste(10, h10, 0x5B02, &eAnswered, &er);
      check("Y3.6: session 10 ends its send", eAnswered && er.state == fn::PasteState::Failed);
      check("Y3.6: ...and the sender is closed", wait_until([&] { return !host.GetCounters().sendOpen; }, 3000));
    }

    // Z1 (r6 2a): the switch happens while the OLD session's helper is still starting (up, said
    // hello, not yet the channel's). That helper must never become the new session's: the old
    // request is dropped, the new session starts its own (exactly one more launch), its offer stays,
    // and nothing crashes -- 20 times over.
    std::printf("      --- Z1: a helper start that crosses the switch never becomes the new session's helper ---\n");
    {
      int ok = 0, parkedOk = 0, launchesOk = 0, stateOk = 0, aDropped = 0;
      const int rounds = 20;
      for (int i = 0; i < rounds; ++i) {
        const uint64_t oldE = 11 + 2 * static_cast<uint64_t>(i), newE = oldE + 1;
        host.OnSessionEnd(oldE);  // a clean session, no live helper (the previous one is retired)
        const auto before = host.GetCounters();
        {
          std::lock_guard<std::mutex> l(lpm);
          launchPark = true;
          launchParked = false;
          launchRelease = false;
        }
        bool aAnswered = false;
        fn::OfferReply a;
        std::thread at([&] { offer_call(oldE, 0xA100 + static_cast<uint64_t>(i), &aAnswered, &a); });
        bool parkedNow = false;
        {
          std::unique_lock<std::mutex> l(lpm);
          parkedNow = lpcv.wait_for(l, std::chrono::seconds(15), [&] { return launchParked; });
        }
        host.OnSessionEnd(newE);  // the switch, while the old session's helper is up but not adopted
        {
          std::lock_guard<std::mutex> l(lpm);
          launchPark = false;
          launchRelease = true;
          lpcv.notify_all();
        }
        at.join();
        bool cAnswered = false;
        fn::OfferReply c;
        offer_call(newE, 0xA200 + static_cast<uint64_t>(i), &cAnswered, &c);
        Sleep(150);  // room for a late "gone" of the crossed helper, if any
        const fn::PasteState sc = state_of(newE, 0xA200 + static_cast<uint64_t>(i), &answered);
        const auto after = host.GetCounters();
        parkedOk += parkedNow ? 1 : 0;
        aDropped += !aAnswered ? 1 : 0;
        launchesOk += after.helperLaunches == before.helperLaunches + 2 ? 1 : 0;
        stateOk += (cAnswered && c.verdict == fn::Verdict::Accept && answered && sc == fn::PasteState::None) ? 1 : 0;
        ok += (parkedNow && !aAnswered && after.helperLaunches == before.helperLaunches + 2 && cAnswered &&
               c.verdict == fn::Verdict::Accept && answered && sc == fn::PasteState::None)
                  ? 1
                  : 0;
      }
      check("Z1: the old session's start was held at 'up, not yet the channel's' every round", parkedOk == rounds,
            std::to_string(parkedOk) + "/" + std::to_string(rounds));
      check("Z1: the old request was dropped every round (its helper was closed, not adopted)", aDropped == rounds,
            std::to_string(aDropped) + "/" + std::to_string(rounds));
      check("Z1: the new session started exactly one helper of its own every round (launches +2)", launchesOk == rounds,
            std::to_string(launchesOk) + "/" + std::to_string(rounds));
      check("Z1: the new session's offer was accepted and stayed every round", stateOk == rounds,
            std::to_string(stateOk) + "/" + std::to_string(rounds));
      check("Z1: all rounds clean (no crash, no leak into the new session)", ok == rounds, std::to_string(ok) + "/" + std::to_string(rounds));
    }

    // Z2 (r6 2b): the OLD session's helper answers a Pin (op 1) and that answer is still on its way
    // (its reader is parked at point 7) when the switch happens and the NEW session reserves the same
    // op 1 and is about to ask its own helper (parked at point 8). The old answer must not be taken:
    // the new session's Prepare gets its own helper's answer -- its own file's size, not the old one's.
    std::printf("      --- Z2: an old helper's late PinResult (same op) does not answer the new session's pin ---\n");
    {
      const uint64_t oldE = 61, newE = 62;
      const std::wstring z2Old = remoteDir + L"\\epochZ2old.txt", z2New = remoteDir + L"\\epochZ2new.txt";
      write_file(z2Old, make_content(2048, 181));
      write_file(z2New, make_content(4096, 182));
      const uint64_t hOld = host_offer_in(oldE, z2Old);
      check("Z2: the old session sees its copy (2048 bytes)", hOld != 0);
      const auto before = host.GetCounters();
      {
        std::lock_guard<std::mutex> l(pm);
        parkPoint = kPinResultPoint;  // the old helper's next frame: the PinResult
        parkEpoch = oldE;
        parked = false;
        release = false;
        parkPoint2 = 8;  // the new session's Prepare, reserved, before its own pin
        parkEpoch2 = newE;
        parked2 = false;
        release2 = false;
      }
      bool aAnswered = false;
      fn::PrepareReply a;
      std::thread at([&] { prepare_rtop(oldE, hOld, 1, &aAnswered, &a); });
      bool readerParked = false;
      {
        std::unique_lock<std::mutex> l(pm);
        readerParked = pcv.wait_for(l, std::chrono::seconds(10), [&] { return parked; });
      }
      check("Z2: the old helper's PinResult was held before the host looked at it", readerParked);
      host.OnSessionEnd(newE);  // the switch: the old helper is retired, its reader still holds the answer
      at.join();               // the old Prepare gives up (its session ended, or its pin timed out)
      const uint64_t hNew = host_offer_in(newE, z2New);
      check("Z2: the new session sees its copy (4096 bytes)", hNew != 0 && hNew != hOld);
      bool bAnswered = false;
      fn::PrepareReply b;
      std::thread bt([&] { prepare_rtop(newE, hNew, 1, &bAnswered, &b); });
      bool bParked = false;
      {
        std::unique_lock<std::mutex> l(pm);
        bParked = pcv.wait_for(l, std::chrono::seconds(10), [&] { return parked2; });
      }
      check("Z2: the new session's Prepare was held with op 1 reserved, before its own pin", bParked);
      {
        std::lock_guard<std::mutex> l(pm);
        release = true;  // the old answer arrives now, into the new session's wait
        pcv.notify_all();
      }
      Sleep(200);
      const auto mid = host.GetCounters();
      {
        std::lock_guard<std::mutex> l(pm);
        release2 = true;  // now the new session asks its own helper
        pcv.notify_all();
      }
      bt.join();
      check("Z2: the old helper's answer was not taken (a stale frame)", mid.staleHelperFrames > before.staleHelperFrames,
            "stale frames +" + std::to_string(mid.staleHelperFrames - before.staleHelperFrames));
      check("Z2: the new session's Prepare was accepted", bAnswered && b.verdict == fn::Verdict::Accept,
            "answered=" + std::to_string(bAnswered) + " verdict=" + std::to_string(static_cast<int>(b.verdict)));
      check("Z2: ...with ITS OWN helper's answer: its file's size, not the old session's",
            bAnswered && b.items.size() == 1 && b.items[0].size == 4096,
            "items=" + std::to_string(b.items.size()) + " size0=" + std::to_string(b.items.empty() ? 0 : b.items[0].size));
      const auto after = host.GetCounters();
      check("Z2: the sender is the new session's", after.sendOpen && after.sendEpochTag == b.epochTag && after.sendPasteOp == 1);
      bool eAnswered = false;
      fn::EndReply er;
      end_paste(newE, hNew, 1, &eAnswered, &er);
      check("Z2: the new session ends its send", eAnswered && er.state == fn::PasteState::Failed);
      check("Z2: ...and the sender is closed", wait_until([&] { return !host.GetCounters().sendOpen; }, 3000));
    }

    // Z3 (r7 1): the SAME session's helper is replaced (its process died, so the next request starts
    // another), while the old helper's reader still holds a PinResult (op 1) and has not yet seen
    // the pipe go. Owner alone cannot tell the two helpers apart: the old PinResult must not answer
    // the new helper's pin, and the old helper's "gone" must not end the paste begun with the new one.
    std::printf("      --- Z3: an old helper of the SAME session: its late PinResult / gone do not touch the new helper's paste ---\n");
    {
      const uint64_t E = 71;
      const std::wstring z3A = remoteDir + L"\\epochZ3a.txt", z3B = remoteDir + L"\\epochZ3b.txt";
      write_file(z3A, make_content(2048, 191));
      write_file(z3B, make_content(4096, 192));
      const uint64_t hA = host_offer_in(E, z3A);
      check("Z3: the session sees its first copy (2048 bytes)", hA != 0);
      const DWORD pid1 = lastHelperPid.load();
      const auto before = host.GetCounters();
      {
        std::lock_guard<std::mutex> l(pm);
        parkPoint = kPinResultPoint;  // the helper's next frame: the PinResult of the first Prepare
        parkEpoch = E;
        parked = false;
        release = false;
      }
      bool aAnswered = false;
      fn::PrepareReply a;
      std::thread at([&] { prepare_rtop(E, hA, 1, &aAnswered, &a); });
      bool readerParked = false;
      {
        std::unique_lock<std::mutex> l(pm);
        readerParked = pcv.wait_for(l, std::chrono::seconds(10), [&] { return parked; });
      }
      check("Z3: the first helper's PinResult was held before the host looked at it", readerParked);
      // The first helper dies (as if it crashed): the next request of this same session starts another.
      HANDLE hp = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid1);
      check("Z3: the first helper's process could be opened", hp != nullptr, "pid=" + std::to_string(pid1));
      if (hp) {
        TerminateProcess(hp, 9);
        WaitForSingleObject(hp, 5000);
        CloseHandle(hp);
      }
      at.join();  // the first Prepare gives up (its pin timed out); its reservation is released
      check("Z3: the first Prepare did not succeed (its helper died mid-pin)", !aAnswered || a.verdict != fn::Verdict::Accept,
            "answered=" + std::to_string(aAnswered) + " verdict=" + std::to_string(static_cast<int>(a.verdict)));
      // A new copy on the remote PC: identified by a NEW helper of the same session (instance 2).
      host.OnHostClipboard(++hostSeq, {z3B});
      uint64_t hB = 0, hBSize = 0, lastSeen = 0, lastSeenSize = 0;
      wait_until([&] {
        std::vector<uint8_t> r2;
        fn::OfferQueryReply q2;
        if (call(fn::FileMsg::OfferQuery, fn::body(fn::OfferQuery{0}), E, &r2) && fn::parse(r2, &q2) && q2.offerId != 0) {
          lastSeen = q2.offerId;
          lastSeenSize = q2.items.empty() ? 0 : q2.items[0].size;
          if (q2.offerId != hA && lastSeenSize == 4096) {
            hB = q2.offerId;
            hBSize = lastSeenSize;
          }
        }
        return hB != 0;
      }, 15000);
      check("Z3: the second copy (4096 bytes) is offered, identified by a new helper of the same session",
            hB != 0 && hBSize == 4096 && host.GetCounters().helperLaunches == before.helperLaunches + 1,
            "launches +" + std::to_string(host.GetCounters().helperLaunches - before.helperLaunches) + " lastSeen=" +
                std::to_string(lastSeen) + " size=" + std::to_string(lastSeenSize) + " hA=" + std::to_string(hA));
      {
        std::lock_guard<std::mutex> l(pm);
        parkPoint2 = 8;  // the new Prepare (op 1 again), reserved, before its own pin
        parkEpoch2 = E;
        parked2 = false;
        release2 = false;
      }
      bool bAnswered = false;
      fn::PrepareReply b;
      std::thread bt([&] { prepare_rtop(E, hB, 1, &bAnswered, &b); });
      bool bParked = false;
      {
        std::unique_lock<std::mutex> l(pm);
        bParked = pcv.wait_for(l, std::chrono::seconds(10), [&] { return parked2; });
      }
      check("Z3: the new Prepare was held with op 1 reserved, before its own pin", bParked);
      {
        std::lock_guard<std::mutex> l(pm);
        release = true;  // the OLD helper's PinResult arrives now; then its reader finds the pipe gone
        pcv.notify_all();
      }
      const bool goneSeen = wait_until([&] { return host.GetCounters().staleHelperGones > before.staleHelperGones; }, 5000);
      const auto mid = host.GetCounters();
      {
        std::lock_guard<std::mutex> l(pm);
        release2 = true;  // now the new Prepare asks its own helper
        pcv.notify_all();
      }
      bt.join();
      check("Z3: the old helper's PinResult was not taken (a stale frame of an earlier instance)",
            mid.staleHelperFrames > before.staleHelperFrames, "stale frames +" + std::to_string(mid.staleHelperFrames - before.staleHelperFrames));
      check("Z3: the old helper's 'gone' was not taken either", goneSeen, "stale gones +" + std::to_string(mid.staleHelperGones - before.staleHelperGones));
      check("Z3: the new Prepare was accepted", bAnswered && b.verdict == fn::Verdict::Accept,
            "answered=" + std::to_string(bAnswered) + " verdict=" + std::to_string(static_cast<int>(b.verdict)));
      check("Z3: ...with the NEW helper's answer: the second file's size", bAnswered && b.items.size() == 1 && b.items[0].size == 4096,
            "items=" + std::to_string(b.items.size()) + " size0=" + std::to_string(b.items.empty() ? 0 : b.items[0].size));
      const auto after = host.GetCounters();
      check("Z3: the paste begun with the new helper is still there (the old 'gone' did not end it)",
            after.sendOpen && after.sendPasteOp == 1 && after.sendEpochTag == b.epochTag,
            "open=" + std::to_string(after.sendOpen) + " failed +" + std::to_string(after.sendFailed - before.sendFailed));
      bool eAnswered = false;
      fn::EndReply er;
      end_paste(E, hB, 1, &eAnswered, &er);
      check("Z3: the session ends its send (the paste was there to end)", eAnswered && er.state == fn::PasteState::Failed,
            "state=" + std::to_string(static_cast<int>(er.state)));
      check("Z3: ...and the sender is closed", wait_until([&] { return !host.GetCounters().sendOpen; }, 3000));
    }

    // Z4 (r7 2): the pipe HANDLE has one owner and is closed once. The current helper's reader is
    // held inside Receive (registered as in flight, before the read) while the session switch
    // retires that helper (ClosePipe): the handle must stay open for the read, be closed exactly
    // once when the read is out, the new session's helper must work on its own handle, no I/O
    // storage may be orphaned, and none of it may crash -- 50 rounds.
    std::printf("      --- Z4: a read in flight and the retiring close: one owner, one CloseHandle, 50 rounds ---\n");
    {
      std::mutex rpm;
      std::condition_variable rpcv;
      bool rparkNext = false, rparked = false, rrelease = false;
      fc::HelperLink::SetReceiveProbeForTest([&](DWORD helperPid) {
        std::unique_lock<std::mutex> l(rpm);
        if (!rparkNext || helperPid != lastHelperPid.load()) return;  // the host's current helper only
        rparkNext = false;
        rparked = true;
        rpcv.notify_all();
        rpcv.wait(l, [&] { return rrelease; });
      });
      const uint32_t orphansBefore = fc::detail::orphan_counter().load();
      uint32_t openBase = 0;  // what is open when nothing is in flux (measured in the first round)
      const int rounds = 50;
      int parkedOk = 0, heldOk = 0, closedOnceOk = 0, newOk = 0, ok = 0;
      for (int i = 0; i < rounds; ++i) {
        const uint64_t E = 201 + 2 * static_cast<uint64_t>(i), N = E + 1;
        bool oAnswered = false;
        fn::OfferReply o;
        offer_call(E, 0xC100 + static_cast<uint64_t>(i), &oAnswered, &o);  // the switch to E and E's helper
        const bool started = oAnswered && o.verdict == fn::Verdict::Accept;
        if (i == 0) {
          Sleep(300);  // the helper retired by this first switch closes on its reader's way out
          openBase = fc::HelperLink::pipes_created() - fc::HelperLink::pipes_closed();
        }
        {
          std::lock_guard<std::mutex> l(rpm);
          rparkNext = true;  // the next Receive (E's reader, polling) is held before its read
          rparked = false;
          rrelease = false;
        }
        bool parkedNow = false;
        {
          std::unique_lock<std::mutex> l(rpm);
          parkedNow = rpcv.wait_for(l, std::chrono::seconds(5), [&] { return rparked; });
        }
        // Settle: the previous round's retired pipe closes on its reader's way out (within a poll);
        // then only the same handles as before the loop (the viewer's, one host helper) are open,
        // so the counts below are exact.
        wait_until([&] { return fc::HelperLink::pipes_created() - fc::HelperLink::pipes_closed() == openBase; }, 3000);
        const uint32_t createdBefore = fc::HelperLink::pipes_created(), closedBefore = fc::HelperLink::pipes_closed();
        host.OnSessionEnd(N);  // retires E's helper: Shutdown, then ClosePipe -- with a read in flight
        Sleep(50);
        const uint32_t closedMid = fc::HelperLink::pipes_closed();
        const bool held = closedMid == closedBefore;  // not closed under the read's feet
        {
          std::lock_guard<std::mutex> l(rpm);
          rrelease = true;  // the read runs on the still-open handle, then the reader closes it, once
          rpcv.notify_all();
        }
        const bool closedOnce = wait_until([&] { return fc::HelperLink::pipes_closed() == closedBefore + 1; }, 5000) &&
                                fc::HelperLink::pipes_closed() == closedBefore + 1;
        bool nAnswered = false;
        fn::OfferReply n;
        offer_call(N, 0xC200 + static_cast<uint64_t>(i), &nAnswered, &n);  // the new session's helper, its own handle
        const bool fresh = nAnswered && n.verdict == fn::Verdict::Accept && fc::HelperLink::pipes_created() == createdBefore + 1;
        parkedOk += parkedNow ? 1 : 0;
        heldOk += held ? 1 : 0;
        closedOnceOk += closedOnce ? 1 : 0;
        newOk += fresh ? 1 : 0;
        ok += (started && parkedNow && held && closedOnce && fresh) ? 1 : 0;
      }
      fc::HelperLink::SetReceiveProbeForTest(nullptr);
      check("Z4: the reader was held inside Receive (in flight, before the read) every round", parkedOk == rounds,
            std::to_string(parkedOk) + "/" + std::to_string(rounds));
      check("Z4: the retiring ClosePipe did not close the handle under the read (deferred) every round", heldOk == rounds,
            std::to_string(heldOk) + "/" + std::to_string(rounds));
      check("Z4: the handle was closed exactly once, by the read on its way out, every round", closedOnceOk == rounds,
            std::to_string(closedOnceOk) + "/" + std::to_string(rounds));
      check("Z4: the new session's helper came up on a handle of its own every round", newOk == rounds,
            std::to_string(newOk) + "/" + std::to_string(rounds));
      check("Z4: no I/O storage was orphaned", fc::detail::orphan_counter().load() == orphansBefore,
            "orphans +" + std::to_string(fc::detail::orphan_counter().load() - orphansBefore));
      check("Z4: all rounds clean (no crash)", ok == rounds, std::to_string(ok) + "/" + std::to_string(rounds));
    }

    // Z5 (r8 1): the helper is replaced in the MIDDLE of a request. The new Prepare has reserved and
    // cleared its answer flag, and is inside H2's start (H1 still the current instance) when H1's
    // late PinResult (op 1) arrives and is stored -- a legitimate adoption at that moment. H2 is then
    // adopted and the new Pin goes to it, and H2's answer is held: the Prepare must NOT answer on
    // H1's stored value; it answers only once H2's own answer comes. Two orders of H1's "gone":
    // (a) held until the end (Codex's order), then let in: not taken; (b) let in during H2's start
    // (H1 still current): the reservation, sent to nobody yet, must survive it.
    std::printf("      --- Z5: an answer stored before the new pin went out is not the new pin's answer ---\n");
    for (int variant = 0; variant < 2; ++variant) {
      const std::string tag = std::string("Z5") + (variant == 0 ? "a" : "b") + ": ";
      const uint64_t E = 401 + static_cast<uint64_t>(variant);  // after Z4 (201..300): epochs only go up
      const std::wstring z5 = remoteDir + (variant == 0 ? L"\\epochZ5a.txt" : L"\\epochZ5b.txt");
      write_file(z5, make_content(2048, 211 + variant));
      uint64_t h = host_offer_in(E, z5);
      // Settle: the worker may identify the copy a second time (an offer query that came while its
      // helper was starting asks once more, up to half a second later). Take the offer as it stands
      // once that is over, so no identification is in flight when H1 is ended below -- otherwise the
      // worker itself would start H2 for it, and there would be no start left for the new Prepare.
      Sleep(1500);
      {
        std::vector<uint8_t> r0;
        fn::OfferQueryReply q0;
        if (call(fn::FileMsg::OfferQuery, fn::body(fn::OfferQuery{0}), E, &r0) && fn::parse(r0, &q0) && q0.offerId != 0) h = q0.offerId;
      }
      check(tag + "the session sees its copy", h != 0);
      const DWORD pid1 = lastHelperPid.load();
      const auto before = host.GetCounters();
      // 1. H1's PinResult (op 1) is held on its reader; the first Prepare times out and releases
      //    its reservation; H1 dies (its reader, parked, has not noticed).
      {
        std::lock_guard<std::mutex> l(pm);
        parkPoint = kPinResultPoint;
        parkEpoch = E;
        parked = false;
        release = false;
        parkPoint2 = 10;  // H1's "gone", when it comes
        parkEpoch2 = E;
        parked2 = false;
        release2 = false;
      }
      bool aAnswered = false;
      fn::PrepareReply a;
      std::thread at([&] { prepare_rtop(E, h, 1, &aAnswered, &a); });
      bool readerParked = false;
      {
        std::unique_lock<std::mutex> l(pm);
        readerParked = pcv.wait_for(l, std::chrono::seconds(10), [&] { return parked; });
      }
      const auto atPark = host.GetCounters();
      check(tag + "H1's PinResult was held", readerParked,
            "launches +" + std::to_string(atPark.helperLaunches - before.helperLaunches) + " hostOffers +" +
                std::to_string(atPark.hostOffers - before.hostOffers) + " pid1=" + std::to_string(pid1));
      HANDLE hp = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid1);
      bool killed = false;
      if (hp) {
        killed = TerminateProcess(hp, 9) != FALSE && WaitForSingleObject(hp, 5000) == WAIT_OBJECT_0;
        CloseHandle(hp);
      }
      check(tag + "H1's process was ended", killed, "pid1=" + std::to_string(pid1));
      at.join();
      {
        const auto c = host.GetCounters();
        std::printf("      Z5 after the first Prepare: launches +%llu hostOffers +%llu pidNow=%lu\n",
                    static_cast<unsigned long long>(c.helperLaunches - before.helperLaunches),
                    static_cast<unsigned long long>(c.hostOffers - before.hostOffers), static_cast<unsigned long>(lastHelperPid.load()));
      }
      check(tag + "the first Prepare gave up (its pin timed out) and released its reservation",
            aAnswered && a.verdict != fn::Verdict::Accept && hostArbiter.use() == BulkUse::Idle,
            "verdict=" + std::to_string(static_cast<int>(a.verdict)));
      // 2. The new Prepare (op 1, the cached offer) reserves, clears its flag, starts H2 -- and is
      //    held inside H2's start.
      {
        std::lock_guard<std::mutex> l(lpm);
        launchPark = true;
        launchParked = false;
        launchRelease = false;
      }
      bool bAnswered = false;
      fn::PrepareReply b;
      std::atomic<bool> bDone{false};
      std::thread bt([&] {
        prepare_rtop(E, h, 1, &bAnswered, &b);
        bDone.store(true);
      });
      bool lp = false;
      {
        std::unique_lock<std::mutex> l(lpm);
        lp = lpcv.wait_for(l, std::chrono::seconds(15), [&] { return launchParked; });
      }
      const auto atB = host.GetCounters();
      check(tag + "the new Prepare is held inside H2's start (H1 still the current instance)", lp,
            "launches +" + std::to_string(atB.helperLaunches - before.helperLaunches) + " hostOffers +" +
                std::to_string(atB.hostOffers - before.hostOffers) + " pidNow=" + std::to_string(lastHelperPid.load()) +
                " bDone=" + std::to_string(bDone.load()));
      // 3. H1's PinResult goes in now (current instance: stored). Its "gone" follows and is held.
      {
        std::lock_guard<std::mutex> l(pm);
        release = true;
        pcv.notify_all();
      }
      bool goneParked = false;
      {
        std::unique_lock<std::mutex> l(pm);
        goneParked = pcv.wait_for(l, std::chrono::seconds(10), [&] { return parked2; });
      }
      check(tag + "H1's 'gone' followed and was held", goneParked);
      if (variant == 1) {
        // (b) the old "gone" goes in while H2 is still starting: H1 is the current instance, but the
        // reservation was sent to nobody -- it must survive.
        std::lock_guard<std::mutex> l(pm);
        release2 = true;
        pcv.notify_all();
      }
      Sleep(200);
      {
        std::lock_guard<std::mutex> l(pm);
        parkPoint = kPinResultPoint;  // H2's PinResult, when it comes
        parkEpoch = E;
        parked = false;
        release = false;
      }
      // 4. H2 is adopted, the new Pin goes to it; H2's answer is held.
      {
        std::lock_guard<std::mutex> l(lpm);
        launchRelease = true;
        lpcv.notify_all();
      }
      bool h2Parked = false;
      {
        std::unique_lock<std::mutex> l(pm);
        h2Parked = pcv.wait_for(l, std::chrono::seconds(15), [&] { return parked; });
      }
      check(tag + "H2 answered the new Pin and that answer is held", h2Parked);
      Sleep(1000);
      check(tag + "the new Prepare did NOT answer on H1's stored value (it waits for H2's)", !bDone.load(),
            "answered=" + std::to_string(bAnswered) + " verdict=" + std::to_string(static_cast<int>(b.verdict)));
      {
        std::lock_guard<std::mutex> l(pm);
        release = true;  // H2's answer goes in
        pcv.notify_all();
      }
      bt.join();
      check(tag + "...and accepted once H2's own answer came", bAnswered && b.verdict == fn::Verdict::Accept && b.items.size() == 1,
            "answered=" + std::to_string(bAnswered) + " verdict=" + std::to_string(static_cast<int>(b.verdict)));
      const auto after = host.GetCounters();
      check(tag + "the sender is this Prepare's", after.sendOpen && after.sendEpochTag == b.epochTag && after.sendPasteOp == 1);
      if (variant == 0) {
        std::lock_guard<std::mutex> l(pm);
        release2 = true;  // (a) H1's "gone" goes in only now: not the current instance's
        pcv.notify_all();
      }
      check(tag + "H1's 'gone' did not end the paste " + (variant == 0 ? "(let in after H2: stale)" : "(let in during H2's start: sent to nobody)"),
            wait_until([&] { return host.GetCounters().staleHelperGones > before.staleHelperGones || variant == 1; }, 5000) &&
                host.GetCounters().sendOpen,
            "stale gones +" + std::to_string(host.GetCounters().staleHelperGones - before.staleHelperGones));
      bool eAnswered = false;
      fn::EndReply er;
      end_paste(E, h, 1, &eAnswered, &er);
      check(tag + "the session ends its send", eAnswered && er.state == fn::PasteState::Failed);
      check(tag + "...and the sender is closed", wait_until([&] { return !host.GetCounters().sendOpen; }, 3000));
    }
    host.SetEpochProbeForTest(nullptr);
  }
  hostRx.join();
  viewRx.join();
  viewer.Stop();
  host.Stop();
  check("every pipe handle this process created was closed exactly once (r7)",
        fc::HelperLink::pipes_closed() == fc::HelperLink::pipes_created(),
        "created=" + std::to_string(fc::HelperLink::pipes_created()) + " closed=" + std::to_string(fc::HelperLink::pipes_closed()));
  closesocket(hostSock);
  closesocket(viewSock);
  check("the control link never went out of step", !linkBroken.load());
  {
    // Who wrote it, if it moved: this test (or a child it started) would be a defect; another program
    // on this console (the user copying while the test runs) is not this test's doing.
    const DWORD seqNow = GetClipboardSequenceNumber();
    DWORD ownerPid = 0;
    if (HWND owner = GetClipboardOwner()) GetWindowThreadProcessId(owner, &ownerPid);
    const bool moved = seqNow != userClipBefore;
    const bool ours = moved && ownerPid == GetCurrentProcessId();
    check("the user's clipboard (WinSta0) was not written by this test", !ours,
          moved ? "moved by pid " + std::to_string(ownerPid) + (ours ? " (THIS TEST)" : " (not this test)") : "unchanged");
  }
  if (gFailures) {
    // The helper's own account (it names no path) -- the staging directory goes next.
    for (const std::wstring* log : {&helperLog, &viewerHelperLog}) {
      std::printf("\n--- %s ---\n", log == &helperLog ? "host helper log" : "viewer helper log");
      if (FILE* f = _wfopen(log->c_str(), L"rb")) {
        char b[4096];
        size_t n;
        while ((n = std::fread(b, 1, sizeof(b), f)) > 0) std::fwrite(b, 1, n, stdout);
        std::fclose(f);
      }
    }
  }
  // The private station is the logon session's unnamed one (a Medium process cannot name one), so
  // ANOTHER run on this PC -- another agent's test at the same time -- shares its clipboard. The
  // helpers log who took the clipboard over; a writer that is none of ours makes a failed run
  // INVALID (interfered with), not a product failure. A run without one is judged as it is.
  std::vector<DWORD> foreign;
  for (const std::wstring* log : {&helperLog, &viewerHelperLog}) {
    std::ifstream in(*log);
    std::string line;
    while (std::getline(in, line)) {
      size_t at = line.find(" ready pid=");
      if (at != std::string::npos) ourPids.push_back(static_cast<DWORD>(std::strtoul(line.c_str() + at + 11, nullptr, 10)));
      at = line.find("(owner pid=");
      if (at != std::string::npos) {
        const DWORD pid = static_cast<DWORD>(std::strtoul(line.c_str() + at + 11, nullptr, 10));
        if (pid != 0) foreign.push_back(pid);
      }
    }
  }
  std::vector<DWORD> strangers;
  for (DWORD pid : foreign) {
    bool ours = false;
    for (DWORD o : ourPids) ours = ours || o == pid;
    bool listed = false;
    for (DWORD s : strangers) listed = listed || s == pid;
    if (!ours && !listed) strangers.push_back(pid);
  }
  if (!strangers.empty()) {
    std::string list;
    for (DWORD s : strangers) list += std::to_string(s) + " ";
    std::printf("\nNOTE  another process wrote the shared window station's clipboard during this run: pid %s\n", list.c_str());
  }
  const bool removed = staging.Remove();
  check("the staging directory is removed" + (removed ? std::string() : ": " + staging.why()), removed);
  if (gFailures && !strangers.empty()) {
    std::printf("\nRESULT: INVALID  (%d checks, %d failed -- with a foreign clipboard writer on the shared station; rerun alone)\n",
                gChecks, gFailures);
    return 3;
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "PASSED", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
