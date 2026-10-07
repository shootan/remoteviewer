// "로그인 전에도 원격 접속 허용" -- the request codec, the service's decision over its primitives, and the
// real pipe's two refusals (prelogin-ui r1, item 4). The primitives here are in-memory stand-ins for
// the machine store, the floor and the SCM; the decision and the pipe are the product's.

#include <windows.h>
#include <sddl.h>

#include <atomic>
#include <cstdio>
#include <map>
#include <string>
#include <functional>
#include <thread>

#include "prelogin_admin_pipe.hpp"

namespace pl = remote60::native_poc::prelogin;

namespace {
int gChecks = 0, gFailures = 0;
void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}

// The service's world, in memory.
struct World {
  std::map<std::wstring, pl::MachineRecord> files;
  uint64_t floor = 0;
  uint32_t startType = 3;  // DEMAND
  bool haveRemembered = false;
  uint32_t remembered = 0;
  int calls = 0;
  uint32_t failSetStartType = 0;
  int osReboots = 0;          // r2: the OS seam
  uint32_t osRebootResult = 0;
  int readies = 0;
  uint32_t readySession = 0;
  uint64_t readyStart = 0;
  pl::AdminPrimitives primitives() {
    pl::AdminPrimitives p;
    p.requestOsReboot = [this]() -> uint32_t { ++osReboots; return osRebootResult; };
    p.noteUserHostReady = [this](uint32_t s, uint64_t t) { ++readies; readySession = s; readyStart = t; };
    p.recordPath = [this] { ++calls; return std::wstring(L"C:\\ProgramData\\GNLink\\credential\\machine.cred"); };
    p.machineId = [this] { ++calls; return std::string("11111111-2222-3333-4444-555555555555"); };
    p.readFloor = [this] { ++calls; return floor; };
    p.raiseFloor = [this](uint64_t f) { ++calls; if (f > floor) floor = f; return true; };
    p.readRecord = [this](const std::wstring& path, pl::MachineRecord* out) {
      ++calls;
      auto it = files.find(path);
      if (it == files.end()) return false;
      *out = it->second;
      return true;
    };
    p.writeRecord = [this](const std::wstring& path, const pl::MachineRecord& r) -> uint32_t { ++calls; files[path] = r; return 0; };
    p.removeRecord = [this](const std::wstring& path) -> uint32_t { ++calls; files.erase(path); return 0; };
    p.featureEnabled = [this] {
      ++calls;
      auto it = files.find(L"C:\\ProgramData\\GNLink\\credential\\machine.cred");
      if (it == files.end()) return false;
      pl::BindingContext ctx;
      ctx.currentMachineId = "11111111-2222-3333-4444-555555555555";
      ctx.originAllowed = true;
      ctx.minOwnerGeneration = floor;
      return pl::ValidateMachineRecord(it->second, ctx) == pl::RecordVerdict::Usable;
    };
    p.queryStartType = [this](uint32_t* out) { ++calls; *out = startType; return true; };
    p.setStartType = [this](uint32_t t) -> uint32_t {
      ++calls;
      if (failSetStartType) return failSetStartType;
      startType = t;
      return 0;
    };
    p.loadRememberedStartType = [this](uint32_t* out) { ++calls; if (!haveRemembered) return false; *out = remembered; return true; };
    p.rememberStartType = [this](uint32_t t) { ++calls; haveRemembered = true; remembered = t; return true; };
    p.forgetRememberedStartType = [this] { ++calls; haveRemembered = false; };
    return p;
  }
};

const std::wstring kPath = L"C:\\ProgramData\\GNLink\\credential\\machine.cred";

pl::AdminRequest enable_request() {
  pl::AdminRequest r;
  r.op = pl::AdminOp::Enable;
  r.directoryUrl = "https://gnlink.example:8443";
  r.accountId = "host-user";
  r.hostId = "h-1234";
  r.hostToken = "token-abcdef";
  return r;
}

std::wstring current_user_sddl() {
  HANDLE tok = nullptr;
  OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
  DWORD need = 0;
  GetTokenInformation(tok, TokenUser, nullptr, 0, &need);
  std::string buf(need, '\0');
  GetTokenInformation(tok, TokenUser, buf.data(), need, &need);
  CloseHandle(tok);
  LPWSTR sid = nullptr;
  ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &sid);
  std::wstring s = L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sid) + L")";
  LocalFree(sid);
  return s;
}

bool wait_until_ms(const std::function<bool()>& done, int ms) {
  const ULONGLONG until = GetTickCount64() + ms;
  while (GetTickCount64() < until) {
    if (done()) return true;
    Sleep(50);
  }
  return done();
}

bool process_is_elevated() {
  HANDLE tok = nullptr;
  OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
  TOKEN_ELEVATION e{};
  DWORD n = 0;
  GetTokenInformation(tok, TokenElevation, &e, sizeof(e), &n);
  CloseHandle(tok);
  return e.TokenIsElevated != 0;
}

// Runs a server on a test pipe until stopped; the requests are the client's, the decision the product's.
struct TestServer {
  std::atomic<bool> run{true};
  std::thread t;
  pl::RebootLedger ledger;
  void start(World* w, const std::wstring& name, const std::wstring& sddl, pl::AdminIdentityCheck check,
             pl::AdminIdentityCheck host = nullptr) {
    t = std::thread([this, w, name, sddl, check, host] {
      pl::AdminPrimitives p = w->primitives();
      pl::RunPreloginAdminServer(p, [this] { return run.load(); }, name.c_str(), sddl.c_str(), check, host, &ledger);
    });
    Sleep(200);
  }
  void stop() {
    run = false;
    if (t.joinable()) t.join();
  }
};
}  // namespace

int main() {
  // ---------------------------------------------------------------- codec
  {
    const pl::AdminRequest r = enable_request();
    pl::AdminRequest back;
    std::vector<uint8_t> b = pl::EncodeAdminRequest(r);
    check("Enable request round-trips", pl::DecodeAdminRequest(b, &back) && back.op == pl::AdminOp::Enable &&
                                            back.hostToken == r.hostToken && back.directoryUrl == r.directoryUrl);
    std::vector<uint8_t> t = b;
    t.pop_back();
    check("...truncated is refused", !pl::DecodeAdminRequest(t, &back));
    t = b;
    t.push_back(0);
    check("...trailing bytes are refused", !pl::DecodeAdminRequest(t, &back));
    t = b;
    t[6] = 9;  // the op byte
    check("...an unknown op is refused", !pl::DecodeAdminRequest(t, &back));
    pl::AdminRequest big = r;
    big.hostToken.assign(pl::kAdminMaxToken + 1, 'x');
    check("...an oversize field is refused", !pl::DecodeAdminRequest(pl::EncodeAdminRequest(big), &back));
    pl::AdminRequest q;
    q.op = pl::AdminOp::Query;
    check("Query carries no credential", pl::EncodeAdminRequest(q).size() == 7);
    pl::AdminReply rep{pl::AdminStatus::Failed, true, 1314, 77}, rback;
    check("reply round-trips", pl::DecodeAdminReply(pl::EncodeAdminReply(rep), &rback) && rback.status == pl::AdminStatus::Failed &&
                                   rback.enabled && rback.win32 == 1314 && rback.ownerGeneration == 77);
  }

  // ---------------------------------------------------------------- the decision
  {
    World w;
    pl::AdminPrimitives p = w.primitives();
    const pl::AdminReply refused = pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request()), /*clientIsAdmin=*/false, p);
    check("a client that is not an elevated admin: Refused, and nothing of the service is touched",
          refused.status == pl::AdminStatus::Refused && w.calls == 0 && w.files.empty() && w.startType == 3, std::to_string(w.calls));
  }
  {
    World w;
    w.floor = 5;
    pl::AdminPrimitives p = w.primitives();
    const pl::AdminReply on = pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request()), true, p);
    const auto it = w.files.find(kPath);
    check("on: Ok and the service now says it is on", on.status == pl::AdminStatus::Ok && on.enabled);
    check("...the record is written by the service: enabled, above the floor, this machine, the server's URL",
          it != w.files.end() && it->second.enabled && it->second.ownerGeneration == 6 &&
              it->second.machineId == "11111111-2222-3333-4444-555555555555" &&
              it->second.directoryOrigin == "https://gnlink.example:8443");
    pl::DirectoryCredential dc;
    check("...its credential unpacks to the host's directory credential (the --prelogin worker's format)",
          it != w.files.end() && pl::UnpackDirectoryCredential(it->second.credential, &dc) && dc.accountId == "host-user" &&
              dc.hostId == "h-1234" && dc.hostToken == "token-abcdef");
    check("...the start type was remembered (DEMAND) and raised to AUTO", w.haveRemembered && w.remembered == 3 && w.startType == 2);
    const pl::MachineRecord before = it->second;
    const pl::AdminReply again = pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request()), true, p);
    check("a second on keeps the first remembered start type (not AUTO)", again.status == pl::AdminStatus::Ok && w.remembered == 3);
    pl::AdminRequest off;
    off.op = pl::AdminOp::Disable;
    const pl::AdminReply o = pl::HandleAdminRequest(pl::EncodeAdminRequest(off), true, p);
    check("off: Ok, the service says it is off, the record is gone",
          o.status == pl::AdminStatus::Ok && !o.enabled && w.files.find(kPath) == w.files.end());
    check("...the start type is put back as it was (DEMAND) and forgotten", w.startType == 3 && !w.haveRemembered);
    check("...the floor is above every record written so far", w.floor > before.ownerGeneration && w.floor >= 8, std::to_string(w.floor));
    w.files[kPath] = before;  // an old enabled blob put back (backup restore / rollback)
    check("an old enabled record put back after off is NOT usable (Revoked by the floor)", !p.featureEnabled());
    pl::AdminRequest q;
    q.op = pl::AdminOp::Query;
    check("...and a query says off", !pl::HandleAdminRequest(pl::EncodeAdminRequest(q), true, p).enabled);
  }
  {
    World w;
    w.startType = 4;  // DISABLED before the feature
    pl::AdminPrimitives p = w.primitives();
    pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request()), true, p);
    pl::AdminRequest off;
    off.op = pl::AdminOp::Disable;
    pl::HandleAdminRequest(pl::EncodeAdminRequest(off), true, p);
    check("off restores the ORIGINAL start type, not always DEMAND", w.startType == 4, std::to_string(w.startType));
  }
  {
    World w;
    w.failSetStartType = 1060;
    pl::AdminPrimitives p = w.primitives();
    const pl::AdminReply r = pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request()), true, p);
    check("on, but the start type cannot be set: Failed with the error, and not half on (record withdrawn, floor raised)",
          r.status == pl::AdminStatus::Failed && r.win32 == 1060 && !r.enabled && w.files.empty() && w.floor >= 2);
  }
  {
    World w;
    pl::AdminPrimitives p = w.primitives();
    pl::AdminRequest r = enable_request();
    r.hostToken.clear();
    check("on without the host's credential: BadRequest, nothing written",
          pl::HandleAdminRequest(pl::EncodeAdminRequest(r), true, p).status == pl::AdminStatus::BadRequest && w.files.empty());
  }

  // ---------------------------------------------------------------- the real pipe
  const bool elevated = process_is_elevated();
  std::printf("      this test runs %s\n", elevated ? "ELEVATED" : "not elevated");
  {
    // The product's identity check, on a pipe this user can open: a non-elevated client is Refused.
    World w;
    TestServer s;
    const std::wstring name = L"\\\\.\\pipe\\GNLinkTest-admin-" + std::to_wstring(GetCurrentProcessId()) + L"-a";
    s.start(&w, name, current_user_sddl(), pl::ClientIsElevatedAdministrator);
    const pl::AdminReply r = pl::SendPreloginAdminRequest(enable_request(), 3000, name.c_str());
    s.stop();
    if (!elevated) {
      check("the product's check over a real pipe: a non-elevated client is Refused, nothing written",
            r.status == pl::AdminStatus::Refused && w.files.empty(), std::to_string(static_cast<int>(r.status)));
    } else {
      check("(elevated run) the product's check over a real pipe admits an elevated admin", r.status == pl::AdminStatus::Ok);
    }
  }
  {
    // The product's DACL (SYSTEM + Administrators): a non-elevated client cannot even open it.
    World w;
    TestServer s;
    const std::wstring name = L"\\\\.\\pipe\\GNLinkTest-admin-" + std::to_wstring(GetCurrentProcessId()) + L"-b";
    s.start(&w, name, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)", [](void*) { return true; });
    const pl::AdminReply r = pl::SendPreloginAdminRequest(enable_request(), 3000, name.c_str());
    s.stop();
    if (!elevated) {
      check("the product's DACL: a non-elevated client is denied at open (Refused), nothing written",
            r.status == pl::AdminStatus::Refused && r.win32 == ERROR_ACCESS_DENIED && w.files.empty(),
            std::to_string(static_cast<int>(r.status)) + " " + std::to_string(r.win32));
    }
  }
  {
    // With an admitted client, the request over the real pipe reaches the service's primitives.
    World w;
    TestServer s;
    const std::wstring name = L"\\\\.\\pipe\\GNLinkTest-admin-" + std::to_wstring(GetCurrentProcessId()) + L"-c";
    s.start(&w, name, current_user_sddl(), [](void*) { return true; });
    const pl::AdminReply on = pl::SendPreloginAdminRequest(enable_request(), 3000, name.c_str());
    pl::AdminRequest off;
    off.op = pl::AdminOp::Disable;
    const bool wroteOn = w.files.count(kPath) == 1;
    const pl::AdminReply o = pl::SendPreloginAdminRequest(off, 3000, name.c_str());
    s.stop();
    check("over the real pipe: on reaches the service (record written, AUTO)", on.status == pl::AdminStatus::Ok && on.enabled && wroteOn);
    check("over the real pipe: off reaches it (record gone, start type back)",
          o.status == pl::AdminStatus::Ok && !o.enabled && w.files.empty() && w.startType == 3);
  }
  {
    const pl::AdminReply r = pl::SendPreloginAdminRequest(enable_request(), 200, L"\\\\.\\pipe\\GNLinkTest-admin-nobody");
    check("no service listening: Unavailable", r.status == pl::AdminStatus::Unavailable);
  }

  // ---------------------------------------------------------------- r2: reboot (item 6) and ready (item 7)
  {
    pl::AdminRequest rb;
    rb.op = pl::AdminOp::Reboot;
    rb.requestId = 0x1234;
    pl::AdminRequest rb2 = rb;
    rb2.requestId = 0x5678;
    pl::ClientFacts host;
    host.elevatedAdmin = true;
    host.hostProcess = true;
    {
      World w;
      pl::RebootLedger ledger;
      pl::AdminPrimitives p = w.primitives();
      pl::ClientFacts notHost = host;
      notHost.hostProcess = false;
      const pl::AdminReply a = pl::HandleAdminRequest(pl::EncodeAdminRequest(rb), notHost, p, &ledger);
      check("reboot from a process that is not this install's streaming host: Refused, OS seam 0",
            a.status == pl::AdminStatus::Refused && a.rebootResult == 2 && w.osReboots == 0);
      pl::ClientFacts notAdmin = host;
      notAdmin.elevatedAdmin = false;
      const pl::AdminReply b = pl::HandleAdminRequest(pl::EncodeAdminRequest(rb), notAdmin, p, &ledger);
      check("reboot from the host binary run without elevation: Refused, OS seam 0",
            b.status == pl::AdminStatus::Refused && w.osReboots == 0);
      pl::AdminRequest zero = rb;
      zero.requestId = 0;
      check("reboot id 0: BadRequest, OS seam 0",
            pl::HandleAdminRequest(pl::EncodeAdminRequest(zero), host, p, &ledger).status == pl::AdminStatus::BadRequest && w.osReboots == 0);
      const pl::AdminReply c = pl::HandleAdminRequest(pl::EncodeAdminRequest(rb), host, p, &ledger);
      check("reboot from the host: Accepted, OS seam called ONCE", c.status == pl::AdminStatus::Ok && c.rebootResult == 0 && w.osReboots == 1);
      const pl::AdminReply d = pl::HandleAdminRequest(pl::EncodeAdminRequest(rb), host, p, &ledger);
      check("the same id again: the same answer (Accepted), no second OS call", d.rebootResult == 0 && w.osReboots == 1);
      const pl::AdminReply e = pl::HandleAdminRequest(pl::EncodeAdminRequest(rb2), host, p, &ledger);
      check("another id while the restart is under way: Refused, no second OS call", e.rebootResult == 1 && w.osReboots == 1);
      check("...and asked again, still Refused (its own answer cached)",
            pl::HandleAdminRequest(pl::EncodeAdminRequest(rb2), host, p, &ledger).rebootResult == 1 && w.osReboots == 1);
    }
    {
      World w;
      w.osRebootResult = 1115;  // ERROR_SHUTDOWN_IN_PROGRESS
      pl::RebootLedger ledger;
      pl::AdminPrimitives p = w.primitives();
      const pl::AdminReply a = pl::HandleAdminRequest(pl::EncodeAdminRequest(rb), host, p, &ledger);
      check("the OS refuses: Failed with its error, never escalated (one call)", a.rebootResult == 3 && a.win32 == 1115 && w.osReboots == 1);
      check("...the same id: the same Failed, no second call",
            pl::HandleAdminRequest(pl::EncodeAdminRequest(rb), host, p, &ledger).rebootResult == 3 && w.osReboots == 1);
      w.osRebootResult = 0;
      check("...a NEW id after a failure may try again (one more call)",
            pl::HandleAdminRequest(pl::EncodeAdminRequest(rb2), host, p, &ledger).rebootResult == 0 && w.osReboots == 2);
    }
    {
      World w;
      pl::AdminPrimitives p = w.primitives();
      pl::AdminRequest ready;
      ready.op = pl::AdminOp::Ready;
      pl::ClientFacts h = host;
      h.sessionId = 3;
      h.processStartTime = 1000;
      pl::ClientFacts notHost = h;
      notHost.hostProcess = false;
      check("ready from a process that is not the host: Refused, not recorded",
            pl::HandleAdminRequest(pl::EncodeAdminRequest(ready), notHost, p, nullptr).status == pl::AdminStatus::Refused && w.readies == 0);
      check("ready from the host: recorded with the session and start time the SERVER established",
            pl::HandleAdminRequest(pl::EncodeAdminRequest(ready), h, p, nullptr).status == pl::AdminStatus::Ok && w.readies == 1 &&
                w.readySession == 3 && w.readyStart == 1000);
    }
    {
      pl::UserHostReadyTracker t;
      check("handoff: no ready yet -> not complete (the prelogin worker keeps the lease)", !t.Completes(3, 500));
      t.Note(3, 400);
      check("handoff: a ready from a host started BEFORE this launch (an old generation) -> discarded", !t.Completes(3, 500));
      t.Note(4, 600);
      check("handoff: a ready from another session -> not complete", !t.Completes(3, 500));
      t.Note(3, 600);
      check("handoff: a ready from this session's host started after the launch -> complete", t.Completes(3, 500));
      t.Reset();
      check("handoff: a repeated LOGON relaunch resets it; the earlier ready no longer counts", !t.Completes(3, 700));
      t.Note(3, 650);
      check("...and a ready from before that relaunch is stale", !t.Completes(3, 700));
    }
    {
      // The real pipe: the product's host check is the image path. This test is not GNLinkStream.exe.
      World w;
      TestServer s;
      const std::wstring name = L"\\\\.\\pipe\\GNLinkTest-admin-" + std::to_wstring(GetCurrentProcessId()) + L"-d";
      s.start(&w, name, current_user_sddl(), [](void*) { return true; },
              [](void* pipe) { return pl::ClientImageIs(pipe, L"C:\\Program Files\\GNLink\\GNLinkStream.exe"); });
      const pl::AdminReply forged = pl::SendPreloginAdminRequest(rb, 3000, name.c_str());
      pl::AdminRequest ready;
      ready.op = pl::AdminOp::Ready;
      const pl::AdminReply forgedReady = pl::SendPreloginAdminRequest(ready, 3000, name.c_str());
      s.stop();
      check("over the real pipe, a client that is not the install's GNLinkStream.exe: reboot Refused, OS seam 0",
            forged.status == pl::AdminStatus::Refused && w.osReboots == 0);
      check("...and its ready is Refused, not recorded", forgedReady.status == pl::AdminStatus::Refused && w.readies == 0);
    }
    {
      wchar_t me[MAX_PATH] = {};
      GetModuleFileNameW(nullptr, me, MAX_PATH);
      const std::wstring self = me;
      World w;
      TestServer s;
      const std::wstring name = L"\\\\.\\pipe\\GNLinkTest-admin-" + std::to_wstring(GetCurrentProcessId()) + L"-e";
      s.start(&w, name, current_user_sddl(), [](void*) { return true; },
              [self](void* pipe) { return pl::ClientImageIs(pipe, self); });  // this test stands in as "the host"
      const pl::AdminReply a = pl::SendPreloginAdminRequest(rb, 3000, name.c_str());
      const pl::AdminReply b = pl::SendPreloginAdminRequest(rb, 3000, name.c_str());
      pl::AdminRequest ready;
      ready.op = pl::AdminOp::Ready;
      const pl::AdminReply c = pl::SendPreloginAdminRequest(ready, 3000, name.c_str());
      s.stop();
      FILETIME cr{}, ex{}, k{}, u{};
      GetProcessTimes(GetCurrentProcess(), &cr, &ex, &k, &u);
      const uint64_t myStart = (static_cast<uint64_t>(cr.dwHighDateTime) << 32) | cr.dwLowDateTime;
      DWORD mySession = 0;
      ProcessIdToSessionId(GetCurrentProcessId(), &mySession);
      check("over the real pipe, the host's reboot: Accepted, OS seam once; the retransmit: same answer, still once",
            a.rebootResult == 0 && b.rebootResult == 0 && w.osReboots == 1);
      check("over the real pipe, the host's ready: recorded with ITS session and process start time",
            c.status == pl::AdminStatus::Ok && w.readies == 1 && w.readySession == mySession && w.readyStart == myStart);
    }
    {
      // The host's side of item 7, bounded both ways: never registered -> nothing is sent; the service
      // not there yet -> retried until it is (and no further).
      wchar_t me[MAX_PATH] = {};
      GetModuleFileNameW(nullptr, me, MAX_PATH);
      const std::wstring self = me;
      const std::wstring name = L"\\\\.\\pipe\\GNLinkTest-admin-" + std::to_wstring(GetCurrentProcessId()) + L"-f";
      World w;
      TestServer s;
      s.start(&w, name, current_user_sddl(), [](void*) { return true; }, [self](void* pipe) { return pl::ClientImageIs(pipe, self); });
      pl::NotifyUserHostReadyInBackground([] { return false; }, 400, 5, 100, name);
      Sleep(1500);
      check("ready while the host never registered: nothing is sent (the service keeps prelogin)", w.readies == 0);
      s.stop();
      std::atomic<bool> registered{false};
      pl::NotifyUserHostReadyInBackground([&registered] { return registered.load(); }, 10000, 8, 400, name);
      Sleep(300);
      check("...and not before registration, even with the service up later", w.readies == 0);
      registered = true;
      Sleep(900);  // the first attempts find no pipe
      World w2;
      TestServer s2;
      s2.start(&w2, name, current_user_sddl(), [](void*) { return true; }, [self](void* pipe) { return pl::ClientImageIs(pipe, self); });
      const bool arrived = wait_until_ms([&] { return w2.readies >= 1; }, 5000);
      s2.stop();
      check("registered, the service not up yet: the ready is retried and arrives once it is", arrived && w2.readies == 1,
            std::to_string(w2.readies));
    }
  }

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
