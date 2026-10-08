// Win32 implementation of the trusted service-pipe client layer (trusted_service_pipe.hpp).

#include "trusted_service_pipe.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace remote60::native_poc {

const char* trusted_pipe_status_name(TrustedPipeStatus s) {
  switch (s) {
    case TrustedPipeStatus::Trusted: return "trusted";
    case TrustedPipeStatus::Untrusted: return "untrusted";
    case TrustedPipeStatus::Unavailable: return "unavailable";
  }
  return "?";
}

const char* pipe_verify_stage_name(PipeVerifyStage s) {
  switch (s) {
    case PipeVerifyStage::None: return "none";
    case PipeVerifyStage::ServerPid: return "server-pid";
    case PipeVerifyStage::ServiceStatus: return "service-status";
    case PipeVerifyStage::ServerImage: return "server-image";
    case PipeVerifyStage::ServiceBinary: return "service-binary";
    case PipeVerifyStage::FinalRecheck: return "final-recheck";
  }
  return "?";
}

TrustedPipeStatus ClassifyServerIdentity(const ServerIdentityFacts& f) {
  // Fail closed on any missing fact: an identity that cannot be established is never trusted.
  if (!f.serverPidKnown || !f.serviceRunning) return TrustedPipeStatus::Unavailable;
  // The pipe's server must BE the SCM's running-service process -- not merely a same-named/SYSTEM one.
  if (f.serverPid != f.servicePid) return TrustedPipeStatus::Untrusted;
  // And its image must be the service's configured binary (closes pid reuse between the two lookups).
  if (!f.serverImageKnown || !f.serviceBinaryKnown) return TrustedPipeStatus::Unavailable;
  if (!f.imageSameFile) return TrustedPipeStatus::Untrusted;
  // R2: the earlier observations are only one instant each. Trust requires the end-of-collection
  // re-check to have held (same live process, still the SCM's running pid); otherwise a stop/replace
  // slipped through the window -> not trusted.
  if (!f.finalConsistent) return TrustedPipeStatus::Unavailable;
  return TrustedPipeStatus::Trusted;
}

namespace {

struct FileId {
  DWORD volume = 0;
  DWORD indexHigh = 0;
  DWORD indexLow = 0;
  bool read = false;
};

FileId file_id_of(const std::wstring& path) {
  FileId id;
  if (path.empty()) return id;
  HANDLE h = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (h == INVALID_HANDLE_VALUE) return id;
  BY_HANDLE_FILE_INFORMATION bi{};
  if (GetFileInformationByHandle(h, &bi)) {
    id.volume = bi.dwVolumeSerialNumber;
    id.indexHigh = bi.nFileIndexHigh;
    id.indexLow = bi.nFileIndexLow;
    id.read = true;
  }
  CloseHandle(h);
  return id;
}

std::wstring image_of_process(HANDLE p) {
  wchar_t buf[MAX_PATH * 2];
  DWORD size = static_cast<DWORD>(std::size(buf));
  std::wstring out;
  if (QueryFullProcessImageNameW(p, 0, buf, &size)) out.assign(buf, size);
  return out;
}

// The executable path from a service's configured ImagePath (strip a leading quote/token + arguments).
std::wstring service_binary_path(const wchar_t* serviceName) {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return {};
  SC_HANDLE svc = OpenServiceW(scm, serviceName, SERVICE_QUERY_CONFIG);
  if (!svc) {
    CloseServiceHandle(scm);
    return {};
  }
  DWORD needed = 0;
  QueryServiceConfigW(svc, nullptr, 0, &needed);
  std::wstring out;
  if (needed > 0) {
    std::vector<uint8_t> buf(needed);
    auto* cfg = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data());
    if (QueryServiceConfigW(svc, cfg, needed, &needed) && cfg->lpBinaryPathName) {
      std::wstring raw = cfg->lpBinaryPathName;
      // "C:\dir\svc.exe" args  OR  C:\dir\svc.exe args
      if (!raw.empty() && raw.front() == L'"') {
        const size_t close = raw.find(L'"', 1);
        out = close == std::wstring::npos ? raw.substr(1) : raw.substr(1, close - 1);
      } else {
        const size_t space = raw.find(L' ');
        out = space == std::wstring::npos ? raw : raw.substr(0, space);
      }
    }
  }
  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  return out;
}

// The SCM's RUNNING pid for a service (0 + running=false if not running / not installed / query failed).
// `errOut` (optional) gets the original Win32 error of whichever SCM step failed.
bool service_running_pid(const wchar_t* serviceName, uint32_t* pidOut, uint32_t* errOut = nullptr) {
  *pidOut = 0;
  if (errOut) *errOut = 0;
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) { if (errOut) *errOut = GetLastError(); return false; }
  SC_HANDLE svc = OpenServiceW(scm, serviceName, SERVICE_QUERY_STATUS);
  if (!svc) {
    if (errOut) *errOut = GetLastError();  // ERROR_SERVICE_DOES_NOT_EXIST when not installed
    CloseServiceHandle(scm);
    return false;
  }
  SERVICE_STATUS_PROCESS ssp{};
  DWORD needed = 0;
  const bool ok = QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp),
                                       sizeof(ssp), &needed) != FALSE;
  if (!ok && errOut) *errOut = GetLastError();
  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  if (!ok || ssp.dwCurrentState != SERVICE_RUNNING || ssp.dwProcessId == 0) return false;
  *pidOut = ssp.dwProcessId;
  return true;
}

}  // namespace

ServerIdentityFacts GatherServerIdentity(void* pipeHandle, const wchar_t* serviceName) {
  ServerIdentityFacts f;
  auto note_fail = [&](PipeVerifyStage stage, uint32_t err) {
    if (f.failStage == PipeVerifyStage::None) { f.failStage = stage; f.failWin32 = err; }
  };

  ULONG serverPid = 0;
  if (GetNamedPipeServerProcessId(static_cast<HANDLE>(pipeHandle), &serverPid) && serverPid != 0) {
    f.serverPidKnown = true;
    f.serverPid = static_cast<uint32_t>(serverPid);
  } else {
    note_fail(PipeVerifyStage::ServerPid, GetLastError());
  }

  uint32_t svcPid = 0;
  uint32_t svcErr = 0;
  if (service_running_pid(serviceName, &svcPid, &svcErr)) {
    f.serviceRunning = true;
    f.servicePid = svcPid;
  } else {
    note_fail(PipeVerifyStage::ServiceStatus, svcErr);
  }

  // R2: open the server process ONCE and HOLD the handle across the whole collection, so the pid cannot
  // be reused mid-check. Read its image through that held handle; compare to the service's binary.
  HANDLE serverProc = nullptr;
  if (f.serverPidKnown) {
    serverProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, f.serverPid);
    if (!serverProc) {
      note_fail(PipeVerifyStage::ServerImage, GetLastError());
    } else {
      const FileId img = file_id_of(image_of_process(serverProc));
      f.serverImageKnown = img.read;
      if (!img.read) note_fail(PipeVerifyStage::ServerImage, GetLastError());
      const FileId bin = file_id_of(service_binary_path(serviceName));
      f.serviceBinaryKnown = bin.read;
      if (!bin.read) note_fail(PipeVerifyStage::ServiceBinary, GetLastError());
      f.imageSameFile = img.read && bin.read && img.volume == bin.volume &&
                        img.indexHigh == bin.indexHigh && img.indexLow == bin.indexLow;
    }
  }

  // R2 final re-check: with the server handle still held, confirm it is still alive AND the SCM still
  // reports that SAME pid RUNNING. A stop/replace across the window (or an inability to re-confirm)
  // makes finalConsistent false -> the classifier refuses.
  if (serverProc) {
    const bool stillAlive = WaitForSingleObject(serverProc, 0) == WAIT_TIMEOUT;  // not signalled = alive
    uint32_t svcPid2 = 0, svcErr2 = 0;
    const bool stillRunning = service_running_pid(serviceName, &svcPid2, &svcErr2);
    f.finalConsistent = stillAlive && stillRunning && svcPid2 == f.serverPid;
    if (!f.finalConsistent) note_fail(PipeVerifyStage::FinalRecheck, stillRunning ? 0 : svcErr2);
    CloseHandle(serverProc);
  } else {
    f.finalConsistent = false;  // never opened the process -> cannot have re-confirmed it
  }
  return f;
}

void LogTrustChange(const char* channel, TrustedPipeStatus status, const ServerIdentityFacts& facts) {
  char line[256];
  _snprintf_s(line, _TRUNCATE,
              "trust[%s]=%s stage=%s win32=%lu serverPid=%lu svcPid=%lu finalConsistent=%d",
              channel ? channel : "?", trusted_pipe_status_name(status),
              pipe_verify_stage_name(facts.failStage), static_cast<unsigned long>(facts.failWin32),
              static_cast<unsigned long>(facts.serverPid), static_cast<unsigned long>(facts.servicePid),
              facts.finalConsistent ? 1 : 0);
  static std::mutex mu;
  static std::map<std::string, std::string> last;
  std::lock_guard<std::mutex> lk(mu);
  std::string& prev = last[channel ? channel : "?"];
  if (prev == line) return;  // only on change -> no per-input spam
  prev = line;
  std::fprintf(stderr, "[secure-pipe] %s\n", line);
  OutputDebugStringA(line);
}

void* OpenVerifiedServicePipe(const wchar_t* name, uint32_t desiredAccess, uint32_t flagsAndAttributes,
                              const wchar_t* serviceName, uint32_t waitMs, TrustedPipeStatus* status,
                              ServerIdentityGatherer gather, ServerIdentityFacts* diagOut) {
  if (status) *status = TrustedPipeStatus::Unavailable;
  (void)WaitNamedPipeW(name, waitMs == 0 ? NMPWAIT_USE_DEFAULT_WAIT : waitMs);
  // SECURITY_IDENTIFICATION: let the server identify the caller, but grant it no impersonation power.
  HANDLE pipe = CreateFileW(name, desiredAccess, 0, nullptr, OPEN_EXISTING,
                            flagsAndAttributes | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
  const ServerIdentityGatherer g = gather ? gather : &GatherServerIdentity;
  const ServerIdentityFacts facts = g(pipe, serviceName);
  const TrustedPipeStatus verdict = ClassifyServerIdentity(facts);
  if (status) *status = verdict;
  if (diagOut) *diagOut = facts;
  if (verdict != TrustedPipeStatus::Trusted) {
    CloseHandle(pipe);  // close ONLY the handle we opened; never touch the server process
    return INVALID_HANDLE_VALUE;
  }
  return pipe;
}

}  // namespace remote60::native_poc
