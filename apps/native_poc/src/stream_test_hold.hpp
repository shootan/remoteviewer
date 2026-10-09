#pragma once

// TEST BUILDS ONLY (REMOTE60_STREAM_TEST_SEAM, i.e. GNLinkStreamMigrationTest; never GNLinkStream):
// named stop points an e2e can hold the host at, to put an event (a disconnect, a new session, a
// screen unplugged) exactly between two steps of a monitor selection (t-970r4zgo r3). A point is
// held while the file <GNLINK_STREAM_TEST_HOLD_DIR>\<name> exists; without the variable nothing
// is ever held. Product builds do not include this header's body.

#ifdef REMOTE60_STREAM_TEST_SEAM

#include <windows.h>

#include <atomic>
#include <iostream>
#include <string>

namespace remote60::native_poc {

// name: plain ASCII.
inline bool stream_test_hold_set(const char* name) {
  wchar_t dir[MAX_PATH] = {};
  const DWORD n = GetEnvironmentVariableW(L"GNLINK_STREAM_TEST_HOLD_DIR", dir, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return false;
  std::wstring path = std::wstring(dir) + L"\\";
  for (const char* p = name; *p; ++p) path.push_back(static_cast<wchar_t>(*p));
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Blocks the calling thread while the point is held (bounded by stop).
inline void stream_test_hold_wait(const char* name, const std::atomic<bool>& stop) {
  if (!stream_test_hold_set(name)) return;
  std::cout << "[stream-test-seam] holding at " << name << "\n" << std::flush;
  while (!stop.load() && stream_test_hold_set(name)) Sleep(20);
  std::cout << "[stream-test-seam] released " << name << "\n" << std::flush;
}

}  // namespace remote60::native_poc

#endif
