// File copy P->R, end to end in one process plus the real helper and a real paste consumer.
// (t-zdmsd4gb r1 step 1)
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
  bool Create() {
    ws = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);  // unnamed: medium may not name one
    if (!ws) return false;
    wchar_t name[256] = L"";
    DWORD len = 0;
    GetUserObjectInformationW(ws, UOI_NAME, name, sizeof(name), &len);
    HWINSTA orig = GetProcessWindowStation();
    SetProcessWindowStation(ws);
    dk = CreateDesktopW(L"Default", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    SetProcessWindowStation(orig);
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
  const auto host_send = [&](const void* d, size_t n) {
    return sendto(hostSock, static_cast<const char*>(d), static_cast<int>(n), 0, reinterpret_cast<const sockaddr*>(&viewAddr),
                  sizeof(viewAddr)) > 0;
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
  FileCopyClient viewer;
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
  std::thread pump([&] {
    while (!stop.load()) {
      if (viewer.Pump(link) < 0) linkBroken.store(true);
      Sleep(10);
    }
  });

  uint64_t revision = 100;
  const auto paste = [&](const std::wstring& dest, uint64_t expectBytes, int waitSec, Child* consumer) {
    CreateDirectoryW(dest.c_str(), nullptr);
    const std::wstring res = dest + L".result.txt";
    const std::wstring cmd = L"\"" + consumerExe + L"\" --consumer --mode drop --dest \"" + dest + L"\" --result \"" + res +
                             L"\" --expect-bytes " + std::to_wstring(expectBytes) + L" --wait-sec " + std::to_wstring(waitSec);
    return consumer->Start(cmd, station.desktop);
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
    check("every delivered byte passed its chunk check, none rejected",
          hc.chunksRejected == 0 && hc.chunksVerified > 0 && hc.bytesDelivered == totalA,
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

  // ------------------------------------------------------------------ teardown
  stop.store(true);
  pump.join();
  hostRx.join();
  viewRx.join();
  viewer.Stop();
  host.Stop();
  closesocket(hostSock);
  closesocket(viewSock);
  check("the control link never went out of step", !linkBroken.load());
  check("the user's clipboard (WinSta0) did not move", GetClipboardSequenceNumber() == userClipBefore);
  if (gFailures) {
    // The helper's own account (it names no path) -- the staging directory goes next.
    std::printf("\n--- helper log ---\n");
    if (FILE* f = _wfopen(helperLog.c_str(), L"rb")) {
      char b[4096];
      size_t n;
      while ((n = std::fread(b, 1, sizeof(b), f)) > 0) std::fwrite(b, 1, n, stdout);
      std::fclose(f);
    }
  }
  const bool removed = staging.Remove();
  check("the staging directory is removed" + (removed ? std::string() : ": " + staging.why()), removed);
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "PASSED", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
