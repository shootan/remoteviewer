// See lock_cred_channel.hpp.

#include "lock_cred_channel.hpp"

#include <sddl.h>
#include <tlhelp32.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")

namespace remote60::native_poc::lock_cred_channel {

bool IdIsWellFormed(const std::wstring& id) {
  if (id.size() != 32) return false;
  for (wchar_t c : id) {
    if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return false;
  }
  return true;
}

std::wstring PipeNameFor(const std::wstring& id) { return std::wstring(kPipePrefix) + id; }

HANDLE CreateServerPipe(const wchar_t* sddl, std::wstring* idOut, uint32_t* err, const std::wstring& fixedId) {
  *err = 0;
  std::wstring id = fixedId;
  if (id.empty()) {
    uint8_t rnd[16] = {};
    if (BCryptGenRandom(nullptr, rnd, sizeof(rnd), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
      *err = ERROR_GEN_FAILURE;
      return INVALID_HANDLE_VALUE;
    }
    static const wchar_t kHex[] = L"0123456789abcdef";
    for (uint8_t b : rnd) {
      id.push_back(kHex[b >> 4]);
      id.push_back(kHex[b & 0xF]);
    }
  }
  if (!IdIsWellFormed(id)) {
    *err = ERROR_INVALID_NAME;
    return INVALID_HANDLE_VALUE;
  }
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr)) {
    *err = GetLastError();
    return INVALID_HANDLE_VALUE;
  }
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.lpSecurityDescriptor = sd;
  sa.bInheritHandle = FALSE;
  const std::wstring name = PipeNameFor(id);
  HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096,
                                 4096, 0, &sa);
  if (pipe == INVALID_HANDLE_VALUE) *err = GetLastError();
  LocalFree(sd);
  if (pipe != INVALID_HANDLE_VALUE && idOut) *idOut = id;
  return pipe;
}

namespace {
// Waits for an overlapped operation, bounded; on timeout the I/O is cancelled and waited out.
bool finish_overlapped(HANDLE pipe, OVERLAPPED* ov, DWORD timeoutMs, DWORD* done) {
  if (WaitForSingleObject(ov->hEvent, timeoutMs) != WAIT_OBJECT_0) {
    CancelIoEx(pipe, ov);
    GetOverlappedResult(pipe, ov, done, TRUE);
    return false;
  }
  return GetOverlappedResult(pipe, ov, done, FALSE) != FALSE;
}

bool token_is_local_system(HANDLE process) {
  HANDLE tok = nullptr;
  if (!OpenProcessToken(process, TOKEN_QUERY, &tok)) return false;
  BYTE buf[SECURITY_MAX_SID_SIZE + sizeof(TOKEN_USER)] = {};
  DWORD n = 0;
  bool system = false;
  if (GetTokenInformation(tok, TokenUser, buf, sizeof(buf), &n)) {
    BYTE sys[SECURITY_MAX_SID_SIZE];
    DWORD sysSize = sizeof(sys);
    system = CreateWellKnownSid(WinLocalSystemSid, nullptr, sys, &sysSize) &&
             EqualSid(reinterpret_cast<TOKEN_USER*>(buf)->User.Sid, sys);
  }
  CloseHandle(tok);
  return system;
}
}  // namespace

bool WriteBounded(HANDLE pipe, const void* data, DWORD n, DWORD timeoutMs) {
  OVERLAPPED ov{};
  ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!ov.hEvent) return false;
  DWORD done = 0;
  bool ok = WriteFile(pipe, data, n, &done, &ov) != FALSE;
  if (!ok && GetLastError() == ERROR_IO_PENDING) ok = finish_overlapped(pipe, &ov, timeoutMs, &done);
  CloseHandle(ov.hEvent);
  return ok && done == n;
}

bool ReadBounded(HANDLE pipe, void* data, DWORD n, DWORD timeoutMs) {
  auto* p = static_cast<uint8_t*>(data);
  DWORD total = 0;
  const ULONGLONG deadline = GetTickCount64() + timeoutMs;
  while (total < n) {
    const ULONGLONG now = GetTickCount64();
    if (now >= deadline) return false;
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return false;
    DWORD done = 0;
    bool ok = ReadFile(pipe, p + total, n - total, &done, &ov) != FALSE;
    if (!ok && GetLastError() == ERROR_IO_PENDING) ok = finish_overlapped(pipe, &ov, static_cast<DWORD>(deadline - now), &done);
    CloseHandle(ov.hEvent);
    if (!ok || done == 0) return false;
    total += done;
  }
  return true;
}

bool AcceptOwnedChild(HANDLE pipe, HANDLE child, DWORD childPid, DWORD expectSession, bool requireSystem, DWORD timeoutMs,
                      const char** why) {
  auto fail = [why](const char* w) {
    if (why) *why = w;
    return false;
  };
  OVERLAPPED ov{};
  ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!ov.hEvent) return fail("event");
  bool connected = ConnectNamedPipe(pipe, &ov) != FALSE;
  DWORD e = GetLastError();
  if (!connected && e == ERROR_PIPE_CONNECTED) connected = true;
  if (!connected && e == ERROR_IO_PENDING) {
    // Bounded, and not past the child's own end: an agent that dies before connecting ends the wait.
    HANDLE waits[2] = {ov.hEvent, child};
    const DWORD w = WaitForMultipleObjects(2, waits, FALSE, timeoutMs);
    if (w == WAIT_OBJECT_0) {
      DWORD done = 0;
      connected = GetOverlappedResult(pipe, &ov, &done, FALSE) != FALSE;
    } else {
      CancelIoEx(pipe, &ov);
      DWORD done = 0;
      GetOverlappedResult(pipe, &ov, &done, TRUE);
    }
  }
  CloseHandle(ov.hEvent);
  if (!connected) return fail("no-connection");
  // The client is THE child: its process id is the one this service holds a handle to (so it cannot have
  // been reused), it is still running, in the target session, as SYSTEM.
  ULONG clientPid = 0, clientSession = 0;
  if (!GetNamedPipeClientProcessId(pipe, &clientPid) || clientPid != childPid) return fail("client-not-the-child");
  if (WaitForSingleObject(child, 0) != WAIT_TIMEOUT) return fail("child-not-running");
  if (!GetNamedPipeClientSessionId(pipe, &clientSession) || clientSession != expectSession) return fail("client-session");
  DWORD childSession = 0;
  if (!ProcessIdToSessionId(childPid, &childSession) || childSession != expectSession) return fail("child-session");
  if (requireSystem && !token_is_local_system(child)) return fail("child-not-system");
  Hello hello{};
  if (!ReadBounded(pipe, &hello, sizeof(hello), timeoutMs)) return fail("no-hello");
  if (hello.magic != kHelloMagic || hello.size != sizeof(hello) || hello.version != kVersion || hello.pid != childPid) {
    return fail("bad-hello");
  }
  if (why) *why = "ok";
  return true;
}

DWORD ParentProcessId() {
  const DWORD me = GetCurrentProcessId();
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return 0;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  DWORD parent = 0;
  for (BOOL more = Process32FirstW(snap, &pe); more; more = Process32NextW(snap, &pe)) {
    if (pe.th32ProcessID == me) {
      parent = pe.th32ParentProcessID;
      break;
    }
  }
  CloseHandle(snap);
  return parent;
}

HANDLE OpenFromAgent(const std::wstring& id, DWORD expectedServerPid, ServerCheck verifyServer, const char** why) {
  auto fail = [why](const char* w) {
    if (why) *why = w;
    return INVALID_HANDLE_VALUE;
  };
  if (!IdIsWellFormed(id)) return fail("bad-id");
  if (expectedServerPid == 0) return fail("no-parent");
  const std::wstring name = PipeNameFor(id);
  if (!WaitNamedPipeW(name.c_str(), 3000)) return fail("no-pipe");
  HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                            SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) return fail("open");
  ULONG serverPid = 0;
  if (!GetNamedPipeServerProcessId(pipe, &serverPid) || serverPid != expectedServerPid) {
    CloseHandle(pipe);
    return fail("server-not-the-parent");
  }
  if (!verifyServer || !verifyServer(pipe)) {
    CloseHandle(pipe);
    return fail("server-not-trusted");
  }
  if (why) *why = "ok";
  return pipe;
}

}  // namespace remote60::native_poc::lock_cred_channel
