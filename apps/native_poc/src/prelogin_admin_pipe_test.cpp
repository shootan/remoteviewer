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
  bool failRemember = false;   // r5 G6
  bool failFloor = false;       // r6 G6: the floor cannot be read
  uint32_t failWrite = 0;
  bool failRaise = false;     // r9: the floor cannot be raised
  bool markBound = false;     // r9 R1: the owed-revoke mark (unbound in the older cases)
  int mark = 0;
  bool issuedKept = false;     // r11 U1: the issued bound has a value (false: none ever kept)
  uint64_t issued = 0;
  bool issuedUnreadable = false;
  bool failRaiseIssued = false;
  bool failReadRecord = false;  // the record is there but cannot be read (ACL / IO / DPAPI)
  std::string order;            // "issued " / "record " as Enable does them
  int osReboots = 0;          // r2: the OS seam
  uint32_t osRebootResult = 0;
  int readies = 0;
  uint32_t readySession = 0;
  uint64_t readyStart = 0;
  std::string readyNonce;
  pl::AdminPrimitives primitives() {
    pl::AdminPrimitives p;
    p.requestOsReboot = [this]() -> uint32_t { ++osReboots; return osRebootResult; };
    p.noteUserHostReady = [this](uint32_t s, uint64_t t, const std::string& n, uint32_t) { ++readies; readySession = s; readyStart = t; readyNonce = n; };
    p.recordPath = [this] { ++calls; return std::wstring(L"C:\\ProgramData\\GNLink\\credential\\machine.cred"); };
    p.machineId = [this] { ++calls; return std::string("11111111-2222-3333-4444-555555555555"); };
    p.readFloor = [this](uint64_t* out) {
      ++calls;
      *out = floor;
      return !failFloor;
    };
    p.raiseFloor = [this](uint64_t f) {
      ++calls;
      if (failRaise) return false;
      if (f > floor) floor = f;
      return true;
    };
    if (markBound) {
      p.revokePending = [this] { return mark; };
      p.clearRevokePending = [this] { mark = 0; return true; };
    }
    p.markRevokePending = [this] { mark = 1; return true; };
    p.readIssued = [this](uint64_t* out) {
      ++calls;
      *out = issuedKept ? issued : 0;
      return issuedUnreadable ? -1 : issuedKept ? 1 : 0;
    };
    p.raiseIssued = [this](uint64_t g) {
      ++calls;
      if (failRaiseIssued || issuedUnreadable) return false;
      order += "issued ";
      issuedKept = true;
      if (g > issued) issued = g;
      return true;
    };
    p.probeRecord = [this](const std::wstring& path) -> int { return files.count(path) ? 1 : 0; };
    p.readRecord = [this](const std::wstring& path, pl::MachineRecord* out) {
      ++calls;
      if (failReadRecord) return false;
      auto it = files.find(path);
      if (it == files.end()) return false;
      *out = it->second;
      return true;
    };
    p.writeRecord = [this](const std::wstring& path, const pl::MachineRecord& r) -> uint32_t {
      ++calls;
      if (failWrite) return failWrite;
      order += "record ";
      files[path] = r;
      return 0;
    };
    p.removeRecord = [this](const std::wstring& path) -> uint32_t { ++calls; files.erase(path); return 0; };
    p.featureEnabled = [this] {
      ++calls;
      if (markBound && mark != 0) return false;  // r9: the service reads the mark first
      auto it = files.find(L"C:\\ProgramData\\GNLink\\credential\\machine.cred");
      if (it == files.end()) return false;
      pl::BindingContext ctx;
      ctx.currentMachineId = "11111111-2222-3333-4444-555555555555";
      ctx.originAllowed = true;
      ctx.minOwnerGeneration = floor;
      return pl::ValidateMachineRecord(it->second, ctx) == pl::RecordVerdict::Usable;
    };
    p.allowedDirectoryUrl = "https://gnlink.example:8443";  // r6 V7: the server records may name
    p.queryStartType = [this](uint32_t* out) { ++calls; *out = startType; return true; };
    p.setStartType = [this](uint32_t t) -> uint32_t {
      ++calls;
      if (failSetStartType) return failSetStartType;
      startType = t;
      return 0;
    };
    p.loadRememberedStartType = [this](uint32_t* out) { ++calls; if (!haveRemembered) return false; *out = remembered; return true; };
    p.rememberStartType = [this](uint32_t t) {
      ++calls;
      if (failRemember) return false;
      haveRemembered = true;
      remembered = t;
      return true;
    };
    p.forgetRememberedStartType = [this] { ++calls; haveRemembered = false; };
    return p;
  }
};

const std::wstring kPath = L"C:\\ProgramData\\GNLink\\credential\\machine.cred";

pl::AdminRequest enable_request(uint64_t expectedFloor = 0) {
  pl::AdminRequest r;
  r.op = pl::AdminOp::Enable;
  r.expectedFloor = expectedFloor;  // r7 H2: the floor the requester read (Query) before asking
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

// r5 G3: the server these tests serve is this process; the client is told to trust exactly it.
const pl::ServerCheck self_server = [](void* pipe) { return pl::ServerIsProcess(pipe, GetCurrentProcessId()); };

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
    const pl::AdminReply on = pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request(5)), true, p);
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
    const pl::AdminReply again = pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request(5)), true, p);
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
    // r7 H2: an Enable decided before a sign-out's Disable arrives after it. The old owner's request names the
    // floor it read; the Disable raised the floor; the late Enable is refused and the feature stays off.
    World w;
    pl::AdminPrimitives p = w.primitives();
    pl::AdminRequest q;
    q.op = pl::AdminOp::Query;
    const pl::AdminReply seen = pl::HandleAdminRequest(pl::EncodeAdminRequest(q), true, p);  // the old owner reads
    pl::AdminRequest off;
    off.op = pl::AdminOp::Disable;
    pl::HandleAdminRequest(pl::EncodeAdminRequest(off), true, p);  // sign out gets in first
    const uint64_t floorAfterOff = w.floor;
    const pl::AdminReply late = pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request(seen.ownerGeneration)), true, p);
    check("H2: a late Enable decided before a Disable: Stale, not made the newest",
          late.status == pl::AdminStatus::Stale && !late.enabled, std::to_string(static_cast<int>(late.status)));
    check("...nothing written, the feature stays off, the start type as it was, the floor unchanged",
          w.files.empty() && !p.featureEnabled() && w.startType == 3 && w.floor == floorAfterOff);
    pl::AdminReply back;
    check("...Stale round-trips through the reply encoding",
          pl::DecodeAdminReply(pl::EncodeAdminReply(late), &back) && back.status == pl::AdminStatus::Stale &&
              back.ownerGeneration == floorAfterOff);
    const pl::AdminReply fresh = pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request(late.ownerGeneration)), true, p);
    check("...a new owner who read the new floor may turn it on", fresh.status == pl::AdminStatus::Ok && fresh.enabled);
    pl::AdminRequest rt = enable_request(42);
    pl::AdminRequest rtBack;
    check("...the expected floor round-trips in the Enable request",
          pl::DecodeAdminRequest(pl::EncodeAdminRequest(rt), &rtBack) && rtBack.expectedFloor == 42);
  }
  {
    // r11 U1: Enable issues its generation durably BEFORE the record carries it, and a revoke puts the floor
    // above every generation issued -- also when the record it removes cannot be read.
    World w;
    w.floor = 3;
    pl::AdminPrimitives p = w.primitives();
    for (int i = 0; i < 4; ++i) pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request(3)), true, p);
    const pl::MachineRecord seven = w.files[kPath];
    check("U1: four Enables over floor 3: the record is at 7, the issued bound is 7, issued BEFORE each record",
          seven.ownerGeneration == 7 && w.issuedKept && w.issued == 7 &&
              w.order.rfind("issued record ", 0) == 0 && w.order.find("record issued record ") != std::string::npos,
          std::to_string(seven.ownerGeneration) + " / " + w.order);
    w.failReadRecord = true;  // floor 3, record 7, only its read fails
    pl::AdminRequest off;
    off.op = pl::AdminOp::Disable;
    const pl::AdminReply o = pl::HandleAdminRequest(pl::EncodeAdminRequest(off), true, p);
    check("U1: Disable with the record unreadable: the floor goes above the issued 7 (r10 took it for 0: 4)",
          o.status == pl::AdminStatus::Ok && w.floor > 7 && w.files.empty(), std::to_string(w.floor));
    w.failReadRecord = false;
    w.files[kPath] = seven;  // the old generation-7 copy put back
    check("...that copy put back is NOT usable (product ValidateMachineRecord: Revoked)", !p.featureEnabled());
  }
  {
    World w;
    w.issuedUnreadable = true;
    pl::AdminPrimitives p = w.primitives();
    const pl::AdminReply on = pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request()), true, p);
    check("U1: Enable when the issued bound cannot be read: Failed, nothing issued, no record, start type as it was",
          on.status == pl::AdminStatus::Failed && on.win32 == ERROR_READ_FAULT && w.files.empty() && w.startType == 3);
    World k;
    k.failRaiseIssued = true;
    pl::AdminPrimitives kp = k.primitives();
    const pl::AdminReply kon = pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request()), true, kp);
    check("U1: Enable when the bound cannot be kept: Failed, no record published, start type as it was",
          kon.status == pl::AdminStatus::Failed && k.files.empty() && k.startType == 3);
    World f;
    f.failWrite = ERROR_ACCESS_DENIED;
    pl::AdminPrimitives fp = f.primitives();
    pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request()), true, fp);
    check("U1: a record that then fails to be written leaves the bound raised (above, never below)",
          f.files.empty() && f.issuedKept && f.issued == 1);
  }
  {
    // r11 U1: no bound was ever kept (a record from before it) and the record cannot be read: the revoke
    // cannot prove where its floor must go -- access stays off, the revoke stays owed, the record stays.
    World w;
    w.markBound = true;
    w.floor = 3;
    pl::AdminPrimitives p = w.primitives();
    pl::MachineRecord legacy;
    legacy.accountId = 1;
    legacy.hostId = 2;
    legacy.ownerGeneration = 7;
    legacy.enabled = true;
    legacy.directoryOrigin = "https://gnlink.example:8443";
    legacy.machineId = "11111111-2222-3333-4444-555555555555";
    legacy.credential = {1, 2, 3};
    w.files[kPath] = legacy;
    w.failReadRecord = true;
    pl::AdminRequest off;
    off.op = pl::AdminOp::Disable;
    const pl::AdminReply o = pl::HandleAdminRequest(pl::EncodeAdminRequest(off), true, p);
    w.failReadRecord = false;
    check("U1: unprovable Disable: Failed, the mark is SET, the record LEFT, the feature off",
          o.status == pl::AdminStatus::Failed && w.mark == 1 && w.files.size() == 1 && !p.featureEnabled(),
          "floor " + std::to_string(w.floor));
    w.failReadRecord = true;
    check("...the service's tick cannot prove it either: still owed", pl::CompletePendingRevoke(p).status ==
                                                                          pl::AdminStatus::Failed && w.mark == 1);
    w.failReadRecord = false;
    check("...once the record can be read: carried out above it (floor > 7), the mark cleared",
          pl::CompletePendingRevoke(p).status == pl::AdminStatus::Ok && w.mark == 0 && w.floor > 7 && w.files.empty(),
          std::to_string(w.floor));
    w.files[kPath] = legacy;
    check("...and that record put back is Revoked", !p.featureEnabled());
    World u;
    u.markBound = true;
    u.issuedUnreadable = true;
    pl::AdminPrimitives up = u.primitives();
    u.mark = 1;
    check("U1: an owed revoke whose bound cannot be read stays owed (mark kept)",
          pl::CompletePendingRevoke(up).status == pl::AdminStatus::Failed && u.mark == 1);
  }
  {
    // r9 R1: a sign-out's Disable that never finished. The Host put the mark down first; the next request
    // (here the Host's own Query, as before an Enable) carries the revoke out before it answers.
    World w;
    w.markBound = true;
    pl::AdminPrimitives p = w.primitives();
    pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request()), true, p);
    const uint64_t recGen = w.files.begin()->second.ownerGeneration;
    w.mark = 1;  // the sign-out's mark; its Disable is lost
    check("R1: with the mark down the feature is off at once, before any revoke ran", !p.featureEnabled() && w.files.size() == 1);
    pl::AdminRequest q;
    q.op = pl::AdminOp::Query;
    const pl::AdminReply qr = pl::HandleAdminRequest(pl::EncodeAdminRequest(q), true, p);
    check("R1: the next request carries the owed revoke out first: record gone, floor past it, mark cleared",
          w.files.empty() && w.floor > recGen && w.mark == 0, std::to_string(w.floor));
    check("...and the Query reports the floor after it, off", qr.status == pl::AdminStatus::Ok && !qr.enabled &&
                                                                  qr.ownerGeneration == w.floor);
    const pl::AdminReply again = pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request(qr.ownerGeneration)), true, p);
    check("...a new owner who read that floor may turn it on again", again.status == pl::AdminStatus::Ok && again.enabled);

    World f;
    f.markBound = true;
    pl::AdminPrimitives fp = f.primitives();
    pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request()), true, fp);
    f.mark = 1;
    f.failRaise = true;
    const pl::AdminReply owed = pl::CompletePendingRevoke(fp);
    check("R1: the owed revoke fails (the floor cannot be raised): Failed, the mark STAYS, the feature stays off",
          owed.status == pl::AdminStatus::Failed && f.mark == 1 && !fp.featureEnabled());
    const pl::AdminReply on = pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request(f.floor)), true, fp);
    check("...and nothing can be turned on above a revoke still owed (the Enable is refused, nothing written over it)",
          on.status == pl::AdminStatus::Failed && f.mark == 1 && !fp.featureEnabled());
    f.failRaise = false;
    check("...once it can be carried out, it is (the service's next tick), and the mark goes",
          pl::CompletePendingRevoke(fp).status == pl::AdminStatus::Ok && f.mark == 0 && f.files.empty());
    World n;
    n.markBound = true;
    pl::AdminPrimitives np = n.primitives();
    const int before = n.calls;
    check("nothing owed: the tick touches nothing", pl::CompletePendingRevoke(np).status == pl::AdminStatus::Ok &&
                                                        n.calls == before);
  }
  {
    // r9 R5: Ctrl+Alt+Del is asked by the streaming host only, for its own session.
    World w;
    pl::AdminPrimitives p = w.primitives();
    uint32_t asked = 0;
    int sasCalls = 0;
    p.requestSas = [&](uint32_t session, uint32_t* win32, bool* cad) -> uint8_t {
      ++sasCalls;
      asked = session;
      *win32 = 0;
      *cad = true;
      return 0;
    };
    pl::AdminRequest sas;
    sas.op = pl::AdminOp::Sas;
    pl::AdminRequest back;
    check("R5: the Sas op round-trips (no payload)", pl::DecodeAdminRequest(pl::EncodeAdminRequest(sas), &back) &&
                                                      back.op == pl::AdminOp::Sas);
    pl::ClientFacts host;
    host.elevatedAdmin = true;
    host.hostProcess = true;
    host.sessionId = 1;
    const pl::AdminReply r = pl::HandleAdminRequest(pl::EncodeAdminRequest(sas), host, p, nullptr);
    check("R5: from the streaming host: the service is asked for THAT host's session, once; Sent (+ DisableCAD)",
          r.status == pl::AdminStatus::Ok && r.rebootResult == 0 && r.enabled && sasCalls == 1 && asked == 1);
    pl::ClientFacts admin;
    admin.elevatedAdmin = true;  // an elevated admin, but not the streaming host
    const pl::AdminReply refused = pl::HandleAdminRequest(pl::EncodeAdminRequest(sas), admin, p, nullptr);
    check("R5: from any other client: Refused (NotAuthorized), the service is not asked",
          refused.status == pl::AdminStatus::Refused && refused.rebootResult == 3 && sasCalls == 1);
    pl::AdminReply unconfirmed;
    unconfirmed.status = pl::AdminStatus::Ok;
    unconfirmed.rebootResult = 5;  // SentUnconfirmed (the policy unset)
    unconfirmed.enabled = true;    // DisableCAD
    pl::AdminReply unconfirmedBack;
    check("R5: every Sas answer (up to SentUnconfirmed = 5) and the DisableCAD bit survive the reply encoding",
          pl::DecodeAdminReply(pl::EncodeAdminReply(unconfirmed), &unconfirmedBack) && unconfirmedBack.rebootResult == 5 &&
              unconfirmedBack.enabled);
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
    check("on, but the start type cannot be set: Failed with the error, and nothing was ever published (no record)",
          r.status == pl::AdminStatus::Failed && r.win32 == 1060 && !r.enabled && w.files.empty());
  }
  {
    // r6 V7: a record names the product's own server, or nothing.
    World w;
    pl::AdminPrimitives p = w.primitives();
    pl::AdminRequest other = enable_request();
    other.directoryUrl = "https://elsewhere.example";
    const pl::AdminReply r = pl::HandleAdminRequest(pl::EncodeAdminRequest(other), true, p);
    check("r6 V7: on, naming another server: refused (BadRequest), nothing written, start type as it was",
          r.status == pl::AdminStatus::BadRequest && w.files.empty() && w.startType == 3);
    pl::AdminRequest path = enable_request();
    path.directoryUrl = "HTTPS://GNLINK.EXAMPLE:8443/some/path";
    check("...the same origin in another case / with a path is the same server (accepted)",
          pl::HandleAdminRequest(pl::EncodeAdminRequest(path), true, p).status == pl::AdminStatus::Ok);
    World e;
    pl::AdminPrimitives ep = e.primitives();
    ep.allowedDirectoryUrl.clear();
    check("...and with no server configured nothing is accepted (fail closed)",
          pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request()), true, ep).status == pl::AdminStatus::BadRequest &&
              e.files.empty());
  }
  {
    World w;
    w.failRemember = true;
    pl::AdminPrimitives p = w.primitives();
    const pl::AdminReply r = pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request()), true, p);
    check("G6: on, but the original start type cannot be remembered: Failed, NOTHING changed (no record, start type as it was)",
          r.status == pl::AdminStatus::Failed && !r.enabled && w.files.empty() && w.startType == 3);
  }
  {
    World w;
    w.failWrite = 5;
    pl::AdminPrimitives p = w.primitives();
    const pl::AdminReply r = pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request()), true, p);
    {
      World u;
      pl::AdminPrimitives up = u.primitives();
      pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request()), true, up);
      u.failFloor = true;  // the floor becomes unreadable (r6 G6: not floor 0)
      const pl::AdminReply on2 = pl::HandleAdminRequest(pl::EncodeAdminRequest(enable_request()), true, up);
      check("r6 G6: the floor cannot be read: on is refused (Failed), the existing record is not replaced",
            on2.status == pl::AdminStatus::Failed && on2.win32 == ERROR_READ_FAULT && u.files.size() == 1);
      pl::AdminRequest off;
      off.op = pl::AdminOp::Disable;
      const uint64_t recGen = u.files.begin()->second.ownerGeneration;
      const pl::AdminReply o = pl::HandleAdminRequest(pl::EncodeAdminRequest(off), true, up);
      check("...and off still withdraws (record gone, floor raised past it) but says Failed",
            o.status == pl::AdminStatus::Failed && u.files.empty() && u.floor > recGen);
    }
    check("G6: on, but the record cannot be written: Failed, and the start type is put back (DEMAND), not left AUTO",
          r.status == pl::AdminStatus::Failed && r.win32 == 5 && !r.enabled && w.files.empty() && w.startType == 3,
          std::to_string(w.startType));
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
    const pl::AdminReply r = pl::SendPreloginAdminRequest(enable_request(), 3000, name.c_str(), self_server);
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
    const pl::AdminReply r = pl::SendPreloginAdminRequest(enable_request(), 3000, name.c_str(), self_server);
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
    const pl::AdminReply on = pl::SendPreloginAdminRequest(enable_request(), 3000, name.c_str(), self_server);
    pl::AdminRequest off;
    off.op = pl::AdminOp::Disable;
    const bool wroteOn = w.files.count(kPath) == 1;
    const pl::AdminReply o = pl::SendPreloginAdminRequest(off, 3000, name.c_str(), self_server);
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
      check("handoff: no ready yet -> not complete (the prelogin worker keeps the lease)", !t.Completes(3, 500, "n-1"));
      {
        // r10: an adopted Host (not launched by the service: no nonce) -- its own streaming child's ready.
        pl::UserHostReadyTracker a;
        a.Note(3, 900, "", 4242);  // the stream, pid 4242
        const auto parentOf = [](uint32_t pid, uint64_t start) -> uint32_t {
          return (pid == 4242 && start == 900) ? 77u : 0u;  // its parent: the adopted GNLinkHost, pid 77
        };
        check("r10: the adopted Host's own child's ready completes the handoff (no nonce needed)",
              a.CompletesAdopted(3, 77, parentOf));
        check("...not when the sender is another Host's child", !a.CompletesAdopted(3, 78, parentOf));
        check("...nor from another session", !a.CompletesAdopted(4, 77, parentOf));
        check("...nor when that process is gone / its pid reused (the parent cannot be told)",
              !a.CompletesAdopted(3, 77, [](uint32_t, uint64_t) { return 0u; }));
      }
      t.Note(3, 400, "n-1");
      check("handoff: a ready from a host started BEFORE this launch (an old generation) -> discarded", !t.Completes(3, 500, "n-1"));
      t.Note(4, 600, "n-1");
      check("handoff: a ready from another session -> not complete", !t.Completes(3, 500, "n-1"));
      t.Note(3, 600, "n-1");
      check("handoff: a ready from this session's host started after the launch -> complete", t.Completes(3, 500, "n-1"));
      t.Reset();
      check("handoff: a repeated LOGON relaunch resets it; the earlier ready no longer counts", !t.Completes(3, 700, "n-1"));
      t.Note(3, 650, "n-1");
      check("...and a ready from before that relaunch is stale", !t.Completes(3, 700, "n-1"));
      t.Note(3, 800, "n-0");
      check("r6 G9: a ready from a host of this session, after the launch, but not THIS launch's (another nonce) -> not complete",
            !t.Completes(3, 700, "n-1"));
      t.Note(3, 800, "");
      check("...nor one that names no nonce", !t.Completes(3, 700, "n-1"));
      t.Note(3, 800, "n-1");
      check("...while the launch's own nonce completes it", t.Completes(3, 700, "n-1"));
      check("...and a launch without a nonce is never completed", !t.Completes(3, 700, ""));
    }
    {
      // The real pipe: the product's host check is the image path. This test is not GNLinkStream.exe.
      World w;
      TestServer s;
      const std::wstring name = L"\\\\.\\pipe\\GNLinkTest-admin-" + std::to_wstring(GetCurrentProcessId()) + L"-d";
      s.start(&w, name, current_user_sddl(), [](void*) { return true; },
              [](void* pipe) { return pl::ClientImageIs(pipe, L"C:\\Program Files\\GNLink\\GNLinkStream.exe"); });
      const pl::AdminReply forged = pl::SendPreloginAdminRequest(rb, 3000, name.c_str(), self_server);
      pl::AdminRequest ready;
      ready.op = pl::AdminOp::Ready;
      const pl::AdminReply forgedReady = pl::SendPreloginAdminRequest(ready, 3000, name.c_str(), self_server);
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
      const pl::AdminReply a = pl::SendPreloginAdminRequest(rb, 3000, name.c_str(), self_server);
      const pl::AdminReply b = pl::SendPreloginAdminRequest(rb, 3000, name.c_str(), self_server);
      pl::AdminRequest ready;
      ready.op = pl::AdminOp::Ready;
      const pl::AdminReply c = pl::SendPreloginAdminRequest(ready, 3000, name.c_str(), self_server);
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
      pl::UserHostReadyNotifier never;
      pl::UserHostReadyNotifier::Options o1;
      o1.waitReadyMs = 400;
      o1.attempts = 5;
      o1.delayMs = 100;
      o1.pipeName = name;
      o1.check = self_server;
      never.Start([] { return false; }, o1);
      Sleep(1500);
      check("ready while the host never registered: nothing is sent (the service keeps prelogin)", w.readies == 0);
      s.stop();
      std::atomic<bool> registered{false};
      pl::UserHostReadyNotifier later;
      pl::UserHostReadyNotifier::Options o2 = o1;
      o2.waitReadyMs = 10000;
      o2.attempts = 8;
      o2.delayMs = 400;
      later.Start([&registered] { return registered.load(); }, o2);
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

  // ---------------------------------------------------------------- r12 L1: the Ready notifier's owner
  {
    wchar_t me[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, me, MAX_PATH);
    const std::wstring self = me;
    pl::UserHostReadyNotifier::Options refresh;
    refresh.refreshMs = 1000;  // the product's 5 s, shortened; the same loop
    refresh.check = self_server;
    {
      // Stopped while it sleeps between refreshes.
      const std::wstring name = L"\\\\.\\pipe\\GNLinkTest-admin-" + std::to_wstring(GetCurrentProcessId()) + L"-l1a";
      World w;
      TestServer s;
      s.start(&w, name, current_user_sddl(), [](void*) { return true; }, [self](void* pipe) { return pl::ClientImageIs(pipe, self); });
      std::atomic<int> calls{0};
      pl::UserHostReadyNotifier n;
      pl::UserHostReadyNotifier::Options o = refresh;
      o.pipeName = name;
      n.Start([&calls] { ++calls; return true; }, o);
      const bool first = wait_until_ms([&] { return w.readies >= 1; }, 5000);
      Sleep(200);  // now in the refresh wait
      const ULONGLONG t0 = GetTickCount64();
      n.Stop();
      const ULONGLONG took = GetTickCount64() - t0;
      const int callsAtStop = calls.load(), readiesAtStop = w.readies;
      Sleep(2500);  // two refresh periods
      s.stop();
      check("L1: stopped while it sleeps between refreshes: Stop returns at once (the wait is woken)",
            first && took < 500, std::to_string(took) + " ms");
      check("L1: ...and after Stop: readyNow is not called again and no Ready is sent (2.5 s, refresh 1 s)",
            calls.load() == callsAtStop && w.readies == readiesAtStop,
            std::to_string(calls.load() - callsAtStop) + " calls, " + std::to_string(w.readies - readiesAtStop) + " readies");
    }
    {
      // Stopped while readyNow itself runs: Stop waits for it, and nothing is sent after it.
      const std::wstring name = L"\\\\.\\pipe\\GNLinkTest-admin-" + std::to_wstring(GetCurrentProcessId()) + L"-l1b";
      World w;
      TestServer s;
      s.start(&w, name, current_user_sddl(), [](void*) { return true; }, [self](void* pipe) { return pl::ClientImageIs(pipe, self); });
      std::atomic<bool> inside{false}, release{false};
      std::atomic<int> calls{0};
      pl::UserHostReadyNotifier n;
      pl::UserHostReadyNotifier::Options o = refresh;
      o.pipeName = name;
      n.Start([&] {
        ++calls;
        inside = true;
        while (!release) Sleep(10);
        inside = false;
        return true;
      }, o);
      wait_until_ms([&] { return inside.load(); }, 3000);
      std::thread releaser([&] { Sleep(300); release = true; });
      const ULONGLONG t0 = GetTickCount64();
      n.Stop();
      const ULONGLONG took = GetTickCount64() - t0;
      releaser.join();
      const bool insideAfter = inside.load();
      const int callsAtStop = calls.load();
      Sleep(1500);
      s.stop();
      check("L1: stopped while readyNow runs: Stop returns only after it finished (the callback is not left running)",
            !insideAfter && took >= 250 && took < 2000, std::to_string(took) + " ms");
      check("L1: ...and the Ready it was about to decide is NOT sent, nor any later one",
            n.sendsStarted() == 0 && w.readies == 0 && calls.load() == callsAtStop,
            std::to_string(n.sendsStarted()) + " sends, " + std::to_string(w.readies) + " readies");
    }
    {
      // Stopped while it waits for the service's answer: a server that takes the request and never replies.
      const std::wstring name = L"\\\\.\\pipe\\GNLinkTest-admin-" + std::to_wstring(GetCurrentProcessId()) + L"-l1c";
      HANDLE server = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                       1, 4096, 4096, 0, nullptr);
      std::atomic<bool> connected{false};
      std::thread accept([&] {
        if (ConnectNamedPipe(server, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) connected = true;
      });
      pl::UserHostReadyNotifier n;
      pl::UserHostReadyNotifier::Options o = refresh;
      o.pipeName = name;
      n.Start([] { return true; }, o);
      wait_until_ms([&] { return connected.load(); }, 3000);
      Sleep(400);  // the request is written; the client waits for the answer (up to 5 s)
      const ULONGLONG t0 = GetTickCount64();
      n.Stop();
      const ULONGLONG took = GetTickCount64() - t0;
      accept.join();
      DisconnectNamedPipe(server);
      CloseHandle(server);
      check("L1: stopped while it waits for the service's answer: the request is cancelled and waited out, Stop "
            "returns within a second (not the 5 s I/O bound)",
            connected.load() && n.sendsStarted() == 1 && took < 1000, std::to_string(took) + " ms");
    }
    {
      // The owner goes out of scope with what readyNow reads (declared before it): destroyed after it stopped.
      const std::wstring name = L"\\\\.\\pipe\\GNLinkTest-admin-" + std::to_wstring(GetCurrentProcessId()) + L"-l1d";
      World w;
      TestServer s;
      s.start(&w, name, current_user_sddl(), [](void*) { return true; }, [self](void* pipe) { return pl::ClientImageIs(pipe, self); });
      static std::atomic<bool> gone{false};
      static std::atomic<int> afterGone{0};
      struct Agent {
        std::string status = "online";
        ~Agent() { gone = true; }
      };
      {
        Agent agent;  // what the product's readyNow reads (the directory agent's status)
        pl::UserHostReadyNotifier n;
        pl::UserHostReadyNotifier::Options o = refresh;
        o.pipeName = name;
        n.Start([&agent] {
          if (gone) ++afterGone;
          return agent.status == "online";
        }, o);
        wait_until_ms([&] { return w.readies >= 1; }, 5000);
      }  // n destroyed first (declared last), then agent
      const int readiesAtEnd = w.readies;
      Sleep(2500);
      s.stop();
      check("L1: the owner leaves scope with what readyNow reads: no call after it is gone, no Ready after",
            gone.load() && afterGone.load() == 0 && w.readies == readiesAtEnd && readiesAtEnd >= 1,
            std::to_string(afterGone.load()) + " calls after, " + std::to_string(w.readies - readiesAtEnd) + " readies after");
    }
    {
      // U4 kept: the service not there at first, then up -- the refresh still finds it.
      const std::wstring name = L"\\\\.\\pipe\\GNLinkTest-admin-" + std::to_wstring(GetCurrentProcessId()) + L"-l1e";
      pl::UserHostReadyNotifier n;
      pl::UserHostReadyNotifier::Options o = refresh;
      o.pipeName = name;
      o.attempts = 2;
      o.delayMs = 200;
      n.Start([] { return true; }, o);
      Sleep(1500);  // its quick retries are spent
      World w;
      TestServer s;
      s.start(&w, name, current_user_sddl(), [](void*) { return true; }, [self](void* pipe) { return pl::ClientImageIs(pipe, self); });
      const bool arrived = wait_until_ms([&] { return w.readies >= 1; }, 4000);
      n.Stop();
      s.stop();
      check("U4 kept: a service that comes up after the quick retries still gets the Ready at the refresh pace", arrived);
    }
  }

  // ---------------------------------------------------------------- r5 G3: the client checks the server
  {
    // A server that is not the installed service -- this test, standing in for a process that took the
    // name -- is sent nothing: not the Enable credential, not a Query whose answer would be believed.
    World w;
    std::atomic<int> requestsRead{0};
    TestServer s;
    const std::wstring name = L"\\\\.\\pipe\\GNLinkTest-admin-" + std::to_wstring(GetCurrentProcessId()) + L"-g";
    s.start(&w, name, current_user_sddl(), [&requestsRead](void*) { ++requestsRead; return true; });
    const pl::AdminReply on = pl::SendPreloginAdminRequest(enable_request(), 3000, name.c_str());  // the product's check
    pl::AdminRequest q;
    q.op = pl::AdminOp::Query;
    const pl::AdminReply query = pl::SendPreloginAdminRequest(q, 3000, name.c_str());
    const pl::AdminReply other =
        pl::SendPreloginAdminRequest(enable_request(), 3000, name.c_str(), [](void* p) { return pl::ServerIsProcess(p, 4); });
    Sleep(300);
    s.stop();
    check("G3: a pipe server that is not the installed GNLinkSecureInput: Enable is Untrusted",
          on.status == pl::AdminStatus::Untrusted, std::to_string(static_cast<int>(on.status)));
    check("...and so is a Query", query.status == pl::AdminStatus::Untrusted);
    check("...and a server that is not the process named", other.status == pl::AdminStatus::Untrusted);
    check("...and the server read NOTHING from any of them (no credential crossed)",
          requestsRead.load() == 0 && w.calls == 0 && w.files.empty(), std::to_string(requestsRead.load()));
  }
  {
    // The service owns the name: a second server -- or the service starting after someone took it --
    // does not serve beside the first.
    World w;
    TestServer s;
    const std::wstring name = L"\\\\.\\pipe\\GNLinkTest-admin-" + std::to_wstring(GetCurrentProcessId()) + L"-h";
    s.start(&w, name, current_user_sddl(), [](void*) { return true; });
    std::atomic<bool> run2{true};
    const std::wstring sddl = current_user_sddl();
    const ULONGLONG t0 = GetTickCount64();
    const bool second = pl::RunPreloginAdminServer(w.primitives(), [&run2] { return run2.load(); }, name.c_str(), sddl.c_str(),
                                                   [](void*) { return true; });
    const ULONGLONG took = GetTickCount64() - t0;
    s.stop();
    check("G3: a second server on a name already served does not start",
          !second && took < 1000, std::to_string(took) + " ms");
  }
  {
    // Someone took the name FIRST, the way a squatter would -- a server that allows any number of
    // instances -- before the service starts: the service must not join it as one more instance.
    World w;
    const std::wstring name = L"\\\\.\\pipe\\GNLinkTest-admin-" + std::to_wstring(GetCurrentProcessId()) + L"-squat";
    const std::wstring sddl = current_user_sddl();
    PSECURITY_DESCRIPTOR sd = nullptr;
    ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr);
    SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
    HANDLE squat = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                    PIPE_UNLIMITED_INSTANCES, 4096, 8192, 0, &sa);
    LocalFree(sd);
    std::atomic<bool> run2{true};
    std::thread stopper([&run2] {
      Sleep(1500);
      run2 = false;
    });
    const bool served = pl::RunPreloginAdminServer(w.primitives(), [&run2] { return run2.load(); }, name.c_str(), sddl.c_str(),
                                                   [](void*) { return true; });
    stopper.join();
    if (squat != INVALID_HANDLE_VALUE) CloseHandle(squat);
    check("G3: the name already held (unlimited instances) by someone else: the service does not serve on it "
          "(FILE_FLAG_FIRST_PIPE_INSTANCE)",
          squat != INVALID_HANDLE_VALUE && !served);
  }

  // ---------------------------------------------------------------- r5 G4: every wait is bounded
  {
    // A server that accepts and then says nothing: the client gives up within its I/O bound.
    const std::wstring name = L"\\\\.\\pipe\\GNLinkTest-admin-" + std::to_wstring(GetCurrentProcessId()) + L"-silent";
    HANDLE silent = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1,
                                     4096, 4096, 0, nullptr);
    std::thread acceptor([silent] { ConnectNamedPipe(silent, nullptr); });
    Sleep(100);
    const ULONGLONG t0 = GetTickCount64();
    pl::AdminRequest q;
    q.op = pl::AdminOp::Query;
    const pl::AdminReply r = pl::SendPreloginAdminRequest(q, 1000, name.c_str(), self_server, 600);
    const ULONGLONG took = GetTickCount64() - t0;
    acceptor.join();
    CloseHandle(silent);
    check("G4: a server that never answers: the client returns within its bound, not Ok",
          r.status != pl::AdminStatus::Ok && took < 2500, std::to_string(took) + " ms, win32 " + std::to_string(r.win32));
  }
  {
    // A client that connects and never writes: the server's read is bounded and the next client is served.
    World w;
    TestServer s;
    const std::wstring name = L"\\\\.\\pipe\\GNLinkTest-admin-" + std::to_wstring(GetCurrentProcessId()) + L"-i";
    s.start(&w, name, current_user_sddl(), [](void*) { return true; });
    HANDLE mute = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    const ULONGLONG t0 = GetTickCount64();
    pl::AdminRequest q;
    q.op = pl::AdminOp::Query;
    const pl::AdminReply r = pl::SendPreloginAdminRequest(q, 9000, name.c_str(), self_server);
    const ULONGLONG took = GetTickCount64() - t0;
    if (mute != INVALID_HANDLE_VALUE) CloseHandle(mute);
    s.stop();
    check("G4: a client that never writes holds the server only until its read bound; the next is served",
          mute != INVALID_HANDLE_VALUE && r.status == pl::AdminStatus::Ok && took < 8000, std::to_string(took) + " ms");
  }
  {
    // Stopping the service while a read is pending: the server returns promptly (the cancelled read is
    // waited out before its buffer goes away).
    World w;
    TestServer s;
    const std::wstring name = L"\\\\.\\pipe\\GNLinkTest-admin-" + std::to_wstring(GetCurrentProcessId()) + L"-j";
    s.start(&w, name, current_user_sddl(), [](void*) { return true; });
    HANDLE mute = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    Sleep(200);  // the server is in its read
    const ULONGLONG t0 = GetTickCount64();
    s.stop();
    const ULONGLONG took = GetTickCount64() - t0;
    if (mute != INVALID_HANDLE_VALUE) CloseHandle(mute);
    check("G4: stop during a pending read: the server returns within a second", took < 1000, std::to_string(took) + " ms");
  }

  // ---------------------------------------------------------------- r5 G10: whose request it is
  {
    World w;
    pl::RebootLedger ledger;
    uint64_t now = 1000;
    ledger.nowMs = [&now] { return now; };
    pl::AdminPrimitives p = w.primitives();
    pl::ClientFacts h;
    h.elevatedAdmin = true;
    h.hostProcess = true;
    h.processId = 100;
    h.processStartTime = 5;
    pl::AdminRequest a;
    a.op = pl::AdminOp::Reboot;
    a.requestId = 1;  // every PC viewer's first id
    a.peerKey = 0xAAAA;
    pl::AdminRequest b = a;
    b.peerKey = 0xBBBB;  // another viewer, same id
    const pl::AdminReply ra = pl::HandleAdminRequest(pl::EncodeAdminRequest(a), h, p, &ledger);
    const pl::AdminReply rb = pl::HandleAdminRequest(pl::EncodeAdminRequest(b), h, p, &ledger);
    check("G10: two viewers' request 1 are two requests: the first Accepted, the second Refused (in flight) -- not the first's answer",
          ra.rebootResult == 0 && rb.rebootResult == 1 && w.osReboots == 1);
    pl::ClientFacts other = h;
    other.processId = 101;
    check("...the same peer key and id from another host process is another request too",
          pl::HandleAdminRequest(pl::EncodeAdminRequest(a), other, p, &ledger).rebootResult == 1 && w.osReboots == 1);
    check("...while the first viewer's retransmit gets its own answer (Accepted), no second call",
          pl::HandleAdminRequest(pl::EncodeAdminRequest(a), h, p, &ledger).rebootResult == 0 && w.osReboots == 1);
    now += pl::kRebootInFlightMs - 1;
    pl::AdminRequest c = a;
    c.requestId = 3;
    check("...still in flight just before the limit: Refused",
          pl::HandleAdminRequest(pl::EncodeAdminRequest(c), h, p, &ledger).rebootResult == 1);
    now += 2;
    pl::AdminRequest d = a;
    d.requestId = 5;
    check("an accepted restart that never happened (cancelled) stops blocking: a new request is Accepted again",
          pl::HandleAdminRequest(pl::EncodeAdminRequest(d), h, p, &ledger).rebootResult == 0 && w.osReboots == 2);
    pl::AdminRequest e = a;
    e.peerKey = 0x1;
    pl::AdminRequest back;
    check("the Reboot request carries its peer key across the wire",
          pl::DecodeAdminRequest(pl::EncodeAdminRequest(e), &back) && back.peerKey == 0x1 && back.requestId == 1);
  }

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
