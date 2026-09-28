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

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
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
        detail::pipe_io_once<true>(pair.client, wire.data() + cuts[i], static_cast<DWORD>(cuts[i + 1] - cuts[i]), 5000, nullptr, &moved);
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
    uint8_t junk[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    DWORD moved = 0;
    detail::pipe_io_once<true>(pair.client, junk, sizeof(junk), 1000, nullptr, &moved);
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

  std::printf("\n%s  (%d checks, %d failed)\n", gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
