// GNLinkClipHelper: the Medium-integrity clipboard helper of the host. (file-copy-helper r1)
//
// What it is: a process the elevated host (GNLinkStream) starts as the interactive user -- the
// host's TokenLinkedToken -- so that Explorer, which runs at Medium, can paste files that live on
// the viewer's PC, and so that files the user copied on this PC are opened with the user's own
// rights, never the administrator's. It has no network: one pipe to the host, given on its
// command line together with a nonce that proves to the host which process connected.
//
//   GNLinkClipHelper.exe --pipe \\.\pipe\GNLinkClip-<hex> --nonce <64 hex> --host-pid <pid>
//                        [--idle-ms N] [--log <file>]
//
// Threads:
//   main    STA (OleInitialize). Owns the RemoteFilesDataObject on the clipboard and a hidden
//           top-level window that hears WM_ENDSESSION and a 1 s timer. Never waits on the pipe
//           directly: the data object's waits go through PipeTransport, which pumps.
//   reader  reads frames from the pipe; answers StatFiles / Pin / ReadLocal / Unpin itself (it
//           owns the LocalFileTable); completes the STA's pending waits (PasteDescriptor /
//           ReadData); posts Publish / Clear / Shutdown / disconnect to the window.
//   sender  writes frames queued by any thread.
//
// Exit: the host says Shutdown, the pipe breaks, the session ends, or the host process ends --
// the clipboard is cleared (OleSetClipboard(NULL)) if it is still ours, pins are released, exit 0.
// A remote-file object must not outlive the connection it reads from.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ole2.h>

#include <atomic>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "file_copy_data_object.hpp"
#include "file_copy_local_files.hpp"
#include "file_copy_pipe.hpp"
#include "file_copy_pipe_io.hpp"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")

using namespace remote60::native_poc::file_copy;

namespace {

constexpr UINT kMsgPublish = WM_APP + 1;     // lParam: PublishRemoteFiles*
constexpr UINT kMsgClear = WM_APP + 2;       // lParam: ClearRemoteFiles*
constexpr UINT kMsgShutdown = WM_APP + 3;    // wParam: 0 host said so, 1 pipe gone, 2 host process gone
constexpr UINT_PTR kTimerId = 1;

// ------------------------------------------------------------------------------ logging

class Log {
 public:
  void Open(const std::wstring& path) {
    if (!path.empty()) file_ = _wfopen(path.c_str(), L"a");
  }
  void Line(const std::string& text) {
    std::lock_guard<std::mutex> lock(mu_);
    const double t = static_cast<double>(GetTickCount64() - t0_) / 1000.0;
    char prefix[64];
    std::snprintf(prefix, sizeof(prefix), "%9.3f tid=%lu ", t, GetCurrentThreadId());
    std::fprintf(stderr, "[GNLinkClipHelper] %s%s\n", prefix, text.c_str());
    std::fflush(stderr);
    if (file_) {
      std::fprintf(file_, "%s%s\n", prefix, text.c_str());
      std::fflush(file_);
    }
  }
  ~Log() {
    if (file_) std::fclose(file_);
  }

 private:
  std::mutex mu_;
  FILE* file_ = nullptr;
  const ULONGLONG t0_ = GetTickCount64();
};

Log gLog;
void logf(const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  gLog.Line(buf);
}

// ------------------------------------------------------------------------------ options

struct Options {
  std::wstring pipe;
  std::array<uint8_t, kNonceBytes> nonce{};
  DWORD hostPid = 0;
  DWORD idleMs = 60000;
  std::wstring log;
  bool ok = false;
};

Options parse(int argc, wchar_t** argv) {
  Options o;
  bool haveNonce = false;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::wstring k = argv[i];
    const std::wstring v = argv[i + 1];
    if (k == L"--pipe") o.pipe = v;
    else if (k == L"--nonce") haveNonce = hex_decode(v, o.nonce.data(), o.nonce.size());
    else if (k == L"--host-pid") o.hostPid = static_cast<DWORD>(_wtoi(v.c_str()));
    else if (k == L"--idle-ms") o.idleMs = static_cast<DWORD>(_wtoi(v.c_str()));
    else if (k == L"--log") o.log = v;
  }
  o.ok = !o.pipe.empty() && haveNonce && o.hostPid != 0;
  return o;
}

// ------------------------------------------------------------------------------ pipe threads

/** Frames any thread wants sent, written by the sender thread in order. */
class SendQueue {
 public:
  explicit SendQueue(HANDLE abort) : abort_(abort) { event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr); }
  ~SendQueue() { CloseHandle(event_); }
  void Push(PipeFrame frame) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      queue_.push_back(std::move(frame));
    }
    SetEvent(event_);
  }
  bool Empty() {
    std::lock_guard<std::mutex> lock(mu_);
    return queue_.empty();
  }
  // Blocks until a frame is queued or abort is signalled. False on abort.
  bool Pop(PipeFrame* frame) {
    for (;;) {
      {
        std::lock_guard<std::mutex> lock(mu_);
        if (!queue_.empty()) {
          *frame = std::move(queue_.front());
          queue_.pop_front();
          return true;
        }
      }
      HANDLE waits[2] = {event_, abort_};
      if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) return false;
    }
  }

 private:
  std::mutex mu_;
  std::deque<PipeFrame> queue_;
  HANDLE event_ = nullptr;
  HANDLE abort_;
};

/** A reply the STA thread is waiting for. Keyed by what the reply echoes. */
struct PendingKey {
  PipeMsg type;
  uint64_t a, b, c;
  bool operator<(const PendingKey& o) const {
    if (type != o.type) return type < o.type;
    if (a != o.a) return a < o.a;
    if (b != o.b) return b < o.b;
    return c < o.c;
  }
};
struct Pending {
  HANDLE event = nullptr;  // manual-reset
  PipeFrame reply;
  bool done = false;
  int waiters = 0;
};

class PendingTable {
 public:
  std::shared_ptr<Pending> Register(const PendingKey& key) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = table_.find(key);
    if (it != table_.end()) {
      ++it->second->waiters;  // the same request twice in flight shares one reply
      return it->second;
    }
    auto p = std::make_shared<Pending>();
    p->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    p->waiters = 1;
    // A reply that arrived before anyone waited for it (the host answers PasteBegin at once,
    // while the shell is still between Drop and its descriptor request) is taken up here.
    auto early = early_.find(key);
    if (early != early_.end()) {
      p->reply = std::move(early->second);
      p->done = true;
      SetEvent(p->event);
      early_.erase(early);
    }
    table_[key] = p;
    return p;
  }
  void Complete(const PendingKey& key, PipeFrame reply) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = table_.find(key);
    if (it == table_.end()) {
      if (early_.size() >= 64) early_.erase(early_.begin());  // bounded; a stale reply is worthless
      early_[key] = std::move(reply);
      return;
    }
    it->second->reply = std::move(reply);
    it->second->done = true;
    SetEvent(it->second->event);
  }
  void Forget(const PendingKey& key, const std::shared_ptr<Pending>& p) {
    std::lock_guard<std::mutex> lock(mu_);
    if (--p->waiters > 0) return;
    auto it = table_.find(key);
    if (it != table_.end() && it->second == p) table_.erase(it);
    CloseHandle(p->event);
    p->event = nullptr;
  }

 private:
  std::mutex mu_;
  std::map<PendingKey, std::shared_ptr<Pending>> table_;
  std::map<PendingKey, PipeFrame> early_;
};

struct Shared {
  HANDLE pipe = INVALID_HANDLE_VALUE;
  HANDLE abort = nullptr;  // manual-reset: everything stops
  HWND hwnd = nullptr;
  std::unique_ptr<SendQueue> send;
  PendingTable pending;
  DWORD idleMs = 60000;
  std::atomic<uint64_t> nextPasteOp{1};
  std::atomic<bool> disconnectPosted{false};
};

Shared gShared;

void post_shutdown(WPARAM why) {
  if (gShared.disconnectPosted.exchange(true)) return;
  if (gShared.hwnd) PostMessageW(gShared.hwnd, kMsgShutdown, why, 0);
}

bool pipe_broken(HANDLE pipe) {
  DWORD avail = 0;
  if (PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr)) return false;
  const DWORD e = GetLastError();
  return e == ERROR_BROKEN_PIPE || e == ERROR_PIPE_NOT_CONNECTED || e == ERROR_INVALID_HANDLE || e == ERROR_NO_DATA;
}

void sender_main() {
  PipeFrame frame;
  while (gShared.send->Pop(&frame)) {
    // The helper is an isolated process: a cancellation that never completes ends it (r3 ②).
    if (!pipe_send_frame(gShared.pipe, frame, 5000, gShared.abort, StuckIoPolicy::TerminateProcess)) {
      if (WaitForSingleObject(gShared.abort, 0) != WAIT_OBJECT_0) {
        logf("send failed type=%u -> pipe gone", static_cast<unsigned>(frame.type));
        post_shutdown(1);
      }
      return;
    }
  }
}

// The reader owns the local file table: every Stat / Pin / ReadLocal / Unpin runs here.
void reader_main() {
  LocalFileTable table;
  FrameReader reader(StuckIoPolicy::TerminateProcess);  // keeps a half-received frame across the 1 s polls (r2)
  for (;;) {
    PipeFrame f;
    if (!reader.Receive(gShared.pipe, &f, 1000, gShared.abort)) {
      const DWORD err = GetLastError();
      if (WaitForSingleObject(gShared.abort, 0) == WAIT_OBJECT_0) break;
      if (err != WAIT_TIMEOUT || pipe_broken(gShared.pipe)) {
        logf("pipe gone (err=%lu)", err);
        post_shutdown(1);
        break;
      }
      const uint32_t expired = table.ExpireLeases();
      if (expired) logf("lease expired: released %u handle(s)", expired);
      continue;
    }
    switch (f.type) {
      case PipeMsg::HelloAck:
        break;
      case PipeMsg::Shutdown:
        logf("host says shutdown");
        post_shutdown(0);
        break;
      case PipeMsg::PublishRemoteFiles: {
        auto m = std::make_unique<PublishRemoteFiles>();
        if (!decode(f, m.get())) {
          logf("PublishRemoteFiles malformed");
          break;
        }
        PostMessageW(gShared.hwnd, kMsgPublish, 0, reinterpret_cast<LPARAM>(m.release()));
        break;
      }
      case PipeMsg::ClearRemoteFiles: {
        auto m = std::make_unique<ClearRemoteFiles>();
        if (!decode(f, m.get())) break;
        PostMessageW(gShared.hwnd, kMsgClear, 0, reinterpret_cast<LPARAM>(m.release()));
        break;
      }
      case PipeMsg::PasteDescriptor: {
        PasteDescriptor m;
        if (!decode(f, &m)) {
          logf("PasteDescriptor malformed");
          break;
        }
        gShared.pending.Complete({PipeMsg::PasteDescriptor, m.offerId, m.pasteOp, 0}, std::move(f));
        break;
      }
      case PipeMsg::ReadData: {
        ReadData m;
        if (!decode(f, &m)) {
          logf("ReadData malformed");
          break;
        }
        gShared.pending.Complete({PipeMsg::ReadData, m.pasteOp, (static_cast<uint64_t>(m.fileIndex) << 32) | (m.offerId & 0xFFFFFFFFu), m.offset},
                                 std::move(f));
        break;
      }
      case PipeMsg::StatFiles: {
        StatFiles m;
        Stats reply;
        if (!decode(f, &m)) {
          logf("StatFiles malformed");
          break;
        }
        reply.requestId = m.requestId;
        for (const auto& p : m.paths) reply.entries.push_back(stat_source_file(std::wstring(p.begin(), p.end())));
        logf("StatFiles req=%llu paths=%zu", static_cast<unsigned long long>(m.requestId), m.paths.size());
        gShared.send->Push(encode(reply));
        break;
      }
      case PipeMsg::Pin: {
        Pin m;
        PinResult reply;
        if (!decode(f, &m)) {
          logf("Pin malformed");
          break;
        }
        reply.pinId = m.pinId;
        if (!table.Pin(m.pinId, m.leaseMs, m.entries, &reply.entries)) {
          reply.entries.assign(m.entries.size(), PinResultEntry{Status::TooMany});
        }
        size_t held = 0;
        for (const auto& e : reply.entries) held += e.status == Status::Ok ? 1 : 0;
        logf("Pin id=%llu lease=%u entries=%zu held=%zu", static_cast<unsigned long long>(m.pinId), m.leaseMs,
             m.entries.size(), held);
        gShared.send->Push(encode(reply));
        break;
      }
      case PipeMsg::ReadLocal: {
        ReadLocal m;
        LocalData reply;
        if (!decode(f, &m)) {
          logf("ReadLocal malformed");
          break;
        }
        reply.pinId = m.pinId;
        reply.fileIndex = m.fileIndex;
        reply.offset = m.offset;
        reply.status = table.Read(m.pinId, m.fileIndex, m.offset, m.length, &reply.data);
        if (reply.status != Status::Ok) {
          logf("ReadLocal pin=%llu index=%u offset=%llu -> %s", static_cast<unsigned long long>(m.pinId), m.fileIndex,
               static_cast<unsigned long long>(m.offset), status_name(reply.status));
        }
        gShared.send->Push(encode(reply));
        break;
      }
      case PipeMsg::Unpin: {
        Unpin m;
        if (!decode(f, &m)) break;
        Unpinned reply;
        reply.pinId = m.pinId;
        reply.released = table.Unpin(m.pinId);
        logf("Unpin id=%llu released=%u", static_cast<unsigned long long>(m.pinId), reply.released);
        gShared.send->Push(encode(reply));
        break;
      }
      default:
        logf("unexpected frame type=%u len=%zu (ignored)", static_cast<unsigned>(f.type), f.payload.size());
        break;
    }
  }
  const size_t left = table.pinned_handles();
  table.ReleaseAll();
  if (left) logf("released %zu pinned handle(s) at exit", left);
}

// ------------------------------------------------------------------------------ transport (STA)

class PipeTransport : public PasteTransport {
 public:
  uint64_t NewPasteOp() override { return gShared.nextPasteOp.fetch_add(1); }
  void PasteBegin(uint64_t offerId, uint64_t pasteOp) override {
    remote60::native_poc::file_copy::PasteBegin m;
    m.offerId = offerId;
    m.pasteOp = pasteOp;
    gShared.send->Push(encode(m));
  }
  Status WaitDescriptor(uint64_t offerId, uint64_t pasteOp, DWORD timeoutMs,
                        std::vector<RemoteFileItem>* confirmed) override {
    const PendingKey key{PipeMsg::PasteDescriptor, offerId, pasteOp, 0};
    auto p = gShared.pending.Register(key);
    const Status st = wait(p, timeoutMs);
    Status out = st;
    if (st == Status::Ok) {
      PasteDescriptor m;
      if (decode(p->reply, &m)) {
        out = m.status;
        *confirmed = std::move(m.items);
      } else {
        out = Status::ReadError;
      }
    }
    gShared.pending.Forget(key, p);
    return out;
  }
  Status Read(uint64_t offerId, uint64_t pasteOp, uint32_t fileIndex, uint64_t offset, uint32_t length,
              DWORD timeoutMs, std::vector<uint8_t>* out) override {
    const PendingKey key{PipeMsg::ReadData, pasteOp, (static_cast<uint64_t>(fileIndex) << 32) | (offerId & 0xFFFFFFFFu), offset};
    auto p = gShared.pending.Register(key);
    if (p->waiters == 1) {
      gShared.send->Push(encode(ReadRequest{offerId, pasteOp, fileIndex, offset, length}));
    }
    const Status st = wait(p, timeoutMs);
    Status result = st;
    if (st == Status::Ok) {
      ReadData m;
      if (decode(p->reply, &m)) {
        result = m.status;
        *out = std::move(m.data);
      } else {
        result = Status::ReadError;
      }
    }
    gShared.pending.Forget(key, p);
    return result;
  }
  void PasteEnd(uint64_t offerId, uint64_t pasteOp, EndReason reason) override {
    remote60::native_poc::file_copy::PasteEnd m;
    m.offerId = offerId;
    m.pasteOp = pasteOp;
    m.reason = reason;
    gShared.send->Push(encode(m));
  }

 private:
  // Waits on the STA while pumping, so the shell's other calls (and our own window messages)
  // keep flowing. Abort ends the wait with Aborted.
  static Status wait(const std::shared_ptr<Pending>& p, DWORD timeoutMs) {
    HANDLE handles[2] = {p->event, gShared.abort};
    DWORD index = 0;
    const HRESULT hr = CoWaitForMultipleHandles(COWAIT_DISPATCH_CALLS | COWAIT_DISPATCH_WINDOW_MESSAGES, timeoutMs, 2,
                                                handles, &index);
    if (hr == RPC_S_CALLPENDING || hr == HRESULT_FROM_WIN32(WAIT_TIMEOUT)) return Status::Timeout;
    if (FAILED(hr)) return Status::Aborted;
    if (index == 1) return Status::Aborted;
    return p->done ? Status::Ok : Status::Aborted;
  }
};

// ------------------------------------------------------------------------------ owner (STA)

struct Owner {
  RemoteFilesDataObject* current = nullptr;  // one AddRef held by us
  // Objects no longer on the clipboard -- a newer offer replaced them, or another program took the
  // clipboard -- with a paste still running on each (one AddRef each). A paste that has begun runs to
  // its end: a new copy changes only what the NEXT paste gets (t-zdmsd4gb, debate "공통 상태").
  std::vector<RemoteFilesDataObject*> retired;
  PipeTransport transport;
  bool quitting = false;
};
Owner gOwner;

/**
 * The current object leaves the clipboard (`why` says for what). With a paste running on it, it is
 * retired and keeps serving that paste; otherwise it ends as before.
 */
void retire_or_abort(EndReason why) {
  if (!gOwner.current) return;
  if (gOwner.current->in_operation()) {
    logf("offer=%llu off the clipboard (%s); paste op=%llu runs on", static_cast<unsigned long long>(gOwner.current->offer_id()),
         end_reason_name(why), static_cast<unsigned long long>(gOwner.current->paste_op()));
    gOwner.retired.push_back(gOwner.current);  // our ref moves with it
    gOwner.current = nullptr;
    return;
  }
  gOwner.current->AbortOperation(why);
  gOwner.current->Release();
  gOwner.current = nullptr;
}

void release_current(bool clearClipboard) {
  if (!gOwner.current) return;
  if (clearClipboard && OleIsCurrentClipboard(gOwner.current) == S_OK) {
    const HRESULT hr = OleSetClipboard(nullptr);
    logf("clipboard cleared hr=0x%08lx", static_cast<unsigned long>(hr));
  }
  gOwner.current->Release();
  gOwner.current = nullptr;
}

void on_publish(std::unique_ptr<PublishRemoteFiles> m) {
  PublishResult result;
  result.offerId = m->offerId;
  result.count = static_cast<uint32_t>(m->items.size());
  bool ok = !m->items.empty() && m->items.size() <= kMaxFiles;
  for (const auto& it : m->items) ok = ok && validate_remote_name(it.name);
  if (!ok) {
    result.status = m->items.size() > kMaxFiles ? Status::TooMany : Status::Refused;
    logf("publish offer=%llu refused (%s)", static_cast<unsigned long long>(m->offerId), status_name(result.status));
    gShared.send->Push(encode(result));
    return;
  }
  retire_or_abort(EndReason::Cleared);
  DataObjectConfig cfg;
  auto* obj = new RemoteFilesDataObject(m->offerId, m->items, &gOwner.transport, cfg,
                                        [](const std::string& line) { gLog.Line(line); });
  const HRESULT hr = OleSetClipboard(obj);
  if (FAILED(hr)) {
    logf("publish offer=%llu OleSetClipboard hr=0x%08lx", static_cast<unsigned long long>(m->offerId),
         static_cast<unsigned long>(hr));
    obj->Release();
    result.status = Status::Refused;
    gShared.send->Push(encode(result));
    return;
  }
  gOwner.current = obj;  // our ref; OLE holds its own
  logf("publish offer=%llu items=%u on the clipboard", static_cast<unsigned long long>(m->offerId), result.count);
  gShared.send->Push(encode(result));
}

void on_clear(std::unique_ptr<ClearRemoteFiles> m) {
  if (!gOwner.current || gOwner.current->offer_id() != m->offerId) {
    logf("clear offer=%llu: not current", static_cast<unsigned long long>(m->offerId));
    return;
  }
  gOwner.current->AbortOperation(EndReason::Cleared);
  release_current(true);
  logf("clear offer=%llu done", static_cast<unsigned long long>(m->offerId));
}

void shutdown(const char* why) {
  if (gOwner.quitting) return;
  gOwner.quitting = true;
  logf("shutdown: %s", why);
  if (gOwner.current) {
    gOwner.current->AbortOperation(EndReason::Disconnected);
    release_current(true);
  }
  for (RemoteFilesDataObject* r : gOwner.retired) {  // the session is over: every paste ends
    r->AbortOperation(EndReason::Disconnected);
    r->Release();
  }
  gOwner.retired.clear();
  // Abort is raised after the loop, once the sender has had its chance to put the last PasteEnd
  // on the wire (when the pipe is still there to take it).
  PostQuitMessage(0);
}

void on_timer() {
  // Retired objects: released once their paste is over; the idle rule still ends a paste nobody reads.
  for (auto it = gOwner.retired.begin(); it != gOwner.retired.end();) {
    RemoteFilesDataObject* r = *it;
    if (!r->in_operation()) {
      logf("retired offer=%llu released (its paste is over)", static_cast<unsigned long long>(r->offer_id()));
      r->Release();
      it = gOwner.retired.erase(it);
      continue;
    }
    if (!r->waiting() && GetTickCount64() - r->last_activity_ms() > gShared.idleMs) {
      logf("paste op=%llu idle for %lu ms: ending it", static_cast<unsigned long long>(r->paste_op()),
           static_cast<unsigned long>(gShared.idleMs));
      r->AbortOperation(EndReason::Idle);
    }
    ++it;
  }
  if (!gOwner.current) return;
  if (OleIsCurrentClipboard(gOwner.current) != S_OK) {
    // Something else took the clipboard: the offer is over. A paste already running on it runs on.
    // Who took it (a pid, no names), for the field log.
    DWORD ownerPid = 0;
    if (HWND owner = GetClipboardOwner()) GetWindowThreadProcessId(owner, &ownerPid);
    logf("clipboard taken over: offer=%llu dropped (owner pid=%lu seq=%lu)",
         static_cast<unsigned long long>(gOwner.current->offer_id()), static_cast<unsigned long>(ownerPid),
         static_cast<unsigned long>(GetClipboardSequenceNumber()));
    retire_or_abort(EndReason::Released);
    return;
  }
  // Idle means nobody is asking; a wait on the host in progress is the opposite of idle.
  if (gOwner.current->in_operation() && !gOwner.current->waiting() &&
      GetTickCount64() - gOwner.current->last_activity_ms() > gShared.idleMs) {
    logf("paste op=%llu idle for %lu ms: ending it", static_cast<unsigned long long>(gOwner.current->paste_op()),
         static_cast<unsigned long>(gShared.idleMs));
    gOwner.current->AbortOperation(EndReason::Idle);
  }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case kMsgPublish:
      on_publish(std::unique_ptr<PublishRemoteFiles>(reinterpret_cast<PublishRemoteFiles*>(lp)));
      return 0;
    case kMsgClear:
      on_clear(std::unique_ptr<ClearRemoteFiles>(reinterpret_cast<ClearRemoteFiles*>(lp)));
      return 0;
    case kMsgShutdown:
      shutdown(wp == 0 ? "host asked" : wp == 1 ? "pipe gone" : "host process gone");
      return 0;
    case WM_TIMER:
      if (wp == kTimerId) on_timer();
      return 0;
    case WM_QUERYENDSESSION:
      return TRUE;
    case WM_ENDSESSION:
      if (wp) shutdown("session ending");
      return 0;
    case WM_DESTROY:
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

// The host process itself: if it goes, the pipe goes too, but a waiting handle is a cleaner
// signal than a read error, and it works even before the first frame.
void host_watch_main(HANDLE host) {
  HANDLE waits[2] = {host, gShared.abort};
  if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0) post_shutdown(2);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  const Options o = parse(argc, argv);
  gLog.Open(o.log);
  if (!o.ok) {
    logf("usage: --pipe <name> --nonce <64 hex> --host-pid <pid> [--idle-ms N] [--log F]");
    return 2;
  }
  gShared.idleMs = o.idleMs == 0 ? 60000 : o.idleMs;
  gShared.abort = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  gShared.send = std::make_unique<SendQueue>(gShared.abort);

  // Connect and prove ourselves before anything else: the host's AwaitHello is bounded.
  gShared.pipe = CreateFileW(o.pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                             FILE_FLAG_OVERLAPPED, nullptr);
  if (gShared.pipe == INVALID_HANDLE_VALUE) {
    logf("pipe open failed err=%lu", GetLastError());
    return 3;
  }
  Hello hello;
  hello.nonce = o.nonce;
  hello.pid = GetCurrentProcessId();
  if (!pipe_send_frame(gShared.pipe, encode(hello), 5000, nullptr, StuckIoPolicy::TerminateProcess)) {
    logf("hello send failed err=%lu", GetLastError());
    return 3;
  }
  PipeFrame ack;
  FrameReader helloReader(StuckIoPolicy::TerminateProcess);
  if (!helloReader.Receive(gShared.pipe, &ack, 10000) || ack.type != PipeMsg::HelloAck) {
    logf("no HelloAck (refused?) err=%lu", GetLastError());
    return 4;
  }

  const HRESULT hr = OleInitialize(nullptr);
  if (FAILED(hr)) {
    logf("OleInitialize hr=0x%08lx", static_cast<unsigned long>(hr));
    return 5;
  }
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"GNLinkClipHelperOwner";
  RegisterClassExW(&wc);
  // A real top-level window (never shown) rather than a message-only one: message-only windows
  // are not on the broadcast list, and WM_ENDSESSION is a broadcast.
  gShared.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"GNLink clipboard helper",
                                 WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
  if (!gShared.hwnd) {
    logf("owner window failed err=%lu", GetLastError());
    OleUninitialize();
    return 5;
  }
  SetTimer(gShared.hwnd, kTimerId, 1000, nullptr);

  HANDLE host = OpenProcess(SYNCHRONIZE, FALSE, o.hostPid);
  std::thread reader(reader_main);
  std::thread sender(sender_main);
  std::thread watch;
  if (host) watch = std::thread(host_watch_main, host);
  logf("ready pid=%lu host=%lu idleMs=%lu", GetCurrentProcessId(), o.hostPid, gShared.idleMs);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  // Out of the loop only through shutdown(): the clipboard is ours no more. Let the sender drain
  // briefly (a last PasteEnd), then stop everything.
  for (int i = 0; i < 50 && !gShared.send->Empty() && !pipe_broken(gShared.pipe); ++i) Sleep(10);
  SetEvent(gShared.abort);
  CancelIoEx(gShared.pipe, nullptr);
  if (reader.joinable()) reader.join();
  if (sender.joinable()) sender.join();
  if (watch.joinable()) watch.join();
  if (host) CloseHandle(host);
  KillTimer(gShared.hwnd, kTimerId);
  DestroyWindow(gShared.hwnd);
  CloseHandle(gShared.pipe);
  gShared.pipe = INVALID_HANDLE_VALUE;
  OleUninitialize();
  logf("exit 0");
  return 0;
}
