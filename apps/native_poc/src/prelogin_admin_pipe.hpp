#pragma once

// "로그인 전에도 원격 접속 허용" -- the setting's path from GNLinkHost to the service (prelogin-ui r1,
// item 4 of prelogin-service).
//
// Role:   GNLinkHost (an elevated admin app) asks the GNLinkSecureInput service -- the SINGLE writer of
//         the machine record and of its own start mode -- to turn pre-login access on or off, or to say
//         whether it is on. The service does only these fixed things: no path, no command, no start mode
//         travels in a request. On: the machine record is sealed with the signed-in host's directory
//         credential (packed as prelogin_machine_record.hpp packs it), its owner generation above both
//         the old record's and the durable floor; the service's start type, as it was, is remembered
//         and raised to AUTO. Off: the floor is raised past the record (an old blob can never come
//         back), the record is removed, and the remembered start type is put back. An upgrade never
//         turns it on -- only this request does.
// Trust:  the pipe's DACL admits SYSTEM and Administrators only (a non-elevated admin token holds BA
//         deny-only and cannot open it), remote clients are rejected, and the server additionally
//         checks the impersonated client token: an Administrators member with an elevated token.
//         A refused client gets Refused and nothing is done.
// Thread: the server runs on its own thread in the service; each request is one connect, one message
//         each way. Pure parts (codec, HandleAdminRequest, the enable/disable bookkeeping over injected
//         primitives) are testable without the service.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "prelogin_machine_record.hpp"

namespace remote60::native_poc::prelogin {

constexpr wchar_t kPreloginAdminPipeName[] = L"\\\\.\\pipe\\GNLinkPreloginAdmin";
constexpr uint32_t kAdminMagic = 0x41504C47u;  // "GLPA"
constexpr uint16_t kAdminVersion = 1;
constexpr size_t kAdminMaxString = 512;
constexpr size_t kAdminMaxToken = 2048;

enum class AdminOp : uint8_t { Query = 1, Enable = 2, Disable = 3 };
enum class AdminStatus : uint8_t {
  Ok = 0,
  Refused = 1,     // the client is not an elevated administrator
  BadRequest = 2,  // malformed / unknown op / missing credential
  Failed = 3,      // the store or the service configuration failed; win32 says why
  Unavailable = 4, // client side: the service's pipe is not there (not installed / not running)
};

struct AdminRequest {
  AdminOp op = AdminOp::Query;
  // Enable only: the signed-in host's directory credential, as GNLinkHost holds it.
  std::string directoryUrl;
  std::string accountId;
  std::string hostId;
  std::string hostToken;  // NEVER logged
};

struct AdminReply {
  AdminStatus status = AdminStatus::BadRequest;
  bool enabled = false;          // pre-login access is on after this request
  uint32_t win32 = 0;
  uint64_t ownerGeneration = 0;  // the generation the record / floor now carries
};

std::vector<uint8_t> EncodeAdminRequest(const AdminRequest& r);
bool DecodeAdminRequest(const std::vector<uint8_t>& b, AdminRequest* out);
std::vector<uint8_t> EncodeAdminReply(const AdminReply& r);
bool DecodeAdminReply(const std::vector<uint8_t>& b, AdminReply* out);

// What the service does, as primitives it already owns (secure_input_service_main.cpp binds them).
struct AdminPrimitives {
  std::function<std::wstring()> recordPath;                 // the machine record's fixed path
  std::function<std::string()> machineId;                   // HKLM MachineGuid
  std::function<uint64_t()> readFloor;                      // the durable owner-generation floor
  std::function<bool(uint64_t)> raiseFloor;                 // monotonic
  std::function<bool(const std::wstring&, MachineRecord*)> readRecord;   // false = none / unreadable
  std::function<uint32_t(const std::wstring&, const MachineRecord&)> writeRecord;  // 0 = ok, else win32
  std::function<uint32_t(const std::wstring&)> removeRecord;            // 0 = ok / already gone
  std::function<bool()> featureEnabled;                     // the service's own judgement of "on now"
  // Start type: the current one, setting one, and where the pre-enable one is remembered.
  std::function<bool(uint32_t*)> queryStartType;
  std::function<uint32_t(uint32_t)> setStartType;           // 0 = ok, else win32
  std::function<bool(uint32_t*)> loadRememberedStartType;   // false = none remembered
  std::function<bool(uint32_t)> rememberStartType;
  std::function<void()> forgetRememberedStartType;
};

// The one decision per request (pure over the primitives). clientIsAdmin = the server's identity check.
AdminReply HandleAdminRequest(const std::vector<uint8_t>& request, bool clientIsAdmin, const AdminPrimitives& p);

// FNV-1a 64 of an id string, never 0 -- the record's numeric accountId / hostId (the strings travel in
// the packed credential, which is what the --prelogin worker uses).
uint64_t AdminIdHash(const std::string& s);

#ifdef _WIN32
// The server loop: one client at a time, while keepRunning(). `pipeName`/`sddl`/`isAdmin` are the
// product's unless a test passes its own (a test pipe name, the test user's DACL, a stand-in check).
using AdminIdentityCheck = std::function<bool(void* pipe)>;
bool ClientIsElevatedAdministrator(void* pipe);
void RunPreloginAdminServer(const AdminPrimitives& p, std::function<bool()> keepRunning,
                            const wchar_t* pipeName = kPreloginAdminPipeName,
                            const wchar_t* sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",
                            AdminIdentityCheck isAdmin = ClientIsElevatedAdministrator);
// The product's start-type and record primitives: the SCM (and HKLM\SOFTWARE\GNLink
// PreloginOriginalStartType for the pre-enable type), and prelogin_machine_store + DeleteFileW.
void BindProductStartType(AdminPrimitives* p, const wchar_t* serviceName);
void BindProductMachineStore(AdminPrimitives* p);
// GNLinkHost's side: one request, one reply. Unavailable when the pipe is not there within waitMs.
AdminReply SendPreloginAdminRequest(const AdminRequest& r, uint32_t waitMs, const wchar_t* pipeName = kPreloginAdminPipeName);
#endif

}  // namespace remote60::native_poc::prelogin
