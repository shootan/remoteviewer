// See file_copy_local_files.hpp.

#include "file_copy_local_files.hpp"

#include <algorithm>

namespace remote60::native_poc::file_copy {

namespace {

bool starts_with(const std::wstring& s, const wchar_t* prefix) {
  const size_t n = wcslen(prefix);
  return s.size() >= n && _wcsnicmp(s.c_str(), prefix, n) == 0;
}

bool has_extension(const std::wstring& path, const wchar_t* ext) {
  const size_t n = wcslen(ext);
  if (path.size() < n) return false;
  return _wcsicmp(path.c_str() + path.size() - n, ext) == 0;
}

uint64_t default_now_ms() { return GetTickCount64(); }

uint64_t filetime_u64(const FILETIME& ft) {
  return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

Status status_from_open_error(DWORD error) {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_NET_NAME:
      return Status::NotFound;
    case ERROR_ACCESS_DENIED:
      return Status::AccessDenied;
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
      return Status::SharingViolation;
    default:
      return Status::ReadError;
  }
}

// Identity and shape of an OPEN file. Fills `entry` and says whether it is a plain disk file.
Status describe_open_file(HANDLE h, FileId* id, uint64_t* size, uint64_t* mtime, uint32_t* attributes) {
  if (GetFileType(h) != FILE_TYPE_DISK) return Status::BadPath;
  FILE_BASIC_INFO basic{};
  if (!GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof(basic))) return Status::ReadError;
  if (basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) return Status::NotAFile;
  if (basic.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return Status::Excluded;
  FILE_STANDARD_INFO standard{};
  if (!GetFileInformationByHandleEx(h, FileStandardInfo, &standard, sizeof(standard))) return Status::ReadError;
  if (standard.Directory) return Status::NotAFile;
  FILE_ID_INFO info{};
  if (!GetFileInformationByHandleEx(h, FileIdInfo, &info, sizeof(info))) return Status::ReadError;
  id->volumeSerial = info.VolumeSerialNumber;
  static_assert(sizeof(info.FileId.Identifier) == 16, "FILE_ID_128 is 16 bytes");
  std::copy(std::begin(info.FileId.Identifier), std::end(info.FileId.Identifier), id->id.begin());
  *size = static_cast<uint64_t>(standard.EndOfFile.QuadPart);
  *mtime = static_cast<uint64_t>(basic.LastWriteTime.QuadPart);
  *attributes = basic.FileAttributes;
  return Status::Ok;
}

}  // namespace

Status classify_source_path(const std::wstring& path) {
  // Explicit local absolute paths only: "X:\..." with backslashes. That refuses UNC, relative and
  // drive-relative forms, the "\\?\" / "\\.\" / "\??\" object-manager prefixes and forward slashes
  // in one rule -- the shell puts CF_HDROP paths on the clipboard in exactly this form. (r3 ④)
  if (path.size() < 4) return Status::BadPath;
  const wchar_t d = path[0];
  if (!((d >= L'A' && d <= L'Z') || (d >= L'a' && d <= L'z')) || path[1] != L':' || path[2] != L'\\') return Status::BadPath;
  for (size_t i = 0; i < path.size(); ++i) {
    const wchar_t c = path[i];
    // A colon anywhere but as the drive separator names an alternate data stream or a device.
    if (c == L'/' || c < 0x20 || (c == L':' && i != 1)) return Status::BadPath;
  }
  // No "." / ".." components, no empty component (a doubled or trailing backslash).
  size_t start = 3;
  for (size_t i = 3; i <= path.size(); ++i) {
    if (i == path.size() || path[i] == L'\\') {
      const std::wstring component = path.substr(start, i - start);
      if (component.empty() || component == L"." || component == L"..") return Status::BadPath;
      start = i + 1;
    }
  }
  if (has_extension(path, L".lnk") || has_extension(path, L".url")) return Status::Excluded;
  return Status::Ok;
}

Status open_source_file(const std::wstring& path, DWORD access, DWORD share, HANDLE* out) {
  *out = INVALID_HANDLE_VALUE;
  const Status c = classify_source_path(path);
  if (c != Status::Ok) return c;
  // The link itself, not what it points at: a reparse point as the final component is Excluded
  // before anything is opened. GetFileAttributes does not follow. A directory is not a file.
  const DWORD attrs = GetFileAttributesW(path.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return status_from_open_error(GetLastError());
  if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) return Status::Excluded;
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) return Status::NotAFile;
  // FILE_FLAG_OPEN_REPARSE_POINT: if the final component became a reparse point since the check
  // above, the handle is the link itself (describe_open_file reports it Excluded), never the target.
  HANDLE h = CreateFileW(path.c_str(), access, share, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (h == INVALID_HANDLE_VALUE) return status_from_open_error(GetLastError());
  // Where the handle REALLY is. A junction or symlinked directory anywhere above the file, a
  // substituted or mapped drive, a share: the resolved path differs from the one asked for, or is
  // not a local drive letter at all. The handle, not the text, is the authority. (r3 ④)
  std::wstring finalPath(32768, L'\0');
  const DWORD n = GetFinalPathNameByHandleW(h, finalPath.data(), static_cast<DWORD>(finalPath.size()),
                                            FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (n == 0 || n >= finalPath.size()) {
    CloseHandle(h);
    return Status::NotLocal;  // no DOS name: an unlettered volume, or nothing this feature copies
  }
  finalPath.resize(n);
  if (starts_with(finalPath, L"\\\\?\\")) finalPath.erase(0, 4);
  if (starts_with(finalPath, L"UNC\\") || classify_source_path(finalPath) == Status::BadPath) {
    CloseHandle(h);
    return Status::NotLocal;
  }
  if (_wcsicmp(finalPath.c_str(), path.c_str()) != 0) {
    CloseHandle(h);
    return Status::PathThroughLink;
  }
  *out = h;
  return Status::Ok;
}

std::u16string basename_of(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  const std::wstring base = slash == std::wstring::npos ? path : path.substr(slash + 1);
  return std::u16string(base.begin(), base.end());
}

StatEntry stat_source_file(const std::wstring& path) {
  StatEntry entry;
  HANDLE h = INVALID_HANDLE_VALUE;
  entry.status = open_source_file(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &h);
  if (entry.status != Status::Ok) return entry;
  entry.status = describe_open_file(h, &entry.id, &entry.size, &entry.mtime, &entry.attributes);
  CloseHandle(h);
  if (entry.status == Status::Ok) {
    entry.name = basename_of(path);
    if (!validate_remote_name(entry.name)) {
      entry.status = Status::Refused;
      entry.name.clear();
    }
  } else {
    entry = StatEntry{entry.status};
  }
  return entry;
}

LocalFileTable::LocalFileTable(NowMs now) : now_(now ? std::move(now) : NowMs(default_now_ms)) {}

LocalFileTable::~LocalFileTable() { ReleaseAll(); }

uint32_t LocalFileTable::release(PinSet& set) {
  uint32_t n = 0;
  for (auto& f : set.files) {
    if (f.handle != INVALID_HANDLE_VALUE) {
      CloseHandle(f.handle);
      f.handle = INVALID_HANDLE_VALUE;
      ++n;
    }
  }
  return n;
}

bool LocalFileTable::Pin(uint64_t pinId, uint32_t leaseMs, const std::vector<PinRequestEntry>& entries,
                         std::vector<PinResultEntry>* results) {
  results->clear();
  if (entries.size() > kMaxFiles) return false;
  Unpin(pinId);
  PinSet set;
  set.leaseMs = leaseMs;
  set.deadlineMs = now_() + leaseMs;
  for (const auto& e : entries) {
    PinResultEntry r;
    PinnedFile pf;
    const std::wstring path(e.path.begin(), e.path.end());
    {
      // FILE_SHARE_READ alone: another reader is fine, a writer (or a delete / rename) is refused
      // now, and none can arrive while this handle is held. No privileged retry, ever. The same
      // path policy as Stat, applied to the handle actually opened (r3 ④).
      HANDLE h = INVALID_HANDLE_VALUE;
      r.status = open_source_file(path, GENERIC_READ, FILE_SHARE_READ, &h);
      if (r.status == Status::Ok) {
        uint32_t attributes = 0;
        r.status = describe_open_file(h, &r.id, &r.size, &r.mtime, &attributes);
        if (r.status == Status::Ok && r.id != e.expectedId) {
          r.status = Status::Replaced;  // not the file the offer named
        } else if (r.status == Status::Ok && ((e.expectedSize != 0 && e.expectedSize != r.size) ||
                                              (e.expectedMtime != 0 && e.expectedMtime != r.mtime))) {
          r.status = Status::Changed;  // the same file, rewritten since the offer
        }
        if (r.status == Status::Ok) {
          pf.handle = h;
          pf.size = r.size;
        } else {
          CloseHandle(h);
        }
      }
    }
    if (r.status != Status::Ok) {
      r.id = FileId{};
      r.size = 0;
      r.mtime = 0;
    }
    results->push_back(r);
    set.files.push_back(pf);
  }
  // A pin that holds nothing is not registered: there is nothing to read from and nothing to
  // release, and Unpin of it answers 0.
  bool held = false;
  for (const auto& f : set.files) held = held || f.handle != INVALID_HANDLE_VALUE;
  if (held) pins_[pinId] = std::move(set);
  return true;
}

Status LocalFileTable::Read(uint64_t pinId, uint32_t fileIndex, uint64_t offset, uint32_t length,
                            std::vector<uint8_t>* out) {
  out->clear();
  auto it = pins_.find(pinId);
  if (it == pins_.end()) return Status::UnknownId;
  PinSet& set = it->second;
  const uint64_t now = now_();
  if (now > set.deadlineMs) {
    release(set);
    pins_.erase(it);
    return Status::LeaseExpired;
  }
  if (length > kMaxChunkBytes || fileIndex >= set.files.size()) return Status::BadRequest;
  PinnedFile& f = set.files[fileIndex];
  if (f.handle == INVALID_HANDLE_VALUE) return Status::UnknownId;  // an entry Pin refused
  if (offset > f.size) return Status::BadRequest;
  set.deadlineMs = now + set.leaseMs;
  const uint64_t avail = f.size - offset;
  const DWORD want = static_cast<DWORD>(std::min<uint64_t>(length, avail));
  out->resize(want);
  if (want == 0) return Status::Ok;
  OVERLAPPED ov{};
  ov.Offset = static_cast<DWORD>(offset);
  ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
  DWORD got = 0;
  if (!ReadFile(f.handle, out->data(), want, &got, &ov)) {
    if (GetLastError() != ERROR_HANDLE_EOF) {
      out->clear();
      return Status::ReadError;
    }
    got = 0;
  }
  out->resize(got);
  return Status::Ok;
}

uint32_t LocalFileTable::Unpin(uint64_t pinId) {
  auto it = pins_.find(pinId);
  if (it == pins_.end()) return 0;
  const uint32_t n = release(it->second);
  pins_.erase(it);
  return n;
}

uint32_t LocalFileTable::ExpireLeases() {
  const uint64_t now = now_();
  uint32_t released = 0;
  for (auto it = pins_.begin(); it != pins_.end();) {
    if (now > it->second.deadlineMs) {
      released += release(it->second);
      it = pins_.erase(it);
    } else {
      ++it;
    }
  }
  return released;
}

void LocalFileTable::ReleaseAll() {
  for (auto& kv : pins_) release(kv.second);
  pins_.clear();
}

size_t LocalFileTable::pinned_handles() const {
  size_t n = 0;
  for (const auto& kv : pins_) {
    for (const auto& f : kv.second.files) {
      if (f.handle != INVALID_HANDLE_VALUE) ++n;
    }
  }
  return n;
}

}  // namespace remote60::native_poc::file_copy
