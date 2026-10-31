// ka-pc-impl r2 (Codex N5): Channel::Close is bounded in the orderings a single CancelSynchronousIo misses.
//
// The REAL header (managed_viewer_ipc.hpp) over real anonymous pipes, with the real WriteFile / ReadFile /
// CancelSynchronousIo -- each wrapped only to hold a thread at one chosen point, so an ordering that the scheduler
// produces rarely is produced every run:
//   1. the writer held just BEFORE its first WriteFile until Close's first cancel has run and missed (nothing was
//      pending: ERROR_NOT_FOUND) -- then it writes into a pipe nobody reads. (Codex's probe ordering, close_done=0.)
//   2. the same for the reader: held before a ReadFile until the first cancel has missed.
//   3. a write already blocked when Close starts (a peer that never reads).
//   4. a peer that reads half a frame and stops: Close still ends; the peer's side breaks on the half frame.
//   5. the normal drain: a peer that reads gets the last queued message whole before Close returns.
// Each case: Close must return within kBoundMs, and the held point must actually have been reached (a hold that
// never fired proves nothing). No files, no other process, no window.
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>

namespace race {
HANDLE gWriteHeld = nullptr, gWriteGo = nullptr, gReadHeld = nullptr, gReadGo = nullptr;
std::atomic<bool> gHoldWrite{false}, gHoldRead{false};
std::atomic<int> gCancels{0}, gCancelMisses{0};
std::atomic<bool> gWriteEnteredAfterMiss{false}, gReadEnteredAfterMiss{false};

BOOL write_file(HANDLE h, LPCVOID p, DWORD n, LPDWORD put, LPOVERLAPPED ov) {
  if (gHoldWrite.exchange(false)) {
    SetEvent(gWriteHeld);
    WaitForSingleObject(gWriteGo, 5000);
    gWriteEnteredAfterMiss.store(gCancelMisses.load() > 0);
  }
  return WriteFile(h, p, n, put, ov);
}
BOOL read_file(HANDLE h, LPVOID p, DWORD n, LPDWORD got, LPOVERLAPPED ov) {
  if (gHoldRead.exchange(false)) {
    SetEvent(gReadHeld);
    WaitForSingleObject(gReadGo, 5000);
    gReadEnteredAfterMiss.store(gCancelMisses.load() > 0);
  }
  return ReadFile(h, p, n, got, ov);
}
BOOL cancel_io(HANDLE t) {
  const BOOL ok = CancelSynchronousIo(t);
  const DWORD err = ok ? ERROR_SUCCESS : GetLastError();
  gCancels.fetch_add(1);
  if (!ok && err == ERROR_NOT_FOUND) {
    gCancelMisses.fetch_add(1);
    SetEvent(gWriteGo);  // a held thread goes on only after a cancel has MISSED it
    SetEvent(gReadGo);
  }
  SetLastError(err);
  return ok;
}
}  // namespace race

#define WriteFile race::write_file
#define ReadFile race::read_file
#define CancelSynchronousIo race::cancel_io
#include "managed_viewer_ipc.hpp"
#undef WriteFile
#undef ReadFile
#undef CancelSynchronousIo

namespace {
int gFailures = 0;
void check(const char* name, bool ok, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ", detail.c_str());
  if (!ok) ++gFailures;
}
using namespace remote60::native_poc::managed_ipc;
constexpr ULONGLONG kBoundMs = 3000;

void reset() {
  for (HANDLE h : {race::gWriteHeld, race::gWriteGo, race::gReadHeld, race::gReadGo}) ResetEvent(h);
  race::gHoldWrite = false;
  race::gHoldRead = false;
  race::gCancels = 0;
  race::gCancelMisses = 0;
  race::gWriteEnteredAfterMiss = false;
  race::gReadEnteredAfterMiss = false;
}

// Close on its own thread; true if it returned within kBoundMs. On a timeout the test's own peer handles are
// closed to let it end (so a failing run still finishes), and false is returned.
bool close_bounded(Channel& c, ULONGLONG* ms, HANDLE peerA, HANDLE peerB) {
  std::atomic<bool> done{false};
  const ULONGLONG t0 = GetTickCount64();
  std::thread closer([&] { c.Close(); done.store(true); });
  while (!done.load() && GetTickCount64() - t0 < kBoundMs) Sleep(5);
  *ms = GetTickCount64() - t0;
  const bool bounded = done.load();
  if (!bounded) {
    if (peerA) CloseHandle(peerA);
    if (peerB) CloseHandle(peerB);
  }
  closer.join();
  return bounded;
}

Message big(size_t n) {
  Message m;
  m.type = Type::State;
  m.fields = {{"x", std::string(n, 'a')}};
  return m;
}
}  // namespace

int main() {
  race::gWriteHeld = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  race::gWriteGo = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  race::gReadHeld = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  race::gReadGo = CreateEventW(nullptr, TRUE, FALSE, nullptr);

  {  // 1. the writer held before its first WriteFile; the first cancel misses; then it writes to a non-reader
    reset();
    HANDLE inR, peerW, peerR, outW;
    CreatePipe(&inR, &peerW, nullptr, 4096);
    CreatePipe(&peerR, &outW, nullptr, 4096);
    race::gHoldWrite = true;
    Channel c;
    c.Start(inR, outW);
    c.Send(big(12000));
    const bool held = WaitForSingleObject(race::gWriteHeld, 2000) == WAIT_OBJECT_0;
    ULONGLONG ms = 0;
    const bool bounded = close_bounded(c, &ms, peerR, peerW);
    check("1. writer held before WriteFile, first cancel missed: the hold fired and the cancel missed",
          held && race::gCancelMisses.load() > 0 && race::gWriteEnteredAfterMiss.load(),
          "misses=" + std::to_string(race::gCancelMisses.load()));
    check("1. ...Close still returns in bounded time (no peer close needed)", bounded,
          std::to_string(ms) + " ms, cancels=" + std::to_string(race::gCancels.load()));
    if (bounded) { CloseHandle(peerR); CloseHandle(peerW); }
  }
  {  // 2. the reader held before a ReadFile; the first cancel on it misses; nothing ever arrives
    reset();
    HANDLE inR, peerW, peerR, outW;
    CreatePipe(&inR, &peerW, nullptr, 4096);
    CreatePipe(&peerR, &outW, nullptr, 4096);
    race::gHoldRead = true;
    Channel c;
    c.Start(inR, outW);
    const bool held = WaitForSingleObject(race::gReadHeld, 2000) == WAIT_OBJECT_0;
    ULONGLONG ms = 0;
    const bool bounded = close_bounded(c, &ms, peerR, peerW);
    check("2. reader held before ReadFile, first cancel missed: the hold fired and the cancel missed",
          held && race::gCancelMisses.load() > 0 && race::gReadEnteredAfterMiss.load(),
          "misses=" + std::to_string(race::gCancelMisses.load()));
    check("2. ...Close still returns in bounded time (the peer's write end left open)", bounded, std::to_string(ms) + " ms");
    if (bounded) { CloseHandle(peerR); CloseHandle(peerW); }
  }
  {  // 3. a write already blocked when Close starts
    reset();
    HANDLE inR, peerW, peerR, outW;
    CreatePipe(&inR, &peerW, nullptr, 4096);
    CreatePipe(&peerR, &outW, nullptr, 4096);
    Channel c;
    c.Start(inR, outW);
    for (int i = 0; i < 8; ++i) c.Send(big(12000));
    Sleep(200);
    ULONGLONG ms = 0;
    const bool bounded = close_bounded(c, &ms, peerR, peerW);
    check("3. a write already blocked (a peer that never reads): Close bounded", bounded, std::to_string(ms) + " ms");
    if (bounded) { CloseHandle(peerR); CloseHandle(peerW); }
  }
  {  // 4. the peer reads half a frame, then stops reading
    reset();
    HANDLE inR, peerW, peerR, outW;
    CreatePipe(&inR, &peerW, nullptr, 4096);
    CreatePipe(&peerR, &outW, nullptr, 4096);
    Channel c;
    c.Start(inR, outW);
    c.Send(big(12000));
    std::string half(6000, '\0');
    DWORD got = 0, total = 0;
    while (total < half.size() && ReadFile(peerR, &half[total], static_cast<DWORD>(half.size() - total), &got, nullptr) && got)
      total += got;
    ULONGLONG ms = 0;
    const bool bounded = close_bounded(c, &ms, nullptr, nullptr);
    check("4. a peer that read half a frame and stopped: Close bounded", bounded, std::to_string(ms) + " ms");
    // what the peer has now is a frame cut short, then EOF: a channel on that side refuses it, delivering nothing
    Channel peer;
    peer.Start(peerR, peerW);
    Message m;
    const bool nothing = !peer.Receive(&m, 1500);
    check("4. ...the peer sees the cut frame then EOF: its channel ends, delivering nothing", nothing && !peer.open(), peer.why());
    peer.Close();
  }
  {  // 5. the normal drain: the peer reads, the last message arrives whole before Close returns
    reset();
    HANDLE inR, peerW, peerR, outW;
    CreatePipe(&inR, &peerW, nullptr, 1 << 16);
    CreatePipe(&peerR, &outW, nullptr, 1 << 16);
    Channel c, peer;
    c.Start(inR, outW);
    peer.Start(peerR, peerW);
    Message bye;
    bye.type = Type::Bye;
    bye.fields = {{"reason", "release"}};
    c.Send(big(9000));
    c.Send(bye);
    ULONGLONG ms = 0;
    const bool bounded = close_bounded(c, &ms, nullptr, nullptr);
    Message a, b2;
    const bool first = peer.Receive(&a, 2000) && a.type == Type::State && a.get("x").size() == 9000;
    const bool last = peer.Receive(&b2, 2000) && b2.type == Type::Bye && b2.get("reason") == "release";
    check("5. normal drain: Close returns at once", bounded && ms < 1000, std::to_string(ms) + " ms");
    check("5. ...and both queued messages arrived whole, the last one (Bye) included", first && last);
    peer.Close();
  }
  std::printf("\nka_pc_ipc_close_race_test: %s\n", gFailures ? "FAILED" : "ALL PASS");
  return gFailures ? 1 : 0;
}
