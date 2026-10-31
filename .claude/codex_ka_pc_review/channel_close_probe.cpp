#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <cstdio>

// Review-only scheduler seam: stop the actual header's writer immediately before its first
// synchronous WriteFile. CancelSynchronousIo cannot cancel an I/O which has not begun.
static HANDLE beforeWrite, allowWrite;
static std::atomic<bool> firstWrite{true}, writeEntered{false};
static std::atomic<DWORD> firstCancelError{0};
static std::atomic<int> cancels{0};
static BOOL review_write(HANDLE h, LPCVOID p, DWORD n, LPDWORD put, LPOVERLAPPED ov) {
  if (firstWrite.exchange(false)) {
    SetEvent(beforeWrite);
    if (WaitForSingleObject(allowWrite, 5000) != WAIT_OBJECT_0) return FALSE;
  }
  writeEntered.store(true);
  return WriteFile(h, p, n, put, ov);
}
static BOOL review_cancel(HANDLE h) {
  BOOL ok = CancelSynchronousIo(h);
  DWORD err = ok ? ERROR_SUCCESS : GetLastError();
  if (cancels.fetch_add(1) == 0) {
    firstCancelError.store(err);
    SetEvent(allowWrite);
  }
  SetLastError(err);
  return ok;
}
#define WriteFile review_write
#define CancelSynchronousIo review_cancel
#include "C:/work/gnlink-wt/ka-pc/apps/native_poc/src/managed_viewer_ipc.hpp"
#undef WriteFile
#undef CancelSynchronousIo

int main() {
  HANDLE inRead = nullptr, peerWrite = nullptr, peerRead = nullptr, outWrite = nullptr;
  beforeWrite = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  allowWrite = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!CreatePipe(&inRead, &peerWrite, nullptr, 4096) ||
      !CreatePipe(&peerRead, &outWrite, nullptr, 4096)) return 2;
  using namespace remote60::native_poc::managed_ipc;
  Channel c;
  c.Start(inRead, outWrite);
  Message m;
  m.type = Type::State;
  m.fields = {{"payload", std::string(12000, 'x')}};
  if (!c.Send(m) || WaitForSingleObject(beforeWrite, 2000) != WAIT_OBJECT_0) ExitProcess(3);
  std::atomic<bool> closed{false};
  std::thread closer([&] { c.Close(); closed.store(true); });
  const ULONGLONG t0 = GetTickCount64();
  while (GetTickCount64() - t0 < 1800 && !closed.load()) Sleep(10);
  const bool reproduced = !closed.load() && writeEntered.load() && firstCancelError.load() == ERROR_NOT_FOUND;
  std::printf("REVIEW close_done=%d write_entered=%d cancel_error=%lu elapsed_ms=%llu\n",
              closed.load() ? 1 : 0, writeEntered.load() ? 1 : 0,
              static_cast<unsigned long>(firstCancelError.load()), GetTickCount64() - t0);
  // Own pipe handles only: breaking the peer wakes the otherwise unbounded join.
  CloseHandle(peerRead);
  CloseHandle(peerWrite);
  if (WaitForSingleObject(closer.native_handle(), 2000) != WAIT_OBJECT_0) ExitProcess(4);
  closer.join();
  CloseHandle(beforeWrite);
  CloseHandle(allowWrite);
  std::printf("REVIEW recovered_after_peer_close=%d\n", closed.load() ? 1 : 0);
  return reproduced ? 0 : 1;
}
