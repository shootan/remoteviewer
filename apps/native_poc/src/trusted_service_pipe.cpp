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
    case PipeVerifyStage::Open: return "open";
    case PipeVerifyStage::ServerPid: return "server-pid";
    case PipeVerifyStage::ServiceStatus: return "service-status";
    case PipeVerifyStage::ServerImage: return "server-image";
    case PipeVerifyStage::ServiceBinary: return "service-binary";
    case PipeVerifyStage::ServerPidMismatch: return "server-pid-mismatch";
    case PipeVerifyStage::ImageMismatch: return "image-mismatch";
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

FileId file_id_of(const std::wstring& path, uint32_t* errOut = nullptr) {
  FileId id;
  if (errOut) *errOut = 0;
  if (path.empty()) { if (errOut) *errOut = ERROR_PATH_NOT_FOUND; return id; }
  HANDLE h = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (h == INVALID_HANDLE_VALUE) { if (errOut) *errOut = GetLastError(); return id; }  // capture NOW
  BY_HANDLE_FILE_INFORMATION bi{};
  if (GetFileInformationByHandle(h, &bi)) {
    id.volume = bi.dwVolumeSerialNumber;
    id.indexHigh = bi.nFileIndexHigh;
    id.indexLow = bi.nFileIndexLow;
    id.read = true;
  } else if (errOut) {
    *errOut = GetLastError();
  }
  CloseHandle(h);
  return id;
}

std::wstring image_of_process(HANDLE p, uint32_t* errOut = nullptr) {
  if (errOut) *errOut = 0;
  wchar_t buf[MAX_PATH * 2];
  DWORD size = static_cast<DWORD>(std::size(buf));
  std::wstring out;
  if (QueryFullProcessImageNameW(p, 0, buf, &size)) out.assign(buf, size);
  else if (errOut) *errOut = GetLastError();  // C3: keep the real error (e.g. 5), not file_id's 3
  return out;
}

}  // namespace

// r6 D3: the two Win32 calls whose failures the diagnostics must keep, replaceable by a test (default: the
// real ones).
int __stdcall TrustedPipeWin32Seam::DefaultQueryServiceConfig(void* service, void* config, unsigned long size,
                                                             unsigned long* needed) {
  return QueryServiceConfigW(static_cast<SC_HANDLE>(service), static_cast<LPQUERY_SERVICE_CONFIGW>(config), size, needed);
}
unsigned long __stdcall TrustedPipeWin32Seam::DefaultWaitForSingleObject(void* handle, unsigned long ms) {
  return WaitForSingleObject(static_cast<HANDLE>(handle), ms);
}
TrustedPipeWin32Seam& TrustedPipeWin32ForTest() {
  static TrustedPipeWin32Seam seam;
  return seam;
}

namespace {

// The executable path from a service's configured ImagePath (strip a leading quote/token + arguments).
std::wstring service_binary_path(const wchar_t* serviceName, uint32_t* errOut = nullptr) {
  if (errOut) *errOut = 0;
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) { if (errOut) *errOut = GetLastError(); return {}; }
  SC_HANDLE svc = OpenServiceW(scm, serviceName, SERVICE_QUERY_CONFIG);
  if (!svc) {
    if (errOut) *errOut = GetLastError();
    CloseServiceHandle(scm);
    return {};
  }
  DWORD needed = 0;
  std::wstring out;
  // r6 D3: the size query is EXPECTED to fail with ERROR_INSUFFICIENT_BUFFER; any other failure (or none
  // with no size) is the real error, kept as it is right after the call.
  const auto query = TrustedPipeWin32ForTest().queryServiceConfig;
  if (!query(svc, nullptr, 0, &needed)) {
    const DWORD e = GetLastError();
    if (e != ERROR_INSUFFICIENT_BUFFER && errOut) *errOut = e;
  }
  if (needed == 0 && errOut && *errOut == 0) *errOut = ERROR_INVALID_DATA;
  if (needed > 0) {
    std::vector<uint8_t> buf(needed);
    auto* cfg = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data());
    const BOOL got = query(svc, cfg, needed, &needed);
    if (!got && errOut) *errOut = GetLastError();  // r6 D3: the second query's own failure
    if (got && cfg->lpBinaryPathName) {
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

// R2 final re-check: the server process still alive AND the SCM still reporting that same pid RUNNING.
// r6 D3: a failed wait's error is taken RIGHT AFTER the wait (before the SCM calls overwrite it); otherwise
// the SCM re-query's error (0 when the service is simply stopped or the pid moved).
bool final_recheck(HANDLE serverProc, const wchar_t* serviceName, uint32_t serverPid, uint32_t* errOut) {
  *errOut = 0;
  const DWORD waitRc = TrustedPipeWin32ForTest().waitForSingleObject(serverProc, 0);
  const DWORD waitErr = waitRc == WAIT_FAILED ? GetLastError() : 0;
  const bool stillAlive = waitRc == WAIT_TIMEOUT;  // not signalled = alive; WAIT_FAILED = cannot tell
  uint32_t svcPid2 = 0, svcErr2 = 0;
  const bool stillRunning = service_running_pid(serviceName, &svcPid2, &svcErr2);
  const bool ok = stillAlive && stillRunning && svcPid2 == serverPid;
  if (!ok) *errOut = waitRc == WAIT_FAILED ? waitErr : (stillRunning ? 0 : svcErr2);
  return ok;
}

}  // namespace

std::wstring ServiceBinaryPathForTest(const wchar_t* serviceName, uint32_t* errOut) {
  return service_binary_path(serviceName, errOut);
}
bool FinalRecheckForTest(void* serverProcess, const wchar_t* serviceName, uint32_t serverPid, uint32_t* errOut) {
  return final_recheck(static_cast<HANDLE>(serverProcess), serviceName, serverPid, errOut);
}

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
      // C3: keep the ORIGINAL error of the step that actually failed. A query failure returns an empty
      // path; passing that to file_id_of would overwrite the real error (e.g. 5 access-denied / 122
      // buffer) with ERROR_PATH_NOT_FOUND. So read the path+its error first, and only file_id a path
      // that was actually obtained.
      uint32_t imgQErr = 0, binQErr = 0;
      const std::wstring imgPath = image_of_process(serverProc, &imgQErr);
      const std::wstring binPath = service_binary_path(serviceName, &binQErr);
      uint32_t imgFErr = 0, binFErr = 0;
      const FileId img = file_id_of(imgPath, &imgFErr);
      const FileId bin = file_id_of(binPath, &binFErr);
      f.serverImageKnown = img.read;
      if (!img.read) note_fail(PipeVerifyStage::ServerImage, imgPath.empty() ? imgQErr : imgFErr);
      f.serviceBinaryKnown = bin.read;
      if (!bin.read) note_fail(PipeVerifyStage::ServiceBinary, binPath.empty() ? binQErr : binFErr);
      f.imageSameFile = img.read && bin.read && img.volume == bin.volume &&
                        img.indexHigh == bin.indexHigh && img.indexLow == bin.indexLow;
    }
  }

  // Record the DISTINGUISHING reason for an otherwise-complete-but-not-trusted result (R3): a pid that
  // does not match the SCM's running pid, or an image that is a different file. win32=0 here -- the
  // syscalls succeeded; it is the comparison that refused.
  if (f.serverPidKnown && f.serviceRunning && f.serverPid != f.servicePid)
    note_fail(PipeVerifyStage::ServerPidMismatch, 0);
  else if (f.serverPidKnown && f.serviceRunning && f.serverImageKnown && f.serviceBinaryKnown &&
           !f.imageSameFile)
    note_fail(PipeVerifyStage::ImageMismatch, 0);

  // R2 final re-check: with the server handle still held, confirm it is still alive AND the SCM still
  // reports that SAME pid RUNNING. A stop/replace across the window (or an inability to re-confirm)
  // makes finalConsistent false -> the classifier refuses.
  if (serverProc) {
    uint32_t rc = 0;
    f.finalConsistent = final_recheck(serverProc, serviceName, f.serverPid, &rc);
    if (!f.finalConsistent) note_fail(PipeVerifyStage::FinalRecheck, rc);
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
  if (diagOut) *diagOut = ServerIdentityFacts{};  // default: finalConsistent=false, stage=None
  (void)WaitNamedPipeW(name, waitMs == 0 ? NMPWAIT_USE_DEFAULT_WAIT : waitMs);
  // SECURITY_IDENTIFICATION: let the server identify the caller, but grant it no impersonation power.
  HANDLE pipe = CreateFileW(name, desiredAccess, 0, nullptr, OPEN_EXISTING,
                            flagsAndAttributes | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    // R3: the open itself failed -- distinguish absent (FILE_NOT_FOUND) / access denied / busy, instead
    // of collapsing all into a bare Unavailable with stage=None.
    if (diagOut) { diagOut->failStage = PipeVerifyStage::Open; diagOut->failWin32 = GetLastError(); }
    return INVALID_HANDLE_VALUE;
  }
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
