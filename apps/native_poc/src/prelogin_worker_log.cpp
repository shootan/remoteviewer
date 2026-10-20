// See prelogin_worker_log.hpp (prelogin-field D3).

#include "prelogin_worker_log.hpp"

#include <aclapi.h>
#include <fcntl.h>
#include <io.h>
#include <sddl.h>
#include <shlobj.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace remote60::native_poc::prelogin {

namespace {

// SYSTEM + Administrators only, not inherited from the folder (protected).
constexpr wchar_t kLogFileSddl[] = L"D:P(A;;FA;;;SY)(A;;FA;;;BA)";
constexpr char kDrainMarker[] = "\x1eGNLINK-WORKER-LOG-DRAIN ";

// The opened object is a regular file, not a redirect, with one name, a trusted owner and nobody else in its DACL.
bool file_handle_acceptable(HANDLE h, const std::function<bool(PSID)>& ownerTrusted) {
  BY_HANDLE_FILE_INFORMATION bi{};
  if (!GetFileInformationByHandle(h, &bi)) return false;
  if (bi.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) return false;
  if (bi.nNumberOfLinks != 1) return false;
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (GetSecurityInfo(h, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, nullptr, nullptr,
                      nullptr, nullptr, &sd) != ERROR_SUCCESS) {
    return false;
  }
  const bool ok = SecurityDescriptorAcceptable(sd, ChainLevel::Private, ownerTrusted);
  LocalFree(sd);
  return ok;
}

HANDLE open_existing_no_follow(const std::wstring& path, DWORD access, DWORD share) {
  return CreateFileW(path.c_str(), access, share, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
}

}  // namespace

std::wstring ProductWorkerLogPath() {
  PWSTR programData = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &programData)) || !programData) return {};
  std::wstring p = programData;
  CoTaskMemFree(programData);
  return p + L"\\GNLink\\logs\\prelogin_worker.log";
}

bool ProcessIsLocalSystem() {
  HANDLE tok = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
  BYTE buf[256];
  DWORD n = 0;
  bool system = false;
  if (GetTokenInformation(tok, TokenUser, buf, sizeof(buf), &n)) {
    BYTE sys[SECURITY_MAX_SID_SIZE];
    DWORD sn = sizeof(sys);
    system = CreateWellKnownSid(WinLocalSystemSid, nullptr, sys, &sn) &&
             EqualSid(reinterpret_cast<TOKEN_USER*>(buf)->User.Sid, sys);
  }
  CloseHandle(tok);
  return system;
}

// ---- WorkerLogFile -------------------------------------------------------------------------------------------

WorkerLogFile::~WorkerLogFile() { Close(); }

void WorkerLogFile::Close() {
  if (file_ != INVALID_HANDLE_VALUE) {
    FlushFileBuffers(file_);
    CloseHandle(file_);
  }
  file_ = INVALID_HANDLE_VALUE;
  chain_.Release();
}

DWORD WorkerLogFile::Open(const std::wstring& path, const std::function<bool(PSID)>& ownerTrusted, uint64_t fileCap,
                          size_t lineCap, const wchar_t* fileSddl) {
  Close();
  failedAt.clear();
  broken_ = false;
  path_ = path;
  ownerTrusted_ = ownerTrusted;
  fileSddl_ = fileSddl ? fileSddl : kLogFileSddl;
  fileCap_ = fileCap;
  lineCap_ = lineCap < 64 ? 64 : lineCap;
  // The folders: ProgramData (identity, owner), GNLink (nobody else may change it), logs (SYSTEM+Administrators
  // only; created so when missing) -- held, without delete sharing, for as long as the log is open.
  const DWORD chain = HoldRecordChain(path_, /*create=*/true, ownerTrusted_, &chain_);
  if (chain != 0) {
    failedAt = chain_.failedAt.empty() ? path_ : chain_.failedAt;
    return chain;
  }
  return OpenCurrent();
}

DWORD WorkerLogFile::OpenCurrent() {
  // Held without write or delete sharing: no second writer, no rename or replacement underneath it. DELETE is
  // asked for so the rotation can rename it through this very handle.
  constexpr DWORD kAccess = GENERIC_READ | GENERIC_WRITE | READ_CONTROL | DELETE;
  HANDLE h = open_existing_no_follow(path_, kAccess, FILE_SHARE_READ);
  if (h != INVALID_HANDLE_VALUE) {
    if (!file_handle_acceptable(h, ownerTrusted_)) {  // checked BEFORE anything is written or truncated
      CloseHandle(h);
      failedAt = path_;
      return ERROR_ACCESS_DENIED;
    }
  } else {
    const DWORD e = GetLastError();
    if (e != ERROR_FILE_NOT_FOUND) {
      failedAt = path_;
      return e;
    }
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(fileSddl_.c_str(), SDDL_REVISION_1, &sd, nullptr)) {
      failedAt = path_;
      return GetLastError();
    }
    SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
    h = CreateFileW(path_.c_str(), kAccess, FILE_SHARE_READ, &sa, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                    nullptr);
    const DWORD ce = GetLastError();
    LocalFree(sd);
    if (h == INVALID_HANDLE_VALUE) {
      failedAt = path_;
      return ce;
    }
    if (!file_handle_acceptable(h, ownerTrusted_)) {
      CloseHandle(h);
      failedAt = path_;
      return ERROR_ACCESS_DENIED;
    }
  }
  LARGE_INTEGER size{};
  GetFileSizeEx(h, &size);
  LARGE_INTEGER zero{};
  SetFilePointerEx(h, zero, nullptr, FILE_END);
  file_ = h;
  size_ = static_cast<uint64_t>(size.QuadPart);
  if (size_ >= fileCap_ && !Rotate()) {
    broken_ = true;
    return ERROR_ACCESS_DENIED;
  }
  return 0;
}

bool WorkerLogFile::Rotate() {
  if (file_ == INVALID_HANDLE_VALUE) return false;
  const std::wstring older = path_ + L".1";
  // .1: deleted only after it passed the same check as the log itself -- never a link, a redirect or a file
  // somebody else owns or can write. Opened exclusively, so it cannot change between the check and the delete.
  HANDLE o = open_existing_no_follow(older, DELETE | READ_CONTROL | FILE_READ_ATTRIBUTES, 0);
  if (o != INVALID_HANDLE_VALUE) {
    if (!file_handle_acceptable(o, ownerTrusted_)) {
      CloseHandle(o);
      failedAt = older;
      return false;
    }
    FILE_DISPOSITION_INFO del{TRUE};
    const bool deleted = SetFileInformationByHandle(o, FileDispositionInfo, &del, sizeof(del)) != FALSE;
    CloseHandle(o);
    if (!deleted) {
      failedAt = older;
      return false;
    }
  } else if (GetLastError() != ERROR_FILE_NOT_FOUND) {
    failedAt = older;
    return false;
  }
  // The current file becomes .1 through the handle that was checked (not by name).
  std::vector<uint8_t> buf(sizeof(FILE_RENAME_INFO) + (older.size() + 1) * sizeof(wchar_t), 0);
  auto* ren = reinterpret_cast<FILE_RENAME_INFO*>(buf.data());
  ren->ReplaceIfExists = FALSE;
  ren->RootDirectory = nullptr;
  ren->FileNameLength = static_cast<DWORD>(older.size() * sizeof(wchar_t));
  std::memcpy(ren->FileName, older.c_str(), older.size() * sizeof(wchar_t));
  FlushFileBuffers(file_);
  if (!SetFileInformationByHandle(file_, FileRenameInfo, ren, static_cast<DWORD>(buf.size()))) {
    failedAt = older;
    return false;
  }
  CloseHandle(file_);
  file_ = INVALID_HANDLE_VALUE;
  size_ = 0;
  return OpenCurrent() == 0;
}

bool WorkerLogFile::WriteLine(const std::string& lineIn) {
  if (file_ == INVALID_HANDLE_VALUE || broken_) {
    ++dropped_;
    return false;
  }
  std::string line = lineIn;
  if (!line.empty() && line.back() == '\n') line.pop_back();
  if (!line.empty() && line.back() == '\r') line.pop_back();
  if (line.size() > lineCap_ - 1) {
    static const char kCut[] = " ...[cut]";
    line.resize(lineCap_ - 1 - (sizeof(kCut) - 1));
    line += kCut;
  }
  line += '\n';
  if (size_ + line.size() > fileCap_) {
    if (!Rotate()) {
      broken_ = true;  // stays within the cap: nothing more rather than a file that grows
      ++dropped_;
      return false;
    }
  }
  DWORD wrote = 0;
  if (!WriteFile(file_, line.data(), static_cast<DWORD>(line.size()), &wrote, nullptr) || wrote != line.size()) {
    ++dropped_;  // disk full, ...: counted and dropped, never retried in a loop
    if (wrote > 0) size_ += wrote;
    return false;
  }
  size_ += wrote;
  return true;
}

void WorkerLogFile::Flush() {
  if (file_ != INVALID_HANDLE_VALUE) FlushFileBuffers(file_);
}

// ---- WorkerLogSink -------------------------------------------------------------------------------------------

DWORD WorkerLogSink::Install(const std::wstring& path, const std::function<bool(PSID)>& ownerTrusted,
                             const std::string& header, const wchar_t* fileSddl) {
  if (installed_.load()) return 0;
  {
    std::lock_guard<std::mutex> lk(fileMu_);
    const DWORD e = file_.Open(path, ownerTrusted, kWorkerLogFileCap, kWorkerLogLineCap, fileSddl);
    if (e != 0) return e;
    file_.WriteLine(header);
    file_.Flush();
  }
  {
    const HANDLE original = GetStdHandle(STD_OUTPUT_HANDLE);
    if (original && original != INVALID_HANDLE_VALUE && GetFileType(original) != FILE_TYPE_UNKNOWN) {
      DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(), &forward_, 0, FALSE, DUPLICATE_SAME_ACCESS);
    }
  }
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, FALSE};
  if (!CreatePipe(&readEnd_, &writeEnd_, &sa, 64 * 1024)) return GetLastError();
  drained_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  // fd 1 and fd 2 (printf, fprintf(stderr), std::cout / std::cerr through the CRT) onto the pipe. A worker
  // started without std handles has no valid stdout stream: it is opened on NUL first, then re-pointed.
  for (FILE* stream : {stdout, stderr}) {
    HANDLE dup = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), writeEnd_, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
      return GetLastError();
    }
    const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(dup), _O_BINARY);
    if (fd < 0) {
      CloseHandle(dup);
      return ERROR_INVALID_HANDLE;
    }
    FILE* reopened = nullptr;
    if (_fileno(stream) < 0) freopen_s(&reopened, "NUL", "wb", stream);
    if (_dup2(fd, _fileno(stream)) != 0) {
      _close(fd);
      return ERROR_INVALID_HANDLE;
    }
    _close(fd);
    setvbuf(stream, nullptr, _IONBF, 0);  // a line printed is a line in the pipe -- also before a TerminateProcess
  }
  SetStdHandle(STD_OUTPUT_HANDLE, writeEnd_);
  SetStdHandle(STD_ERROR_HANDLE, writeEnd_);
  std::cout.clear();
  std::cerr.clear();
  collector_ = std::thread([this] { Collect(); });
  collector_.detach();  // lives as long as the process; the object is never destroyed (TheWorkerLogSink)
  installed_ = true;
  return 0;
}

void WorkerLogSink::Collect() {
  std::string pending;
  char buf[4096];
  for (;;) {
    DWORD got = 0;
    if (!ReadFile(readEnd_, buf, sizeof(buf), &got, nullptr) || got == 0) {
      // The write end is never closed while the process runs: a failure here ends the collector, and every later
      // line is lost (not waited on).
      return;
    }
    pending.append(buf, got);
    size_t nl;
    while ((nl = pending.find('\n')) != std::string::npos) {
      const std::string line = pending.substr(0, nl);
      pending.erase(0, nl + 1);
      if (line.rfind(kDrainMarker, 0) == 0) {
        const uint64_t n = std::strtoull(line.c_str() + sizeof(kDrainMarker) - 1, nullptr, 10);
        {
          std::lock_guard<std::mutex> lk(fileMu_);
          file_.Flush();
        }
        drainDone_.store(n);
        SetEvent(drained_);
        continue;
      }
      {
        std::lock_guard<std::mutex> lk(fileMu_);
        file_.WriteLine(line);
      }
      if (forward_) {
        DWORD w = 0;
        const std::string out = line + "\n";
        WriteFile(forward_, out.data(), static_cast<DWORD>(out.size()), &w, nullptr);
      }
    }
    if (pending.size() > kWorkerLogLineCap * 4) {  // no newline for a long while: write what there is as a line
      std::lock_guard<std::mutex> lk(fileMu_);
      file_.WriteLine(pending);
      pending.clear();
    }
  }
}

bool WorkerLogSink::Drain(uint32_t timeoutMs) {
  if (!installed_.load()) return false;
  std::fflush(stdout);
  std::fflush(stderr);
  std::cout.flush();
  std::cerr.flush();
  const uint64_t n = ++drainAsked_;
  const std::string marker = std::string(kDrainMarker) + std::to_string(n) + "\n";
  DWORD wrote = 0;
  if (!WriteFile(writeEnd_, marker.data(), static_cast<DWORD>(marker.size()), &wrote, nullptr)) return false;
  const ULONGLONG deadline = GetTickCount64() + timeoutMs;
  while (drainDone_.load() < n) {
    const ULONGLONG now = GetTickCount64();
    if (now >= deadline) return false;
    const ULONGLONG left = deadline - now;
    WaitForSingleObject(drained_, static_cast<DWORD>(left < 50 ? left : 50));  // several drains may share the event
  }
  return true;
}

WorkerLogSink& TheWorkerLogSink() {
  static WorkerLogSink* sink = new WorkerLogSink();  // never destroyed: the collector may run until the process ends
  return *sink;
}

void DrainWorkerLogBeforeExit() {
  WorkerLogSink& s = TheWorkerLogSink();
  if (s.installed()) (void)s.Drain(2000);
}

}  // namespace remote60::native_poc::prelogin
