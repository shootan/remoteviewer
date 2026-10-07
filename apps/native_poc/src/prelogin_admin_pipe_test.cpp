// "로그인 전에도 원격 접속 허용" -- the request codec, the service's decision over its primitives, and the
// real pipe's two refusals (prelogin-ui r1, item 4). The primitives here are in-memory stand-ins for
// the machine store, the floor and the SCM; the decision and the pipe are the product's.

#include <windows.h>
#include <sddl.h>

#include <atomic>
#include <cstdio>
#include <map>
#include <string>
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
  pl::AdminPrimitives primitives() {
    pl::AdminPrimitives p;
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
  void start(World* w, const std::wstring& name, const std::wstring& sddl, pl::AdminIdentityCheck check) {
    t = std::thread([this, w, name, sddl, check] {
      pl::AdminPrimitives p = w->primitives();
      pl::RunPreloginAdminServer(p, [this] { return run.load(); }, name.c_str(), sddl.c_str(), check);
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

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
