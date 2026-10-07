// "로그인 전에도 원격 접속 허용" -- request codec, the service's decision, the pipe server and client
// (prelogin-ui r1, item 4). See prelogin_admin_pipe.hpp.

#include "prelogin_admin_pipe.hpp"

#include <algorithm>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <sddl.h>
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
  }
  if (r.op == AdminOp::Reboot) put_u32(&o, r.requestId);
  return o;
}

bool DecodeAdminRequest(const std::vector<uint8_t>& b, AdminRequest* out) {
  Reader rd{b};
  uint32_t magic = 0;
  uint16_t version = 0;
  uint8_t op = 0;
  if (!rd.u32(&magic) || magic != kAdminMagic || !rd.u16(&version) || version != kAdminVersion || !rd.u8(&op)) return false;
  if (op < static_cast<uint8_t>(AdminOp::Query) || op > static_cast<uint8_t>(AdminOp::Ready)) return false;
  *out = AdminRequest{};
  out->op = static_cast<AdminOp>(op);
  if (out->op == AdminOp::Enable) {
    if (!rd.str(&out->directoryUrl, kAdminMaxString) || !rd.str(&out->accountId, kAdminMaxString) ||
        !rd.str(&out->hostId, kAdminMaxString) || !rd.str(&out->hostToken, kAdminMaxToken)) {
      return false;
    }
  }
  if (out->op == AdminOp::Reboot && !rd.u32(&out->requestId)) return false;
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
  if (!rd.u32(&magic) || magic != kAdminMagic || !rd.u8(&status) || status > static_cast<uint8_t>(AdminStatus::Unavailable) ||
      !rd.u8(&enabled) || enabled > 1 || !rd.u32(&out->win32) || !rd.u64(&out->ownerGeneration) ||
      !rd.u8(&out->rebootResult) || out->rebootResult > 3 || !rd.done()) {
    return false;
  }
  out->status = static_cast<AdminStatus>(status);
  out->enabled = enabled == 1;
  return true;
}

AdminReply HandleAdminRequest(const std::vector<uint8_t>& request, const ClientFacts& who, const AdminPrimitives& p,
                              RebootLedger* ledger) {
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
  if (r.op == AdminOp::Reboot || r.op == AdminOp::Ready) {
    // Only this install's streaming host asks these (item 6 / 7).
    if (!who.hostProcess) {
      reply.status = AdminStatus::Refused;
      reply.rebootResult = 2;
      return reply;
    }
    if (r.op == AdminOp::Ready) {
      if (p.noteUserHostReady) p.noteUserHostReady(who.sessionId, who.processStartTime);
      reply.status = AdminStatus::Ok;
      return reply;
    }
    if (r.requestId == 0 || !ledger || !p.requestOsReboot) {
      reply.status = AdminStatus::BadRequest;
      return reply;
    }
    std::lock_guard<std::mutex> lock(ledger->mu);
    for (const auto& a : ledger->answered) {
      if (a.first == r.requestId) return a.second;  // the same id: the same answer, no second OS call
    }
    reply.status = AdminStatus::Ok;
    if (ledger->inFlight) {
      reply.rebootResult = 1;  // Refused: a restart is already under way
    } else {
      const uint32_t w = p.requestOsReboot();
      if (w == 0) {
        reply.rebootResult = 0;  // Accepted: the OS took it -- not "done"
        ledger->inFlight = true;
      } else {
        reply.rebootResult = 3;  // Failed, never escalated to a forced restart
        reply.win32 = w;
      }
    }
    ledger->answered.emplace_back(r.requestId, reply);
    if (ledger->answered.size() > 32) ledger->answered.erase(ledger->answered.begin());
    return reply;
  }
  if (r.op == AdminOp::Query) {
    reply.status = AdminStatus::Ok;
    reply.enabled = p.featureEnabled();
    reply.ownerGeneration = p.readFloor();
    return reply;
  }
  const std::wstring path = p.recordPath();
  MachineRecord old;
  const bool had = !path.empty() && p.readRecord(path, &old);
  const uint64_t gen = (std::max)(had ? old.ownerGeneration : 0ull, p.readFloor()) + 1;
  if (r.op == AdminOp::Enable) {
    if (r.directoryUrl.empty() || r.accountId.empty() || r.hostId.empty() || r.hostToken.empty()) {
      reply.status = AdminStatus::BadRequest;
      return reply;
    }
    const std::string machine = p.machineId();
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
    const uint32_t w = p.writeRecord(path, rec);
    if (w != 0) {
      reply.status = AdminStatus::Failed;
      reply.win32 = w;
      return reply;
    }
    // The start type as it was before the feature, remembered once (a second enable keeps the first).
    uint32_t remembered = 0, current = 0;
    if (!p.loadRememberedStartType(&remembered) && p.queryStartType(&current)) p.rememberStartType(current);
    const uint32_t s = p.setStartType(kStartAuto);
    if (s != 0) {
      // Not half on: the record just written is withdrawn the way "off" withdraws it.
      (void)p.raiseFloor(gen + 1);
      (void)p.removeRecord(path);
      reply.status = AdminStatus::Failed;
      reply.win32 = s;
      reply.enabled = p.featureEnabled();
      return reply;
    }
    reply.status = AdminStatus::Ok;
    reply.enabled = p.featureEnabled();
    reply.ownerGeneration = gen;
    return reply;
  }
  // Disable: the floor first (an old blob can never come back), then the record, then the start type.
  if (!p.raiseFloor(gen)) {
    reply.status = AdminStatus::Failed;
    reply.enabled = p.featureEnabled();
    return reply;
  }
  const uint32_t rm = path.empty() ? 0 : p.removeRecord(path);
  uint32_t original = kStartDemand;
  const bool haveOriginal = p.loadRememberedStartType(&original);
  const uint32_t s = p.setStartType(haveOriginal ? original : kStartDemand);
  if (s == 0 && haveOriginal) p.forgetRememberedStartType();
  reply.status = (rm == 0 && s == 0) ? AdminStatus::Ok : AdminStatus::Failed;
  reply.win32 = rm != 0 ? rm : s;
  reply.enabled = p.featureEnabled();
  reply.ownerGeneration = gen;
  return reply;
}

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
  DWORD session = 0;
  if (ProcessIdToSessionId(pid, &session)) who->sessionId = session;
  if (HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
    FILETIME c{}, e{}, k{}, u{};
    if (GetProcessTimes(proc, &c, &e, &k, &u)) who->processStartTime = (static_cast<uint64_t>(c.dwHighDateTime) << 32) | c.dwLowDateTime;
    CloseHandle(proc);
  }
}
}  // namespace

void RunPreloginAdminServer(const AdminPrimitives& p, std::function<bool()> keepRunning, const wchar_t* pipeName,
                            const wchar_t* sddl, AdminIdentityCheck isAdmin, AdminIdentityCheck isHost, RebootLedger* ledger) {
  while (keepRunning()) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor, nullptr)) return;
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.lpSecurityDescriptor = descriptor;
    HANDLE pipe = CreateNamedPipeW(pipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                   PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                                   4096, 8192, 0, &security);
    LocalFree(descriptor);
    if (pipe == INVALID_HANDLE_VALUE) return;
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool connected = ConnectNamedPipe(pipe, &ov) != FALSE;
    if (!connected) {
      const DWORD e = GetLastError();
      if (e == ERROR_PIPE_CONNECTED) {
        connected = true;
      } else if (e == ERROR_IO_PENDING) {
        while (keepRunning()) {
          if (WaitForSingleObject(ov.hEvent, 250) == WAIT_OBJECT_0) {
            DWORD ignored = 0;
            connected = GetOverlappedResult(pipe, &ov, &ignored, FALSE) != FALSE;
            break;
          }
        }
        if (!connected) CancelIoEx(pipe, &ov);
      }
    }
    if (connected) {
      std::vector<uint8_t> request(8192);
      DWORD got = 0;
      ResetEvent(ov.hEvent);
      BOOL read = ReadFile(pipe, request.data(), static_cast<DWORD>(request.size()), &got, &ov);
      if (!read && GetLastError() == ERROR_IO_PENDING) {
        read = WaitForSingleObject(ov.hEvent, 5000) == WAIT_OBJECT_0 && GetOverlappedResult(pipe, &ov, &got, FALSE);
        if (!read) CancelIoEx(pipe, &ov);
      }
      if (read) {
        request.resize(got);
        ClientFacts who;
        who.elevatedAdmin = isAdmin(pipe);
        who.hostProcess = isHost && isHost(pipe);
        client_session_and_start(pipe, &who);
        const AdminReply reply = HandleAdminRequest(request, who, p, ledger);
        const std::vector<uint8_t> out = EncodeAdminReply(reply);
        DWORD wrote = 0;
        ResetEvent(ov.hEvent);
        if (!WriteFile(pipe, out.data(), static_cast<DWORD>(out.size()), &wrote, &ov) && GetLastError() == ERROR_IO_PENDING) {
          if (WaitForSingleObject(ov.hEvent, 5000) != WAIT_OBJECT_0) CancelIoEx(pipe, &ov);
        }
        FlushFileBuffers(pipe);
      }
      DisconnectNamedPipe(pipe);
    }
    CloseHandle(ov.hEvent);
    CloseHandle(pipe);
  }
}

AdminReply SendPreloginAdminRequest(const AdminRequest& r, uint32_t waitMs, const wchar_t* pipeName) {
  AdminReply reply;
  reply.status = AdminStatus::Unavailable;
  // The server serves one client at a time and makes its next instance after each: between two
  // requests the name can be briefly absent (FILE_NOT_FOUND) or taken (PIPE_BUSY). Both are waited
  // out within waitMs; anything else is the answer.
  const ULONGLONG deadline = GetTickCount64() + waitMs;
  HANDLE pipe = INVALID_HANDLE_VALUE;
  for (;;) {
    pipe = CreateFileW(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                       SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
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
  DWORD mode = PIPE_READMODE_MESSAGE;
  SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
  const std::vector<uint8_t> out = EncodeAdminRequest(r);
  DWORD wrote = 0;
  std::vector<uint8_t> in(64);
  DWORD got = 0;
  if (WriteFile(pipe, out.data(), static_cast<DWORD>(out.size()), &wrote, nullptr) &&
      ReadFile(pipe, in.data(), static_cast<DWORD>(in.size()), &got, nullptr)) {
    in.resize(got);
    if (!DecodeAdminReply(in, &reply)) {
      reply = AdminReply{};
      reply.status = AdminStatus::BadRequest;
    }
  } else {
    reply.win32 = GetLastError();
  }
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

uint8_t DelegateRebootToService(uint32_t requestId, uint32_t* win32) {
  AdminRequest r;
  r.op = AdminOp::Reboot;
  r.requestId = requestId;
  const std::wstring pipe = HostServicePipeName();
  const AdminReply reply = SendPreloginAdminRequest(r, 3000, pipe.c_str());
  *win32 = 0;
  if (reply.status == AdminStatus::Ok) {
    *win32 = reply.win32;
    return reply.rebootResult;
  }
  if (reply.status == AdminStatus::Refused) return 2;  // the service did not take this host as the host
  *win32 = reply.status == AdminStatus::Unavailable ? ERROR_SERVICE_NOT_ACTIVE : reply.win32;
  return 3;
}

void NotifyUserHostReadyInBackground(std::function<bool()> readyNow, uint32_t waitReadyMs, int attempts, uint32_t delayMs,
                                     const std::wstring& pipeName) {
  const std::wstring pipe = pipeName.empty() ? HostServicePipeName() : pipeName;
  std::thread([readyNow, waitReadyMs, attempts, delayMs, pipe] {
    const ULONGLONG until = GetTickCount64() + waitReadyMs;
    while (readyNow && !readyNow()) {
      if (GetTickCount64() >= until) return;  // never registered: no ready, the service keeps prelogin
      Sleep(250);
    }
    AdminRequest r;
    r.op = AdminOp::Ready;
    for (int i = 0; i < attempts; ++i) {
      if (SendPreloginAdminRequest(r, 2000, pipe.c_str()).status == AdminStatus::Ok) return;
      Sleep(delayMs);
    }
  }).detach();
}

#endif

}  // namespace remote60::native_poc::prelogin
