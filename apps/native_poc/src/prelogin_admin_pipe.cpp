// "로그인 전에도 원격 접속 허용" -- request codec, the service's decision, the pipe server and client
// (prelogin-ui r1, item 4). See prelogin_admin_pipe.hpp.

#include "prelogin_admin_pipe.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cwctype>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <sddl.h>
#include <tlhelp32.h>

#include "secure_input_protocol.hpp"
#endif


namespace remote60::native_poc::prelogin {

namespace {

constexpr uint32_t kStartAuto = 2;    // SERVICE_AUTO_START
constexpr uint32_t kStartDemand = 3;  // SERVICE_DEMAND_START

void put_u8(std::vector<uint8_t>* o, uint8_t v) { o->push_back(v); }
void put_u16(std::vector<uint8_t>* o, uint16_t v) {
  o->push_back(static_cast<uint8_t>(v & 0xff));
  o->push_back(static_cast<uint8_t>(v >> 8));
}
void put_u32(std::vector<uint8_t>* o, uint32_t v) {
  for (int i = 0; i < 4; ++i) o->push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
}
void put_u64(std::vector<uint8_t>* o, uint64_t v) {
  for (int i = 0; i < 8; ++i) o->push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
}
void put_str(std::vector<uint8_t>* o, const std::string& s) {
  put_u16(o, static_cast<uint16_t>(s.size()));
  o->insert(o->end(), s.begin(), s.end());
}

struct Reader {
  const std::vector<uint8_t>& b;
  size_t pos = 0;
  bool u8(uint8_t* v) {
    if (pos + 1 > b.size()) return false;
    *v = b[pos++];
    return true;
  }
  bool u16(uint16_t* v) {
    if (pos + 2 > b.size()) return false;
    *v = static_cast<uint16_t>(b[pos] | (b[pos + 1] << 8));
    pos += 2;
    return true;
  }
  bool u32(uint32_t* v) {
    if (pos + 4 > b.size()) return false;
    uint32_t r = 0;
    for (int i = 0; i < 4; ++i) r |= static_cast<uint32_t>(b[pos + i]) << (8 * i);
    pos += 4;
    *v = r;
    return true;
  }
  bool u64(uint64_t* v) {
    if (pos + 8 > b.size()) return false;
    uint64_t r = 0;
    for (int i = 0; i < 8; ++i) r |= static_cast<uint64_t>(b[pos + i]) << (8 * i);
    pos += 8;
    *v = r;
    return true;
  }
  bool str(std::string* s, size_t max) {
    uint16_t n = 0;
    if (!u16(&n) || n > max || pos + n > b.size()) return false;
    s->assign(reinterpret_cast<const char*>(b.data() + pos), n);
    pos += n;
    return true;
  }
  bool done() const { return pos == b.size(); }
};

}  // namespace

// The same scheme://host[:port], ignoring case and a trailing slash or path. Empty never matches.
bool same_origin(const std::string& a, const std::string& b) {
  auto origin = [](std::string u) {
    for (auto& c : u) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const size_t scheme = u.find("://");
    if (scheme == std::string::npos) return std::string();
    const size_t path = u.find('/', scheme + 3);
    return path == std::string::npos ? u : u.substr(0, path);
  };
  const std::string oa = origin(a), ob = origin(b);
  return !oa.empty() && oa == ob;
}

uint64_t AdminIdHash(const std::string& s) {
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ull;
  }
  return h == 0 ? 1 : h;
}

std::vector<uint8_t> EncodeAdminRequest(const AdminRequest& r) {
  std::vector<uint8_t> o;
  put_u32(&o, kAdminMagic);
  put_u16(&o, kAdminVersion);
  put_u8(&o, static_cast<uint8_t>(r.op));
  if (r.op == AdminOp::Enable) {
    put_str(&o, r.directoryUrl);
    put_str(&o, r.accountId);
    put_str(&o, r.hostId);
    put_str(&o, r.hostToken);
    put_u64(&o, r.expectedFloor);
  }
  if (r.op == AdminOp::Reboot) {
    put_u32(&o, r.requestId);
    put_u64(&o, r.peerKey);
  }
  if (r.op == AdminOp::Ready) put_str(&o, r.readyNonce);
  return o;
}

bool DecodeAdminRequest(const std::vector<uint8_t>& b, AdminRequest* out) {
  Reader rd{b};
  uint32_t magic = 0;
  uint16_t version = 0;
  uint8_t op = 0;
  if (!rd.u32(&magic) || magic != kAdminMagic || !rd.u16(&version) || version != kAdminVersion || !rd.u8(&op)) return false;
  if (op < static_cast<uint8_t>(AdminOp::Query) || op > static_cast<uint8_t>(AdminOp::Sas)) return false;
  *out = AdminRequest{};
  out->op = static_cast<AdminOp>(op);
  if (out->op == AdminOp::Enable) {
    if (!rd.str(&out->directoryUrl, kAdminMaxString) || !rd.str(&out->accountId, kAdminMaxString) ||
        !rd.str(&out->hostId, kAdminMaxString) || !rd.str(&out->hostToken, kAdminMaxToken) || !rd.u64(&out->expectedFloor)) {
      return false;
    }
  }
  if (out->op == AdminOp::Reboot && (!rd.u32(&out->requestId) || !rd.u64(&out->peerKey))) return false;
  if (out->op == AdminOp::Ready && !rd.str(&out->readyNonce, 128)) return false;
  return rd.done();
}

std::vector<uint8_t> EncodeAdminReply(const AdminReply& r) {
  std::vector<uint8_t> o;
  put_u32(&o, kAdminMagic);
  put_u8(&o, static_cast<uint8_t>(r.status));
  put_u8(&o, r.enabled ? 1 : 0);
  put_u32(&o, r.win32);
  put_u64(&o, r.ownerGeneration);
  put_u8(&o, r.rebootResult);
  return o;
}

bool DecodeAdminReply(const std::vector<uint8_t>& b, AdminReply* out) {
  Reader rd{b};
  uint32_t magic = 0;
  uint8_t status = 0, enabled = 0;
  if (!rd.u32(&magic) || magic != kAdminMagic || !rd.u8(&status) ||
      (status > static_cast<uint8_t>(AdminStatus::Unavailable) && status != static_cast<uint8_t>(AdminStatus::Stale)) ||
      !rd.u8(&enabled) || enabled > 1 || !rd.u32(&out->win32) || !rd.u64(&out->ownerGeneration) ||
      !rd.u8(&out->rebootResult) || out->rebootResult > 5 || !rd.done()) {  // 0..3 Reboot, 0..5 Sas (r9 R5)
    return false;
  }
  out->status = static_cast<AdminStatus>(status);
  out->enabled = enabled == 1;
  return true;
}

namespace {

// Disable: the floor first (an old blob can never come back), then the record, then the start type.
AdminReply disable_now(const AdminPrimitives& p, bool* floorRaised = nullptr, uint32_t* removeError = nullptr) {
  AdminReply reply;
  if (floorRaised) *floorRaised = false;
  if (removeError) *removeError = ERROR_CANCELLED;  // not attempted
  const std::wstring path = p.recordPath ? p.recordPath() : std::wstring();
  MachineRecord old;
  const bool had = !path.empty() && p.readRecord && p.readRecord(path, &old);
  uint64_t floor = 0;
  const bool floorKnown = p.readFloor && p.readFloor(&floor);
  const uint64_t gen = (std::max)(had ? old.ownerGeneration : 0ull, floorKnown ? floor : 0ull) + 1;
  if (!p.raiseFloor || !p.raiseFloor(gen)) {
    reply.status = AdminStatus::Failed;
    reply.enabled = p.featureEnabled && p.featureEnabled();
    return reply;
  }
  if (floorRaised) *floorRaised = true;
  const uint32_t rm = path.empty() || !p.removeRecord ? 0 : p.removeRecord(path);
  if (removeError) *removeError = path.empty() ? ERROR_INVALID_PARAMETER : rm;
  uint32_t original = kStartDemand;
  const bool haveOriginal = p.loadRememberedStartType && p.loadRememberedStartType(&original);
  const uint32_t s = p.setStartType ? p.setStartType(haveOriginal ? original : kStartDemand) : 0;
  if (s == 0 && haveOriginal && p.forgetRememberedStartType) p.forgetRememberedStartType();
  // An unreadable floor: the record is still removed and the floor raised past it, but say Failed.
  reply.status = (rm == 0 && s == 0 && floorKnown) ? AdminStatus::Ok : AdminStatus::Failed;
  reply.win32 = rm != 0 ? rm : s != 0 ? s : floorKnown ? 0 : ERROR_READ_FAULT;
  reply.enabled = p.featureEnabled && p.featureEnabled();
  reply.ownerGeneration = gen;
  return reply;
}

bool revoke_pending(const AdminPrimitives& p) { return p.revokePending && p.revokePending() != 0; }

// Under the transaction: the pending revoke, if any, carried out; the mark cleared only on success.
AdminReply complete_pending_revoke_locked(const AdminPrimitives& p) {
  AdminReply reply;
  reply.status = AdminStatus::Ok;
  if (!revoke_pending(p)) return reply;
  reply = disable_now(p);
  if (reply.status == AdminStatus::Ok && p.clearRevokePending && !p.clearRevokePending()) {
    reply.status = AdminStatus::Failed;
    reply.win32 = ERROR_CANTWRITE;
  }
  return reply;
}

AdminReply handle_admin_request_locked(const std::vector<uint8_t>& request, const ClientFacts& who,
                                       const AdminPrimitives& p, RebootLedger* ledger);

}  // namespace

AdminReply CompletePendingRevoke(const AdminPrimitives& p) {
  std::unique_lock<std::mutex> lock;
  if (p.transaction) lock = std::unique_lock<std::mutex>(*p.transaction);
  return complete_pending_revoke_locked(p);
}

UninstallRevokeResult RevokeForUninstall(const AdminPrimitives& p, const std::function<bool()>& markPending,
                                         const std::function<bool()>& stopService) {
  UninstallRevokeResult out;
  out.marked = markPending && markPending();
  out.serviceStopped = stopService && stopService();
  if (!out.serviceStopped) {
    // Two writers would race (the live service's Enable between its floor check and its publish). The
    // mark is down: the service finishes the revoke itself on its next tick or start.
    out.reply.status = AdminStatus::Unavailable;
    return out;
  }
  {
    std::unique_lock<std::mutex> lock;
    if (p.transaction) lock = std::unique_lock<std::mutex>(*p.transaction);
    out.reply = disable_now(p, &out.floorRaised, &out.removeError);
  }
  // r10 T1: the postcondition is what the revoke actually DID -- the durable floor raised, the record's
  // removal reported, and the record file absent now -- not the reply (a start type that cannot be put back
  // while the service is being removed is not access left on), not "it cannot be read" (an ACL / IO / DPAPI
  // error is not absence), and not featureEnabled (the mark this removal set already turns that off).
  const std::wstring path = p.recordPath ? p.recordPath() : std::wstring();
  out.recordState = !path.empty() && p.probeRecord ? p.probeRecord(path) : -1;
  out.revoked = out.floorRaised && out.removeError == 0 && out.recordState == 0;
  if (out.revoked && p.clearRevokePending) (void)p.clearRevokePending();
  return out;
}

AdminReply HandleAdminRequest(const std::vector<uint8_t>& request, const ClientFacts& who, const AdminPrimitives& p,
                              RebootLedger* ledger) {
  std::unique_lock<std::mutex> lock;
  if (p.transaction) lock = std::unique_lock<std::mutex>(*p.transaction);
  return handle_admin_request_locked(request, who, p, ledger);
}

namespace {

AdminReply handle_admin_request_locked(const std::vector<uint8_t>& request, const ClientFacts& who,
                                       const AdminPrimitives& p, RebootLedger* ledger) {
  AdminReply reply;
  if (!who.elevatedAdmin) {  // nothing about the request is read for a client that may not ask
    reply.status = AdminStatus::Refused;
    reply.rebootResult = 2;  // NotAuthorized, should a reboot have been meant
    return reply;
  }
  AdminRequest r;
  if (!DecodeAdminRequest(request, &r)) {
    reply.status = AdminStatus::BadRequest;
    return reply;
  }
  if (r.op == AdminOp::Reboot || r.op == AdminOp::Ready || r.op == AdminOp::Sas) {
    // Only this install's streaming host asks these (item 6 / 7, r9 R5).
    if (!who.hostProcess) {
      reply.status = AdminStatus::Refused;
      reply.rebootResult = r.op == AdminOp::Sas ? 3 : 2;  // NotAuthorized, either way
      return reply;
    }
    if (r.op == AdminOp::Sas) {
      // r9 R5: the answer travels in rebootResult (a PreloginSasResult for this op); `enabled` = DisableCAD.
      reply.status = AdminStatus::Ok;
      bool cadNotRequired = false;
      reply.rebootResult = p.requestSas ? p.requestSas(who.sessionId, &reply.win32, &cadNotRequired) : 4;
      reply.enabled = cadNotRequired;
      return reply;
    }
    if (r.op == AdminOp::Ready) {
      if (p.noteUserHostReady) p.noteUserHostReady(who.sessionId, who.processStartTime, r.readyNonce, who.processId);
      reply.status = AdminStatus::Ok;
      return reply;
    }
    if (r.requestId == 0 || !ledger || !p.requestOsReboot) {
      reply.status = AdminStatus::BadRequest;
      return reply;
    }
    std::lock_guard<std::mutex> lock(ledger->mu);
    // The request is this host process's, this remote peer's, this id (r5 G10).
    RebootKey key;
    key.processId = who.processId;
    key.processStartTime = who.processStartTime;
    key.peerKey = r.peerKey;
    key.requestId = r.requestId;
    for (const auto& a : ledger->answered) {
      if (a.first == key) return a.second;  // the same request: the same answer, no second OS call
    }
    const uint64_t now = ledger->nowMs ? ledger->nowMs() : GetTickCount64();
    if (ledger->inFlight && now - ledger->inFlightSinceMs >= kRebootInFlightMs) ledger->inFlight = false;  // it was cancelled
    reply.status = AdminStatus::Ok;
    if (ledger->inFlight) {
      reply.rebootResult = 1;  // Refused: a restart is already under way
    } else {
      const uint32_t w = p.requestOsReboot();
      if (w == 0) {
        reply.rebootResult = 0;  // Accepted: the OS took it -- not "done"
        ledger->inFlight = true;
        ledger->inFlightSinceMs = now;
      } else {
        reply.rebootResult = 3;  // Failed, never escalated to a forced restart
        reply.win32 = w;
      }
    }
    ledger->answered.emplace_back(key, reply);
    if (ledger->answered.size() > 32) ledger->answered.erase(ledger->answered.begin());
    return reply;
  }
  // r9 R1: a revoke that is still owed is carried out before anything is answered or decided -- a Query
  // reports the floor after it, an Enable is decided against it (and may then be Stale), a Disable is it.
  if (revoke_pending(p)) {
    const AdminReply owed = complete_pending_revoke_locked(p);
    if (r.op == AdminOp::Disable) return owed;
    if (owed.status != AdminStatus::Ok && r.op == AdminOp::Enable) {
      return owed;  // still owed: nothing may be turned on above it
    }
  }
  if (r.op == AdminOp::Query) {
    reply.status = AdminStatus::Ok;
    reply.enabled = p.featureEnabled();
    uint64_t floor = 0;
    reply.ownerGeneration = p.readFloor(&floor) ? floor : 0;
    return reply;
  }
  const std::wstring path = p.recordPath();
  MachineRecord old;
  const bool had = !path.empty() && p.readRecord(path, &old);
  uint64_t floor = 0;
  const bool floorKnown = p.readFloor(&floor);
  const uint64_t gen = (std::max)(had ? old.ownerGeneration : 0ull, floorKnown ? floor : 0ull) + 1;
  if (r.op == AdminOp::Enable) {
    if (r.directoryUrl.empty() || r.accountId.empty() || r.hostId.empty() || r.hostToken.empty()) {
      reply.status = AdminStatus::BadRequest;
      return reply;
    }
    // r6 V7: the worker registers with what the record names -- so the record names the product's own
    // server or nothing. An administrator's request for any other address is refused, not recorded.
    if (!same_origin(r.directoryUrl, p.allowedDirectoryUrl)) {
      reply.status = AdminStatus::BadRequest;
      reply.win32 = ERROR_INVALID_PARAMETER;
      return reply;
    }
    const std::string machine = p.machineId();
    if (floorKnown && floor != r.expectedFloor) {
      // r7 H2: decided before a Disable that has since raised the floor -- the old owner's, refused.
      reply.status = AdminStatus::Stale;
      reply.enabled = p.featureEnabled();
      reply.ownerGeneration = floor;
      return reply;
    }
    if (!floorKnown) {  // r6 G6: a record above an unknown floor might sit below a revoke -- fail safe
      reply.status = AdminStatus::Failed;
      reply.win32 = ERROR_READ_FAULT;
      reply.enabled = p.featureEnabled();
      return reply;
    }
    if (path.empty() || machine.empty()) {
      reply.status = AdminStatus::Failed;
      return reply;
    }
    MachineRecord rec;
    rec.accountId = AdminIdHash(r.accountId);
    rec.hostId = AdminIdHash(r.hostId);
    rec.ownerGeneration = gen;
    rec.enabled = true;
    rec.directoryOrigin = r.directoryUrl;  // what the --prelogin worker hands its directory client
    rec.machineId = machine;
    DirectoryCredential dc;
    dc.accountId = r.accountId;
    dc.hostId = r.hostId;
    dc.hostToken = r.hostToken;
    rec.credential = PackDirectoryCredential(dc);
    // r5 G6: nothing is published until everything else is in place, so the service's reconcile can
    // never find a half-on state. First the start type as it was before the feature, remembered once (a
    // second enable keeps the first) -- and if it cannot be remembered, nothing changes at all.
    uint32_t remembered = 0, current = 0;
    uint32_t original = 0;
    if (p.loadRememberedStartType(&remembered)) {
      original = remembered;
    } else if (!p.queryStartType(&current) || !p.rememberStartType(current)) {
      reply.status = AdminStatus::Failed;
      reply.win32 = ERROR_CANTWRITE;
      reply.enabled = p.featureEnabled();
      return reply;
    } else {
      original = current;
    }
    const uint32_t s = p.setStartType(kStartAuto);
    if (s != 0) {
      reply.status = AdminStatus::Failed;
      reply.win32 = s;
      reply.enabled = p.featureEnabled();
      return reply;
    }
    // Then the record, last: until it exists the feature is simply off.
    const uint32_t w = p.writeRecord(path, rec);
    if (w != 0) {
      (void)p.setStartType(original);  // back as it was
      reply.status = AdminStatus::Failed;
      reply.win32 = w;
      reply.enabled = p.featureEnabled();
      return reply;
    }
    reply.status = AdminStatus::Ok;
    reply.enabled = p.featureEnabled();
    reply.ownerGeneration = gen;
    return reply;
  }
  // Disable (r9: the one definition, disable_now -- the reconcile's owed revoke and uninstall use it too).
  (void)gen;
  reply = disable_now(p);
  if (reply.status == AdminStatus::Ok && revoke_pending(p) && p.clearRevokePending) (void)p.clearRevokePending();
  return reply;
}

}  // namespace

#ifdef _WIN32

bool ClientIsElevatedAdministrator(void* pipeHandle) {
  HANDLE pipe = static_cast<HANDLE>(pipeHandle);
  if (!ImpersonateNamedPipeClient(pipe)) return false;
  HANDLE token = nullptr;
  const BOOL opened = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token);
  RevertToSelf();
  if (!opened) return false;
  bool ok = false;
  {
    // LocalSystem (the --prelogin worker) is admitted as such.
    BYTE system[SECURITY_MAX_SID_SIZE];
    DWORD systemSize = sizeof(system);
    BOOL isSystem = FALSE;
    if (CreateWellKnownSid(WinLocalSystemSid, nullptr, system, &systemSize) &&
        CheckTokenMembership(token, system, &isSystem) && isSystem) {
      CloseHandle(token);
      return true;
    }
  }
  TOKEN_ELEVATION elevation{};
  DWORD n = 0;
  if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &n) && elevation.TokenIsElevated) {
    BYTE sid[SECURITY_MAX_SID_SIZE];
    DWORD sidSize = sizeof(sid);
    BOOL member = FALSE;
    ok = CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, sid, &sidSize) &&
         CheckTokenMembership(token, sid, &member) && member;
  }
  CloseHandle(token);
  return ok;
}

bool ClientImageIs(void* pipeHandle, const std::wstring& expectedImage) {
  ULONG pid = 0;
  if (expectedImage.empty() || !GetNamedPipeClientProcessId(static_cast<HANDLE>(pipeHandle), &pid) || pid == 0) return false;
  HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!proc) return false;
  wchar_t path[MAX_PATH * 2] = {};
  DWORD n = MAX_PATH * 2;
  const bool ok = QueryFullProcessImageNameW(proc, 0, path, &n) != FALSE &&
                  CompareStringOrdinal(path, static_cast<int>(n), expectedImage.c_str(), static_cast<int>(expectedImage.size()), TRUE) ==
                      CSTR_EQUAL;
  CloseHandle(proc);
  return ok;
}

namespace {
void client_session_and_start(HANDLE pipe, ClientFacts* who) {
  ULONG pid = 0;
  if (!GetNamedPipeClientProcessId(pipe, &pid) || pid == 0) return;
  who->processId = pid;
  DWORD session = 0;
  if (ProcessIdToSessionId(pid, &session)) who->sessionId = session;
  if (HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
    FILETIME c{}, e{}, k{}, u{};
    if (GetProcessTimes(proc, &c, &e, &k, &u)) who->processStartTime = (static_cast<uint64_t>(c.dwHighDateTime) << 32) | c.dwLowDateTime;
    CloseHandle(proc);
  }
}

constexpr DWORD kNoLimit = INFINITE;

// One overlapped operation that was started (`started` = its call's return value), finished: waited
// for up to `ms` (kNoLimit: until keepRunning() turns false), in quarter-second slices so a stop is
// noticed. On timeout or stop it is cancelled AND its completion is waited for, so the OVERLAPPED, the
// buffer and the event the caller owns are never released while the kernel may still write to them
// (r5 G4: CancelIoEx only asks). True when it completed successfully; *n the bytes.
bool finish_io(HANDLE h, OVERLAPPED* ov, BOOL started, DWORD ms, const std::function<bool()>& keepRunning, DWORD* n,
               DWORD* error = nullptr) {
  DWORD got = 0;
  if (!started) {
    const DWORD e = GetLastError();
    if (e != ERROR_IO_PENDING) {
      if (error) *error = e;
      return false;
    }
    const ULONGLONG end = ms == kNoLimit ? 0 : GetTickCount64() + ms;
    for (;;) {
      DWORD slice = 250;
      if (ms != kNoLimit) {
        const ULONGLONG now = GetTickCount64();
        slice = now >= end ? 0 : static_cast<DWORD>((std::min)(end - now, 250ull));
      }
      if (WaitForSingleObject(ov->hEvent, slice) == WAIT_OBJECT_0) break;
      const bool expired = ms != kNoLimit && GetTickCount64() >= end;
      if (expired || (keepRunning && !keepRunning())) {
        CancelIoEx(h, ov);
        GetOverlappedResult(h, ov, &got, TRUE);  // the cancel is only a request: wait for the completion
        if (error) *error = expired ? WAIT_TIMEOUT : ERROR_OPERATION_ABORTED;
        return false;
      }
    }
  }
  const BOOL ok = GetOverlappedResult(h, ov, &got, FALSE);
  if (!ok && error) *error = GetLastError();
  if (n) *n = got;
  return ok != FALSE;
}

std::wstring process_image(DWORD pid) {
  std::wstring out;
  if (HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
    wchar_t path[MAX_PATH * 2] = {};
    DWORD n = MAX_PATH * 2;
    if (QueryFullProcessImageNameW(proc, 0, path, &n)) out.assign(path, n);
    CloseHandle(proc);
  }
  return out;
}

// The executable of a service's configured command line: the quoted path, or the text up to ".exe".
std::wstring service_binary(const wchar_t* commandLine) {
  std::wstring s = commandLine ? commandLine : L"";
  if (!s.empty() && s[0] == L'"') {
    const size_t close = s.find(L'"', 1);
    return close == std::wstring::npos ? std::wstring() : s.substr(1, close - 1);
  }
  std::wstring lower = s;
  for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
  const size_t exe = lower.find(L".exe");
  return exe == std::wstring::npos ? s : s.substr(0, exe + 4);
}
}  // namespace

bool ServerIsProcess(void* pipeHandle, uint32_t pid) {
  ULONG server = 0;
  return pid != 0 && GetNamedPipeServerProcessId(static_cast<HANDLE>(pipeHandle), &server) && server == pid;
}

bool ServerIsInstalledService(void* pipeHandle) {
  ULONG server = 0;
  if (!GetNamedPipeServerProcessId(static_cast<HANDLE>(pipeHandle), &server) || server == 0) return false;
  bool ok = false;
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return false;
  if (SC_HANDLE svc = OpenServiceW(scm, kSecureInputServiceName, SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG)) {
    SERVICE_STATUS_PROCESS status{};
    DWORD need = 0;
    if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &need) &&
        status.dwCurrentState == SERVICE_RUNNING && status.dwProcessId == server) {
      // The running service holds that process id; and that process is the installed service binary.
      QueryServiceConfigW(svc, nullptr, 0, &need);
      std::vector<uint8_t> buf(need ? need : 1);
      auto* cfg = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data());
      if (need && QueryServiceConfigW(svc, cfg, need, &need)) {
        const std::wstring expected = service_binary(cfg->lpBinaryPathName);
        const std::wstring actual = process_image(server);
        ok = !expected.empty() && !actual.empty() &&
             CompareStringOrdinal(expected.c_str(), static_cast<int>(expected.size()), actual.c_str(),
                                  static_cast<int>(actual.size()), TRUE) == CSTR_EQUAL;
      }
    }
    CloseServiceHandle(svc);
  }
  CloseServiceHandle(scm);
  return ok;
}

bool RunPreloginAdminServer(const AdminPrimitives& p, std::function<bool()> keepRunning, const wchar_t* pipeName,
                            const wchar_t* sddl, AdminIdentityCheck isAdmin, AdminIdentityCheck isHost, RebootLedger* ledger) {
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor, nullptr)) return false;
  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.lpSecurityDescriptor = descriptor;
  // One instance, created first and kept (r5 G3): if the name is already someone's, this fails rather than
  // serving beside them; and it is never closed and re-made between requests, so nobody can take it then.
  HANDLE pipe = CreateNamedPipeW(pipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                 PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                                 4096, 8192, 0, &security);
  LocalFree(descriptor);
  if (pipe == INVALID_HANDLE_VALUE) return false;
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!event) {
    CloseHandle(pipe);
    return false;
  }
  while (keepRunning()) {
    OVERLAPPED ov{};
    ov.hEvent = event;
    ResetEvent(event);
    bool connected = false;
    if (ConnectNamedPipe(pipe, &ov)) {
      connected = true;
    } else if (GetLastError() == ERROR_PIPE_CONNECTED) {
      connected = true;
    } else {
      connected = finish_io(pipe, &ov, FALSE, kNoLimit, keepRunning, nullptr);
    }
    if (connected) {
      std::vector<uint8_t> request(8192);
      DWORD got = 0;
      ov = OVERLAPPED{};
      ov.hEvent = event;
      ResetEvent(event);
      const BOOL r = ReadFile(pipe, request.data(), static_cast<DWORD>(request.size()), &got, &ov);
      if (finish_io(pipe, &ov, r, 5000, keepRunning, &got)) {
        request.resize(got);
        ClientFacts who;
        who.elevatedAdmin = isAdmin(pipe);
        who.hostProcess = isHost && isHost(pipe);
        client_session_and_start(pipe, &who);
        const AdminReply reply = HandleAdminRequest(request, who, p, ledger);
        const std::vector<uint8_t> out = EncodeAdminReply(reply);
        ov = OVERLAPPED{};
        ov.hEvent = event;
        ResetEvent(event);
        DWORD wrote = 0;
        const BOOL w = WriteFile(pipe, out.data(), static_cast<DWORD>(out.size()), &wrote, &ov);
        if (finish_io(pipe, &ov, w, 5000, keepRunning, &wrote)) {
          // Let the client read it before the pipe is disconnected (which drops unread data): wait, bounded,
          // for the client to close its end. No FlushFileBuffers -- it can wait on the client forever.
          uint8_t probe = 0;
          DWORD n = 0;
          ov = OVERLAPPED{};
          ov.hEvent = event;
          ResetEvent(event);
          const BOOL c = ReadFile(pipe, &probe, 1, &n, &ov);
          finish_io(pipe, &ov, c, 2000, keepRunning, &n);
        }
      }
    }
    DisconnectNamedPipe(pipe);
  }
  CloseHandle(event);
  CloseHandle(pipe);
  return true;
}

AdminReply SendPreloginAdminRequest(const AdminRequest& r, uint32_t waitMs, const wchar_t* pipeName,
                                    const ServerCheck& verifyServer, uint32_t ioMs) {
  AdminReply reply;
  reply.status = AdminStatus::Unavailable;
  // The server serves one client at a time: while it does, the name is taken (PIPE_BUSY). That is
  // waited out within waitMs; so is a name not there yet (FILE_NOT_FOUND); anything else is the answer.
  const ULONGLONG deadline = GetTickCount64() + waitMs;
  HANDLE pipe = INVALID_HANDLE_VALUE;
  for (;;) {
    pipe = CreateFileW(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                       FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) break;
    const DWORD e = GetLastError();
    reply.win32 = e;
    if (e == ERROR_ACCESS_DENIED) {
      reply.status = AdminStatus::Refused;
      return reply;
    }
    const ULONGLONG now = GetTickCount64();
    if ((e != ERROR_FILE_NOT_FOUND && e != ERROR_PIPE_BUSY) || now >= deadline) return reply;
    if (e == ERROR_PIPE_BUSY) {
      WaitNamedPipeW(pipeName, static_cast<DWORD>((std::min)(deadline - now, 500ull)));
    } else {
      Sleep(50);
    }
  }
  // r5 G3: who is at the other end is settled BEFORE anything is written -- a credential (Enable) or a
  // request whose answer would be believed never goes to a server that is not the installed service.
  if (!verifyServer || !verifyServer(pipe)) {
    CloseHandle(pipe);
    reply = AdminReply{};
    reply.status = AdminStatus::Untrusted;
    return reply;
  }
  DWORD mode = PIPE_READMODE_MESSAGE;
  SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  const std::vector<uint8_t> out = EncodeAdminRequest(r);
  std::vector<uint8_t> in(64);
  DWORD wrote = 0, got = 0, error = 0;
  OVERLAPPED ov{};
  ov.hEvent = event;
  bool ok = event != nullptr;
  if (ok) {
    const BOOL w = WriteFile(pipe, out.data(), static_cast<DWORD>(out.size()), &wrote, &ov);
    ok = finish_io(pipe, &ov, w, ioMs, nullptr, &wrote, &error) && wrote == out.size();
  }
  if (ok) {
    ov = OVERLAPPED{};
    ov.hEvent = event;
    ResetEvent(event);
    const BOOL rd = ReadFile(pipe, in.data(), static_cast<DWORD>(in.size()), &got, &ov);
    ok = finish_io(pipe, &ov, rd, ioMs, nullptr, &got, &error);
  }
  if (ok) {
    in.resize(got);
    if (!DecodeAdminReply(in, &reply)) {
      reply = AdminReply{};
      reply.status = AdminStatus::BadRequest;
    }
  } else {
    reply.win32 = error;
  }
  if (event) CloseHandle(event);
  CloseHandle(pipe);
  return reply;
}

std::wstring HostServicePipeName() {
#ifdef REMOTE60_STREAM_TEST_SEAM
  // TEST BUILD ONLY (GNLinkStreamMigrationTest): a test pipe, never the service's.
  wchar_t v[256] = {};
  const DWORD n = GetEnvironmentVariableW(L"GNLINK_STREAM_TEST_ADMIN_PIPE", v, 256);
  const std::wstring name(v, n < 256 ? n : 0);
  if (name.rfind(L"\\\\.\\pipe\\GNLinkTest-", 0) == 0) return name;
  return L"\\\\.\\pipe\\GNLinkTest-none";
#else
  return kPreloginAdminPipeName;
#endif
}

ServerCheck HostServiceServerCheck() {
#ifdef REMOTE60_STREAM_TEST_SEAM
  // TEST BUILD ONLY: the test that serves the test pipe names itself; nobody named, nobody trusted.
  wchar_t v[32] = {};
  const DWORD n = GetEnvironmentVariableW(L"GNLINK_STREAM_TEST_ADMIN_SERVER_PID", v, 32);
  const uint32_t pid = n > 0 && n < 32 ? static_cast<uint32_t>(wcstoul(v, nullptr, 10)) : 0;
  return [pid](void* pipe) { return ServerIsProcess(pipe, pid); };
#else
  return ServerIsInstalledService;
#endif
}

uint8_t DelegateRebootToService(uint32_t requestId, uint64_t peerKey, uint32_t* win32) {
  AdminRequest r;
  r.op = AdminOp::Reboot;
  r.requestId = requestId;
  r.peerKey = peerKey;
  const std::wstring pipe = HostServicePipeName();
  const AdminReply reply = SendPreloginAdminRequest(r, 3000, pipe.c_str(), HostServiceServerCheck());
  *win32 = 0;
  if (reply.status == AdminStatus::Ok) {
    *win32 = reply.win32;
    return reply.rebootResult <= 3 ? reply.rebootResult : 3;  // a Sas-range value is no RebootResult
  }
  if (reply.status == AdminStatus::Refused) return 2;  // the service did not take this host as the host
  *win32 = reply.status == AdminStatus::Unavailable ? ERROR_SERVICE_NOT_ACTIVE
           : reply.status == AdminStatus::Untrusted ? ERROR_ACCESS_DENIED
                                                    : reply.win32;
  return 3;
}

uint32_t ParentProcessOf(uint32_t pid, uint64_t startTime) {
  HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!p) return 0;
  FILETIME c{}, e{}, k{}, u{};
  const bool timed = GetProcessTimes(p, &c, &e, &k, &u) != FALSE;
  CloseHandle(p);
  const uint64_t created = (static_cast<uint64_t>(c.dwHighDateTime) << 32) | c.dwLowDateTime;
  if (!timed || created != startTime) return 0;  // not the process that sent it
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return 0;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  uint32_t parent = 0;
  for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
    if (pe.th32ProcessID == pid) {
      parent = pe.th32ParentProcessID;
      break;
    }
  }
  CloseHandle(snap);
  return parent;
}

uint8_t DelegateSasToService(uint32_t* win32, bool* cadNotRequired) {
  *cadNotRequired = false;
  AdminRequest r;
  r.op = AdminOp::Sas;
  const std::wstring pipe = HostServicePipeName();
  const AdminReply reply = SendPreloginAdminRequest(r, 3000, pipe.c_str(), HostServiceServerCheck());
  *win32 = 0;
  if (reply.status == AdminStatus::Ok) {
    *win32 = reply.win32;
    *cadNotRequired = reply.enabled;
    return reply.rebootResult;
  }
  if (reply.status == AdminStatus::Refused) return 3;  // NotAuthorized: the service did not take this host
  *win32 = reply.status == AdminStatus::Unavailable ? ERROR_SERVICE_NOT_ACTIVE
           : reply.status == AdminStatus::Untrusted ? ERROR_ACCESS_DENIED
                                                    : reply.win32;
  return 4;  // Failed
}

bool QueryDurablePreloginEnabled() {
  AdminRequest r;
  r.op = AdminOp::Query;
  const std::wstring pipe = HostServicePipeName();
  const AdminReply reply = SendPreloginAdminRequest(r, 500, pipe.c_str(), HostServiceServerCheck(), 2000);
  return reply.status == AdminStatus::Ok && reply.enabled;
}

void NotifyUserHostReadyInBackground(std::function<bool()> readyNow, uint32_t waitReadyMs, int attempts, uint32_t delayMs,
                                     const std::wstring& pipeName, ServerCheck check) {
  const std::wstring pipe = pipeName.empty() ? HostServicePipeName() : pipeName;
  if (!check) check = HostServiceServerCheck();
  std::thread([readyNow, waitReadyMs, attempts, delayMs, pipe, check] {
    const ULONGLONG until = GetTickCount64() + waitReadyMs;
    while (readyNow && !readyNow()) {
      if (GetTickCount64() >= until) return;  // never registered: no ready, the service keeps prelogin
      Sleep(250);
    }
    AdminRequest r;
    r.op = AdminOp::Ready;
    // r6 G9: the nonce the service gave the Host that started this process (inherited from it).
    wchar_t nonce[129] = {};
    const DWORD n = GetEnvironmentVariableW(L"REMOTE60_PRELOGIN_READY_NONCE", nonce, 129);
    if (n > 0 && n < 129) r.readyNonce.assign(nonce, nonce + n);
    for (int i = 0; i < attempts; ++i) {
      if (SendPreloginAdminRequest(r, 2000, pipe.c_str(), check).status == AdminStatus::Ok) return;
      Sleep(delayMs);
    }
  }).detach();
}

#endif

}  // namespace remote60::native_poc::prelogin
