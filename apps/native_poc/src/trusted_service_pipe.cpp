// Win32 implementation of the trusted service-pipe client layer (trusted_service_pipe.hpp).

#include "trusted_service_pipe.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <iterator>
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

TrustedPipeStatus ClassifyServerIdentity(const ServerIdentityFacts& f) {
  // Fail closed on any missing fact: an identity that cannot be established is never trusted.
  if (!f.serverPidKnown || !f.serviceRunning) return TrustedPipeStatus::Unavailable;
  // The pipe's server must BE the SCM's running-service process -- not merely a same-named/SYSTEM one.
  if (f.serverPid != f.servicePid) return TrustedPipeStatus::Untrusted;
  // And its image must be the service's configured binary (closes pid reuse between the two lookups).
  if (!f.serverImageKnown || !f.serviceBinaryKnown) return TrustedPipeStatus::Unavailable;
  return f.imageSameFile ? TrustedPipeStatus::Trusted : TrustedPipeStatus::Untrusted;
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

// The image file of a running process, by pid, holding the handle only for the read.
std::wstring image_of_pid(uint32_t pid) {
  HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!p) return {};
  wchar_t buf[MAX_PATH * 2];
  DWORD size = static_cast<DWORD>(std::size(buf));
  std::wstring out;
  if (QueryFullProcessImageNameW(p, 0, buf, &size)) out.assign(buf, size);
  CloseHandle(p);
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
bool service_running_pid(const wchar_t* serviceName, uint32_t* pidOut) {
  *pidOut = 0;
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return false;
  SC_HANDLE svc = OpenServiceW(scm, serviceName, SERVICE_QUERY_STATUS);
  if (!svc) {
    CloseServiceHandle(scm);
    return false;
  }
  SERVICE_STATUS_PROCESS ssp{};
  DWORD needed = 0;
  const bool ok = QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp),
                                       sizeof(ssp), &needed) != FALSE;
  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  if (!ok || ssp.dwCurrentState != SERVICE_RUNNING || ssp.dwProcessId == 0) return false;
  *pidOut = ssp.dwProcessId;
  return true;
}

}  // namespace

ServerIdentityFacts GatherServerIdentity(void* pipeHandle, const wchar_t* serviceName) {
  ServerIdentityFacts f;
  ULONG serverPid = 0;
  if (GetNamedPipeServerProcessId(static_cast<HANDLE>(pipeHandle), &serverPid) && serverPid != 0) {
    f.serverPidKnown = true;
    f.serverPid = static_cast<uint32_t>(serverPid);
  }
  uint32_t svcPid = 0;
  if (service_running_pid(serviceName, &svcPid)) {
    f.serviceRunning = true;
    f.servicePid = svcPid;
  }
  // Only worth the image compare when the pids line up; but gather both file ids unconditionally so the
  // classifier (not this layer) owns the policy. The server image is read from the live pid; the service
  // binary from its SCM config.
  if (f.serverPidKnown) {
    const FileId img = file_id_of(image_of_pid(f.serverPid));
    f.serverImageKnown = img.read;
    const FileId bin = file_id_of(service_binary_path(serviceName));
    f.serviceBinaryKnown = bin.read;
    f.imageSameFile = img.read && bin.read && img.volume == bin.volume &&
                      img.indexHigh == bin.indexHigh && img.indexLow == bin.indexLow;
  }
  return f;
}

void* OpenVerifiedServicePipe(const wchar_t* name, uint32_t desiredAccess, uint32_t flagsAndAttributes,
                              const wchar_t* serviceName, uint32_t waitMs, TrustedPipeStatus* status,
                              ServerIdentityGatherer gather) {
  if (status) *status = TrustedPipeStatus::Unavailable;
  (void)WaitNamedPipeW(name, waitMs == 0 ? NMPWAIT_USE_DEFAULT_WAIT : waitMs);
  // SECURITY_IDENTIFICATION: let the server identify the caller, but grant it no impersonation power.
  HANDLE pipe = CreateFileW(name, desiredAccess, 0, nullptr, OPEN_EXISTING,
                            flagsAndAttributes | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
  const ServerIdentityGatherer g = gather ? gather : &GatherServerIdentity;
  const TrustedPipeStatus verdict = ClassifyServerIdentity(g(pipe, serviceName));
  if (status) *status = verdict;
  if (verdict != TrustedPipeStatus::Trusted) {
    CloseHandle(pipe);  // close ONLY the handle we opened; never touch the server process
    return INVALID_HANDLE_VALUE;
  }
  return pipe;
}

}  // namespace remote60::native_poc
