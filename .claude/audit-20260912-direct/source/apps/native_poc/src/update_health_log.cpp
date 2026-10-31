#include "update_health_log.hpp"

#include <windows.h>

namespace remote60::native_poc::update {

uint64_t file_size_or_zero(const std::wstring& path) {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
  return (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

std::string read_from_offset(const std::wstring& path, uint64_t offset, uint64_t maxBytes) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return {};

  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || static_cast<uint64_t>(size.QuadPart) <= offset) {
    // Nothing new. Includes the case where the file SHRANK below the mark, which is what a log
    // rotation looks like -- and a rotated log's remaining bytes are not this run's evidence.
    CloseHandle(file);
    return {};
  }
  LARGE_INTEGER at{};
  at.QuadPart = static_cast<LONGLONG>(offset);
  if (!SetFilePointerEx(file, at, nullptr, FILE_BEGIN)) {
    CloseHandle(file);
    return {};
  }

  const uint64_t remaining = static_cast<uint64_t>(size.QuadPart) - offset;
  const DWORD want = static_cast<DWORD>(remaining < maxBytes ? remaining : maxBytes);
  std::string buffer(want, '\0');
  DWORD read = 0;
  const bool ok = ReadFile(file, buffer.data(), want, &read, nullptr) != FALSE;
  CloseHandle(file);
  if (!ok) return {};
  buffer.resize(read);
  return buffer;
}

}  // namespace remote60::native_poc::update
