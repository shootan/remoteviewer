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
#include <cstdio>
#include <fstream>
#include <functional>
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

 private:
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
  cfg.launcher = [&](fc::HelperLink* link, std::string* why) {
    std::wstring sid;
    if (!fc::current_process_user_sid(&sid)) {
      *why = "no user SID";
      return false;
    }
    return link->CreateServerPipe(sid, why) &&
           link->Launch(helperExe, nullptr, station.desktop.c_str(), L"--idle-ms 8000 --log \"" + helperLog + L"\"", why) &&
           link->AwaitHello(10000, why);
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
  viewer.SetHelperLauncher([&](fc::HelperLink* link, std::string* why) {
    return fc::launch_file_copy_helper_as_self(helperExe, local.desktop.c_str(),
                                               L"--idle-ms 8000 --log \"" + viewerHelperLog + L"\"", link, why);
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
    const int aftersBefore = fakeImage.afters.load();
    Child consumer;
    const std::wstring dest = root + L"destQ3";
    paste_on(local, dest, content.size(), 60, &consumer);
    // While the paste waits for the image: the remote clipboard gets another copy (not files).
    Sleep(700);
    host.OnHostClipboard(++hostSeq, {});
    const DWORD code = consumer.Wait(90000);
    std::map<std::wstring, std::vector<uint8_t>> setQ = {{L"d5R.bin", content}};
    std::string detail;
    check("R->P: the paste waited for the image's end, then completed whole", code == 0 && same_files(dest, setQ, &detail), detail);
    check("...and because the remote clipboard changed meanwhile, the image may NOT go again", wait_until([&] {
            return fakeImage.afters.load() == aftersBefore + 1 && fakeImage.lastMayResume.load() == 0;
          }, 5000), "mayResume=" + std::to_string(fakeImage.lastMayResume.load()));
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
  hostRx.join();
  viewRx.join();
  viewer.Stop();
  host.Stop();
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
