// The frame reader against the race it was rewritten for. (file-copy-helper r2)
//
// The verifier saw the r1 e2e lose PasteBegin / PasteEnd frames intermittently under load. The
// cause is in the receive path, not in the paste logic: a byte-mode pipe read completes as soon as
// any bytes arrive, so when a frame landed between a poll's wait timing out and CancelIoEx, the
// first version dropped the bytes that completed read had moved and the stream was left mid-frame.
// PasteBegin's payload is exactly 16 bytes -- one header -- so the loss even realigned itself, and
// a later PasteEnd on the same pipe decoded fine: precisely the shape in the verifier's run 2.
//
// This test makes that window wide and hits it thousands of times: frames written every 0-2 ms,
// polled with 1 ms timeouts. The r2 FrameReader must receive every frame, in order, intact. The
// pre-r2 algorithm is kept HERE (legacy_receive_frame -- not product code) and run on the same
// traffic so the loss is measured, not assumed. A chunked 200 KiB frame arriving across several
// timed-out polls must also come out whole.
//
// Build: remote60_file_copy_pipe_io_test (CMake). Tags: pure-logic + pipe (loopback, in process).

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "file_copy_helper_host.hpp"
#include "file_copy_pipe_io.hpp"

using namespace remote60::native_poc::file_copy;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}

struct PipePair {
  HANDLE server = INVALID_HANDLE_VALUE;
  HANDLE client = INVALID_HANDLE_VALUE;
  bool Open(const wchar_t* name) {
    server = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 64 * 1024,
                              64 * 1024, 0, nullptr);
    if (server == INVALID_HANDLE_VALUE) return false;
    client = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (client == INVALID_HANDLE_VALUE) return false;
    OVERLAPPED io{};
    io.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    const BOOL c = ConnectNamedPipe(server, &io);
    const DWORD e = c ? ERROR_SUCCESS : GetLastError();
    bool ok = c || e == ERROR_PIPE_CONNECTED;
    if (!ok && e == ERROR_IO_PENDING) {
      DWORD n = 0;
      ok = WaitForSingleObject(io.hEvent, 5000) == WAIT_OBJECT_0 && GetOverlappedResult(server, &io, &n, FALSE);
    }
    CloseHandle(io.hEvent);
    return ok;
  }
  ~PipePair() {
    if (client != INVALID_HANDLE_VALUE) CloseHandle(client);
    if (server != INVALID_HANDLE_VALUE) CloseHandle(server);
  }
};

// ---------------------------------------------------------- the pre-r2 receive path, verbatim
// (61092c0 file_copy_pipe_io.hpp). Kept only to measure what it loses on the same traffic.
namespace legacy {
template <bool kWrite>
bool pipe_transfer_exact(HANDLE pipe, void* data, DWORD size, DWORD timeoutMs, HANDLE abort) {
  auto* cursor = static_cast<uint8_t*>(data);
  DWORD done = 0;
  const ULONGLONG deadline = GetTickCount64() + timeoutMs;
  while (done < size) {
    OVERLAPPED io{};
    io.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!io.hEvent) return false;
    DWORD moved = 0;
    BOOL started;
    if (kWrite) started = WriteFile(pipe, cursor + done, size - done, &moved, &io);
    else started = ReadFile(pipe, cursor + done, size - done, &moved, &io);
    const DWORD error = started ? ERROR_SUCCESS : GetLastError();
    bool ok = started != FALSE;
    if (!started && error == ERROR_IO_PENDING) {
      const ULONGLONG now = GetTickCount64();
      const DWORD remaining = now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
      HANDLE waits[2] = {io.hEvent, abort};
      const DWORD count = abort ? 2 : 1;
      const DWORD w = WaitForMultipleObjects(count, waits, FALSE, remaining);
      if (w == WAIT_OBJECT_0) {
        ok = GetOverlappedResult(pipe, &io, &moved, FALSE) != FALSE;
      } else {
        CancelIoEx(pipe, &io);
        WaitForSingleObject(io.hEvent, 2000);
        ok = false;
        CloseHandle(io.hEvent);
        return false;  // <-- the bytes a just-completed read moved are dropped here
      }
    }
    CloseHandle(io.hEvent);
    if (!ok || moved == 0) return false;
    done += moved;
  }
  return true;
}
bool receive_frame(HANDLE pipe, PipeFrame* frame, DWORD timeoutMs) {
  uint8_t header[kFrameHeaderBytes];
  if (!pipe_transfer_exact<false>(pipe, header, sizeof(header), timeoutMs, nullptr)) return false;
  uint32_t length = 0;
  if (!decode_frame_header(header, &frame->type, &length)) return false;
  frame->payload.resize(length);
  if (length == 0) return true;
  return pipe_transfer_exact<false>(pipe, frame->payload.data(), length, 5000, nullptr);
}
}  // namespace legacy

struct Traffic {
  int sent = 0;
  int received = 0;
  int outOfOrder = 0;
  int corrupt = 0;
  int garbled = 0;  // receive calls that failed with ERROR_INVALID_DATA
};

// Writer: `count` PasteBegin frames (offerId = sequence) at random 0-2 ms gaps.
void write_bursts(HANDLE pipe, int count, std::atomic<bool>* done) {
  std::mt19937 rng(12345);
  std::uniform_int_distribution<int> gap(0, 2);
  for (int i = 1; i <= count; ++i) {
    PasteBegin m;
    m.offerId = static_cast<uint64_t>(i);
    m.pasteOp = static_cast<uint64_t>(i) * 7;
    pipe_send_frame(pipe, encode(m), 5000);
    const int g = gap(rng);
    if (g) Sleep(g);
  }
  done->store(true);
}

template <class ReceiveFn>
Traffic run_traffic(int count, ReceiveFn&& receive) {
  PipePair pair;
  const std::wstring name = L"\\\\.\\pipe\\GNLinkClipTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                            std::to_wstring(GetTickCount64());
  Traffic t;
  if (!pair.Open(name.c_str())) return t;
  std::atomic<bool> done{false};
  std::thread writer(write_bursts, pair.client, count, &done);
  uint64_t expect = 1;
  const ULONGLONG deadline = GetTickCount64() + 60000;
  while (GetTickCount64() < deadline) {
    PipeFrame f;
    if (!receive(pair.server, &f, 1)) {  // 1 ms polls: the window at every boundary
      const DWORD e = GetLastError();
      if (e == ERROR_INVALID_DATA) {
        // The stream is no longer framed. The product drops the pipe here; so does this loop --
        // a reader that stopped consuming would otherwise leave the writer blocked on a full pipe.
        ++t.garbled;
        break;
      }
      if (done.load() && e == WAIT_TIMEOUT) {
        // Drain: a few more quiet polls after the writer finished.
        bool quiet = true;
        for (int i = 0; i < 20 && quiet; ++i) quiet = !receive(pair.server, &f, 5);
        if (quiet) break;
        // (a late frame came out: fall through to count it next iteration by re-receiving)
        PasteBegin m;
        if (decode(f, &m)) {
          ++t.received;
          if (m.offerId != expect) ++t.outOfOrder;
          if (m.pasteOp != m.offerId * 7) ++t.corrupt;
          expect = m.offerId + 1;
        } else {
          ++t.corrupt;
        }
      }
      continue;
    }
    PasteBegin m;
    if (decode(f, &m)) {
      ++t.received;
      if (m.offerId != expect) ++t.outOfOrder;
      if (m.pasteOp != m.offerId * 7) ++t.corrupt;
      expect = m.offerId + 1;
    } else {
      ++t.corrupt;
    }
    if (done.load() && t.received == count) break;
  }
  // Whatever happened, the writer must come home: closing the server end fails its sends at once.
  CloseHandle(pair.server);
  pair.server = INVALID_HANDLE_VALUE;
  writer.join();
  t.sent = count;
  return t;
}

std::string describe(const Traffic& t) {
  return "sent=" + std::to_string(t.sent) + " received=" + std::to_string(t.received) + " lost=" +
         std::to_string(t.sent - t.received) + " outOfOrder=" + std::to_string(t.outOfOrder) + " corrupt=" +
         std::to_string(t.corrupt) + " garbledReads=" + std::to_string(t.garbled);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc > 1 && wcscmp(argv[1], L"--terminate-policy-child") == 0) {
    // The helper's policy, in a process of its own: a stuck cancellation ends it with 47.
    PipePair pair;
    const std::wstring name = L"\\\\.\\pipe\\GNLinkClipTest-child-" + std::to_wstring(GetCurrentProcessId());
    if (!pair.Open(name.c_str())) return 3;
    FrameReader helper(StuckIoPolicy::TerminateProcess);
    PipeFrame f;
    detail::simulate_stuck_cancel() = true;
    helper.Receive(pair.server, &f, 20, nullptr);  // does not return
    return 4;
  }
  std::printf("--- 3000 frames at 0-2 ms gaps, 1 ms polls ---\n");
  {
    FrameReader reader;
    const Traffic t = run_traffic(3000, [&](HANDLE p, PipeFrame* f, DWORD ms) { return reader.Receive(p, f, ms, nullptr); });
    check("r2 FrameReader: EVERY FRAME, IN ORDER, INTACT", t.received == t.sent && t.outOfOrder == 0 && t.corrupt == 0 && t.garbled == 0,
          describe(t));
  }
  {
    const Traffic t = run_traffic(3000, [](HANDLE p, PipeFrame* f, DWORD ms) { return legacy::receive_frame(p, f, ms); });
    std::printf("      pre-r2 path on the same traffic: %s\n", describe(t).c_str());
    check("(reproduction) the pre-r2 path loses or garbles frames on this traffic", t.received < t.sent || t.garbled > 0 || t.corrupt > 0,
          "if this ever passes with 0 loss, the window was not hit this run -- the r2 assertion above is the contract");
  }

  std::printf("\n--- a frame that arrives in pieces across timed-out polls ---\n");
  {
    PipePair pair;
    const std::wstring name = L"\\\\.\\pipe\\GNLinkClipTest-chunk-" + std::to_wstring(GetCurrentProcessId());
    check("pipe pair", pair.Open(name.c_str()));
    ReadData big;
    big.offerId = 5;
    big.pasteOp = 6;
    big.fileIndex = 1;
    big.offset = 262144;
    big.status = Status::Ok;
    big.data.resize(200 * 1024);
    for (size_t i = 0; i < big.data.size(); ++i) big.data[i] = static_cast<uint8_t>(i * 131);
    std::vector<uint8_t> wire;
    encode_frame(encode(big), &wire);
    // Written in three pieces with 60 ms gaps, from another thread, raw.
    std::thread writer([&] {
      const size_t cut1 = 7, cut2 = 100 * 1024 + 3;
      const size_t cuts[4] = {0, cut1, cut2, wire.size()};
      for (int i = 0; i < 3; ++i) {
        DWORD moved = 0;
        detail::pipe_io_once<true>(pair.client, wire, cuts[i], static_cast<DWORD>(cuts[i + 1] - cuts[i]), 5000, nullptr, &moved,
                                   StuckIoPolicy::OrphanAndFail);
        Sleep(60);
      }
    });
    FrameReader reader;
    PipeFrame f;
    int timeouts = 0;
    bool got = false;
    for (int i = 0; i < 100 && !got; ++i) {
      got = reader.Receive(pair.server, &f, 10, nullptr);
      if (!got) {
        if (GetLastError() != WAIT_TIMEOUT) {
          check("a non-timeout failure while the frame is in pieces", false, "err=" + std::to_string(GetLastError()));
          break;
        }
        ++timeouts;
      }
    }
    writer.join();
    ReadData d;
    check("THE PIECES COME OUT AS ONE INTACT FRAME AFTER SEVERAL TIMED-OUT POLLS",
          got && decode(f, &d) && d.data == big.data && d.offset == 262144, "timeouts=" + std::to_string(timeouts));
    check("...and it took more than one poll (the case is real)", timeouts >= 3, std::to_string(timeouts));
    check("...nothing is left pending", reader.pending_bytes() == 0);
  }

  std::printf("\n--- the errors a reader can see ---\n");
  {
    PipePair pair;
    const std::wstring name = L"\\\\.\\pipe\\GNLinkClipTest-err-" + std::to_wstring(GetCurrentProcessId());
    check("pipe pair", pair.Open(name.c_str()));
    FrameReader reader;
    PipeFrame f;
    check("nothing yet: WAIT_TIMEOUT", !reader.Receive(pair.server, &f, 20, nullptr) && GetLastError() == WAIT_TIMEOUT);
    std::vector<uint8_t> junk = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    DWORD moved = 0;
    detail::pipe_io_once<true>(pair.client, junk, 0, static_cast<DWORD>(junk.size()), 1000, nullptr, &moved, StuckIoPolicy::OrphanAndFail);
    check("16 bytes that are not a header: ERROR_INVALID_DATA", !reader.Receive(pair.server, &f, 500, nullptr) && GetLastError() == ERROR_INVALID_DATA);
    HANDLE abort = CreateEventW(nullptr, TRUE, TRUE, nullptr);  // already signalled
    FrameReader r2;
    check("abort signalled: ERROR_OPERATION_ABORTED", !r2.Receive(pair.server, &f, 5000, abort) && GetLastError() == ERROR_OPERATION_ABORTED);
    CloseHandle(abort);
    CloseHandle(pair.client);
    pair.client = INVALID_HANDLE_VALUE;
    FrameReader r3;
    check("the other end closed: a broken pipe, not a timeout", !r3.Receive(pair.server, &f, 2000, nullptr) && GetLastError() != WAIT_TIMEOUT,
          "err=" + std::to_string(GetLastError()));
  }

  std::printf("\n--- r3 ①: the connection wait (AwaitHello's pieces) ---\n");
  {
    // A connection that completes between the wait timing out and its cancellation is a
    // connection, not a loss: the hook connects the client in exactly that window.
    PipePair pair;
    const std::wstring name = L"\\\\.\\pipe\\GNLinkClipTest-conn-" + std::to_wstring(GetCurrentProcessId());
    pair.server = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                   PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 0, nullptr);
    check("a listening server pipe", pair.server != INVALID_HANDLE_VALUE);
    std::unique_ptr<detail::PendingConnect> pending;
    DWORD err = 0;
    check("begin_connect with nobody there is Pending", detail::begin_connect(pair.server, &pending, &err) == detail::ConnectOutcome::Pending && pending);
    detail::before_connect_cancel_hook() = [&] {
      pair.client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
      Sleep(20);  // let the completion land before the cancel is issued
    };
    const detail::ConnectOutcome o = detail::settle_connect(pair.server, pending, 30, nullptr, &err);
    detail::before_connect_cancel_hook() = nullptr;
    check("A CONNECTION THAT BEAT THE CANCELLATION IS Connected, NOT Timeout", o == detail::ConnectOutcome::Connected && !pending,
          std::to_string(static_cast<int>(o)));
    check("...and the pipe is usable: a frame goes through", [&] {
      FrameReader r;
      PipeFrame f;
      PasteBegin m;
      m.offerId = 1;
      return pipe_send_frame(pair.client, encode(m), 2000) && r.Receive(pair.server, &f, 2000) && f.type == PipeMsg::PasteBegin;
    }());
  }
  {
    PipePair pair;
    const std::wstring name = L"\\\\.\\pipe\\GNLinkClipTest-conn2-" + std::to_wstring(GetCurrentProcessId());
    pair.server = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                   PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 0, nullptr);
    std::unique_ptr<detail::PendingConnect> pending;
    DWORD err = 0;
    detail::begin_connect(pair.server, &pending, &err);
    const uint32_t before = orphaned_io_count();
    check("nobody connects: Timeout, storage freed, no orphan",
          detail::settle_connect(pair.server, pending, 30, nullptr, &err) == detail::ConnectOutcome::Timeout && !pending && orphaned_io_count() == before);
    // The launched process ends first: ProcessExited.
    detail::begin_connect(pair.server, &pending, &err);
    HANDLE ended = CreateEventW(nullptr, TRUE, TRUE, nullptr);  // a "process" handle that is already signalled
    check("the helper process ending first: ProcessExited",
          detail::settle_connect(pair.server, pending, 5000, ended, &err) == detail::ConnectOutcome::ProcessExited && !pending);
    CloseHandle(ended);
    // A cancellation that never completes: the storage is leaked and counted; the wait is Stuck.
    detail::begin_connect(pair.server, &pending, &err);
    detail::simulate_stuck_cancel() = true;
    const detail::ConnectOutcome stuck = detail::settle_connect(pair.server, pending, 30, nullptr, &err);
    detail::simulate_stuck_cancel() = false;
    check("A STUCK CANCELLATION IS Stuck: THE STORAGE IS ORPHANED (NOT FREED) AND COUNTED, THE PROCESS LIVES",
          stuck == detail::ConnectOutcome::Stuck && !pending && orphaned_io_count() == before + 1, std::to_string(orphaned_io_count()));
  }

  std::printf("\n--- r3 ②: a stuck read / write under each policy ---\n");
  {
    const uint32_t before = orphaned_io_count();
    PipePair pair;
    const std::wstring name = L"\\\\.\\pipe\\GNLinkClipTest-stuck-" + std::to_wstring(GetCurrentProcessId());
    check("pipe pair", pair.Open(name.c_str()));
    FrameReader host(StuckIoPolicy::OrphanAndFail);
    PipeFrame f;
    detail::simulate_stuck_cancel() = true;
    const bool got = host.Receive(pair.server, &f, 20, nullptr);
    const DWORD e = GetLastError();
    detail::simulate_stuck_cancel() = false;
    check("HOST POLICY: THE READ FAILS WITH ERROR_IO_INCOMPLETE, THE PROCESS SURVIVES, ONE ORPHAN COUNTED",
          !got && e == ERROR_IO_INCOMPLETE && orphaned_io_count() == before + 1 && host.orphaned(), "err=" + std::to_string(e));
    check("...the reader is finished (every later call says so at once)", !host.Receive(pair.server, &f, 5000, nullptr) && GetLastError() == ERROR_IO_INCOMPLETE);
    // A fresh pipe and reader work as before: the orphan cost this link, not the process.
    PipePair fresh;
    check("...a fresh pipe still works", fresh.Open((name + L"-fresh").c_str()) && [&] {
      FrameReader r;
      PasteBegin m;
      m.offerId = 3;
      PipeFrame g;
      return pipe_send_frame(fresh.client, encode(m), 2000) && r.Receive(fresh.server, &g, 2000) && g.type == PipeMsg::PasteBegin;
    }());
    // The bound: at kMaxOrphanedIo (orphans + in flight) no new I/O is started.
    const uint32_t saved = detail::io_reservations().load();
    detail::io_reservations().store(kMaxOrphanedIo);
    FrameReader capped;
    const bool started = capped.Receive(fresh.server, &f, 20, nullptr);
    const DWORD capErr = GetLastError();
    detail::io_reservations().store(saved);
    check("AT THE ORPHAN BOUND NO NEW I/O IS STARTED (ERROR_NO_SYSTEM_RESOURCES)", !started && capErr == ERROR_NO_SYSTEM_RESOURCES,
          "err=" + std::to_string(capErr));
    // A write under the host policy: the wire buffer goes to the orphan; the send fails.
    detail::simulate_stuck_cancel() = true;
    PasteBegin m;
    m.offerId = 4;
    // Fill the pipe so the write must pend: 64 KiB buffer, write 200 KiB with nobody reading.
    ReadData big;
    big.data.resize(200 * 1024);
    const bool sent = pipe_send_frame(fresh.client, encode(big), 20, nullptr, StuckIoPolicy::OrphanAndFail);
    const DWORD sendErr = GetLastError();
    detail::simulate_stuck_cancel() = false;
    check("HOST POLICY: A STUCK WRITE FAILS (ERROR_IO_INCOMPLETE), NO TERMINATION", !sent && sendErr == ERROR_IO_INCOMPLETE && orphaned_io_count() == before + 2,
          "err=" + std::to_string(sendErr) + " orphans=" + std::to_string(orphaned_io_count()));
    // The helper policy terminates the process -- proven by running this test itself as a child.
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = std::wstring(L"\"") + exe + L"\" --terminate-policy-child";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    DWORD code = 0xFFFFFFFF;
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
      if (WaitForSingleObject(pi.hProcess, 15000) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
      else TerminateProcess(pi.hProcess, 200);
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
    check("HELPER POLICY: A STUCK CANCELLATION TERMINATES THAT PROCESS (exit 47), NOT THIS ONE", code == 47, "child exit=" + std::to_string(code));
  }

  std::printf("\n--- r4: the connection wait is under the same admission bound ---\n");
  {
    // Last, because it drives the process-wide bound to its limit (the storage it orphans is
    // leaked for real; the counters are reset at the end so the process's bookkeeping starts over).
    const uint32_t savedOrphans = detail::orphan_counter().load();
    const uint32_t savedReservations = detail::io_reservations().load();
    PipePair pair;
    const std::wstring name = L"\\\\.\\pipe\\GNLinkClipTest-bound-" + std::to_wstring(GetCurrentProcessId());
    pair.server = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                   PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 0, nullptr);
    check("a listening server pipe", pair.server != INVALID_HANDLE_VALUE);
    std::unique_ptr<detail::PendingConnect> pending;
    DWORD err = 0;
    // A normal cancellation gives its reservation back: usage does not grow.
    const uint32_t r0 = detail::io_reservations().load();
    check("begin_connect holds one reservation while the wait is on",
          detail::begin_connect(pair.server, &pending, &err) == detail::ConnectOutcome::Pending && detail::io_reservations().load() == r0 + 1);
    check("a cancelled (timed-out) connection wait gives it back: usage +0, orphans +0",
          detail::settle_connect(pair.server, pending, 30, nullptr, &err) == detail::ConnectOutcome::Timeout && !pending &&
              detail::io_reservations().load() == r0 && orphaned_io_count() == savedOrphans);
    // Stuck cancellations, repeated the way a link that keeps timing out would repeat them: each
    // is admitted while under the bound, each keeps its storage and its reservation.
    const uint32_t attemptsBefore = detail::connect_attempts().load();
    uint32_t stuckCount = 0;
    bool allAdmitted = true;
    while (detail::io_reservations().load() < kMaxOrphanedIo && stuckCount <= kMaxOrphanedIo) {
      if (detail::begin_connect(pair.server, &pending, &err) != detail::ConnectOutcome::Pending) {
        allAdmitted = false;
        break;
      }
      detail::simulate_stuck_cancel() = true;
      const detail::ConnectOutcome o = detail::settle_connect(pair.server, pending, 20, nullptr, &err);
      detail::simulate_stuck_cancel() = false;
      if (o != detail::ConnectOutcome::Stuck) {
        allAdmitted = false;
        break;
      }
      ++stuckCount;
    }
    check("...up to the bound every wait was admitted (one ConnectNamedPipe each), every stuck one was orphaned and counted",
          allAdmitted && stuckCount == kMaxOrphanedIo - r0 && orphaned_io_count() == savedOrphans + stuckCount &&
              detail::connect_attempts().load() == attemptsBefore + stuckCount && detail::io_reservations().load() == kMaxOrphanedIo,
          "stuck=" + std::to_string(stuckCount) + " orphans=" + std::to_string(orphaned_io_count()) + " reserved=" +
              std::to_string(detail::io_reservations().load()));
    // At the bound: the next connection wait makes no OS call at all -- no ConnectNamedPipe, no
    // event handle, no storage -- and this process is still here to say so.
    DWORD handlesBefore = 0, handlesAfter = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handlesBefore);
    const uint32_t attemptsAtBound = detail::connect_attempts().load();
    const detail::ConnectOutcome refused = detail::begin_connect(pair.server, &pending, &err);
    GetProcessHandleCount(GetCurrentProcess(), &handlesAfter);
    check("AT THE BOUND A NEW CONNECTION WAIT IS REFUSED: Failed + ERROR_NO_SYSTEM_RESOURCES, ConnectNamedPipe NOT CALLED, NO EVENT, NO STORAGE, THIS PROCESS LIVES",
          refused == detail::ConnectOutcome::Failed && err == ERROR_NO_SYSTEM_RESOURCES && !pending &&
              detail::connect_attempts().load() == attemptsAtBound && handlesAfter == handlesBefore &&
              detail::io_reservations().load() == kMaxOrphanedIo && orphaned_io_count() == savedOrphans + stuckCount,
          "outcome=" + std::to_string(static_cast<int>(refused)) + " err=" + std::to_string(err) + " attempts=" +
              std::to_string(detail::connect_attempts().load() - attemptsAtBound) + " handles=" + std::to_string(handlesBefore) + "->" +
              std::to_string(handlesAfter));
    // ...and neither a read nor a write is started: it is one bound, whichever operation reached it.
    PipePair fresh;
    check("a fresh pipe pair (handles, not I/O)", fresh.Open((name + L"-fresh").c_str()));
    FrameReader capped;
    PipeFrame f;
    const bool readStarted = capped.Receive(fresh.server, &f, 20, nullptr);
    const DWORD readErr = GetLastError();
    PasteBegin m;
    m.offerId = 5;
    const bool writeStarted = pipe_send_frame(fresh.client, encode(m), 20, nullptr, StuckIoPolicy::OrphanAndFail);
    const DWORD writeErr = GetLastError();
    check("...at the bound reached through connections, a read and a write are refused too (ERROR_NO_SYSTEM_RESOURCES)",
          !readStarted && readErr == ERROR_NO_SYSTEM_RESOURCES && !writeStarted && writeErr == ERROR_NO_SYSTEM_RESOURCES,
          "read err=" + std::to_string(readErr) + " write err=" + std::to_string(writeErr));
    // Bookkeeping reset for the rest of this process (the orphaned storage itself stays leaked).
    detail::io_reservations().store(savedReservations);
    detail::orphan_counter().store(savedOrphans);
    check("after the reset a connection wait is admitted again",
          detail::begin_connect(pair.server, &pending, &err) == detail::ConnectOutcome::Pending &&
              detail::settle_connect(pair.server, pending, 20, nullptr, &err) == detail::ConnectOutcome::Timeout && !pending &&
              detail::io_reservations().load() == savedReservations);
  }

  std::printf("\n%s  (%d checks, %d failed)\n", gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
